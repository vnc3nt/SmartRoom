#!/bin/bash

# SmartRoom WiFi Configuration Script
# 
# Dieses Script hilft dir, WiFi-Credentials für das Matter Device zu setzen

echo "========================================="
echo "  SmartRoom WiFi Konfiguration"
echo "========================================="
echo ""

# WiFi SSID abfragen
read -p "WiFi SSID: " WIFI_SSID
read -sp "WiFi Passwort: " WIFI_PASSWORD
echo ""
echo ""

# Konfiguration in sdkconfig schreiben
echo "Setze WiFi Konfiguration..."

# Prüfe ob idf.py verfügbar ist
if ! command -v idf.py &> /dev/null
then
    echo "Error: idf.py nicht gefunden. Stelle sicher, dass ESP-IDF aktiviert ist:"
    echo "  source ~/esp/esp-idf/export.sh"
    exit 1
fi

# Setze WiFi SSID und Passwort über menuconfig
echo "CONFIG_EXAMPLE_WIFI_SSID=\"$WIFI_SSID\"" >> sdkconfig.defaults
echo "CONFIG_EXAMPLE_WIFI_PASSWORD=\"$WIFI_PASSWORD\"" >> sdkconfig.defaults

echo ""
echo "✓ WiFi Konfiguration gespeichert!"
echo ""
echo "Nächste Schritte:"
echo "  1. Baue das Projekt: idf.py build"
echo "  2. Flashe auf ESP32: idf.py -p /dev/ttyUSB0 flash monitor"
echo ""
echo "Für HomeKit Pairing:"
echo "  - Öffne die Home App"
echo "  - Tippe auf '+' → 'Gerät hinzufügen'"
echo "  - Verwende Setup-Code: 20202021"
echo ""
