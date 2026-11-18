#pragma once

#include <cstdint>

namespace smartroom {

enum class LearnState {
    Idle,
    WaitingLongPress,
    Capturing,
    Success,
    Error,
    Cancelled,
    Timeout,
};

class LearningStateMachine {
public:
    LearningStateMachine(uint32_t debounce_ms, uint32_t long_press_ms, uint32_t timeout_ms);

    void on_button_edge(bool pressed, uint64_t now_us);
    void on_capture_done(bool ok);
    void cancel();
    void tick(uint64_t now_us);

    LearnState state() const;
    bool should_start_capture() const;
    bool consume_start_capture();

private:
    uint32_t m_debounce_ms;
    uint32_t m_long_press_ms;
    uint32_t m_timeout_ms;

    LearnState m_state = LearnState::Idle;
    bool m_pressed = false;
    bool m_start_capture_pending = false;
    uint64_t m_last_edge_us = 0;
    bool m_have_edge = false;
    uint64_t m_press_start_us = 0;
    uint64_t m_capture_start_us = 0;
};

} // namespace smartroom
