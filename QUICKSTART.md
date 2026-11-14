# 🚀 Quick Start Guide

## Schnellstart in 5 Schritten

### 1️⃣ Hardware anschließen

```
ESP32-C6 GPIO 18 ──[330Ω]──┬── IR LED (+)
                            │
ESP32-C6 GND ───────────────┴── IR LED (-)
```

### 2️⃣ Build & Flash

```bash
# Build das Projekt
./build.sh

# Build + Flash + Monitor
./build.sh --all --port /dev/tty.usbserial-XXXXX
```

**Oder manuell mit ESP-IDF:**

```bash
# ESP-IDF aktivieren
source ~/esp/esp-idf/export.sh

# WiFi konfigurieren (optional)
idf.py menuconfig

# Build
idf.py build

# Flash & Monitor
idf.py -p /dev/tty.usbserial-XXXXX flash monitor
```

### 3️⃣ WiFi konfigurieren

Der ESP32 erstellt automatisch einen WiFi Access Point falls keine Credentials gesetzt sind:

**SSID**: `MATTER-XXXX`  
**Password**: `12345678`

Oder setze deine eigenen WiFi-Daten mit:

```bash
./setup_wifi.sh
```

### 4️⃣ HomeKit Pairing

1. **Öffne Home App** auf iPhone
2. Tippe **'+'** → **'Gerät hinzufügen'**
3. Wähle **'Weitere Optionen...'**
4. Wähle **'SmartRoom'**
5. Gib Setup-Code ein: **`20202021`**

### 5️⃣ IR-Signal testen

- Tippe auf den Button in der Home App
- Das IR-Signal wird gesendet! 🎉

## 📝 Wichtige Informationen

### Standard-Werte

| Parameter | Wert |
|-----------|------|
| **Setup Code** | 20202021 |
| **IR GPIO** | GPIO 18 |
| **IR Address** | 0xFF00 |
| **IR Command** | 0xBA45 |
| **IR Frequenz** | 38 kHz |

### Eigenes IR-Signal verwenden

1. **Öffne** `main/smart.c`
2. **Finde** Zeile ~45:
   ```c
   static const ir_nec_scan_code_t ir_signal = {
       .address = 0xFF00,  // ← Hier ändern
       .command = 0xBA45,  // ← Hier ändern
   };
   ```
3. **Ändere** Address und Command
4. **Build neu**: `./build.sh --all`

### Serial Monitor Shortcuts

| Taste | Funktion |
|-------|----------|
| `Ctrl + ]` | Monitor beenden |
| `Ctrl + T` → `Ctrl + H` | Hilfe anzeigen |
| `Ctrl + T` → `Ctrl + R` | ESP32 Reset |

## 🔧 Troubleshooting

### ❌ "idf.py not found"

```bash
# ESP-IDF aktivieren
source ~/esp/esp-idf/export.sh

# Oder verwende build.sh (macht das automatisch)
./build.sh
```

### ❌ "No serial port found"

```bash
# macOS: Zeige alle Ports
ls /dev/tty.*

# Linux: Zeige alle Ports
ls /dev/ttyUSB* /dev/ttyACM*

# Verwende den gefundenen Port
./build.sh --all --port /dev/tty.usbserial-XXXXX
```

### ❌ Device erscheint nicht in Home App

1. **Prüfe WiFi:** ESP32 muss im selben Netzwerk sein
2. **Prüfe Logs:** Im Serial Monitor nach "WiFi connected" suchen
3. **Prüfe QR-Code:** Sollte im Serial Monitor erscheinen
4. **Factory Reset:** Reset-Button auf ESP32 drücken

### ❌ "Commissioning failed"

1. **Lösche alte Pairings:**
   ```bash
   idf.py erase-flash
   ./build.sh --all
   ```

2. **Verwende korrekten Setup-Code:** `20202021`

3. **Prüfe mDNS:** Router muss mDNS/Bonjour unterstützen

## 📱 HomeKit Automation

Nach dem Pairing kannst du:

- **Shortcuts** erstellen (z.B. "Hey Siri, Fernbedienung")
- **Automationen** einrichten (z.B. bei Sonnenuntergang)
- **Szenen** erstellen (z.B. "Film schauen")

## 💡 Tipps

- **IR-LED richtig ausrichten** - Die LED muss auf das zu steuernde Gerät zeigen
- **IR-Reichweite** - Typisch 5-10 Meter bei direkter Sicht
- **Multiple Devices** - Du kannst mehrere ESP32s mit verschiedenen IR-Signalen nutzen
- **Debug-Modus** - Verwende `idf.py monitor` um IR-Übertragung zu verifizieren

## 📚 Weitere Hilfe

- **Vollständige Doku**: Siehe `README_SETUP.md`
- **ESP-IDF Doku**: https://docs.espressif.com/projects/esp-idf/
- **Matter Doku**: https://docs.espressif.com/projects/esp-matter/

---

**Bei Problemen:** Öffne ein Issue auf GitHub oder kontaktiere den Entwickler.

**Viel Erfolg!** 🎉
