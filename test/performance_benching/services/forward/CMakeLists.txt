# On-device CCOUNT microbenchmark for forward. See test/performance_benching/README.md.
#
#   idf.py -C test/performance_benching/services/forward set-target esp32s3
#   idf.py -C test/performance_benching/services/forward build
#   idf.py -C test/performance_benching/services/forward -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

# This bench's feature switches, applied to the whole build so they reach the separately-compiled
# library and not just this translation unit. Must precede project().
add_compile_definitions(
    PC_ENABLE_FORWARD=1
    PC_FWD_MAX_IFACES=4
    PC_FWD_MAX_RULES=4
    PC_FWD_MAX_ACL=4
    PC_FWD_MAX_ROUTES=4
    PC_FWD_INSPECT=1
)

include("${CMAKE_CURRENT_LIST_DIR}/../../common/bench_project.cmake")
project(bench_forward)
