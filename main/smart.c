#include "esp_log.h"
#include "esp_matter.h"

static const char *TAG = "Matter";

void app_main(void) {
    ESP_LOGI(TAG, "Matter initialisieren...");

    // Konfigurationsdaten für den Matter-Knoten
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des Matter-Knotens");
        return;
    }

    // Konfigurationsdaten für das Licht-Endpunkt
    on_off_light::config_t light_config;
    light_config.on_off.on_off = false; // Standardmäßig ausgeschaltet

    endpoint_t *endpoint = on_off_light::create(node, &light_config, ENDPOINT_FLAG_NONE, NULL);
    if (!endpoint) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des Licht-Endpunkts");
        return;
    }

    uint16_t endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Licht-Endpunkt erstellt mit ID %d", endpoint_id);

    // Matter-Knoten starten
    node::start(node);
}
