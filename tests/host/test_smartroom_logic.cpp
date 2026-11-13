#include <cassert>
#include <vector>

#include "smartroom_ir_codec.h"
#include "smartroom_learning.h"
#include "smartroom_store_record.h"

using namespace smartroom;

static void test_duplicate_normalization()
{
    IrSignal a;
    a.durations_us = {8990, 4470, 560, 1680, 560, 560, 30};

    IrSignal b;
    b.durations_us = {9010, 4510, 550, 1705, 545, 575};

    assert(is_duplicate_signal(a, b));
}

static void test_record_integrity_validation()
{
    StoredSignalRecord rec;
    rec.signal.stable_id = 0x1234;
    rec.signal.carrier_hz = 38000;
    rec.signal.durations_us = {9000, 4500, 560, 560, 560, 1690};

    std::vector<uint8_t> blob;
    assert(encode_record(rec, blob));

    StoredSignalRecord decoded;
    assert(decode_record(blob, decoded));
    assert(decoded.signal.stable_id == rec.signal.stable_id);

    blob.back() ^= 0x55;
    assert(!decode_record(blob, decoded));
}

static void test_learning_state_machine()
{
    LearningStateMachine sm(40, 1000, 3000);

    sm.on_button_edge(true, 0);
    sm.tick(500000);
    assert(sm.state() == LearnState::WaitingLongPress);

    sm.tick(1000000);
    assert(sm.consume_start_capture());
    assert(sm.state() == LearnState::Capturing);

    sm.on_capture_done(true);
    assert(sm.state() == LearnState::Success);

    sm.on_button_edge(true, 2000000);
    sm.on_button_edge(false, 2060000);
    assert(sm.state() == LearnState::Cancelled);
}

int main()
{
    test_duplicate_normalization();
    test_record_integrity_validation();
    test_learning_state_machine();
    return 0;
}
