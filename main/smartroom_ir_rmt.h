#pragma once

#include "smartroom_ir_codec.h"

#include <vector>

#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace smartroom {

struct IrRmtConfig {
    gpio_num_t rx_gpio;
    gpio_num_t tx_gpio;
    uint32_t resolution_hz;
    uint32_t default_carrier_hz;
    size_t max_symbols;
};

class IrRmt {
public:
    esp_err_t init(const IrRmtConfig &cfg);
    esp_err_t start_capture();
    esp_err_t wait_capture(IrSignal &out, TickType_t timeout_ticks);
    esp_err_t transmit(const IrSignal &signal);

private:
    static bool on_rx_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data);

    IrRmtConfig m_cfg = {};
    rmt_channel_handle_t m_rx = nullptr;
    rmt_channel_handle_t m_tx = nullptr;
    rmt_encoder_handle_t m_copy_encoder = nullptr;
    QueueHandle_t m_rx_queue = nullptr;

    std::vector<rmt_symbol_word_t> m_rx_symbols;
    std::vector<rmt_symbol_word_t> m_tx_symbols;
    volatile bool m_tx_active = false;
};

} // namespace smartroom
