#include "smartroom_rgb.h"
#include "smartroom_config.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "led_strip_encoder.h"

namespace smartroom {
namespace {
static const char *TAG = "smartroom_rgb";

bool invalid_gpio(int gpio)
{
    return gpio < 0 || gpio >= 48;
}

bool conflicts_with_ir(int gpio)
{
    return gpio == kIrTxGpio || gpio == kIrRxGpio;
}

void xyY_to_rgb(uint16_t x_raw, uint16_t y_raw, uint8_t brightness_percent,
                uint8_t *out_r, uint8_t *out_g, uint8_t *out_b)
{
    if (y_raw == 0) {
        *out_r = *out_g = *out_b = 0;
        return;
    }

    double x = static_cast<double>(x_raw) / 65535.0;
    double y = static_cast<double>(y_raw) / 65535.0;
    if (y <= 1e-6) {
        *out_r = *out_g = *out_b = 0;
        return;
    }

    double Y = 1.0;
    double X = (Y / y) * x;
    double Z = (Y / y) * (1.0 - x - y);

    double r_lin = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
    double g_lin = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
    double b_lin = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;

    r_lin = r_lin < 0.0 ? 0.0 : r_lin;
    g_lin = g_lin < 0.0 ? 0.0 : g_lin;
    b_lin = b_lin < 0.0 ? 0.0 : b_lin;

    double max_lin = r_lin;
    if (g_lin > max_lin) max_lin = g_lin;
    if (b_lin > max_lin) max_lin = b_lin;
    if (max_lin > 1.0) {
        r_lin /= max_lin;
        g_lin /= max_lin;
        b_lin /= max_lin;
    }

    auto gamma = [](double c) {
        return (c <= 0.0031308) ? (12.92 * c) : (1.055 * std::pow(c, 1.0 / 2.4) - 0.055);
    };

    int ri = static_cast<int>(std::round(gamma(r_lin) * 255.0));
    int gi = static_cast<int>(std::round(gamma(g_lin) * 255.0));
    int bi = static_cast<int>(std::round(gamma(b_lin) * 255.0));

    ri = (ri * brightness_percent) / 100;
    gi = (gi * brightness_percent) / 100;
    bi = (bi * brightness_percent) / 100;

    *out_r = static_cast<uint8_t>(ri < 0 ? 0 : (ri > 255 ? 255 : ri));
    *out_g = static_cast<uint8_t>(gi < 0 ? 0 : (gi > 255 ? 255 : gi));
    *out_b = static_cast<uint8_t>(bi < 0 ? 0 : (bi > 255 ? 255 : bi));
}

void hsv_to_rgb(uint8_t hue, uint8_t sat, uint8_t brightness_percent,
                uint8_t *out_r, uint8_t *out_g, uint8_t *out_b)
{
    float h = (hue / 254.0f) * 360.0f;
    float s = sat / 254.0f;
    float v = brightness_percent / 100.0f;

    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r = 0, g = 0, b = 0;
    if (h < 60.0f) { r = c; g = x; }
    else if (h < 120.0f) { r = x; g = c; }
    else if (h < 180.0f) { g = c; b = x; }
    else if (h < 240.0f) { g = x; b = c; }
    else if (h < 300.0f) { r = x; b = c; }
    else { r = c; b = x; }

    *out_r = static_cast<uint8_t>(std::round((r + m) * 255.0f));
    *out_g = static_cast<uint8_t>(std::round((g + m) * 255.0f));
    *out_b = static_cast<uint8_t>(std::round((b + m) * 255.0f));
}
} // namespace

esp_err_t RgbBackend::init(const RgbConfig &cfg)
{
    m_cfg = cfg;
    m_ready = false;

    if (cfg.type == RgbBackendType::None) {
        ESP_LOGW(TAG, "RGB backend disabled until a valid hardware config is selected");
        return ESP_OK;
    }

    if (cfg.type == RgbBackendType::Ws2813) {
        if (invalid_gpio(cfg.ws2813_gpio)) {
            ESP_LOGW(TAG, "WS2813 Data-GPIO nicht konfiguriert (%d). LED-Ausgabe bleibt deaktiviert.", cfg.ws2813_gpio);
            return ESP_ERR_INVALID_ARG;
        }
        if (conflicts_with_ir(cfg.ws2813_gpio)) {
            ESP_LOGE(TAG, "GPIO %d kollidiert mit IR TX (%d) oder IR RX (%d)!", cfg.ws2813_gpio, kIrTxGpio, kIrRxGpio);
            return ESP_ERR_INVALID_ARG;
        }
        if (cfg.ws2813_led_count <= 0) {
            return ESP_ERR_INVALID_ARG;
        }

        rmt_tx_channel_config_t led_channel_cfg = {
            .gpio_num = static_cast<gpio_num_t>(cfg.ws2813_gpio),
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = kWs2813ResolutionHz,
            .mem_block_symbols = 48,
            .trans_queue_depth = 4,
            .intr_priority = 0,
            .flags = {
                .invert_out = false,
                .with_dma = false,
                .io_loop_back = false,
                .io_od_mode = false,
                .allow_pd = false,
            }
        };
        rmt_channel_handle_t chan = nullptr;
        ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&led_channel_cfg, &chan), TAG, "rmt_new_tx_channel failed");

        led_strip_encoder_config_t enc_cfg = { .resolution = kWs2813ResolutionHz };
        rmt_encoder_handle_t enc = nullptr;
        ESP_RETURN_ON_ERROR(rmt_new_led_strip_encoder(&enc_cfg, &enc), TAG, "rmt_new_led_strip_encoder failed");
        ESP_RETURN_ON_ERROR(rmt_enable(chan), TAG, "rmt_enable failed");

        m_rmt_channel = chan;
        m_rmt_encoder = enc;
        m_ready = true;
        ESP_LOGI(TAG, "WS2813 initialisiert auf GPIO %d (%d LEDs)", cfg.ws2813_gpio, cfg.ws2813_led_count);
        return ESP_OK;
    }

    if (cfg.type == RgbBackendType::AnalogLedc) {
        if (invalid_gpio(cfg.gpio_r) || invalid_gpio(cfg.gpio_g) || invalid_gpio(cfg.gpio_b)) {
            return ESP_ERR_INVALID_ARG;
        }

        ledc_timer_config_t timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_8_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");

        const int gpios[3] = {cfg.gpio_r, cfg.gpio_g, cfg.gpio_b};
        for (int i = 0; i < 3; ++i) {
            ledc_channel_config_t channel = {
                .gpio_num = gpios[i],
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .channel = static_cast<ledc_channel_t>(i),
                .intr_type = LEDC_INTR_DISABLE,
                .timer_sel = LEDC_TIMER_0,
                .duty = 0,
                .hpoint = 0,
            };
            ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "ledc channel");
        }

        m_ready = true;
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t RgbBackend::apply(const RgbState &state)
{
    if (!m_ready || m_cfg.type == RgbBackendType::None) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t r = 0, g = 0, b = 0;
    if (state.on) {
        uint8_t brightness_pct = static_cast<uint8_t>((static_cast<uint16_t>(state.level) * 100U) / 254U);
        if (state.use_xy) {
            xyY_to_rgb(state.color_x, state.color_y, brightness_pct, &r, &g, &b);
        } else {
            hsv_to_rgb(state.hue, state.saturation, brightness_pct, &r, &g, &b);
        }
    }

    if (m_cfg.type == RgbBackendType::Ws2813) {
        std::vector<uint8_t> pixels(static_cast<size_t>(m_cfg.ws2813_led_count) * 3, 0);
        for (int i = 0; i < m_cfg.ws2813_led_count; ++i) {
            if (m_cfg.ws2813_grb_order) {
                pixels[i * 3 + 0] = g;
                pixels[i * 3 + 1] = r;
                pixels[i * 3 + 2] = b;
            } else {
                pixels[i * 3 + 0] = r;
                pixels[i * 3 + 1] = g;
                pixels[i * 3 + 2] = b;
            }
        }
        rmt_transmit_config_t tx_cfg = { .loop_count = 0, .flags = { .eot_level = 0, .queue_nonblocking = false } };
        auto chan = static_cast<rmt_channel_handle_t>(m_rmt_channel);
        auto enc = static_cast<rmt_encoder_handle_t>(m_rmt_encoder);
        ESP_RETURN_ON_ERROR(rmt_transmit(chan, enc, pixels.data(), pixels.size(), &tx_cfg), TAG, "rmt_transmit failed");
        return rmt_tx_wait_all_done(chan, portMAX_DELAY);
    }

    if (m_cfg.type == RgbBackendType::AnalogLedc) {
        const uint8_t rgb[3] = {r, g, b};
        for (int i = 0; i < 3; ++i) {
            uint32_t duty = m_cfg.active_high ? rgb[i] : (255 - rgb[i]);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(i), duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(i));
        }
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

bool RgbBackend::is_ready() const
{
    return m_ready;
}

} // namespace smartroom
