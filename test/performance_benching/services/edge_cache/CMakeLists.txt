# On-device CCOUNT microbenchmark for edge_cache. See test/performance_benching/README.md.
#
#   idf.py -C test/performance_benching/services/edge_cache set-target esp32s3
#   idf.py -C test/performance_benching/services/edge_cache build
#   idf.py -C test/performance_benching/services/edge_cache -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

# This bench's feature switches, applied to the whole build so they reach the separately-compiled
# library and not just this translation unit. Must precede project().
add_compile_definitions(
    PROTOCORE_ENABLE_HTTP_CACHE=1
    PROTOCORE_ENABLE_HTTP_CLIENT=1
    PROTOCORE_ENABLE_EDGE_CACHE=1
    PROTOCORE_ENABLE_RANGE=1
)

include("${CMAKE_CURRENT_LIST_DIR}/../../common/bench_project.cmake")
project(bench_edge_cache)
