#include "smartroom_ir_codec.h"
#include <cstdlib>
#include <iostream>

#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "Failed at line " << __LINE__ << "\n"; std::exit(1); } } while (false)

int main()
{
    using namespace smartroom;
    const std::vector<uint16_t> raw{9000, 4500, 40, 560, 560, 65535};
    const auto normalized = normalize_timings(raw);
    CHECK(normalized.size() == raw.size());
    CHECK(normalized[2] == 40);
    CHECK(normalized.back() == 65535);
    CHECK(raw[2] == 40 && raw.back() == 65535);
    CHECK(normalize_timings({0, 1, 119}) == std::vector<uint16_t>({0, 1, 119}));
    IrSignal a;
    a.durations_us = {9000, 4500, 560, 560, 560, 1690};
    IrSignal b = a;
    b.durations_us = {9050, 4450, 570, 550, 560, 1700};
    CHECK(is_duplicate_signal(a, b));
    b = a; b.level0 = !a.level0;
    CHECK(!is_duplicate_signal(a, b));
    b = a; b.carrier_hz = 40000;
    CHECK(!is_duplicate_signal(a, b));
    b = a; b.carrier_active_high = false;
    CHECK(!is_duplicate_signal(a, b));
    b = a; b.durations_us.insert(b.durations_us.begin() + 2, 40);
    CHECK(!is_duplicate_signal(a, b));
    b = a; b.durations_us[2] = 0;
    CHECK(!is_duplicate_signal(b, b));
    CHECK(!validate_frame({}, 100).ok);
    CHECK(validate_frame({9000, 2250, 560}, 100).repeat);
    CHECK(!validate_frame({9000, 2250, 560}, 100).ok);
    CHECK(validate_frame(a.durations_us, 5).overflow);
    CHECK(validate_frame(a.durations_us, 6).ok);
    CHECK(!validate_frame({9000, 4500, 0, 560, 560, 1690}, 100).ok);
    CHECK(stable_signal_id(a) == stable_signal_id(a));
    std::cout << "IR codec regression tests passed\n";
}
