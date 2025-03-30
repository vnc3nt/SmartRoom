cmake_minimum_required(VERSION 3.16)

# Da ESP-Matter bereits in Ihrem components-Verzeichnis existiert, 
# können Sie direkt diesen Pfad verwenden ohne Umgebungsvariablen

# Wichtig: EXTRA_COMPONENT_DIRS muss VOR dem include von project.cmake stehen
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/components/esp-matter/components"
    "${CMAKE_SOURCE_DIR}/components/esp-matter/connectedhomeip/connectedhomeip/config/esp32/components"
    "${CMAKE_SOURCE_DIR}/components/esp-matter/examples/common"
)

# ESP-IDF project.cmake einbinden
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Minimalen Build beibehalten
idf_build_set_property(MINIMAL_BUILD ON)

project(SmartRoom)

# C++ Compiler-Optionen für Matter hinzufügen
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
