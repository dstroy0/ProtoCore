# Shared ESP-IDF project setup for every on-device CCOUNT microbenchmark under
# performance_benching/. Each feature's own CMakeLists.txt sets its PC_ENABLE_* switches with
# add_compile_definitions(), includes this file, then calls project(). Replaces the common.ini that
# the PlatformIO envs extended.
#
# Board/core match performance_benching/library_comparison/protocore: esp32-s3-devkitc-1.

# The repo root is four levels up (<feature>/ -> <layer>/ -> performance_benching/ -> test/ -> root)
# and is itself an IDF component. Derived, so the checkout folder name does not matter.
get_filename_component(PC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
get_filename_component(PC_NAME "${PC_ROOT}" NAME)

set(EXTRA_COMPONENT_DIRS "${PC_ROOT}")

# Plain ESP-IDF: the library's CMakeLists leaves arduino-esp32 out of its REQUIRES under this.
set(PC_WITH_ARDUINO OFF CACHE BOOL "" FORCE)

# Build only what a bench reaches. The library's registry manifest declares arduino-esp32
# unconditionally, so naming the component set here keeps that whole dependency tree out.
set(COMPONENTS main "${PC_NAME}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
