#pragma once

#include "smartroom_ir_codec.h"

#include <vector>

#include "esp_err.h"

namespace smartroom {

class SignalStore {
public:
    esp_err_t init();
    esp_err_t load_all(std::vector<IrSignal> &out_signals);
    esp_err_t upsert(const IrSignal &signal);
    esp_err_t erase(uint32_t stable_id);
    esp_err_t clear_all();
    size_t size() const;

private:
    esp_err_t write_index_locked(const std::vector<uint32_t> &ids);
    esp_err_t read_index_locked(std::vector<uint32_t> &ids);

    std::vector<uint32_t> m_ids;
    bool m_ready = false;
};

} // namespace smartroom
