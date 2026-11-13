#pragma once

#include <cstdint>

#include "esp_err.h"

namespace smartroom {

enum class RgbBackendType {
    None,
    Ws2812,
    AnalogLedc,
};

struct RgbConfig {
    RgbBackendType type = RgbBackendType::None;
    int gpio_r = -1;
    int gpio_g = -1;
    int gpio_b = -1;
    int ws2812_gpio = -1;
    int ws2812_led_count = 0;
    bool active_high = true;
};

struct RgbState {
    bool on = false;
    uint8_t level = 254;
    uint8_t hue = 0;
    uint8_t saturation = 0;
};

class RgbBackend {
public:
    esp_err_t init(const RgbConfig &cfg);
    esp_err_t apply(const RgbState &state);
    bool is_ready() const;

private:
    RgbConfig m_cfg = {};
    bool m_ready = false;
};

} // namespace smartroom
