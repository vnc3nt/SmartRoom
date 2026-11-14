#pragma once

#include <cstdint>

#include "esp_err.h"

namespace smartroom {

enum class RgbBackendType {
    None,
    Ws2813,
    AnalogLedc,
};

struct RgbConfig {
    RgbBackendType type = RgbBackendType::None;
    int gpio_r = -1;
    int gpio_g = -1;
    int gpio_b = -1;
    int ws2813_gpio = -1; // -1 = nicht konfiguriert (Ausgabe deaktiviert)
    int ws2813_led_count = 144;
    bool ws2813_grb_order = true;
    bool active_high = true;
};

struct RgbState {
    bool on = false;
    uint8_t level = 254;
    uint8_t hue = 0;
    uint8_t saturation = 0;
    uint16_t color_x = 20480; // D65 White (~0.3127)
    uint16_t color_y = 21560; // D65 White (~0.3290)
    bool use_xy = true;
};

class RgbBackend {
public:
    esp_err_t init(const RgbConfig &cfg);
    esp_err_t apply(const RgbState &state);
    bool is_ready() const;

private:
    RgbConfig m_cfg = {};
    bool m_ready = false;
    void *m_rmt_channel = nullptr;
    void *m_rmt_encoder = nullptr;
};

} // namespace smartroom
