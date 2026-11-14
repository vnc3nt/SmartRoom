#include "smartroom_rgb.h"

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

namespace smartroom {
namespace {
static const char *TAG = "smartroom_rgb";

bool invalid_gpio(int gpio)
{
    return gpio < 0 || gpio >= 48;
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

    // WS2812 backend intentionally blocked until dedicated driver is configured.
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t RgbBackend::apply(const RgbState &state)
{
    if (!m_ready || m_cfg.type == RgbBackendType::None) {
        return ESP_ERR_INVALID_STATE;
    }

    if (m_cfg.type != RgbBackendType::AnalogLedc) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t value = state.on ? state.level : 0;
    uint32_t duty = m_cfg.active_high ? value : (255 - value);
    for (int i = 0; i < 3; ++i) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(i), duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(i));
    }
    return ESP_OK;
}

bool RgbBackend::is_ready() const
{
    return m_ready;
}

} // namespace smartroom
