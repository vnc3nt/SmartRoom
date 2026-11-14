# 📶 WiFi Konfiguration für SmartRoom

## 🎯 Übersicht

Es gibt **3 Methoden**, um WiFi für dein SmartRoom Matter Device zu konfigurieren:

---

## **Methode 1: Automatisch über HomeKit (Empfohlen!) ✨**

**Dies ist der Standard-Matter-Weg und die einfachste Methode!**

### So funktioniert's:

1. **ESP32 flashen** (ohne WiFi-Config)
   ```bash
   ./build.sh --all
   ```

2. **Home App öffnen**
   - Tippe auf **'+'**
   - Wähle **'Gerät hinzufügen'**

3. **Setup-Code eingeben**: `20202021`

4. **Home App fragt automatisch nach WiFi!**
   - Wähle dein WiFi-Netzwerk aus
   - Gib das Passwort ein
   - Fertig! 🎉

### Vorteile:
- ✅ Kein Code-Ändern nötig
- ✅ Keine Neu-Kompilierung bei WiFi-Wechsel
- ✅ Sicher (Credentials werden verschlüsselt übertragen)
- ✅ Standard-Matter-Verfahren

---

## **Methode 2: Hardcoded im Code (Entwicklung) 💻**

**Für schnelle Tests oder wenn du WiFi fest im Code haben willst.**

### Schritt 1: Code editieren

Öffne `main/smart.c` und ändere Zeile 30-31:

```c
// WiFi Configuration (optional - für hardcoded WiFi)
#define WIFI_SSID           "Dein-WiFi-Name"      // ← Hier ändern
#define WIFI_PASSWORD       "DeinPasswort123"     // ← Hier ändern
```

### Schritt 2: Neu bauen und flashen

```bash
./build.sh --all
```

### Vorteile:
- ✅ WiFi verbindet sich beim Boot
- ✅ Kein BLE Pairing nötig für WiFi

### Nachteile:
- ❌ WiFi-Credentials im Code (unsicher für Produktion)
- ❌ Bei WiFi-Wechsel muss neu kompiliert werden

---

## **Methode 3: Über menuconfig (ESP-IDF Standard) ⚙️**

**Für fortgeschrittene Benutzer, die ESP-IDF menuconfig nutzen.**

### Schritt 1: menuconfig öffnen

```bash
idf.py menuconfig
```

### Schritt 2: WiFi Konfiguration

Navigiere zu:
```
Component config
  → Wi-Fi
    → WiFi Station Settings
      → WiFi SSID: "Dein-WiFi-Name"
      → WiFi Password: "DeinPasswort"
```

### Schritt 3: Speichern und bauen

- Drücke `S` zum Speichern
- Drücke `Q` zum Verlassen
- Baue neu: `idf.py build`

---

## 🔍 **WiFi Status prüfen**

Im **Serial Monitor** siehst du:

```
I (2456) SmartRoom: 📶 WiFi konfiguriert: SSID='Dein-WiFi'
I (2460) SmartRoom:    (WiFi wird während Matter Commissioning verbunden)
```

Oder nach dem Verbinden:

```
I (5234) wifi: connected to WiFi SSID: Dein-WiFi
I (5240) SmartRoom: ✓ WiFi verbunden!
I (5242) SmartRoom:    IP: 192.168.1.123
```

---

## ❓ FAQ

### **F: Welche Methode soll ich nutzen?**

**A:** Für HomeKit: **Methode 1** (automatisch über Home App)!

Das ist der offizielle Matter-Weg und am sichersten.

---

### **F: Warum verbindet sich WiFi nicht?**

**A:** Prüfe:
1. **SSID korrekt?** (Groß-/Kleinschreibung beachten!)
2. **Passwort korrekt?** (keine Leerzeichen am Anfang/Ende)
3. **2.4 GHz WiFi?** (ESP32 unterstützt kein 5 GHz!)
4. **Router erreichbar?** (ESP32 in Reichweite?)

---

### **F: Kann ich WiFi nach dem Pairing ändern?**

**A:** Ja! Zwei Optionen:

**Option 1: Factory Reset**
- Reset-Button auf ESP32 gedrückt halten
- Neu pairen in Home App

**Option 2: Code ändern**
- `main/smart.c` editieren (Zeile 30-31)
- Neu flashen mit `./build.sh --all`

---

### **F: Mein WiFi hat Sonderzeichen im Passwort**

**A:** Kein Problem! Nutze **Methode 1** (Home App) - die behandelt alle Zeichen korrekt.

Bei Hardcoded (Methode 2), Escape-Sequenzen beachten:
```c
#define WIFI_PASSWORD "Mein\"Passwort!"  // " muss escaped werden
```

---

### **F: Brauche ich statische IP?**

**A:** Nein! Matter/HomeKit nutzt mDNS, funktioniert mit DHCP.

Aber wenn du willst, kannst du in `sdkconfig.defaults` hinzufügen:
```
CONFIG_LWIP_DHCP_DOES_ARP_CHECK=y
```

---

## 🔐 Security Hinweise

### ⚠️ **WICHTIG für Produktion:**

- **Niemals** WiFi-Passwörter im Git-Repository committen!
- Nutze **Methode 1** (BLE Provisioning) für echte Produkte
- Oder nutze `sdkconfig` (wird in `.gitignore` aufgeführt)

### Für Entwicklung:

Wenn du **Methode 2** nutzt, füge `main/smart.c` zu `.gitignore` hinzu:
```bash
echo "main/wifi_credentials.h" >> .gitignore
```

Oder erstelle eine separate Datei:

**wifi_credentials.h:**
```c
#define WIFI_SSID     "Mein-WiFi"
#define WIFI_PASSWORD "MeinPasswort"
```

**In smart.c:**
```c
#include "wifi_credentials.h"  // Nicht in Git!
```

---

## 📱 Test-Checklist

Nach WiFi-Konfiguration:

- [ ] ESP32 geflasht
- [ ] Serial Monitor geöffnet
- [ ] "WiFi connected" in Logs sehen
- [ ] IP-Adresse angezeigt
- [ ] Home App findet Device
- [ ] Pairing erfolgreich
- [ ] Button funktioniert

---

## 🆘 Troubleshooting

### **Problem: "WiFi connect failed"**

**Lösung:**
1. Prüfe SSID/Passwort (Zeile 30-31 in `smart.c`)
2. Stelle sicher, dass es **2.4 GHz WiFi** ist
3. Prüfe Router-Logs für blockierte MACs

---

### **Problem: "WiFi connected, but Home App findet Device nicht"**

**Lösung:**
1. iPhone muss im **gleichen WiFi** sein!
2. Router muss **mDNS/Bonjour** erlauben (nicht blockieren)
3. Firewall/AP-Isolation deaktivieren

---

### **Problem: WiFi verbindet, aber disconnected nach paar Sekunden**

**Lösung:**
1. Router-Power-Save-Mode deaktivieren
2. ESP32 näher am Router platzieren
3. In `sdkconfig.defaults` hinzufügen:
   ```
   CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE=y
   ```

---

## 💡 Pro-Tipps

1. **Nutze Methode 1** für den ersten Test
2. Wechsel zu **Methode 2** nur wenn du häufig neu flashst
3. **Serial Monitor** immer offen lassen beim Debugging
4. **WiFi-Passwörter** niemals in Git committen!

---

## 📚 Weitere Infos

- [ESP-IDF WiFi Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html)
- [Matter Commissioning Guide](https://docs.espressif.com/projects/esp-matter/en/latest/)
- [HomeKit Accessory Protocol](https://developer.apple.com/homekit/)

---

**Erstellt**: 2025-10-06  
**Version**: 1.0

