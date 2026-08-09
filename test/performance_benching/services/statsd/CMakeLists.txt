# On-device CCOUNT microbenchmark for services/iot/statsd. See performance_benching/README.md.
#
#   idf.py -C test/performance_benching/services/statsd set-target esp32s3
#   idf.py -C test/performance_benching/services/statsd build
#   idf.py -C test/performance_benching/services/statsd -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

# This bench's feature switches, applied to the whole build (library included), the way the pio env
# carried them as -DPC_ENABLE_STATSD=1. Must precede project().
add_compile_definitions(
    PC_ENABLE_STATSD=1
)

include("${CMAKE_CURRENT_LIST_DIR}/../../common/bench_project.cmake")
project(bench_statsd)
