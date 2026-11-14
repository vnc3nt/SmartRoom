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
#include <esp_matter_feature.h>

// Matter/CHIP includes für Commissioning
#include <app/server/Server.h>
#include <platform/ESP32/ESP32Utils.h>
#include <app/util/attribute-storage.h>
#include <app/CommandHandler.h>
#include <app/ConcreteCommandPath.h>
#include <lib/core/TLVReader.h>
#include <cmath>
#include <app/data-model/Decode.h>
#include <app-common/zap-generated/cluster-objects.h>

// Hardware Driver includes
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Protocol Encoder includes
#include "ir_nec_encoder.h"
#include "led_strip_encoder.h"

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;
using namespace chip::app;
using namespace chip::TLV;

static const char *TAG = "SmartRoom";

// WiFi Configuration (optional - für hardcoded WiFi)
// Wenn leer gelassen, nutzt Matter BLE Provisioning während Pairing
#define WIFI_SSID           ""  // Dein WiFi Name
#define WIFI_PASSWORD       ""     // Dein WiFi Passwort

// IR Transmitter Configuration
#define IR_RESOLUTION_HZ     1000000 // 1MHz resolution, 1 tick = 1us
#define IR_TX_GPIO_NUM       18      // GPIO für IR LED (Transmitter only!)

// WS2813 LED Strip Configuration
#define LED_STRIP_GPIO       19      // GPIO für WS2813 Data (mit Level Shifter!)
#define LED_STRIP_NUM_LEDS   145     // 144 LEDs pro Meter
#define LED_STRIP_RESOLUTION 10000000 // 10MHz resolution für präzises Timing

// Multi-Function Button Configuration (GPIO 7)
#define BUTTON_GPIO    7                    // GPIO für Multi-Function Button
#define PAIRING_HOLD_TIME_MS 3000           // 3 Sekunden = Pairing-Fenster öffnen
#define RESET_HOLD_TIME_MS 10000            // 10 Sekunden = Factory Reset

// Matter Konfiguration
constexpr auto k_timeout_seconds = 300;

// Globale Variablen für IR
static rmt_channel_handle_t ir_tx_channel = NULL;
static rmt_encoder_handle_t nec_encoder = NULL;

// Globale Variablen für LED Strip
static rmt_channel_handle_t led_strip_channel = NULL;
static rmt_encoder_handle_t led_strip_encoder = NULL;
static uint8_t led_strip_pixels[LED_STRIP_NUM_LEDS * 3]; // RGB buffer (GRB order für WS2813)
static rmt_transmit_config_t transmit_config = {
    .loop_count = 0, // kein Loop
    .flags = {
        .eot_level = 0,
        .queue_nonblocking = false,
    }
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
    
    // TX Channel konfigurieren (ESP32-C6: nur 4 Memory Blocks total!)
    rmt_tx_channel_config_t tx_channel_cfg = {
        .gpio_num = (gpio_num_t)IR_TX_GPIO_NUM,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 48,  // Reduziert für Platz für LED Strip
        .trans_queue_depth = 4,
        .intr_priority = 0,
        .flags = {
            .invert_out = false,
            .with_dma = false,
            .io_loop_back = false,
            .io_od_mode = false,
            .allow_pd = false,
        }
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_channel_cfg, &ir_tx_channel));
    
    // Carrier (38kHz) konfigurieren
    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = 38000,
        .duty_cycle = 0.33,
        .flags = {
            .polarity_active_low = false,
            .always_on = false,
        }
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(ir_tx_channel, &carrier_cfg));
    
    // NEC Encoder erstellen
    ir_nec_encoder_config_t nec_encoder_cfg = {
        .resolution = IR_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_ir_nec_encoder(&nec_encoder_cfg, &nec_encoder));
    
    // Channel aktivieren
    ESP_ERROR_CHECK(rmt_enable(ir_tx_channel));
    
    ESP_LOGI(TAG, "IR Transmitter erfolgreich initialisiert");
    return ESP_OK;
}

/**
 * @brief WS2813 LED Strip initialisieren
 */
static esp_err_t led_strip_init(void)
{
    ESP_LOGI(TAG, "Initialisiere WS2813 LED Strip auf GPIO %d (%d LEDs)", LED_STRIP_GPIO, LED_STRIP_NUM_LEDS);
    
    // RMT TX Channel für LED Strip (ESP32-C6: nur 4 Memory Blocks total, kein DMA!)
    rmt_tx_channel_config_t led_channel_cfg = {
        .gpio_num = (gpio_num_t)LED_STRIP_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_STRIP_RESOLUTION,
        .mem_block_symbols = 48,  // Reduziert damit IR + LED beide passen
        .trans_queue_depth = 4,
        .intr_priority = 0,
        .flags = {
            .invert_out = false,
            .with_dma = false, // ESP32-C6 unterstützt kein DMA für RMT!
            .io_loop_back = false,
            .io_od_mode = false,
            .allow_pd = false,
        }
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&led_channel_cfg, &led_strip_channel));
    
    // WS2813 Encoder erstellen
    led_strip_encoder_config_t encoder_cfg = {
        .resolution = LED_STRIP_RESOLUTION,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_cfg, &led_strip_encoder));
    
    // Channel aktivieren
    ESP_ERROR_CHECK(rmt_enable(led_strip_channel));
    
    // Alle LEDs ausschalten
    memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
    
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 0,
            .queue_nonblocking = false,
        }
    };
    ESP_ERROR_CHECK(rmt_transmit(led_strip_channel, led_strip_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_strip_channel, portMAX_DELAY));
    
    ESP_LOGI(TAG, "✓ WS2813 LED Strip initialisiert (144 LEDs, GRB Order)");
    return ESP_OK;
}

/**
 * @brief Multi-Function Button Task
 * 3 Sekunden halten = Commissioning-Fenster öffnen
 * 10 Sekunden halten = Factory Reset
 */
static void multifunction_button_task(void *arg)
{
    // GPIO 7 als Input mit Pull-Up konfigurieren
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BUTTON_GPIO);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    
    ESP_LOGI(TAG, "Multi-Function Button auf GPIO %d:", BUTTON_GPIO);
    ESP_LOGI(TAG, "  • 3s halten  = Pairing-Fenster für Alexa");
    ESP_LOGI(TAG, "  • 10s halten = Factory Reset");
    
    uint32_t press_start_time = 0;
    bool button_pressed = false;
    bool pairing_triggered = false;
    bool reset_triggered = false;
    
    while (1) {
        int button_state = gpio_get_level((gpio_num_t)BUTTON_GPIO);
        
        // Button ist LOW wenn mit GND verbunden (gedrückt)
        if (button_state == 0) {
            if (!button_pressed) {
                // Button wurde gerade gedrückt
                press_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                button_pressed = true;
                pairing_triggered = false;
                reset_triggered = false;
                ESP_LOGI(TAG, "Button gedrückt...");
            } else {
                // Button wird gehalten, prüfe Dauer
                uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                uint32_t press_duration = current_time - press_start_time;
                
                // Prüfe auf Factory Reset (10 Sekunden)
                if (press_duration >= RESET_HOLD_TIME_MS && !reset_triggered) {
                    ESP_LOGW(TAG, "Button für %d Sekunden gedrückt - Factory Reset wird ausgeführt!", 
                             RESET_HOLD_TIME_MS / 1000);
                    reset_triggered = true;
                    
                    // Factory Reset durchführen
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                    esp_matter::factory_reset();
                    
                    // Nach factory_reset() wird System automatisch neu gestartet
                    vTaskDelete(NULL);
                }
                // Prüfe auf Commissioning (3 Sekunden)
                else if (press_duration >= PAIRING_HOLD_TIME_MS && !pairing_triggered && !reset_triggered) {
                    ESP_LOGW(TAG, "Button für %d Sekunden gedrückt - Öffne Pairing-Fenster!", 
                             PAIRING_HOLD_TIME_MS / 1000);
                    pairing_triggered = true;
                    
                    // Commissioning-Fenster für 180 Sekunden öffnen
                    esp_matter::lock::chip_stack_lock(portMAX_DELAY);
                    chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t arg) {
                        chip::Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow(
                            chip::System::Clock::Seconds16(180));
                        ESP_LOGI(TAG, "");
                        ESP_LOGI(TAG, "========================================");
                        ESP_LOGI(TAG, "  ✓ PAIRING-FENSTER GEÖFFNET!");
                        ESP_LOGI(TAG, "========================================");
                        ESP_LOGI(TAG, "");
                        ESP_LOGI(TAG, "Zeit: 180 Sekunden (3 Minuten)");
                        ESP_LOGI(TAG, "");
                        ESP_LOGI(TAG, "ALEXA APP:");
                        ESP_LOGI(TAG, "  1. Geräte → + → Matter");
                        ESP_LOGI(TAG, "  2. 'Ich habe einen Code'");
                        ESP_LOGI(TAG, "  3. Code: MT:X0H.NZ5TMHDT-K");
                        ESP_LOGI(TAG, "     ODER: 20202021");
                        ESP_LOGI(TAG, "");
                        ESP_LOGI(TAG, "========================================");
                    }, 0);
                    esp_matter::lock::chip_stack_unlock();
                }
            }
        } else {
            // Button wurde losgelassen
            if (button_pressed) {
                uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                uint32_t press_duration = current_time - press_start_time;
                
                if (!pairing_triggered && !reset_triggered) {
                    ESP_LOGI(TAG, "Button losgelassen nach %.1f Sekunden", press_duration / 1000.0f);
                    ESP_LOGI(TAG, "  (3s für Pairing, 10s für Reset benötigt)");
                }
                
                button_pressed = false;
                pairing_triggered = false;
                reset_triggered = false;
            }
        }
        
        vTaskDelay(100 / portTICK_PERIOD_MS); // Alle 100ms prüfen
    }
}



/**
 * @brief LED Strip Farbe setzen (alle LEDs gleiche Farbe)
 * @param r Rot (0-255)
 * @param g Grün (0-255)
 * @param b Blau (0-255)
 * @param brightness Helligkeit (0-100%)
 */
static esp_err_t led_strip_set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🎨 led_strip_set_color() aufgerufen:");
    ESP_LOGI(TAG, "   Eingabe RGB: R=%d, G=%d, B=%d", r, g, b);
    ESP_LOGI(TAG, "   Brightness: %d%% (wird angewendet)", brightness);
    
    // Helligkeit anwenden (0-100%)
    r = (r * brightness) / 100;
    g = (g * brightness) / 100;
    b = (b * brightness) / 100;
    
    ESP_LOGI(TAG, "   Nach Brightness: R=%d, G=%d, B=%d", r, g, b);
    ESP_LOGI(TAG, "   🔄 Wandle in GRB-Order für WS2813...");
    
    // WS2813 verwendet GRB Order (durch Test bestätigt!)
    for (int i = 0; i < LED_STRIP_NUM_LEDS; i++) {
        led_strip_pixels[i * 3 + 0] = g;  // Grün (Byte 0)
        led_strip_pixels[i * 3 + 1] = r;  // Rot (Byte 1)
        led_strip_pixels[i * 3 + 2] = b;  // Blau (Byte 2)
    }
    
    ESP_LOGI(TAG, "   Buffer gefüllt: Pixel[0] = G:%d R:%d B:%d (GRB)", 
             led_strip_pixels[0], led_strip_pixels[1], led_strip_pixels[2]);
    
    // An Strip senden
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 0,
            .queue_nonblocking = false,
        }
    };
    
    ESP_LOGI(TAG, "   📡 Sende %d Bytes an RMT...", sizeof(led_strip_pixels));
    esp_err_t err = rmt_transmit(led_strip_channel, led_strip_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config);
    if (err == ESP_OK) {
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_strip_channel, portMAX_DELAY));
        ESP_LOGI(TAG, "✅ LED Strip Update erfolgreich gesendet und bestätigt!");
    } else {
        ESP_LOGE(TAG, "❌ LED Strip Update FEHLER: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "");
    return err;
}

/**
 * @brief LED Strip ausschalten
 */
static esp_err_t led_strip_off(void)
{
    return led_strip_set_color(0, 0, 0, 0);
}

/**
 * @brief IR Signal senden
 */
static esp_err_t send_ir_signal(void)
{
    ESP_LOGI(TAG, "Sende IR Signal: Address=0x%04X, Command=0x%04X", 
             ir_signal.address, ir_signal.command);
    
    esp_err_t err = rmt_transmit(ir_tx_channel, nec_encoder, &ir_signal, 
                                  sizeof(ir_signal), &transmit_config);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "IR Signal erfolgreich gesendet!");
    } else {
        ESP_LOGE(TAG, "Fehler beim Senden des IR Signals: %s", esp_err_to_name(err));
    }
    
    return err;
}

// Globale Variablen für LED Strip Zustand
static bool led_strip_on = false;
static uint8_t led_strip_brightness = 100;  // 0-100%
// XY color space (CIE 1931) - Matter/Apple Home Standard
// D65 White Point: x=0.31271, y=0.32902 → scaled to 0-65535
static uint16_t led_strip_x = 20480;        // ~0.3127 (D65 white)
static uint16_t led_strip_y = 21560;        // ~0.3290 (D65 white)

// Async Update Flags
static volatile bool led_update_pending = false;        // LED hardware update pending
static volatile bool xy_attribute_update_pending = false; // XY attribute sync pending
static volatile bool suppress_attribute_led_update = false; // Verhindert LED-Update bei Attribute-Callback
static uint16_t pending_x = 20480;  // Pending X value for attribute update
static uint16_t pending_y = 21560;  // Pending Y value for attribute update
static uint32_t xy_attribute_update_delay_ms = 0;  // Delay counter for attribute updates

// HSV entfernt - nur noch XY→RGB Konvertierung wird verwendet

/**
 * @brief Convert CIE xy (uint16 0..65535) + relative Y (brightness 0..100%) to sRGB 0..255
 * Based on conversion: x,y -> X,Y,Z then matrix to linear RGB and sRGB gamma
 */
static void xyY_to_rgb(uint16_t x_raw, uint16_t y_raw, uint8_t brightness_percent,
                      uint8_t *out_r, uint8_t *out_g, uint8_t *out_b)
{
    if (y_raw == 0) {
        *out_r = *out_g = *out_b = 0;
        return;
    }

    double x = (double)x_raw / 65535.0;
    double y = (double)y_raw / 65535.0;

    // WICHTIG: Berechne Farbe mit VOLLER Helligkeit (Y=1.0)
    // Helligkeit wird später auf RGB angewendet, um Farbsättigung zu erhalten!
    double Y = 1.0; // Volle Helligkeit für Farbberechnung

    // Avoid division by zero
    if (y <= 1e-6) {
        *out_r = *out_g = *out_b = 0;
        return;
    }

    double X = (Y / y) * x;
    double Z = (Y / y) * (1.0 - x - y);

    // Convert XYZ to linear RGB (sRGB D65)
    double r_lin =  3.2406 * X - 1.5372 * Y - 0.4986 * Z;
    double g_lin = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
    double b_lin =  0.0557 * X - 0.2040 * Y + 1.0570 * Z;

    // Clamp negatives
    r_lin = r_lin < 0.0 ? 0.0 : r_lin;
    g_lin = g_lin < 0.0 ? 0.0 : g_lin;
    b_lin = b_lin < 0.0 ? 0.0 : b_lin;

    // Apply simple normalization: find largest component and scale so max = 1.0 if >1
    double max_lin = r_lin;
    if (g_lin > max_lin) max_lin = g_lin;
    if (b_lin > max_lin) max_lin = b_lin;
    if (max_lin > 1.0) {
        r_lin /= max_lin;
        g_lin /= max_lin;
        b_lin /= max_lin;
    }

    // Gamma correction (sRGB)
    auto gamma = [](double c) {
        if (c <= 0.0031308)
            return 12.92 * c;
        else
            return 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    };

    double r = gamma(r_lin);
    double g = gamma(g_lin);
    double b = gamma(b_lin);

    // Scale by 0..255 (mit voller Farbsättigung)
    int ri = (int)round(r * 255.0);
    int gi = (int)round(g * 255.0);
    int bi = (int)round(b * 255.0);
    
    // JETZT Helligkeit anwenden (NACH der Farbberechnung!)
    ri = (ri * brightness_percent) / 100;
    gi = (gi * brightness_percent) / 100;
    bi = (bi * brightness_percent) / 100;

    if (ri < 0) {
        ri = 0;
    }
    if (ri > 255) {
        ri = 255;
    }

    if (gi < 0) {
        gi = 0;
    }
    if (gi > 255) {
        gi = 255;
    }

    if (bi < 0) {
        bi = 0;
    }
    if (bi > 255) {
        bi = 255;
    }

    *out_r = (uint8_t)ri;
    *out_g = (uint8_t)gi;
    *out_b = (uint8_t)bi;
}

/**
 * @brief LED Strip aktualisieren basierend auf aktuellen Zustand
 * 
 * Konvertiert CIE 1931 XY → sRGB mit Gamma-Korrektur
 */
static void update_led_strip(void)
{
    if (!led_strip_on) {
        led_strip_off();
        return;
    }
    
    // CIE 1931 XY -> sRGB Konvertierung
    uint8_t r = 0, g = 0, b = 0;
    
    
    xyY_to_rgb(led_strip_x, led_strip_y, led_strip_brightness, &r, &g, &b);
    
    ESP_LOGI(TAG, "LED: X=%u Y=%u B=%d%% -> R=%d G=%d B=%d", 
             led_strip_x, led_strip_y, led_strip_brightness, r, g, b);
    
    led_strip_set_color(r, g, b, 100); // Helligkeit bereits eingerechnet
}

/**
 * @brief Matter Attribute Update Callback
 * 
 * Endpoint 1: IR Button (On/Off Light) - Momentary Button
 * Endpoint 2: RGB LED Strip (Color Temperature Light)
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
        
        // Endpoint 1: IR Button
        if (endpoint_id == 1) {
            if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
                bool new_state = val->val.b;
                ESP_LOGI(TAG, "🔘 IR Button: %s", new_state ? "PRESSED" : "RELEASED");
                
                if (new_state) {
                    send_ir_signal();
                    val->val.b = false; // Momentary: sofort wieder aus
                }
            }
        }
        // Endpoint 2: RGB LED Strip
        else if (endpoint_id == 2) {
            if (cluster_id == OnOff::Id) {
                if (attribute_id == OnOff::Attributes::OnOff::Id) {
                    led_strip_on = val->val.b;
                    ESP_LOGI(TAG, "💡 LED Strip: %s", led_strip_on ? "EIN" : "AUS");
                    led_update_pending = true; // Async Update
                }
            }
            else if (cluster_id == LevelControl::Id) {
                if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
                    led_strip_brightness = (val->val.u8 * 100) / 254;
                    ESP_LOGI(TAG, "💡 LED Strip Helligkeit: %d%%", led_strip_brightness);
                    led_update_pending = true; // Async Update
                }
            }
            else if (cluster_id == ColorControl::Id) {
                ESP_LOGI(TAG, "");
                ESP_LOGI(TAG, "╔═════════════════════════════════════════════════════════╗");
                ESP_LOGI(TAG, "║ 🎨 ColorControl Update (CIE 1931 XY)                    ║");
                ESP_LOGI(TAG, "╠═════════════════════════════════════════════════════════╣");
                
                bool should_update = false;
                
                // XY Farbattribute (CIE 1931 color space - Apple Home nutzt diese!)
                if (attribute_id == ColorControl::Attributes::CurrentX::Id) {
                    uint16_t raw_x = val->val.u16;
                    float x_float = raw_x / 65535.0f;
                    led_strip_x = raw_x;
                    ESP_LOGI(TAG, "║ 📐 CurrentX: %u (%.4f)                            ║", raw_x, x_float);
                    should_update = true;
                }
                else if (attribute_id == ColorControl::Attributes::CurrentY::Id) {
                    uint16_t raw_y = val->val.u16;
                    float y_float = raw_y / 65535.0f;
                    led_strip_y = raw_y;
                    ESP_LOGI(TAG, "║ 📐 CurrentY: %u (%.4f)                            ║", raw_y, y_float);
                    should_update = true;
                }
                // HSV Attribute (werden von Apple Home NICHT genutzt, aber müssen existieren)
                else if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
                    ESP_LOGI(TAG, "║ 🔄 CurrentHue: %u (ignoriert - nutze XY)          ║", val->val.u8);
                    // Ignorieren - wir nutzen nur XY intern
                }
                else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
                    ESP_LOGI(TAG, "║ 🔄 CurrentSaturation: %u (ignoriert - nutze XY)   ║", val->val.u8);
                    // Ignorieren - wir nutzen nur XY intern
                }
                else if (attribute_id == ColorControl::Attributes::ColorMode::Id) {
                    uint8_t mode = val->val.u8;
                    const char* mode_str = (mode == 1) ? "XY ✅" : "HSV/ANDERE ⚠️";
                    ESP_LOGI(TAG, "║ 🎨 ColorMode: %d (%s)                              ║", mode, mode_str);
                }
                else {
                    ESP_LOGI(TAG, "║ ⚠️  Unbekanntes Attribut: 0x%08X                   ║", attribute_id);
                }
                
                ESP_LOGI(TAG, "╠═════════════════════════════════════════════════════════╣");
                ESP_LOGI(TAG, "║ Status: LED %s, Update %s                            ║", 
                         led_strip_on ? "EIN ✅" : "AUS ⚪", 
                         should_update ? "JA ⚡" : "NEIN");
                ESP_LOGI(TAG, "╚═════════════════════════════════════════════════════════╝");
                ESP_LOGI(TAG, "");
                
                // Farbe IMMER speichern (auch wenn LED aus ist)
                // LED nur aktualisieren wenn sie eingeschaltet ist UND nicht unterdrückt
                if (should_update && !suppress_attribute_led_update) {
                    if (led_strip_on) {
                        ESP_LOGI(TAG, "⚡ LED Update geplant (async) - LED ist EIN");
                        led_update_pending = true; // Async Update
                    } else {
                        ESP_LOGI(TAG, "💾 Farbe gespeichert - LED bleibt AUS (wird beim Einschalten genutzt)");
                    }
                } else if (suppress_attribute_led_update) {
                    ESP_LOGI(TAG, "🔇 LED Update unterdrückt (Background-Task Attribute-Sync)");
                }
            }
        }
    }

    return err;
}

/**
 * @brief Handler für MoveToHueAndSaturation Command (0x06)
 * 
 * Apple Home sendet trotz CIE 1931 XY Support oft HSV-Commands!
 * ColorControl Cluster (0x0300), Command MoveToHueAndSaturation (0x06)
 * Konvertiert HSV (0-254) → CIE 1931 XY (0-65535) intern
 * 
 * WICHTIG: Muss SCHNELL sein (<100ms) um Matter Timeout zu vermeiden!
 */
static esp_err_t hue_saturation_command_callback(const ConcreteCommandPath &command_path,
                                                  TLVReader &tlv_data, void *opaque_ptr)
{
    ESP_LOGI(TAG, "🎨 MoveToHueAndSaturation (0x06) empfangen");

    // Payload parsen: {Hue: uint8, Saturation: uint8, TransitionTime: uint16, ...}
    chip::app::Clusters::ColorControl::Commands::MoveToHueAndSaturation::DecodableType command_data;
    
    if (CHIP_NO_ERROR != chip::app::DataModel::Decode(tlv_data, command_data)) {
        ESP_LOGE(TAG, "❌ MoveToHueAndSaturation Decode fehlgeschlagen");
        return ESP_FAIL;
    }

    uint8_t hue = command_data.hue;           // 0-254
    uint8_t saturation = command_data.saturation; // 0-254
    
    ESP_LOGI(TAG, "   HSV: H=%d, S=%d (%.1f%% Sättigung)", hue, saturation, (saturation / 254.0f) * 100.0f);

    // ===== Schritt 1: HSV → RGB (linear) =====
    float h_degrees = (hue / 254.0f) * 360.0f;
    float s_normalized = saturation / 254.0f;
    float v_normalized = 1.0f; // Volle Helligkeit für Farbberechnung
    
    float c = v_normalized * s_normalized;
    float h_sector = h_degrees / 60.0f;
    float x = c * (1.0f - fabsf(fmodf(h_sector, 2.0f) - 1.0f));
    float m = v_normalized - c;
    
    float r_linear, g_linear, b_linear;
    
    if (h_sector >= 0.0f && h_sector < 1.0f) {
        r_linear = c; g_linear = x; b_linear = 0.0f;
    } else if (h_sector >= 1.0f && h_sector < 2.0f) {
        r_linear = x; g_linear = c; b_linear = 0.0f;
    } else if (h_sector >= 2.0f && h_sector < 3.0f) {
        r_linear = 0.0f; g_linear = c; b_linear = x;
    } else if (h_sector >= 3.0f && h_sector < 4.0f) {
        r_linear = 0.0f; g_linear = x; b_linear = c;
    } else if (h_sector >= 4.0f && h_sector < 5.0f) {
        r_linear = x; g_linear = 0.0f; b_linear = c;
    } else {
        r_linear = c; g_linear = 0.0f; b_linear = x;
    }
    
    r_linear += m;
    g_linear += m;
    b_linear += m;

    // ===== Schritt 2: RGB → XYZ (sRGB D65) =====
    float X = r_linear * 0.4124564f + g_linear * 0.3575761f + b_linear * 0.1804375f;
    float Y_tristimulus = r_linear * 0.2126729f + g_linear * 0.7151522f + b_linear * 0.0721750f;
    float Z = r_linear * 0.0193339f + g_linear * 0.1191920f + b_linear * 0.9503041f;

    // ===== Schritt 3: XYZ → xy (CIE 1931) =====
    float sum = X + Y_tristimulus + Z;
    float x_chromaticity, y_chromaticity;
    
    if (sum < 0.00001f) {
        // Fallback auf D65 White Point bei Schwarz
        x_chromaticity = 0.31271f;
        y_chromaticity = 0.32902f;
    } else {
        x_chromaticity = X / sum;
        y_chromaticity = Y_tristimulus / sum;
        
        // Gamut Clamping
        if (x_chromaticity + y_chromaticity > 1.0f) {
            float norm_factor = 0.999f / (x_chromaticity + y_chromaticity);
            x_chromaticity *= norm_factor;
            y_chromaticity *= norm_factor;
        }
    }

    // ===== Schritt 4: xy → Matter Format (0-65535) =====
    uint16_t x_matter = (uint16_t)(x_chromaticity * 65535.0f);
    uint16_t y_matter = (uint16_t)(y_chromaticity * 65535.0f);
    
    ESP_LOGI(TAG, "   XY: X=%u (%.4f), Y=%u (%.4f)", x_matter, x_chromaticity, y_matter, y_chromaticity);

    // ===== Schritt 5: Werte direkt setzen (SCHNELL!) =====
    // Attribute werden asynchron vom Background-Task aktualisiert
    led_strip_x = x_matter;
    led_strip_y = y_matter;
    pending_x = x_matter;
    pending_y = y_matter;
    xy_attribute_update_pending = true;  // Async attribute update
    led_update_pending = true;           // Async LED update
    
    ESP_LOGI(TAG, "✅ HSV→XY OK - Async Update geplant (X=%u, Y=%u)", x_matter, y_matter);

    // ===== KRITISCH: Matter Response senden! =====
    // OHNE diese Zeile gibt es "Failed to finalize command response: 3"
    chip::app::CommandHandler *command_handler = (chip::app::CommandHandler *)opaque_ptr;
    if (command_handler != nullptr) {
        command_handler->AddStatus(command_path, chip::Protocols::InteractionModel::Status::Success);
    }

    return ESP_OK;
}

/**
 * @brief Handler für MoveToColorTemperature Command (0x0A)
 * 
 * Für den "Temperatur"-Tab in Apple Home (Warmweiß ↔ Kaltweiß)
 * ColorControl Cluster (0x0300), Command MoveToColorTemperature (0x0A)
 * 
 * Color Temperature in Mireds (Micro Reciprocal Degrees):
 * - 153 mireds = 6500K (kaltweiß/daylight)
 * - 250 mireds = 4000K (neutralweiß)
 * - 454 mireds = 2200K (warmweiß/candle)
 * 
 * Wir konvertieren Mireds → Kelvin → XY für RGB LED Simulation
 */
static esp_err_t move_to_color_temperature_callback(const ConcreteCommandPath &command_path,
                                                     TLVReader &tlv_data, void *opaque_ptr)
{
    ESP_LOGI(TAG, "🌡️  MoveToColorTemperature Command empfangen");
    
    // Command-Daten parsen
    auto &command_data = *(Commands::MoveToColorTemperature::DecodableType *)(&tlv_data);
    
    uint16_t color_temp_mireds = command_data.colorTemperatureMireds;
    uint16_t transition_time = command_data.transitionTime;
    
    // Mireds → Kelvin konvertieren
    uint32_t kelvin = (color_temp_mireds > 0) ? (1000000 / color_temp_mireds) : 4000;
    
    ESP_LOGI(TAG, "   Temperature: %u mireds = %lu K, Transition: %u", 
             color_temp_mireds, kelvin, transition_time);
    
    // Kelvin → CIE 1931 XY Konvertierung (vereinfacht)
    // Basierend auf Planck'schen Strahler
    float x, y;
    
    if (kelvin >= 6500) {
        // Kaltweiß (Daylight): ~6500K
        x = 0.313f;
        y = 0.329f;
    } else if (kelvin >= 5000) {
        // Neutralweiß-Kalt: 5000-6500K
        float t = (kelvin - 5000.0f) / 1500.0f;
        x = 0.333f + t * (0.313f - 0.333f);
        y = 0.333f + t * (0.329f - 0.333f);
    } else if (kelvin >= 4000) {
        // Neutralweiß: 4000-5000K
        float t = (kelvin - 4000.0f) / 1000.0f;
        x = 0.380f + t * (0.333f - 0.380f);
        y = 0.376f + t * (0.333f - 0.376f);
    } else if (kelvin >= 3000) {
        // Warmweiß: 3000-4000K
        float t = (kelvin - 3000.0f) / 1000.0f;
        x = 0.440f + t * (0.380f - 0.440f);
        y = 0.403f + t * (0.376f - 0.403f);
    } else {
        // Sehr warmweiß (Candle): <3000K
        x = 0.500f;
        y = 0.415f;
    }
    
    // XY zu uint16 konvertieren (0-65535)
    uint16_t colorX = (uint16_t)(x * 65535.0f);
    uint16_t colorY = (uint16_t)(y * 65535.0f);
    
    ESP_LOGI(TAG, "   → XY: (%.3f, %.3f) = (%u, %u)", x, y, colorX, colorY);
    
    // LED und Attribute aktualisieren (asynchron)
    led_strip_x = colorX;
    led_strip_y = colorY;
    pending_x = colorX;
    pending_y = colorY;
    xy_attribute_update_pending = true;
    led_update_pending = true;
    
    ESP_LOGI(TAG, "✅ ColorTemperature OK - Async Update geplant");
    
    // Matter Response senden
    chip::app::CommandHandler *command_handler = (chip::app::CommandHandler *)opaque_ptr;
    if (command_handler != nullptr) {
        command_handler->AddStatus(command_path, chip::Protocols::InteractionModel::Status::Success);
    }
    
    return ESP_OK;
}

/**
 * @brief Handler für MoveToColor (XY) Command (0x07)
 * 
 * Matter/Apple Home Standard für RGB-Farbsteuerung
 * ColorControl Cluster (0x0300), Command MoveToColor (0x07)
 * Verwendet CIE 1931 XY color space (0-65535 entspricht 0.0-1.0)
 * 
 * WICHTIG: Muss SCHNELL sein (<100ms) um Matter Timeout zu vermeiden!
 */
static esp_err_t move_to_color_command_callback(const ConcreteCommandPath &command_path,
                                                TLVReader &tlv_data, void *opaque_ptr)
{
    ESP_LOGI(TAG, "🎨 MoveToColor (0x07) empfangen");

    chip::app::Clusters::ColorControl::Commands::MoveToColor::DecodableType command_data;
    
    CHIP_ERROR decode_err = chip::app::DataModel::Decode(tlv_data, command_data);
    if (decode_err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "❌ MoveToColor Decode fehlgeschlagen");
        return ESP_FAIL;
    }
    
    // MoveToColor: colorX (uint16_t), colorY (uint16_t), transitionTime (uint16_t)
    uint16_t colorX = command_data.colorX;
    uint16_t colorY = command_data.colorY;
    
    float x_float = colorX / 65535.0f;
    float y_float = colorY / 65535.0f;
    
    ESP_LOGI(TAG, "   XY: X=%u (%.4f), Y=%u (%.4f)", colorX, x_float, colorY, y_float);
    
    // Gamut Validierung
    if (x_float + y_float > 1.1f) {
        ESP_LOGW(TAG, "⚠️  XY außerhalb Gamut: x+y=%.4f", x_float + y_float);
    }
    
    // Werte direkt setzen (SCHNELL!)
    // Attribute werden asynchron vom Background-Task aktualisiert
    led_strip_x = colorX;
    led_strip_y = colorY;
    pending_x = colorX;
    pending_y = colorY;
    xy_attribute_update_pending = true;  // Async attribute update
    led_update_pending = true;           // Async LED update
    
    ESP_LOGI(TAG, "✅ MoveToColor OK - Async Update geplant (X=%u, Y=%u)", colorX, colorY);

    // ===== KRITISCH: Matter Response senden! =====
    chip::app::CommandHandler *command_handler = (chip::app::CommandHandler *)opaque_ptr;
    if (command_handler != nullptr) {
        command_handler->AddStatus(command_path, chip::Protocols::InteractionModel::Status::Success);
    }

    return ESP_OK;
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
 * @brief Background Task NUR für asynchrone LED Updates
 * 
 * Vermeidet Timeouts in Matter Command Handlers durch
 * asynchrone Verarbeitung von LED Strip Updates (RMT Transfer)
 */
static void led_update_task(void *pvParameters)
{
    ESP_LOGI(TAG, "🔄 LED Update Task gestartet");
    
    while (1) {
        // Prüfe alle 10ms ob ein LED Update pending ist
        if (led_update_pending) {
            led_update_pending = false;
            
            ESP_LOGI(TAG, "⚡ Führe asynchrones LED Update aus...");
            update_led_strip();
        }
        
        // Attribute Update (wenn pending) - MIT VERZÖGERUNG!
        if (xy_attribute_update_pending) {
            xy_attribute_update_delay_ms += 10; // +10ms pro Iteration
            
            // Warte 100ms bevor wir die Attribute aktualisieren
            // Das gibt dem Command Handler Zeit, ESP_OK zurückzugeben
            if (xy_attribute_update_delay_ms >= 100) {
                xy_attribute_update_pending = false;
                xy_attribute_update_delay_ms = 0;
                
                // Unterdrücke LED-Updates, die durch diese Attribute-Änderung getriggert werden
                suppress_attribute_led_update = true;
                
                // Matter-Attribute aktualisieren (OHNE LED-Update zu triggern)
                esp_matter_attr_val_t val_x = esp_matter_uint16(pending_x);
                esp_matter_attr_val_t val_y = esp_matter_uint16(pending_y);
                
                attribute::update(2, ColorControl::Id, ColorControl::Attributes::CurrentX::Id, &val_x);
                attribute::update(2, ColorControl::Id, ColorControl::Attributes::CurrentY::Id, &val_y);
                
                suppress_attribute_led_update = false;
                
                ESP_LOGI(TAG, "📡 Matter-Attribute aktualisiert (nach 100ms): X=%u, Y=%u", pending_x, pending_y);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms = 100Hz
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

    /* Initialize Hardware */
    ESP_ERROR_CHECK(ir_transmitter_init());
    ESP_LOGI(TAG, "✓ IR Transmitter initialisiert");
    
    ESP_ERROR_CHECK(led_strip_init());
    ESP_LOGI(TAG, "✓ WS2813 LED Strip initialisiert");

    /* Create a Matter node */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "✗ Fehler beim Erstellen des Matter-Knotens");
        return;
    }
    ESP_LOGI(TAG, "✓ Matter Node erstellt");

    /* 
     * Erstelle einen "On/Off Light" Endpoint als Workaround
     * HomeKit unterstützt generic_switch nicht, daher nutzen wir ein Licht
     * Das "Einschalten" des Lichts sendet das IR-Signal
     * Das Licht wird automatisch wieder ausgeschaltet (Momentary-Verhalten)
     */
    on_off_light::config_t light_config;
    light_config.on_off.on_off = false;
    light_config.on_off.lighting.start_up_on_off = nullptr;
    
    endpoint_t *endpoint = on_off_light::create(node, &light_config, 
                                                 ENDPOINT_FLAG_NONE, NULL);
    if (!endpoint) {
        ESP_LOGE(TAG, "✗ Fehler beim Erstellen des Light-Endpunkts");
        return;
    }

    uint16_t endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "✓ IR Button Endpoint erstellt (ID: %d)", endpoint_id);

    /* 
     * Endpoint 2: RGB LED Strip als Extended Color Light
     * Unterstützt: On/Off, Helligkeit, Farbton (Hue), Sättigung
     */
    extended_color_light::config_t led_config;
    led_config.on_off.on_off = false;
    led_config.on_off.lighting.start_up_on_off = nullptr;
    led_config.level_control.current_level = 254;  // Max Helligkeit
    led_config.level_control.lighting.start_up_current_level = 254;
    
    // ╔═══════════════════════════════════════════════════════════════╗
    // ║  Color Control: CIE 1931 XY Color Space (Apple Home Standard) ║
    // ╚═══════════════════════════════════════════════════════════════╝
    
    // ColorMode: Verwende XY (CIE 1931) als primären Modus
    led_config.color_control.color_mode = (uint8_t)ColorControl::ColorMode::kCurrentXAndCurrentY;
    led_config.color_control.enhanced_color_mode = (uint8_t)ColorControl::ColorMode::kCurrentXAndCurrentY;
    
    // ColorCapabilities: Deklariere ALLE RGB-Fähigkeiten für maximale Kompatibilität
    // HSV + XY + EnhancedHue für vollen Farbraum in Apple Home
    led_config.color_control.color_capabilities = 
        (uint16_t)chip::app::Clusters::ColorControl::ColorCapabilitiesBitmap::kHueSaturation |
        (uint16_t)chip::app::Clusters::ColorControl::ColorCapabilitiesBitmap::kEnhancedHue |
        (uint16_t)chip::app::Clusters::ColorControl::ColorCapabilitiesBitmap::kXy;
    
    // Initial XY Values: D65 White Point (Standard-Weißpunkt)
    // CIE 1931: x=0.31271, y=0.32902 → skaliert zu 0-65535
    led_config.color_control.xy.current_x = 20480;  // 0.31271 * 65535 ≈ 20480
    led_config.color_control.xy.current_y = 21560;  // 0.32902 * 65535 ≈ 21560
    
    // Initial HSV Values: Weiß (wird intern ignoriert, aber Matter erwartet sie)
    led_config.color_control.hue_saturation.current_hue = 0;
    led_config.color_control.hue_saturation.current_saturation = 254;  // MAX! (0-254, nicht 0-255)
    
    // WICHTIG: Color Temperature Range setzen (für Temperatur-Tab)
    // WS2813 kann Color Temperature nicht nativ, aber wir simulieren es via XY
    led_config.color_control.color_temperature.startup_color_temperature_mireds = 250;  // ~4000K (neutral white)
    
    ESP_LOGI(TAG, "📐 ColorControl konfiguriert:");
    ESP_LOGI(TAG, "   • Color Space: CIE 1931 XY (primär)");
    ESP_LOGI(TAG, "   • ColorMode: kCurrentXAndCurrentY");
    ESP_LOGI(TAG, "   • Capabilities: HSV + XY (0x%04X) ← Apple Home Farbrad!", 
             led_config.color_control.color_capabilities);
    ESP_LOGI(TAG, "   • Initial XY: (%u, %u) = D65 White", 
             led_config.color_control.xy.current_x,
             led_config.color_control.xy.current_y);
    
    endpoint_t *led_endpoint = extended_color_light::create(node, &led_config,
                                                             ENDPOINT_FLAG_NONE, NULL);
    if (!led_endpoint) {
        ESP_LOGE(TAG, "✗ Fehler beim Erstellen des LED Strip Endpunkts");
        return;
    }

    uint16_t led_endpoint_id = endpoint::get_id(led_endpoint);
    ESP_LOGI(TAG, "✓ RGB LED Strip Endpoint erstellt (ID: %d)", led_endpoint_id);
    
    // ============================================================================
    // 🎨 RGB Primaries (Farb-Gamut) für WS2813 definieren
    // ============================================================================
    // HomeKit nutzt diese Werte, um zu wissen, welche Farben die LED darstellen kann!
    // Ohne Primaries denkt HomeKit, die LED kann nur Weiß-nahe Farben.
    //
    // WS2813 Wide-Gamut RGB (CIE 1931 x,y Koordinaten):
    // Maximaler Gamut für RGB LEDs - deckt VOLLEN CIE-Farbraum ab!
    // (Werte absichtlich außerhalb sRGB für maximale Farbauswahl)
    // - Red:   x=0.700, y=0.290 → (45875, 19005)
    // - Green: x=0.170, y=0.797 → (11141, 52215)  
    // - Blue:  x=0.131, y=0.046 → ( 8585,  3014)
    
    cluster_t *color_cluster = cluster::get(led_endpoint, ColorControl::Id);
    if (color_cluster) {
        // NumberOfPrimaries = 3 (RGB)
        attribute::create(color_cluster, ColorControl::Attributes::NumberOfPrimaries::Id,
                         ATTRIBUTE_FLAG_NONE, esp_matter_uint8(3));
        
        // Primary 1: Red (Wide-Gamut)
        attribute::create(color_cluster, ColorControl::Attributes::Primary1X::Id,
                         ATTRIBUTE_FLAG_NONE, esp_matter_uint16(45875));  // x=0.700
        attribute::create(color_cluster, ColorControl::Attributes::Primary1Y::Id,
                         ATTRIBUTE_FLAG_NONE, esp_matter_uint16(19005));  // y=0.290
        attribute::create(color_cluster, ColorControl::Attributes::Primary1Intensity::Id,
                         ATTRIBUTE_FLAG_NONE, esp_matter_uint8(255));     // Max
        
        // Primary 2: Green (Wide-Gamut)
        attribute::create(color_cluster, ColorControl::Attributes::Primary2X::Id,
                         ATTRIBUTE_FLAG_NONE, esp_matter_uint16(11141));  // x=0.170
        attribute::create(color_cluster, ColorControl::Attributes::Primary2Y::Id,
                         ATTRIBUTE_FLAG_NONE, esp_matter_uint16(52215));  // y=0.797
        attribute::create(color_cluster, ColorControl::Attributes::Primary2Intensity::Id,
                         ATTRIBUTE_FLAG_NONE, esp_matter_uint8(255));
        
        // Primary 3: Blue (Wide-Gamut)
        attribute::create(color_cluster, ColorControl::Attributes::Primary3X::Id,
                         ATTRIBUTE_FLAG_NONE, esp_matter_uint16(8585));   // x=0.131
        attribute::create(color_cluster, ColorControl::Attributes::Primary3Y::Id,
                         ATTRIBUTE_FLAG_NONE, esp_matter_uint16(3014));   // y=0.046
        attribute::create(color_cluster, ColorControl::Attributes::Primary3Intensity::Id,
                         ATTRIBUTE_FLAG_NONE, esp_matter_uint8(255));
        
        ESP_LOGI(TAG, "🎨 Wide-Gamut RGB konfiguriert (MAXIMALER Farbraum):");
        ESP_LOGI(TAG, "   • Red:   x=0.700, y=0.290 (45875, 19005)");
        ESP_LOGI(TAG, "   • Green: x=0.170, y=0.797 (11141, 52215)");
        ESP_LOGI(TAG, "   • Blue:  x=0.131, y=0.046 ( 8585,  3014)");
        ESP_LOGI(TAG, "   ⮕ HomeKit kann jetzt den KOMPLETTEN CIE-Farbraum nutzen!");
    } else {
        ESP_LOGW(TAG, "⚠️  ColorControl Cluster nicht gefunden - Primaries nicht gesetzt");
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔧 Registriere Matter Commands...");
    
    // Registriere MoveToColor Command (0x07) für CIE 1931 XY color space
    // (color_cluster ist schon oben definiert)
    if (color_cluster) {
        uint32_t cluster_id = cluster::get_id(color_cluster);
        ESP_LOGI(TAG, "   • ColorControl Cluster: 0x%04X ✅", cluster_id);
        
        command_t *cmd = command::create(color_cluster, 
                                        ColorControl::Commands::MoveToColor::Id,
                                        COMMAND_FLAG_ACCEPTED, 
                                        move_to_color_command_callback);
        
        if (cmd != nullptr) {
            ESP_LOGI(TAG, "   • MoveToColor (0x07) Handler: ✅ REGISTRIERT");
            ESP_LOGI(TAG, "     → Callback: move_to_color_command_callback()");
            ESP_LOGI(TAG, "     → Format: CIE 1931 XY (colorX, colorY: 0-65535)");
        } else {
            ESP_LOGE(TAG, "   • MoveToColor (0x07) Handler: ❌ FEHLER");
        }
        
        // Registriere MoveToHueAndSaturation Command (0x06) für Apple Home Kompatibilität
        command_t *cmd_hsv = command::create(color_cluster, 
                                            ColorControl::Commands::MoveToHueAndSaturation::Id,
                                            COMMAND_FLAG_ACCEPTED, 
                                            hue_saturation_command_callback);
        
        if (cmd_hsv != nullptr) {
            ESP_LOGI(TAG, "   • MoveToHueAndSaturation (0x06) Handler: ✅ REGISTRIERT");
            ESP_LOGI(TAG, "     → Callback: hue_saturation_command_callback()");
            ESP_LOGI(TAG, "     → Format: HSV (hue, saturation: 0-254) → XY intern");
        } else {
            ESP_LOGE(TAG, "   • MoveToHueAndSaturation (0x06) Handler: ❌ FEHLER");
        }
        
        // Registriere MoveToColorTemperature Command (0x0A) für Temperatur-Tab
        command_t *cmd_temp = command::create(color_cluster, 
                                             ColorControl::Commands::MoveToColorTemperature::Id,
                                             COMMAND_FLAG_ACCEPTED, 
                                             move_to_color_temperature_callback);
        
        if (cmd_temp != nullptr) {
            ESP_LOGI(TAG, "   • MoveToColorTemperature (0x0A) Handler: ✅ REGISTRIERT");
            ESP_LOGI(TAG, "     → Callback: move_to_color_temperature_callback()");
            ESP_LOGI(TAG, "     → Format: Mireds (153-454) → K (6500-2200) → XY");
        } else {
            ESP_LOGE(TAG, "   • MoveToColorTemperature (0x0A) Handler: ❌ FEHLER");
        }
    } else {
        ESP_LOGE(TAG, "   • ColorControl Cluster: ❌ NICHT GEFUNDEN!");
        ESP_LOGE(TAG, "     → MoveToColor Command kann nicht registriert werden!");
    }
    ESP_LOGI(TAG, "");

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "✗ Fehler beim Starten von Matter: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "✓ Matter Stack gestartet");

    /* Starte Multi-Function Button Task */
    xTaskCreate(multifunction_button_task, "mf_button", 4096, NULL, 5, NULL);
    
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
    ESP_LOGI(TAG, "Setup Codes:");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "    HomeKit (manuell): 20202021");
    ESP_LOGI(TAG, "    Alexa QR-Code:     MT:X0H.NZ5TMHDT-K");
    ESP_LOGI(TAG, "    Alexa PIN:         20202021");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "MULTI-FUNCTION BUTTON (GPIO 7):");
    ESP_LOGI(TAG, "  • 3 Sekunden  halten = Pairing für Alexa");
    ESP_LOGI(TAG, "  • 10 Sekunden halten = Factory Reset");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    
    // Starte asynchronen LED Update Task (vermeidet Matter Timeouts)
    xTaskCreate(led_update_task, "led_update", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "✓ LED Update Task gestartet (asynchron)");
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "SmartRoom läuft und ist bereit!");
    ESP_LOGI(TAG, "• HomeKit: Button in Home App drücken");
    ESP_LOGI(TAG, "• Alexa:   GPIO 7 + GND für 3s");
}
