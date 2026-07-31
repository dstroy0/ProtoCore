# Minimal ESP-IDF project that builds ProtoCore as a component from the
# local repo checkout (no registry download of the library itself). Run from this directory:
#
#   idf.py set-target esp32      # or esp32s3
#   idf.py build
#
# The library's arduino-esp32 dependency is pulled from the ESP Component Registry automatically
# (declared in the repo-root idf_component.yml).
cmake_minimum_required(VERSION 3.16)

# Point IDF at the repo root (three levels up: minimal -> esp-idf -> examples -> repo root), which
# is itself a component (has the repo-root CMakeLists.txt). Derive the path so the example is
# independent of where the repo is checked out.
get_filename_component(PC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${PC_ROOT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pc_minimal)
