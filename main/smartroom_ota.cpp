#include "smartroom_ota.h"

#include <string>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace smartroom {
namespace {
constexpr const char *TAG = "smartroom_ota";
constexpr const char *kVersionUrl =
    "https://github.com/vnc3nt/SmartRoom/releases/download/latest-ota/version.json";
constexpr const char *kFirmwareUrl =
    "https://github.com/vnc3nt/SmartRoom/releases/download/latest-ota/SmartRoom.bin";
constexpr uint32_t kOtaCheckIntervalMs = 6 * 60 * 60 * 1000;
constexpr uint32_t kOtaRetryMs = 60 * 1000;
constexpr uint32_t kNetworkPollMs = 5000;

bool network_ready()
{
    auto *netif = esp_netif_get_default_netif();
    esp_netif_ip_info_t info = {};
    return netif && esp_netif_is_netif_up(netif) &&
           esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0;
}

esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data && evt->data_len > 0) {
        auto *body = static_cast<std::string *>(evt->user_data);
        if (body->size() + evt->data_len <= 2048) {
            body->append(static_cast<const char *>(evt->data), evt->data_len);
        }
    }
    return ESP_OK;
}

esp_err_t fetch_remote_version(std::string &out_version)
{
    std::string body;
    esp_http_client_config_t cfg = {};
    cfg.url = kVersionUrl;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 10000;
    cfg.buffer_size = 2048;
    cfg.buffer_size_tx = 2048;
    cfg.event_handler = http_event_handler;
    cfg.user_data = &body;

    auto client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200 || body.empty()) {
        ESP_LOGW(TAG, "Manifest fetch failed: HTTP %d, %s", status, esp_err_to_name(err));
        return err == ESP_OK ? ESP_FAIL : err;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const cJSON *ver = cJSON_GetObjectItem(root, "version");
    const bool valid = cJSON_IsString(ver) && ver->valuestring && ver->valuestring[0];
    if (valid) {
        out_version = ver->valuestring;
    }
    cJSON_Delete(root);
    return valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

void ota_task(void *)
{
    while (true) {
        while (!network_ready()) {
            vTaskDelay(pdMS_TO_TICKS(kNetworkPollMs));
        }
        const esp_err_t result = ota_check_and_update();
        vTaskDelay(pdMS_TO_TICKS(result == ESP_OK ? kOtaCheckIntervalMs : kOtaRetryMs));
    }
}
} // namespace

esp_err_t ota_check_and_update()
{
    const esp_app_desc_t *running = esp_app_get_description();
    std::string remote_version;
    const esp_err_t manifest_err = fetch_remote_version(remote_version);
    if (manifest_err != ESP_OK) {
        ESP_LOGW(TAG, "OTA-Versionspruefung fehlgeschlagen: %s", esp_err_to_name(manifest_err));
        return manifest_err;
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
    http_cfg.buffer_size = 2048;
    http_cfg.buffer_size_tx = 2048;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;
    const esp_err_t ret = esp_https_ota(&ota_cfg);
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
    const BaseType_t ok = xTaskCreate(ota_task, "smartroom_ota", 8192, nullptr, 3, nullptr);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

} // namespace smartroom
