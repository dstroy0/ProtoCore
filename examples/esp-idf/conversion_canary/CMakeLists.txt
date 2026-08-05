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

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pc_conversion_canary)
