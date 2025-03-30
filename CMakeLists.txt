cmake_minimum_required(VERSION 3.31)

set(ENV{ESP_MATTER_PATH} "/Users/vnc3nt/esp/esp-matter")


# ESP-Matter-Komponenten einbinden (VOR dem project.cmake Include!)
set(EXTRA_COMPONENT_DIRS "/Users/vnc3nt/esp/esp-matter/components"
                       "/Users/vnc3nt/esp/esp-matter/device_hal/device/esp32c6_devkit_c"
                       "/Users/vnc3nt/esp/esp-matter/connectedhomeip/connectedhomeip/config/esp32/components")


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SmartRoom)

# Minimalen Build beibehalten
idf_build_set_property(MINIMAL_BUILD ON)

# C++ Compiler-Optionen für Matter
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)


