#include <algorithm>
#include <cinttypes>
#include <unordered_map>
#include <vector>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_matter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "smartroom_ir_codec.h"
#include "smartroom_ir_rmt.h"
#include "smartroom_learning.h"
#include "smartroom_rgb.h"
#include "smartroom_store.h"

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

namespace {
static const char *TAG = "smartroom";

enum class WorkerCmdType {
    StartCapture,
    ReplaySignal,
    ResetOnOff,
    ApplyRgb,
};

struct WorkerCmd {
    WorkerCmdType type;
    uint16_t endpoint_id;
    uint32_t stable_id;
};

SignalStore g_store;
IrRmt g_ir;
LearningStateMachine g_learning(CONFIG_SMARTROOM_BUTTON_DEBOUNCE_MS,
                               CONFIG_SMARTROOM_LEARN_LONG_PRESS_MS,
                               CONFIG_SMARTROOM_LEARN_TIMEOUT_MS);
RgbBackend g_rgb;

QueueHandle_t g_worker_queue = nullptr;
std::vector<IrSignal> g_signals;
std::unordered_map<uint16_t, uint32_t> g_endpoint_to_signal;
endpoint_t *g_node_aggregator = nullptr;
uint16_t g_rgb_endpoint = 0;
volatile bool g_internal_onoff_update = false;

bool endpoint_is_signal(uint16_t endpoint)
{
    return g_endpoint_to_signal.find(endpoint) != g_endpoint_to_signal.end();
}

esp_err_t create_signal_endpoint(node_t *node, const IrSignal &signal)
{
    on_off_plug_in_unit::config_t cfg;
    cfg.on_off.on_off = false;

    endpoint_t *ep = on_off_plug_in_unit::create(node, &cfg, ENDPOINT_FLAG_DESTROYABLE, nullptr);
    ESP_RETURN_ON_FALSE(ep != nullptr, ESP_FAIL, TAG, "Failed to create bridged endpoint");
    if (g_node_aggregator) {
        endpoint::set_parent_endpoint(ep, g_node_aggregator);
    }

    uint16_t id = endpoint::get_id(ep);
    g_endpoint_to_signal[id] = signal.stable_id;
    ESP_LOGI(TAG, "Learned signal endpoint created: ep=%u id=%" PRIu32, id, signal.stable_id);
    return ESP_OK;
}

void enqueue_cmd(const WorkerCmd &cmd)
{
    if (g_worker_queue) {
        xQueueSend(g_worker_queue, &cmd, 0);
    }
}

void reset_momentary_onoff(uint16_t endpoint_id)
{
    g_internal_onoff_update = true;
    esp_matter_attr_val_t off_val = esp_matter_bool(false);
    attribute::update(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &off_val);
    g_internal_onoff_update = false;
}

void worker_task(void *)
{
    WorkerCmd cmd = {};
    while (true) {
        if (xQueueReceive(g_worker_queue, &cmd, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (cmd.type == WorkerCmdType::StartCapture) {
            ESP_LOGI(TAG, "Learning active: waiting IR frame");
            if (g_ir.start_capture() != ESP_OK) {
                g_learning.on_capture_done(false);
                ESP_LOGE(TAG, "Capture start failed");
                continue;
            }

            IrSignal captured;
            captured.carrier_hz = CONFIG_SMARTROOM_IR_DEFAULT_CARRIER_HZ;
            captured.carrier_active_high = true;
            esp_err_t err = g_ir.wait_capture(captured, pdMS_TO_TICKS(CONFIG_SMARTROOM_LEARN_TIMEOUT_MS));
            if (err != ESP_OK) {
                g_learning.on_capture_done(false);
                ESP_LOGW(TAG, "Capture timeout/failure");
                continue;
            }

            auto frame_status = validate_frame(captured.durations_us, CONFIG_SMARTROOM_IR_MAX_SYMBOLS * 2);
            if (!frame_status.ok || frame_status.overflow) {
                g_learning.on_capture_done(false);
                ESP_LOGW(TAG, "Captured frame rejected (noise/overflow)");
                continue;
            }

            captured.durations_us = normalize_timings(captured.durations_us);
            captured.stable_id = stable_signal_id(captured);

            bool duplicate = false;
            for (const auto &existing : g_signals) {
                if (is_duplicate_signal(existing, captured)) {
                    duplicate = true;
                    captured.stable_id = existing.stable_id;
                    break;
                }
            }

            if (!duplicate) {
                if (g_signals.size() >= CONFIG_SMARTROOM_IR_MAX_SIGNALS) {
                    g_learning.on_capture_done(false);
                    ESP_LOGE(TAG, "Signal store full");
                    continue;
                }

                if (g_store.upsert(captured) != ESP_OK) {
                    g_learning.on_capture_done(false);
                    ESP_LOGE(TAG, "Failed to persist learned signal");
                    continue;
                }
                g_signals.push_back(captured);
                node_t *node = node::get();
                if (!node || create_signal_endpoint(node, captured) != ESP_OK) {
                    g_learning.on_capture_done(false);
                    g_store.erase(captured.stable_id);
                    g_signals.pop_back();
                    ESP_LOGE(TAG, "Endpoint allocation failed, rolled back signal");
                    continue;
                }
                ESP_LOGI(TAG, "Learned new signal id=%" PRIu32 " pulses=%u", captured.stable_id,
                         static_cast<unsigned>(captured.durations_us.size()));
            } else {
                ESP_LOGI(TAG, "Ignored duplicate learned signal id=%" PRIu32, captured.stable_id);
            }
            g_learning.on_capture_done(true);
            continue;
        }

        if (cmd.type == WorkerCmdType::ReplaySignal) {
            auto it = std::find_if(g_signals.begin(), g_signals.end(), [&](const IrSignal &s) { return s.stable_id == cmd.stable_id; });
            if (it != g_signals.end()) {
                esp_err_t err = g_ir.transmit(*it);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "IR replay failed id=%" PRIu32 " err=%s", it->stable_id, esp_err_to_name(err));
                }
            }
            enqueue_cmd({WorkerCmdType::ResetOnOff, cmd.endpoint_id, cmd.stable_id});
            continue;
        }

        if (cmd.type == WorkerCmdType::ResetOnOff) {
            vTaskDelay(pdMS_TO_TICKS(150));
            reset_momentary_onoff(cmd.endpoint_id);
            continue;
        }

        if (cmd.type == WorkerCmdType::ApplyRgb) {
            esp_matter_attr_val_t onoff = esp_matter_invalid(nullptr);
            esp_matter_attr_val_t level = esp_matter_invalid(nullptr);
            esp_matter_attr_val_t hue = esp_matter_invalid(nullptr);
            esp_matter_attr_val_t sat = esp_matter_invalid(nullptr);
            attribute::get(cmd.endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &onoff);
            attribute::get(cmd.endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, &level);
            attribute::get(cmd.endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id, &hue);
            attribute::get(cmd.endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id, &sat);
            RgbState rgb = {
                .on = onoff.val.b,
                .level = level.val.u8,
                .hue = hue.val.u8,
                .saturation = sat.val.u8,
            };
            g_rgb.apply(rgb);
            continue;
        }
    }
}

void button_task(void *)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << CONFIG_SMARTROOM_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    bool prev_pressed = false;
    while (true) {
        bool pressed = (gpio_get_level(static_cast<gpio_num_t>(CONFIG_SMARTROOM_BUTTON_GPIO)) == 0);
        uint64_t now = esp_timer_get_time();
        if (pressed != prev_pressed) {
            prev_pressed = pressed;
            g_learning.on_button_edge(pressed, now);
        }
        g_learning.tick(now);

        if (g_learning.consume_start_capture()) {
            enqueue_cmd({WorkerCmdType::StartCapture, 0, 0});
            ESP_LOGI(TAG, "Long press detected -> start learning");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    (void)type;
    (void)endpoint_id;
    (void)effect_id;
    (void)effect_variant;
    (void)priv_data;
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    (void)priv_data;
    if (type != PRE_UPDATE) {
        return ESP_OK;
    }

    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id && endpoint_is_signal(endpoint_id)) {
        if (g_internal_onoff_update) {
            return ESP_OK;
        }
        if (val->val.b) {
            uint32_t id = g_endpoint_to_signal[endpoint_id];
            enqueue_cmd({WorkerCmdType::ReplaySignal, endpoint_id, id});
        }
        return ESP_OK;
    }

    if (endpoint_id == g_rgb_endpoint &&
        (cluster_id == OnOff::Id || cluster_id == LevelControl::Id || cluster_id == ColorControl::Id)) {
        enqueue_cmd({WorkerCmdType::ApplyRgb, endpoint_id, 0});
    }

    return ESP_OK;
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    (void)arg;
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "Commissioning failed (failsafe expired)");
        break;
    default:
        break;
    }
}

RgbConfig read_rgb_config()
{
    RgbConfig cfg;
#if CONFIG_SMARTROOM_RGB_BACKEND_WS2812
    cfg.type = RgbBackendType::Ws2812;
#elif CONFIG_SMARTROOM_RGB_BACKEND_ANALOG
    cfg.type = RgbBackendType::AnalogLedc;
#else
    cfg.type = RgbBackendType::None;
#endif
    cfg.gpio_r = CONFIG_SMARTROOM_RGB_GPIO_R;
    cfg.gpio_g = CONFIG_SMARTROOM_RGB_GPIO_G;
    cfg.gpio_b = CONFIG_SMARTROOM_RGB_GPIO_B;
    cfg.ws2812_gpio = CONFIG_SMARTROOM_RGB_RMT_GPIO;
    cfg.ws2812_led_count = CONFIG_SMARTROOM_RGB_LED_COUNT;
    cfg.active_high = CONFIG_SMARTROOM_RGB_ACTIVE_HIGH;
    return cfg;
}

} // namespace

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "SmartRoom start (target from sdkconfig: %s)", CONFIG_IDF_TARGET);

    ESP_ERROR_CHECK(g_store.init());
    ESP_ERROR_CHECK(g_store.load_all(g_signals));

    IrRmtConfig ir_cfg = {
        .rx_gpio = static_cast<gpio_num_t>(CONFIG_SMARTROOM_IR_RX_GPIO),
        .tx_gpio = static_cast<gpio_num_t>(CONFIG_SMARTROOM_IR_TX_GPIO),
        .resolution_hz = CONFIG_SMARTROOM_IR_RESOLUTION_HZ,
        .default_carrier_hz = CONFIG_SMARTROOM_IR_DEFAULT_CARRIER_HZ,
        .max_symbols = CONFIG_SMARTROOM_IR_MAX_SYMBOLS,
    };
    ESP_ERROR_CHECK(g_ir.init(ir_cfg));

    g_worker_queue = xQueueCreate(8, sizeof(WorkerCmd));
    ESP_ERROR_CHECK(g_worker_queue ? ESP_OK : ESP_ERR_NO_MEM);

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ESP_ERROR_CHECK(node ? ESP_OK : ESP_FAIL);

    aggregator::config_t agg_cfg;
    g_node_aggregator = aggregator::create(node, &agg_cfg, ENDPOINT_FLAG_NONE, nullptr);
    ESP_ERROR_CHECK(g_node_aggregator ? ESP_OK : ESP_FAIL);

    extended_color_light::config_t rgb_matter_cfg;
    rgb_matter_cfg.on_off.on_off = false;
    rgb_matter_cfg.level_control.current_level = 1;
    rgb_matter_cfg.color_control.color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
    rgb_matter_cfg.color_control.enhanced_color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
    endpoint_t *rgb_ep = extended_color_light::create(node, &rgb_matter_cfg, ENDPOINT_FLAG_NONE, nullptr);
    ESP_ERROR_CHECK(rgb_ep ? ESP_OK : ESP_FAIL);
    g_rgb_endpoint = endpoint::get_id(rgb_ep);

    for (const auto &sig : g_signals) {
        ESP_ERROR_CHECK(create_signal_endpoint(node, sig));
    }

    RgbConfig rgb_cfg = read_rgb_config();
    err = g_rgb.init(rgb_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RGB backend not initialized (%s). Output remains disabled.", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(esp_matter::start(app_event_cb));

    xTaskCreate(worker_task, "smartroom_worker", 8192, nullptr, 5, nullptr);
    xTaskCreate(button_task, "smartroom_button", 4096, nullptr, 5, nullptr);

    ESP_LOGI(TAG, "Loaded %u learned signals", (unsigned)g_signals.size());
    ESP_LOGI(TAG, "Demodulating receiver note: learned timings are post-demodulation envelope only (carrier cannot be measured).\n"
                  "Some protocols/devices may not replay reliably.");
}


extern "C" size_t smartroom_learned_signal_count(void)
{
    return g_signals.size();
}

extern "C" esp_err_t smartroom_learned_signal_delete(uint32_t stable_id)
{
    auto it = std::find_if(g_signals.begin(), g_signals.end(), [stable_id](const IrSignal &s) { return s.stable_id == stable_id; });
    if (it == g_signals.end()) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(g_store.erase(stable_id), TAG, "erase failed");
    g_signals.erase(it);
    return ESP_OK;
}

extern "C" esp_err_t smartroom_learned_signal_clear(void)
{
    ESP_RETURN_ON_ERROR(g_store.clear_all(), TAG, "clear failed");
    g_signals.clear();
    return ESP_OK;
}
