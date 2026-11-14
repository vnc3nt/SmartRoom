#!/bin/bash

# SmartRoom Build Script
# Dieses Script aktiviert ESP-IDF und baut das Projekt

set -e

echo "========================================="
echo "  SmartRoom Build Script"
echo "========================================="
echo ""

# Farben für Output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# ESP-IDF Pfade (häufige Installationsorte)
ESP_IDF_PATHS=(
    "$HOME/esp/esp-idf"
    "$HOME/.espressif/esp-idf"
    "/opt/esp-idf"
    "./esp-idf"
)

# Suche ESP-IDF
echo "🔍 Suche ESP-IDF Installation..."
ESP_IDF_PATH=""
for path in "${ESP_IDF_PATHS[@]}"; do
    if [ -d "$path" ] && [ -f "$path/export.sh" ]; then
        ESP_IDF_PATH="$path"
        echo -e "${GREEN}✓ ESP-IDF gefunden: $ESP_IDF_PATH${NC}"
        break
    fi
done

if [ -z "$ESP_IDF_PATH" ]; then
    echo -e "${RED}✗ ESP-IDF nicht gefunden!${NC}"
    echo ""
    echo "Bitte installiere ESP-IDF:"
    echo "  https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/"
    echo ""
    echo "Oder setze IDF_PATH manuell:"
    echo "  export IDF_PATH=/pfad/zu/esp-idf"
    exit 1
fi

# ESP-IDF aktivieren
echo "📦 Aktiviere ESP-IDF..."
source "$ESP_IDF_PATH/export.sh"

# IDF Version anzeigen
echo ""
echo "ESP-IDF Version:"
idf.py --version
echo ""

# Wechsle ins Projektverzeichnis
cd "$(dirname "$0")"

# Build-Optionen
BUILD_CLEAN=false
FLASH=false
MONITOR=false
PORT=""

# Parse Argumente
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            BUILD_CLEAN=true
            shift
            ;;
        --flash)
            FLASH=true
            shift
            ;;
        --monitor)
            MONITOR=true
            shift
            ;;
        --port)
            PORT="$2"
            shift 2
            ;;
        --all)
            FLASH=true
            MONITOR=true
            shift
            ;;
        *)
            echo "Unbekannte Option: $1"
            echo ""
            echo "Verwendung: $0 [--clean] [--flash] [--monitor] [--port PORT] [--all]"
            echo ""
            echo "Optionen:"
            echo "  --clean     Führe 'fullclean' vor dem Build aus"
            echo "  --flash     Flashe nach dem Build"
            echo "  --monitor   Starte Serial Monitor nach dem Flash"
            echo "  --port PORT Spezifiziere Serial Port (z.B. /dev/tty.usbserial-0001)"
            echo "  --all       Flash + Monitor"
            exit 1
            ;;
    esac
done

# Clean Build
if [ "$BUILD_CLEAN" = true ]; then
    echo "🧹 Bereinige Build-Verzeichnis..."
    idf.py fullclean
    echo ""
fi

# Build
echo "🔨 Baue Projekt..."
idf.py build

if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}========================================="
    echo "  ✓ Build erfolgreich!"
    echo "=========================================${NC}"
    echo ""
    
    # Zeige Binary-Informationen
    echo "📊 Binary Informationen:"
    idf.py size
    echo ""
else
    echo ""
    echo -e "${RED}========================================="
    echo "  ✗ Build fehlgeschlagen!"
    echo "=========================================${NC}"
    exit 1
fi

# Flash
if [ "$FLASH" = true ]; then
    echo "📤 Flashe auf ESP32..."
    
    # Port automatisch erkennen falls nicht angegeben
    if [ -z "$PORT" ]; then
        echo "🔍 Suche Serial Port..."
        # macOS
        if [[ "$OSTYPE" == "darwin"* ]]; then
            PORT=$(ls /dev/tty.usbserial-* 2>/dev/null | head -n 1)
            if [ -z "$PORT" ]; then
                PORT=$(ls /dev/cu.usbserial-* 2>/dev/null | head -n 1)
            fi
        # Linux
        else
            PORT=$(ls /dev/ttyUSB* 2>/dev/null | head -n 1)
            if [ -z "$PORT" ]; then
                PORT=$(ls /dev/ttyACM* 2>/dev/null | head -n 1)
            fi
        fi
        
        if [ -z "$PORT" ]; then
            echo -e "${RED}✗ Kein Serial Port gefunden!${NC}"
            echo "Bitte verbinde den ESP32 oder gib den Port manuell an:"
            echo "  $0 --flash --port /dev/tty.usbserial-XXXX"
            exit 1
        fi
    fi
    
    echo -e "${GREEN}✓ Verwende Port: $PORT${NC}"
    idf.py -p "$PORT" flash
    
    if [ $? -eq 0 ]; then
        echo ""
        echo -e "${GREEN}✓ Flash erfolgreich!${NC}"
    else
        echo ""
        echo -e "${RED}✗ Flash fehlgeschlagen!${NC}"
        exit 1
    fi
fi

# Monitor
if [ "$MONITOR" = true ]; then
    echo ""
    echo "📡 Starte Serial Monitor..."
    echo "   (Beenden mit Ctrl+])"
    echo ""
    
    if [ -z "$PORT" ]; then
        idf.py monitor
    else
        idf.py -p "$PORT" monitor
    fi
fi

echo ""
echo -e "${GREEN}✓ Fertig!${NC}"
echo ""

if [ "$FLASH" = false ] && [ "$MONITOR" = false ]; then
    echo "💡 Nächste Schritte:"
    echo "   Flash:   $0 --flash"
    echo "   Monitor: $0 --flash --monitor"
    echo "   Alles:   $0 --all"
    echo ""
fi
