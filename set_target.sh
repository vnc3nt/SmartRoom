#!/bin/bash

# ==========================================
# ESP32-C6 Target Setup Script
# ==========================================

# Farben für die Ausgabe
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================="
echo "  ESP32-C6 Target Setup"
echo "========================================="

# Suche ESP-IDF Installation
echo -e "${YELLOW}🔍 Suche ESP-IDF Installation...${NC}"
if [ -d "$HOME/esp/esp-idf" ]; then
    export IDF_PATH="$HOME/esp/esp-idf"
    echo -e "${GREEN}✓ ESP-IDF gefunden: $IDF_PATH${NC}"
else
    echo -e "${RED}✗ ESP-IDF nicht gefunden!${NC}"
    exit 1
fi

# Aktiviere ESP-IDF
echo -e "${YELLOW}📦 Aktiviere ESP-IDF...${NC}"
source "$IDF_PATH/export.sh"

# Setze Target auf ESP32-C6
echo -e "${YELLOW}🎯 Setze Target auf ESP32-C6...${NC}"
idf.py set-target esp32c6

echo -e "${GREEN}✅ Target auf ESP32-C6 gesetzt!${NC}"
echo ""
echo "Führe jetzt './build.sh' aus um zu bauen."
