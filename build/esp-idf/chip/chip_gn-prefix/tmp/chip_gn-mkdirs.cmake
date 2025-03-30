# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/vnc3nt/esp/esp-matter/connectedhomeip/connectedhomeip")
  file(MAKE_DIRECTORY "/Users/vnc3nt/esp/esp-matter/connectedhomeip/connectedhomeip")
endif()
file(MAKE_DIRECTORY
  "/Users/vnc3nt/Documents/Code/VS-Code/ESP32/SmartRoom/SmartRoom/build/esp-idf/chip"
  "/Users/vnc3nt/Documents/Code/VS-Code/ESP32/SmartRoom/SmartRoom/build/esp-idf/chip/chip_gn-prefix"
  "/Users/vnc3nt/Documents/Code/VS-Code/ESP32/SmartRoom/SmartRoom/build/esp-idf/chip/chip_gn-prefix/tmp"
  "/Users/vnc3nt/Documents/Code/VS-Code/ESP32/SmartRoom/SmartRoom/build/esp-idf/chip/chip_gn-prefix/src/chip_gn-stamp"
  "/Users/vnc3nt/Documents/Code/VS-Code/ESP32/SmartRoom/SmartRoom/build/esp-idf/chip/chip_gn-prefix/src"
  "/Users/vnc3nt/Documents/Code/VS-Code/ESP32/SmartRoom/SmartRoom/build/esp-idf/chip/chip_gn-prefix/src/chip_gn-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/vnc3nt/Documents/Code/VS-Code/ESP32/SmartRoom/SmartRoom/build/esp-idf/chip/chip_gn-prefix/src/chip_gn-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/vnc3nt/Documents/Code/VS-Code/ESP32/SmartRoom/SmartRoom/build/esp-idf/chip/chip_gn-prefix/src/chip_gn-stamp${cfgdir}") # cfgdir has leading slash
endif()
