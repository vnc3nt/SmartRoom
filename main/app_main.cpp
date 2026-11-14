#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <unordered_map>
#include <vector>

#include <app/server/Server.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_matter.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "smartroom_config.h"
#include "smartroom_ir_codec.h"
#include "smartroom_ir_rmt.h"
#include "smartroom_ota.h"
#include "smartroom_rgb.h"
#include "smartroom_store.h"

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

namespace {
static const char *TAG = "smartroom";

constexpr int kButtonGpio = 7;
constexpr uint32_t kPairingHoldMs = 3000;
constexpr uint32_t kFactoryResetHoldMs = 10000;

enum class WorkerCmdType {
    StartCapture,
    CancelCapture,
    ReplaySignal,
    ResetOnOff,
    ApplyRgb,
};

struct WorkerCmd {
    WorkerCmdType type;
    uint16_t endpoint_id;
    uint32_t stable_id;
};

smartroom::SignalStore g_store;
smartroom::IrRmt g_ir;
smartroom::RgbBackend g_rgb;

QueueHandle_t g_worker_queue = nullptr;
std::vector<smartroom::IrSignal> g_signals;
std::unordered_map<uint16_t, uint32_t> g_endpoint_to_signal;
endpoint_t *g_node_aggregator = nullptr;
uint16_t g_learn_endpoint = 0;
uint16_t g_rgb_endpoint = 0;

std::atomic<bool> g_internal_onoff_update{false};
std::atomic<bool> g_capture_active{false};
std::atomic<bool> g_capture_cancel_requested{false};

bool endpoint_is_signal(uint16_t endpoint)
{
    return g_endpoint_to_signal.find(endpoint) != g_endpoint_to_signal.end();
}

esp_err_t create_signal_endpoint(node_t *node, const smartroom::IrSignal &signal)
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
    ESP_LOGI(TAG, "Learned signal endpoint created: ep=%u id=%\" PRIu32", id, signal.stable_id);
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
    g_internal_onoff_update.store(true);
    esp_matter_attr_val_t off_val = esp_matter_bool(false);
    attribute::update(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &off_val);
    g_internal_onoff_update.store(false);
}

void worker_task(void *)
{
    WorkerCmd cmd = {};
    while (true) {
        if (xQueueReceive(g_worker_queue, &cmd, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (cmd.type == WorkerCmdType::CancelCapture) {
            g_capture_cancel_requested.store(true);
            continue;
        }

        if (cmd.type == WorkerCmdType::StartCapture) {
            if (g_capture_active.exchange(true)) {
                continue;
            }
            g_capture_cancel_requested.store(false);
            ESP_LOGI(TAG, "IR-Lernfenster aktiv (%u ms)...", static_cast<unsigned>(smartroom::kIrLearnTimeoutMs));

            if (g_ir.start_capture() != ESP_OK) {
                ESP_LOGE(TAG, "Capture start failed");
                g_capture_active.store(false);
                enqueue_cmd({WorkerCmdType::ResetOnOff, g_learn_endpoint, 0});
                continue;
            }

            smartroom::IrSignal captured;
            captured.carrier_hz = smartroom::kIrDefaultCarrierHz;
            captured.carrier_active_high = true;

            esp_err_t err = ESP_ERR_TIMEOUT;
            uint32_t elapsed_ms = 0;
            constexpr uint32_t kStepMs = 200;
            while (elapsed_ms < smartroom::kIrLearnTimeoutMs) {
                if (g_capture_cancel_requested.load()) {
                    err = ESP_ERR_INVALID_STATE;
                    break;
                }
                err = g_ir.wait_capture(captured, pdMS_TO_TICKS(kStepMs));
                if (err == ESP_OK) {
                    break;
                }
                elapsed_ms += kStepMs;
            }

            if (err == ESP_OK && !g_capture_cancel_requested.load()) {
                auto frame_status = smartroom::validate_frame(captured.durations_us, smartroom::kIrMaxSymbols * 2);
                if (!frame_status.ok || frame_status.overflow) {
                    ESP_LOGW(TAG, "Captured frame rejected (noise/overflow)");
                } else {
                    captured.durations_us = smartroom::normalize_timings(captured.durations_us);
                    captured.stable_id = smartroom::stable_signal_id(captured);

                    bool duplicate = false;
                    for (const auto &existing : g_signals) {
                        if (smartroom::is_duplicate_signal(existing, captured)) {
                            duplicate = true;
                            captured.stable_id = existing.stable_id;
                            break;
                        }
                    }

                    if (duplicate) {
                        ESP_LOGI(TAG, "Duplicate IR signal ignored id=%\" PRIu32", captured.stable_id);
                    } else if (g_signals.size() >= smartroom::kIrMaxSignals) {
                        ESP_LOGE(TAG, "Signal store full (%u)", static_cast<unsigned>(smartroom::kIrMaxSignals));
                    } else if (g_store.upsert(captured) != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to persist learned signal");
                    } else {
                        g_signals.push_back(captured);
                        node_t *node = node::get();
                        if (!node || create_signal_endpoint(node, captured) != ESP_OK) {
                            g_store.erase(captured.stable_id);
                            g_signals.pop_back();
                            ESP_LOGE(TAG, "Endpoint allocation failed, rolled back signal");
                        } else {
                            ESP_LOGI(TAG, "Learned new signal id=%\" PRIu32", captured.stable_id);
                        }
                    }
                }
            } else {
                ESP_LOGW(TAG, "IR-Lernfenster beendet (Timeout oder Abbruch)");
            }

            g_capture_active.store(false);
            enqueue_cmd({WorkerCmdType::ResetOnOff, g_learn_endpoint, 0});
            continue;
        }

        if (cmd.type == WorkerCmdType::ReplaySignal) {
            auto it = std::find_if(g_signals.begin(), g_signals.end(),
                                   [&](const smartroom::IrSignal &s) { return s.stable_id == cmd.stable_id; });
            if (it != g_signals.end()) {
                esp_err_t err = g_ir.transmit(*it);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "IR replay failed id=%\" PRIu32 \" err=%s\", it->stable_id, esp_err_to_name(err));
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
            esp_matter_attr_val_t cx = esp_matter_invalid(nullptr);
            esp_matter_attr_val_t cy = esp_matter_invalid(nullptr);
            esp_matter_attr_val_t cmode = esp_matter_invalid(nullptr);

            attribute::get(cmd.endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &onoff);
            attribute::get(cmd.endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, &level);
            attribute::get(cmd.endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id, &hue);
            attribute::get(cmd.endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id, &sat);
            attribute::get(cmd.endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentX::Id, &cx);
            attribute::get(cmd.endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentY::Id, &cy);
            attribute::get(cmd.endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id, &cmode);

            smartroom::RgbState rgb = {
                .on = onoff.val.b,
                .level = level.val.u8,
                .hue = hue.val.u8,
                .saturation = sat.val.u8,
                .color_x = cx.val.u16,
                .color_y = cy.val.u16,
                .use_xy = (cmode.val.u8 == static_cast<uint8_t>(ColorControl::ColorMode::kCurrentXAndCurrentY)),
            };
            g_rgb.apply(rgb);
            continue;
        }
    }
}

void button_task(void *)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << kButtonGpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    uint32_t press_start_ms = 0;
    bool pressed = false;
    bool pairing_triggered = false;
    bool reset_triggered = false;

    while (true) {
        bool is_low = (gpio_get_level(static_cast<gpio_num_t>(kButtonGpio)) == 0);
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

        if (is_low) {
            if (!pressed) {
                pressed = true;
                press_start_ms = now_ms;
                pairing_triggered = false;
                reset_triggered = false;
            } else {
                uint32_t held_ms = now_ms - press_start_ms;
                if (held_ms >= kFactoryResetHoldMs && !reset_triggered) {
                    reset_triggered = true;
                    ESP_LOGW(TAG, "Button 10s gehalten -> Factory Reset");
                    vTaskDelay(pdMS_TO_TICKS(200));
                    esp_matter::factory_reset();
                    vTaskDelete(nullptr);
                } else if (held_ms >= kPairingHoldMs && !pairing_triggered && !reset_triggered) {
                    pairing_triggered = true;
                    ESP_LOGI(TAG, "Button 3s gehalten -> Oeffne Matter Pairing-Fenster (180s)");
                    esp_matter::lock::chip_stack_lock(portMAX_DELAY);
                    chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
                        chip::Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow(
                            chip::System::Clock::Seconds16(180));
                    }, 0);
                    esp_matter::lock::chip_stack_unlock();
                }
            }
        } else {
            pressed = false;
            pairing_triggered = false;
            reset_triggered = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t, uint16_t, uint8_t, uint8_t, void *)
{
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *)
{
    if (type != PRE_UPDATE) {
        return ESP_OK;
    }

    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        if (g_internal_onoff_update.load()) {
            return ESP_OK;
        }
        if (endpoint_id == g_learn_endpoint) {
            enqueue_cmd({val->val.b ? WorkerCmdType::StartCapture : WorkerCmdType::CancelCapture, endpoint_id, 0});
            return ESP_OK;
        }
        if (endpoint_is_signal(endpoint_id) && val->val.b) {
            uint32_t id = g_endpoint_to_signal[endpoint_id];
            enqueue_cmd({WorkerCmdType::ReplaySignal, endpoint_id, id});
            return ESP_OK;
        }
    }

    if (endpoint_id == g_rgb_endpoint &&
        (cluster_id == OnOff::Id || cluster_id == LevelControl::Id || cluster_id == ColorControl::Id)) {
        enqueue_cmd({WorkerCmdType::ApplyRgb, endpoint_id, 0});
    }

    return ESP_OK;
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t)
{
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

} // namespace

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed (%s). Kein automatisches globales Loeschen.", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "SmartRoom start (target: %s)", CONFIG_IDF_TARGET);

    ESP_ERROR_CHECK(g_store.init());
    ESP_ERROR_CHECK(g_store.load_all(g_signals));

    smartroom::IrRmtConfig ir_cfg = {
        .rx_gpio = static_cast<gpio_num_t>(smartroom::kIrRxGpio),
        .tx_gpio = static_cast<gpio_num_t>(smartroom::kIrTxGpio),
        .resolution_hz = smartroom::kIrResolutionHz,
        .default_carrier_hz = smartroom::kIrDefaultCarrierHz,
        .max_symbols = smartroom::kIrMaxSymbols,
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

    on_off_plug_in_unit::config_t learn_cfg;
    learn_cfg.on_off.on_off = false;
    endpoint_t *learn_ep = on_off_plug_in_unit::create(node, &learn_cfg, ENDPOINT_FLAG_NONE, nullptr);
    ESP_ERROR_CHECK(learn_ep ? ESP_OK : ESP_FAIL);
    g_learn_endpoint = endpoint::get_id(learn_ep);

    extended_color_light::config_t rgb_matter_cfg;
    rgb_matter_cfg.on_off.on_off = false;
    rgb_matter_cfg.level_control.current_level = 1;
    rgb_matter_cfg.color_control.color_mode = static_cast<uint8_t>(ColorControl::ColorMode::kCurrentXAndCurrentY);
    rgb_matter_cfg.color_control.enhanced_color_mode = static_cast<uint8_t>(ColorControl::ColorMode::kCurrentXAndCurrentY);
    endpoint_t *rgb_ep = extended_color_light::create(node, &rgb_matter_cfg, ENDPOINT_FLAG_NONE, nullptr);
    ESP_ERROR_CHECK(rgb_ep ? ESP_OK : ESP_FAIL);
    g_rgb_endpoint = endpoint::get_id(rgb_ep);

    for (const auto &sig : g_signals) {
        ESP_ERROR_CHECK(create_signal_endpoint(node, sig));
    }

    smartroom::RgbConfig rgb_cfg;
    rgb_cfg.type = (smartroom::kWs2813DataGpio >= 0) ? smartroom::RgbBackendType::Ws2813 : smartroom::RgbBackendType::None;
    rgb_cfg.ws2813_gpio = smartroom::kWs2813DataGpio;
    rgb_cfg.ws2813_led_count = smartroom::kWs2813LedCount;
    rgb_cfg.ws2813_grb_order = smartroom::kWs2813ColorOrderGrb;
    err = g_rgb.init(rgb_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RGB backend deaktiviert (%s).", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(esp_matter::start(app_event_cb));

    xTaskCreate(worker_task, "smartroom_worker", 8192, nullptr, 5, nullptr);
    xTaskCreate(button_task, "smartroom_button", 4096, nullptr, 5, nullptr);
    smartroom::ota_start_background_task();

    ESP_LOGI(TAG, "Loaded %u learned signals", static_cast<unsigned>(g_signals.size()));
}
