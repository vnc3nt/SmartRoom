#pragma once

#include <cstdint>
#include <vector>

namespace smartroom {

struct IrSignal {
    uint32_t stable_id = 0;
    uint32_t carrier_hz = 38000;
    bool carrier_active_high = true;
    bool level0 = true;
    std::vector<uint16_t> durations_us;
};

struct IrFrameValidation {
    bool ok = false;
    bool noisy = false;
    bool overflow = false;
    bool repeat = false;
};

std::vector<uint16_t> normalize_timings(const std::vector<uint16_t> &durations);
bool is_duplicate_signal(const IrSignal &lhs, const IrSignal &rhs);
uint32_t stable_signal_id(const IrSignal &signal);
IrFrameValidation validate_frame(const std::vector<uint16_t> &durations, size_t max_symbols);

} // namespace smartroom
