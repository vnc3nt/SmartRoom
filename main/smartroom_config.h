#pragma once

#include <cstddef>
#include <cstdint>

namespace smartroom {

// ============================================================================
// Verbindliche Pinbelegung (ESP32-C6 SuperMini)
// ============================================================================
constexpr int kIrTxGpio = 18; // IR-Sender (38 kHz Carrier)
constexpr int kIrRxGpio = 19; // IR-Empfaenger (demoduliert)

// TODO: Offener Hardware-Pin! Bitte den bestaetigten GPIO fuer WS2813 Data eintragen.
// Hinweis: Levelshifter ist angeschlossen und funktioniert.
// Solange kWs2813DataGpio == -1 (GPIO_NUM_NC), bleibt die LED-Ausgabe sicher deaktiviert.
constexpr int kWs2813DataGpio = -1;

// ============================================================================
// WS2813 LED-Strip Konfiguration
// ============================================================================
constexpr int kWs2813LedCount = 144;         // 1 m IP30 Strip mit 144 LEDs
constexpr bool kWs2813ColorOrderGrb = true;  // GRB-Reihenfolge (konfigurierbar)
constexpr uint32_t kWs2813ResolutionHz = 10000000; // 10 MHz RMT-Aufloesung

// ============================================================================
// IR RMT & Lern-Konfiguration
// ============================================================================
constexpr uint32_t kIrResolutionHz = 1000000;       // 1 MHz (1 us pro Tick)
constexpr uint32_t kIrDefaultCarrierHz = 38000;     // 38 kHz Standard-Traeger
constexpr size_t kIrMaxSymbols = 256;               // Maximale RMT-Symbole pro Frame
constexpr size_t kIrMaxSignals = 32;                // Maximale gespeicherte IR-Befehle
constexpr uint32_t kIrLearnTimeoutMs = 15000;       // 15 Sekunden Lernfenster

} // namespace smartroom
