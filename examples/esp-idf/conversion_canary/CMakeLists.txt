# A pure-C ESP-IDF project that compiles and links the library on the target.
#
#   idf.py set-target esp32s3      # or esp32, esp32c6, ...
#   idf.py build
#
# C11 in, C11 out: the library is reached the way its own sources reach it, so a failure here is the
# library's own and reads as one line of compiler output.
cmake_minimum_required(VERSION 3.16)

# Repo root is three levels up (conversion_canary -> esp-idf -> examples -> root) and is itself a
# component. Derived, so the checkout folder name does not matter.
get_filename_component(PC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${PC_ROOT}")

# Plain ESP-IDF: the library's CMakeLists leaves arduino-esp32 out of its REQUIRES under this.
set(PC_WITH_ARDUINO OFF CACHE BOOL "" FORCE)

# Build only what this project reaches. The library's registry manifest declares arduino-esp32
# unconditionally (the manifest format cannot condition on a build flag), so the component manager
# fetches it and its whole dependency tree - rainmaker, zigbee, esp_modem and the rest. Naming the
# component set here keeps those out of the build, which is the difference between a target check
# that answers about this library and one that answers about the registry.
set(COMPONENTS main ProtoCore)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pc_conversion_canary)
