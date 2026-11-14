#include "smartroom_ota.h"

#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace smartroom {
namespace {
static const char *TAG = "smartroom_ota";

// GitHub Release URLs fuer den Branch 'update_release'
constexpr const char *kVersionUrl =
    "https://github.com/vnc3nt/SmartRoom/releases/download/latest-ota/version.json";
constexpr const char *kFirmwareUrl =
    "https://github.com/vnc3nt/SmartRoom/releases/download/latest-ota/SmartRoom.bin";

constexpr uint32_t kOtaCheckIntervalMs = 6 * 60 * 60 * 1000; // Alle 6 Stunden

esp_err_t fetch_remote_version(std::string &out_version)
{
    esp_http_client_config_t cfg = {};
    cfg.url = kVersionUrl;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200 || content_len <= 0 || content_len > 1024) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    std::string body(static_cast<size_t>(content_len), '\0');
    int read_len = esp_http_client_read_response(client, body.data(), content_len);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) {
        return ESP_FAIL;
    }
    cJSON *ver = cJSON_GetObjectItem(root, "version");
    if (cJSON_IsString(ver) && ver->valuestring) {
        out_version = ver->valuestring;
        cJSON_Delete(root);
        return ESP_OK;
    }
    cJSON_Delete(root);
    return ESP_FAIL;
}

void ota_task(void *)
{
    // Kurz warten bis WLAN nach Boot stabil ist
    vTaskDelay(pdMS_TO_TICKS(30000));

    while (true) {
        ota_check_and_update();
        vTaskDelay(pdMS_TO_TICKS(kOtaCheckIntervalMs));
    }
}
} // namespace

esp_err_t ota_check_and_update()
{
    const esp_app_desc_t *running = esp_app_get_description();
    std::string remote_version;
    if (fetch_remote_version(remote_version) != ESP_OK) {
        ESP_LOGD(TAG, "OTA-Versionspruefung fehlgeschlagen (kein Netzwerk oder kein Release)");
        return ESP_FAIL;
    }

    if (remote_version == running->version) {
        ESP_LOGI(TAG, "Firmware ist aktuell (%s)", running->version);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Neue Firmware gefunden: %s (aktuell: %s) -> starte OTA...",
             remote_version.c_str(), running->version);

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = kFirmwareUrl;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    http_cfg.timeout_ms = 30000;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;

    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA erfolgreich! Starte neu...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA fehlgeschlagen: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t ota_start_background_task()
{
    BaseType_t ok = xTaskCreate(ota_task, "smartroom_ota", 8192, nullptr, 3, nullptr);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

} // namespace smartroom
