# Shared ESP-IDF project setup for every on-device CCOUNT microbenchmark under
# performance_benching/. Each feature's own CMakeLists.txt sets its PROTOCORE_ENABLE_* switches with
# add_compile_definitions(), includes this file, then calls project().

# CMAKE_CURRENT_LIST_DIR is this file's own directory, performance_benching/common, whatever depth
# the including bench sits at, so the repo root is three levels up (common/ -> performance_benching/
# -> test/ -> root). It is itself an IDF component. Derived, so the checkout folder name is free.
get_filename_component(PROTOCORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
get_filename_component(PROTOCORE_NAME "${PROTOCORE_ROOT}" NAME)

set(EXTRA_COMPONENT_DIRS "${PROTOCORE_ROOT}")

# Plain ESP-IDF: the library's CMakeLists leaves arduino-esp32 out of its REQUIRES under this.
set(PROTOCORE_WITH_ARDUINO OFF CACHE BOOL "" FORCE)

# Build only what a bench reaches. The library's registry manifest declares arduino-esp32
# unconditionally, so naming the component set here keeps that whole dependency tree out.
set(COMPONENTS main "${PROTOCORE_NAME}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
