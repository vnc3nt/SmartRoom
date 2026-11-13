#include "smartroom_ir_codec.h"

#include <algorithm>
#include <cmath>

namespace smartroom {
namespace {
constexpr uint16_t kNoiseMinUs = 120;
constexpr uint16_t kBucketUs = 50;
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
            continue;
        }
        uint16_t quantized = static_cast<uint16_t>(((d + kBucketUs / 2) / kBucketUs) * kBucketUs);
        if (quantized == 0) {
            continue;
        }
        out.push_back(quantized);
    }

    while (!out.empty() && out.back() <= kNoiseMinUs) {
        out.pop_back();
    }
    return out;
}

bool is_duplicate_signal(const IrSignal &lhs, const IrSignal &rhs)
{
    std::vector<uint16_t> a = normalize_timings(lhs.durations_us);
    std::vector<uint16_t> b = normalize_timings(rhs.durations_us);

    if (a.size() != b.size() || a.empty()) {
        return false;
    }

    for (size_t i = 0; i < a.size(); ++i) {
        uint16_t ref = std::max(a[i], b[i]);
        uint16_t abs_diff = static_cast<uint16_t>(std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
        uint16_t tolerated = std::max<uint16_t>(kToleranceFloorUs, static_cast<uint16_t>(ref * kToleranceRatio));
        if (abs_diff > tolerated) {
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

IrFrameValidation validate_frame(const std::vector<uint16_t> &durations, size_t max_symbols)
{
    IrFrameValidation status;
    if (durations.empty()) {
        return status;
    }
    if (durations.size() > max_symbols) {
        status.overflow = true;
        return status;
    }

    size_t small = 0;
    for (uint16_t d : durations) {
        if (d < kNoiseMinUs) {
            ++small;
        }
    }

    status.noisy = (small * 3) > durations.size();
    status.repeat = durations.size() <= 4;
    status.ok = !status.noisy;
    return status;
}

} // namespace smartroom
