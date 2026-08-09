# On-device CCOUNT microbenchmark for s7comm. See test/performance_benching/README.md.
#
#   idf.py -C test/performance_benching/services/s7comm set-target esp32s3
#   idf.py -C test/performance_benching/services/s7comm build
#   idf.py -C test/performance_benching/services/s7comm -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

# This bench's feature switches, applied to the whole build so they reach the separately-compiled
# library and not just this translation unit. Must precede project().
add_compile_definitions(
    PC_ENABLE_S7COMM=1
)

include("${CMAKE_CURRENT_LIST_DIR}/../../common/bench_project.cmake")
project(bench_s7comm)
