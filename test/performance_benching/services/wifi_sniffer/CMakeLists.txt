# On-device CCOUNT microbenchmark for wifi_sniffer. See test/performance_benching/README.md.
#
#   idf.py -C test/performance_benching/services/wifi_sniffer set-target esp32s3
#   idf.py -C test/performance_benching/services/wifi_sniffer build
#   idf.py -C test/performance_benching/services/wifi_sniffer -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

# This bench's feature switches, applied to the whole build so they reach the separately-compiled
# library and not just this translation unit. Must precede project().
add_compile_definitions(
    PROTOCORE_ENABLE_WIFI_SNIFFER=1
)

include("${CMAKE_CURRENT_LIST_DIR}/../../common/bench_project.cmake")
project(bench_wifi_sniffer)
