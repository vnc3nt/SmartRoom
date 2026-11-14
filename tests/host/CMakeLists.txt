cmake_minimum_required(VERSION 3.16)
project(smartroom_host_tests CXX)

enable_testing()
set(CMAKE_CXX_STANDARD 17)

add_executable(smartroom_host_tests
    test_smartroom_logic.cpp
    ../../main/smartroom_ir_codec.cpp
    ../../main/smartroom_learning.cpp
    ../../main/smartroom_store_record.cpp
)
target_include_directories(smartroom_host_tests PRIVATE ../../main)

add_test(NAME smartroom_host_tests COMMAND smartroom_host_tests)
