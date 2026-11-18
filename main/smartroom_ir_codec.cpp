#include "smartroom_ir_codec.h"

#include <algorithm>
#include <cmath>

namespace smartroom {
namespace {
constexpr uint16_t kNoiseMinUs = 120;
constexpr uint32_t kBucketUs = 50;
constexpr float kToleranceRatio = 0.2f;
constexpr uint16_t kToleranceFloorUs = 150;
uint32_t fnv1a_append(uint32_t h, uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        h ^= (v >> (i * 8)) & 0xFF;
        h *= 16777619u;
    }
    return h;
}
} // namespace

std::vector<uint16_t> normalize_timings(const std::vector<uint16_t> &durations)
{
    std::vector<uint16_t> out;
    out.reserve(durations.size());
    for (uint16_t d : durations) {
        if (d < kNoiseMinUs) {
            out.push_back(d);
            continue;
        }
        const uint32_t rounded = ((uint32_t{d} + kBucketUs / 2) / kBucketUs) * kBucketUs;
        out.push_back(static_cast<uint16_t>(std::min<uint32_t>(rounded, UINT16_MAX)));
    }
    return out;
}

bool is_duplicate_signal(const IrSignal &lhs, const IrSignal &rhs)
{
    if (lhs.level0 != rhs.level0 || lhs.carrier_hz != rhs.carrier_hz ||
        lhs.carrier_active_high != rhs.carrier_active_high) {
        return false;
    }
    const auto a = normalize_timings(lhs.durations_us);
    const auto b = normalize_timings(rhs.durations_us);
    if (a.size() != b.size() || a.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] == 0 || b[i] == 0) {
            return false;
        }
        const uint16_t ref = std::max(a[i], b[i]);
        const int difference = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        const uint16_t tolerated = std::max<uint16_t>(kToleranceFloorUs,
            static_cast<uint16_t>(ref * kToleranceRatio));
        if (difference > tolerated) {
            return false;
        }
    }
    return true;
}

uint32_t stable_signal_id(const IrSignal &signal)
{
    uint32_t h = 2166136261u;
    h = fnv1a_append(h, signal.level0 ? 1u : 0u);
    for (uint16_t d : normalize_timings(signal.durations_us)) {
        h = fnv1a_append(h, d);
    }
    return h;
}

IrFrameValidation validate_frame(const std::vector<uint16_t> &durations, std::size_t max_symbols)
{
    IrFrameValidation status;
    if (durations.empty()) {
        return status;
    }
    if (durations.size() > max_symbols) {
        status.overflow = true;
        return status;
    }
    std::size_t small = 0;
    bool has_zero = false;
    for (uint16_t d : durations) {
        small += d < kNoiseMinUs;
        has_zero = has_zero || d == 0;
    }
    status.noisy = small > durations.size() / 3;
    status.repeat = durations.size() <= 4;
    status.ok = !status.noisy && !status.repeat && !has_zero;
    return status;
}
} // namespace smartroom
