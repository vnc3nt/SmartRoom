#include "smartroom_learning.h"

namespace smartroom {

LearningStateMachine::LearningStateMachine(uint32_t debounce_ms, uint32_t long_press_ms, uint32_t timeout_ms)
    : m_debounce_ms(debounce_ms), m_long_press_ms(long_press_ms), m_timeout_ms(timeout_ms)
{
}

void LearningStateMachine::on_button_edge(bool pressed, uint64_t now_us)
{
    const uint64_t debounce_us = static_cast<uint64_t>(m_debounce_ms) * 1000;
    if (m_have_edge && (now_us - m_last_edge_us) < debounce_us) {
        return;
    }
    m_last_edge_us = now_us;
    m_have_edge = true;
    m_pressed = pressed;

    if (pressed) {
        if (m_state == LearnState::Idle || m_state == LearnState::Success || m_state == LearnState::Error ||
            m_state == LearnState::Cancelled || m_state == LearnState::Timeout) {
            m_state = LearnState::WaitingLongPress;
            m_press_start_us = now_us;
        }
    } else {
        if (m_state == LearnState::WaitingLongPress) {
            m_state = LearnState::Cancelled;
        }
    }
}

void LearningStateMachine::on_capture_done(bool ok)
{
    m_start_capture_pending = false;
    m_state = ok ? LearnState::Success : LearnState::Error;
}

void LearningStateMachine::cancel()
{
    m_start_capture_pending = false;
    m_state = LearnState::Cancelled;
}

void LearningStateMachine::tick(uint64_t now_us)
{
    if (m_state == LearnState::WaitingLongPress && m_pressed) {
        uint64_t elapsed_ms = (now_us - m_press_start_us) / 1000;
        if (elapsed_ms >= m_long_press_ms) {
            m_state = LearnState::Capturing;
            m_capture_start_us = now_us;
            m_start_capture_pending = true;
        }
    }

    if (m_state == LearnState::Capturing) {
        uint64_t elapsed_ms = (now_us - m_capture_start_us) / 1000;
        if (elapsed_ms >= m_timeout_ms) {
            m_state = LearnState::Timeout;
            m_start_capture_pending = false;
        }
    }
}

LearnState LearningStateMachine::state() const
{
    return m_state;
}

bool LearningStateMachine::should_start_capture() const
{
    return m_start_capture_pending;
}

bool LearningStateMachine::consume_start_capture()
{
    bool pending = m_start_capture_pending;
    m_start_capture_pending = false;
    return pending;
}

} // namespace smartroom
