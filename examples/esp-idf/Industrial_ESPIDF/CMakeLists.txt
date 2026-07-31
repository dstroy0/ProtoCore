# Industrial edge-gateway example: ProtoCore built with the ESP-IDF CMake
# toolchain (idf.py). One device fronts a control network with three industrial-facing services at
# once - a web dashboard (HTTP/80), a Modbus TCP slave (fieldbus/502), and an SNMP agent
# (management/UDP 161) - the shape of a real industrial edge gateway. Run from this directory:
#
#   idf.py set-target esp32        # or esp32s3
#   idf.py build                   # first build also fetches arduino-esp32 (a few minutes)
#   idf.py -p <PORT> flash monitor
#
# The library's arduino-esp32 dependency is pulled from the ESP Component Registry automatically
# (declared in the repo-root idf_component.yml).
cmake_minimum_required(VERSION 3.16)

# Point IDF at the repo root (three levels up: Industrial_ESPIDF -> esp-idf -> examples -> repo root),
# which is itself a component (has the repo-root CMakeLists.txt). Derive the path so the example is
# independent of where the repo is checked out.
get_filename_component(PC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${PC_ROOT}")

# Turn the industrial feature set on for the WHOLE build. Every PC_ENABLE_* guard lives inside the
# library's own .cpp sources (compiled by the repo-root component), so the flags must be global
# compile definitions set here, before project() - the ESP-IDF equivalent of Arduino's build_opt.h or
# PlatformIO's build_flags. A #define in main.cpp alone would not reach the separately-compiled
# library, and modbus_server_init() / snmp_agent_init() would then fail to link.
add_compile_definitions(
    PC_ENABLE_MODBUS=1
    PC_ENABLE_SNMP=1
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pc_industrial)
