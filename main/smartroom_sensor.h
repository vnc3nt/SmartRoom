#pragma once

#include <cstdint>
#include "esp_err.h"

namespace smartroom {

enum class SensorKind {
    Illuminance, // Beleuchtungsstaerke (Lux)
    Humidity,    // Relative Luftfeuchtigkeit (%)
    Co2,         // CO2-Konzentration (ppm)
    Temperature, // Temperatur (C)
};

struct SensorReading {
    bool valid = false;
    float value = 0.0f;
    uint64_t timestamp_us = 0;
};

// Modulare Schnittstelle fuer spaetere Sensor-Erweiterungen (ohne fiktive Werte).
class ISensorProvider {
public:
    virtual ~ISensorProvider() = default;
    virtual const char *name() const = 0;
    virtual SensorKind kind() const = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t read(SensorReading &out) = 0;
};

} // namespace smartroom
