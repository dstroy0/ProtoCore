# On-device CCOUNT microbenchmark for coap. See test/performance_benching/README.md.
#
#   idf.py -C test/performance_benching/services/coap set-target esp32s3
#   idf.py -C test/performance_benching/services/coap build
#   idf.py -C test/performance_benching/services/coap -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

# This bench's feature switches, applied to the whole build so they reach the separately-compiled
# library and not just this translation unit. Must precede project().
add_compile_definitions(
    PROTOCORE_ENABLE_COAP=1
    PROTOCORE_ENABLE_COAP_BLOCK=1
    PROTOCORE_COAP_BLOCK_SZX_MAX=2
    PROTOCORE_COAP_BLOCK1_MAX=128
)

include("${CMAKE_CURRENT_LIST_DIR}/../../common/bench_project.cmake")
project(bench_coap)
