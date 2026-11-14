#include "smartroom_ir_rmt.h"

#include <algorithm>
#include <cstdint>

#include "esp_check.h"
#include "esp_log.h"

namespace smartroom {
namespace {
static const char *TAG = "smartroom_ir_rmt";
}

bool IrRmt::on_rx_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    (void)channel;
    auto *self = static_cast<IrRmt *>(user_data);
    BaseType_t task_woken = pdFALSE;
    if (!self->m_tx_active) {
        xQueueSendFromISR(self->m_rx_queue, edata, &task_woken);
    }
    return task_woken == pdTRUE;
}

esp_err_t IrRmt::init(const IrRmtConfig &cfg)
{
    m_cfg = cfg;
    m_rx_symbols.assign(cfg.max_symbols, {});

    rmt_rx_channel_config_t rx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = cfg.resolution_hz,
        .mem_block_symbols = static_cast<uint32_t>(cfg.max_symbols),
        .gpio_num = cfg.rx_gpio,
    };
    ESP_RETURN_ON_ERROR(rmt_new_rx_channel(&rx_cfg, &m_rx), TAG, "new rx failed");

    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = cfg.resolution_hz,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = cfg.tx_gpio,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &m_tx), TAG, "new tx failed");

    rmt_carrier_config_t carrier = {
        .frequency_hz = cfg.default_carrier_hz,
        .duty_cycle = 0.33f,
    };
    ESP_RETURN_ON_ERROR(rmt_apply_carrier(m_tx, &carrier), TAG, "carrier failed");

    rmt_copy_encoder_config_t copy_cfg = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&copy_cfg, &m_copy_encoder), TAG, "copy encoder failed");

    m_rx_queue = xQueueCreate(2, sizeof(rmt_rx_done_event_data_t));
    if (!m_rx_queue) {
        return ESP_ERR_NO_MEM;
    }

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = &IrRmt::on_rx_done,
    };
    ESP_RETURN_ON_ERROR(rmt_rx_register_event_callbacks(m_rx, &cbs, this), TAG, "rx cb reg failed");

    ESP_RETURN_ON_ERROR(rmt_enable(m_rx), TAG, "enable rx failed");
    ESP_RETURN_ON_ERROR(rmt_enable(m_tx), TAG, "enable tx failed");
    return ESP_OK;
}

esp_err_t IrRmt::start_capture()
{
    if (!m_rx) {
        return ESP_ERR_INVALID_STATE;
    }

    rmt_receive_config_t rx_cfg = {
        .signal_range_min_ns = 1000,
        .signal_range_max_ns = 30000000,
    };

    return rmt_receive(m_rx, m_rx_symbols.data(), m_rx_symbols.size() * sizeof(rmt_symbol_word_t), &rx_cfg);
}

esp_err_t IrRmt::wait_capture(IrSignal &out, TickType_t timeout_ticks)
{
    rmt_rx_done_event_data_t rx_data = {};
    if (xQueueReceive(m_rx_queue, &rx_data, timeout_ticks) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    out.durations_us.clear();
    out.durations_us.reserve(rx_data.num_symbols * 2);
    out.level0 = rx_data.received_symbols[0].level0;
    for (size_t i = 0; i < rx_data.num_symbols; ++i) {
        auto &s = rx_data.received_symbols[i];
        out.durations_us.push_back(static_cast<uint16_t>(std::min<uint32_t>(s.duration0, UINT16_MAX)));
        if (s.duration1 > 0) {
            out.durations_us.push_back(static_cast<uint16_t>(std::min<uint32_t>(s.duration1, UINT16_MAX)));
        }
    }

    return ESP_OK;
}

esp_err_t IrRmt::transmit(const IrSignal &signal)
{
    if (!m_tx || signal.durations_us.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    rmt_carrier_config_t carrier = {
        .frequency_hz = signal.carrier_hz ? signal.carrier_hz : m_cfg.default_carrier_hz,
        .duty_cycle = 0.33f,
        .flags = {
            .polarity_active_low = signal.carrier_active_high ? 0U : 1U,
        },
    };
    ESP_RETURN_ON_ERROR(rmt_apply_carrier(m_tx, &carrier), TAG, "carrier apply failed");

    m_tx_symbols.clear();
    for (size_t i = 0; i < signal.durations_us.size(); i += 2) {
        rmt_symbol_word_t word = {};
        word.level0 = (i % 4 == 0) ? signal.level0 : !signal.level0;
        word.duration0 = signal.durations_us[i];
        if (i + 1 < signal.durations_us.size()) {
            word.level1 = !word.level0;
            word.duration1 = signal.durations_us[i + 1];
        }
        m_tx_symbols.push_back(word);
    }

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };

    m_tx_active = true;
    esp_err_t err = rmt_transmit(m_tx, m_copy_encoder, m_tx_symbols.data(), m_tx_symbols.size() * sizeof(rmt_symbol_word_t), &tx_cfg);
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(m_tx, pdMS_TO_TICKS(500));
    }
    m_tx_active = false;
    return err;
}

} // namespace smartroom
