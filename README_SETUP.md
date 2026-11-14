# 🏠 SmartRoom - Matter IR Controller

Ein ESP32-C6 basierter Matter-Controller, der Infrarot-Signale über Apple HomeKit steuert.

## 📋 Features

- ✅ **Matter-Protokoll** - Vollständige Matter-Unterstützung
- ✅ **Apple HomeKit** - Funktioniert nativ mit der Home App
- ✅ **IR-Steuerung** - Sendet NEC IR-Signale (38kHz)
- ✅ **Button Interface** - Erscheint als Programmable Switch in HomeKit
- ✅ **WiFi/Thread** - Unterstützt beide Netzwerkprotokolle

## 🛠️ Hardware

- **ESP32-C6 DevKit** (oder kompatibles Board)
- **IR LED** (z.B. IR333C) an GPIO 18
- **330Ω Widerstand** für die IR LED
- **Stromversorgung** über USB-C

### Schaltplan

```
ESP32-C6                  IR LED (IR333C)
+--------+               +-------+
|        |               |       |
| GPIO18 +---------------+  (+)  |
|        |      330Ω     |       |
|    GND +---------------+  (-)  |
|        |               |       |
+--------+               +-------+
```

## 🚀 Installation

### 1. ESP-IDF Setup

Stelle sicher, dass ESP-IDF 5.2 oder höher installiert ist:

```bash
# ESP-IDF aktivieren
source ~/esp/esp-idf/export.sh

# IDF Version prüfen
idf.py --version
```

### 2. Projekt klonen/öffnen

```bash
cd /Users/vnc3nt/Documents/Code/VS-Code/ESP32/SmartRoom/SmartRoom
```

### 3. WiFi konfigurieren

**Option A: Mit Setup-Script (empfohlen)**

```bash
./setup_wifi.sh
```

**Option B: Manuell**

```bash
idf.py menuconfig
```

Navigiere zu:
- `Example Configuration` → `WiFi SSID`: Dein WiFi Name
- `Example Configuration` → `WiFi Password`: Dein WiFi Passwort

### 4. Projekt bauen

```bash
idf.py build
```

### 5. Auf ESP32 flashen

```bash
# Finde den Port (z.B. /dev/tty.usbserial-* oder /dev/cu.usbserial-*)
ls /dev/tty.*

# Flashe und starte Monitor
idf.py -p /dev/tty.usbserial-XXXXX flash monitor
```

## 📱 HomeKit Einrichtung

### Pairing-Prozess

1. **Öffne die Home App** auf deinem iPhone/iPad
2. Tippe auf **'+'** → **'Gerät hinzufügen'**
3. Wähle **'Weitere Optionen...'**
4. Dein Device sollte als **"SmartRoom"** erscheinen
5. Wähle es aus und gib den **Setup-Code** ein:

```
Setup Code: 20202021
```

### Alternative: QR-Code

Falls ein QR-Code im Serial Monitor angezeigt wird, kannst du diesen auch scannen.

### Nach dem Pairing

- Das Gerät erscheint als **"Switch"** oder **"Button"**
- **Antippen** sendet das IR-Signal
- Der Button setzt sich automatisch zurück (Momentary Switch)

## 🔧 Konfiguration

### IR-Signal ändern

In `main/smart.c`, Zeile ~45:

```c
static const ir_nec_scan_code_t ir_signal = {
    .address = 0xFF00,  // Deine IR-Adresse
    .command = 0xBA45,  // Dein IR-Befehl
};
```

Um dein eigenes IR-Signal zu finden:
1. Nutze den ursprünglichen `ir_nec_transceiver_main.c` zum Empfangen
2. Notiere Address und Command
3. Trage diese Werte in `smart.c` ein

### GPIO ändern

In `main/smart.c`, Zeile ~28:

```c
#define IR_TX_GPIO_NUM       18      // GPIO für IR LED
```

### Matter-Konfiguration

Die wichtigsten Einstellungen sind in `sdkconfig.defaults`:

- **Vendor ID**: 0xFFF1 (Test Vendor)
- **Product ID**: 0x8000
- **Setup PIN**: 20202021
- **Discriminator**: 3840

## 🐛 Troubleshooting

### Build-Fehler

**Problem**: `esp_matter.h not found`

**Lösung**: Stelle sicher, dass die managed_components installiert sind:

```bash
idf.py reconfigure
idf.py build
```

---

**Problem**: `undefined reference to 'app_reset::setup_button'`

**Lösung**: Aktualisiere ESP Matter:

```bash
rm -rf managed_components
idf.py reconfigure
```

### Runtime-Fehler

**Problem**: Device erscheint nicht in Home App

**Lösung**:
1. Prüfe WiFi-Verbindung im Serial Monitor
2. Stelle sicher, dass iPhone im selben Netzwerk ist
3. Prüfe, ob mDNS funktioniert
4. Resette das Device mit Factory Reset Button

---

**Problem**: "Commissioning failed"

**Lösung**:
1. Prüfe Setup-Code (20202021)
2. Lösche alte Pairing-Daten:
   ```bash
   idf.py erase-flash
   idf.py flash monitor
   ```

### Logs analysieren

```bash
# Alle Logs mit Zeitstempel
idf.py monitor --timestamps

# Nur Matter-relevante Logs
idf.py monitor | grep -E "Matter|SmartRoom|CHIP"
```

## 📊 Project Structure

```
SmartRoom/
├── main/
│   ├── smart.c                 # Haupt-Application (Matter + IR)
│   ├── ir_nec_encoder.c        # IR NEC Encoder
│   ├── ir_nec_encoder.h
│   ├── CMakeLists.txt
│   └── idf_component.yml
├── CMakeLists.txt
├── sdkconfig.defaults          # Standard-Konfiguration
├── partitions.csv              # Partition Table
├── setup_wifi.sh               # WiFi Setup Script
└── README_SETUP.md             # Diese Datei
```

## 🔐 Security

**Wichtig für Produktion:**

Dieses Projekt nutzt Test-Credentials:
- Setup PIN: `20202021` (Test-Code)
- Vendor ID: `0xFFF1` (Test Vendor)
- DAC: Example Provider

Für echte Produkte:
1. Registriere einen echten Vendor ID bei CSA
2. Generiere echte Device Attestation Certificates
3. Nutze sichere Setup-Codes
4. Aktiviere Secure Boot & Flash Encryption

## 📖 Weitere Informationen

- [ESP Matter Documentation](https://docs.espressif.com/projects/esp-matter/en/latest/)
- [Matter Specification](https://csa-iot.org/all-solutions/matter/)
- [Apple HomeKit](https://developer.apple.com/homekit/)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)

## 🤝 Contributing

Verbesserungen und Bug-Fixes sind willkommen!

## 📄 License

Dieses Projekt basiert auf ESP-IDF Beispielen und ist unter den gleichen Lizenzbedingungen verfügbar.

---

**Version**: 1.0  
**Datum**: 6. Oktober 2025  
**Autor**: vnc3nt
