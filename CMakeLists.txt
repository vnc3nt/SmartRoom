cmake_minimum_required(VERSION 3.31)

# Partition table
set(PARTITION_CSV_PATH "partitions.csv")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SmartRoom)

# Minimalen Build beibehalten
idf_build_set_property(MINIMAL_BUILD ON)

# C++ Compiler-Optionen für Matter
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)


