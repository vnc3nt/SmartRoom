/*
 * SmartRoom - Matter-basierter IR-Fernbedienungs-Controller
 * 
 * Dieses Programm erstellt einen Matter-Button in HomeKit, der beim Drücken
 * ein IR NEC Signal sendet.
 */

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_event.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

// IR Encoder includes
#include "driver/rmt_tx.h"
#include "ir_nec_encoder.h"

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static const char *TAG = "SmartRoom";

// WiFi Configuration (optional - für hardcoded WiFi)
// Wenn leer gelassen, nutzt Matter BLE Provisioning während Pairing
#define WIFI_SSID           ""  // Dein WiFi Name
#define WIFI_PASSWORD       ""     // Dein WiFi Passwort

// IR Transmitter Configuration
#define IR_RESOLUTION_HZ     1000000 // 1MHz resolution, 1 tick = 1us
#define IR_TX_GPIO_NUM       18      // GPIO für IR LED

// Matter Konfiguration
constexpr auto k_timeout_seconds = 300;

// Globale Variablen für IR
static rmt_channel_handle_t tx_channel = NULL;
static rmt_encoder_handle_t nec_encoder = NULL;
static rmt_transmit_config_t transmit_config = {
    .loop_count = 0, // kein Loop
};

// IR Signal Konfiguration (das Signal, das gesendet wird)
static const ir_nec_scan_code_t ir_signal = {
    .address = 0xFF00,
    .command = 0xBA45,
};

/**
 * @brief IR Transmitter initialisieren
 */
static esp_err_t ir_transmitter_init(void)
{
    ESP_LOGI(TAG, "Initialisiere IR Transmitter auf GPIO %d", IR_TX_GPIO_NUM);
    
    // TX Channel konfigurieren
    rmt_tx_channel_config_t tx_channel_cfg = {
        .gpio_num = (gpio_num_t)IR_TX_GPIO_NUM,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags = {
            .invert_out = false,
            .with_dma = false,
        }
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_channel_cfg, &tx_channel));
    
    // Carrier (38kHz) konfigurieren
    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = 38000,
        .duty_cycle = 0.33,
        .flags = {
            .polarity_active_low = false,
        }
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(tx_channel, &carrier_cfg));
    
    // NEC Encoder erstellen
    ir_nec_encoder_config_t nec_encoder_cfg = {
        .resolution = IR_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_ir_nec_encoder(&nec_encoder_cfg, &nec_encoder));
    
    // Channel aktivieren
    ESP_ERROR_CHECK(rmt_enable(tx_channel));
    
    ESP_LOGI(TAG, "IR Transmitter erfolgreich initialisiert");
    return ESP_OK;
}

/**
 * @brief IR Signal senden
 */
static esp_err_t send_ir_signal(void)
{
    ESP_LOGI(TAG, "Sende IR Signal: Address=0x%04X, Command=0x%04X", 
             ir_signal.address, ir_signal.command);
    
    esp_err_t err = rmt_transmit(tx_channel, nec_encoder, &ir_signal, 
                                  sizeof(ir_signal), &transmit_config);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "IR Signal erfolgreich gesendet!");
    } else {
        ESP_LOGE(TAG, "Fehler beim Senden des IR Signals: %s", esp_err_to_name(err));
    }
    
    return err;
}

/**
 * @brief Matter Attribute Update Callback
 * 
 * Diese Funktion wird aufgerufen, wenn ein Attribut über Matter geändert wird
 */
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                          uint16_t endpoint_id, uint32_t cluster_id,
                                          uint32_t attribute_id, esp_matter_attr_val_t *val,
                                          void *priv_data)
{
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        ESP_LOGI(TAG, "Attribute Update: endpoint=0x%04X, cluster=0x%08X, attribute=0x%08X",
                 endpoint_id, cluster_id, attribute_id);
        
        // Prüfen ob es sich um den OnOff Cluster handelt (unser Button)
        if (cluster_id == OnOff::Id) {
            if (attribute_id == OnOff::Attributes::OnOff::Id) {
                bool new_state = val->val.b;
                ESP_LOGI(TAG, "Button Zustand: %s", new_state ? "GEDRÜCKT" : "LOSGELASSEN");
                
                // Wenn der Button gedrückt wird (true), sende IR Signal
                if (new_state) {
                    send_ir_signal();
                    
                    // Button sofort wieder auf "aus" setzen (Momentary Switch Verhalten)
                    // Dies macht es zu einem echten Button statt einem Schalter
                    esp_matter_attr_val_t off_val = esp_matter_bool(false);
                    attribute::report::send(endpoint_id, cluster_id, attribute_id, &off_val);
                }
            }
        }
    }

    return err;
}

/**
 * @brief Matter Identification Callback
 * 
 * Diese Funktion wird während des Pairing-Prozesses aufgerufen
 */
static esp_err_t app_identification_cb(identification::callback_type_t type,
                                        uint16_t endpoint_id, uint8_t effect_id,
                                        uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type=%u, endpoint=0x%04X, effect=%u",
             type, endpoint_id, effect_id);
    return ESP_OK;
}

/**
 * @brief Matter Device Event Handler
 */
static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address geändert");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning abgeschlossen!");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning Fail-Safe-Timer abgelaufen");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning Session gestartet");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning Session gestoppt");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning Window geöffnet");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning Window geschlossen");
        break;

    default:
        break;
    }
}

/**
 * @brief Main Application Entry Point
 */
extern "C" void app_main(void)
{
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   SmartRoom IR Controller v1.0");
    ESP_LOGI(TAG, "   Matter + HomeKit Integration");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    /* Initialize NVS */
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "✓ NVS initialisiert");

    /* WiFi Info anzeigen */
    #if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
    if (strlen(WIFI_SSID) > 0 && strlen(WIFI_PASSWORD) > 0) {
        ESP_LOGI(TAG, "📶 WiFi konfiguriert: SSID='%s'", WIFI_SSID);
        ESP_LOGI(TAG, "   (WiFi wird während Matter Commissioning verbunden)");
    } else {
        ESP_LOGI(TAG, "📶 Kein WiFi konfiguriert - Matter nutzt BLE Provisioning");
    }
    #else
    ESP_LOGI(TAG, "📶 WiFi Provisioning: Matter nutzt BLE während Pairing");
    #endif

    /* Initialize IR Transmitter */
    ESP_ERROR_CHECK(ir_transmitter_init());
    ESP_LOGI(TAG, "✓ IR Transmitter initialisiert");

    /* Create a Matter node */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "✗ Fehler beim Erstellen des Matter-Knotens");
        return;
    }
    ESP_LOGI(TAG, "✓ Matter Node erstellt");

    /* 
     * Erstelle einen "Generic Switch" Endpoint
     * In HomeKit wird dies als Programmable Switch (Button) erscheinen
     */
    on_off_plugin_unit::config_t switch_config;
    switch_config.on_off.on_off = false;
    
    endpoint_t *endpoint = on_off_plugin_unit::create(node, &switch_config, 
                                                       ENDPOINT_FLAG_NONE, NULL);
    if (!endpoint) {
        ESP_LOGE(TAG, "✗ Fehler beim Erstellen des Switch-Endpunkts");
        return;
    }

    uint16_t endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "✓ Switch Endpoint erstellt (ID: %d)", endpoint_id);

    /* Setup DAC (Device Attestation Credentials) */
    esp_matter::set_custom_dac_provider(chip::Credentials::Examples::GetExampleDACProvider());

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "✗ Fehler beim Starten von Matter: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "✓ Matter Stack gestartet");

    /* Print Pairing Information */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    PAIRING INFORMATIONEN");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Öffne die Home App auf deinem iPhone");
    ESP_LOGI(TAG, "Tippe auf '+' und dann 'Gerät hinzufügen'");
    ESP_LOGI(TAG, "Wähle 'Weitere Optionen...'");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Verwende diesen Setup-Code:");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "    Manual Pairing Code: 20202021");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Oder scanne den QR-Code im seriellen Monitor");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "SmartRoom läuft und ist bereit für Pairing!");
    ESP_LOGI(TAG, "Drücke den Button in der Home App um das IR-Signal zu senden");
}
