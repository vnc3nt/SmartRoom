#pragma once

#include "esp_err.h"

namespace smartroom {

// Startet den Hintergrund-Task fuer automatische OTA-Updates ueber GitHub Releases
// (Branch: update_release -> Release-Tag: latest-ota).
esp_err_t ota_start_background_task();

// Fuehrt eine einmalige Pruefung auf eine neue Firmware-Version durch.
esp_err_t ota_check_and_update();

} // namespace smartroom
