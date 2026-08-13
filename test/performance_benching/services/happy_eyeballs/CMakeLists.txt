# On-device CCOUNT microbenchmark for happy_eyeballs. See test/performance_benching/README.md.
#
#   idf.py -C test/performance_benching/services/happy_eyeballs set-target esp32s3
#   idf.py -C test/performance_benching/services/happy_eyeballs build
#   idf.py -C test/performance_benching/services/happy_eyeballs -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

# This bench's feature switches, applied to the whole build so they reach the separately-compiled
# library and not just this translation unit. Must precede project().
add_compile_definitions(
    PROTOCORE_ENABLE_HAPPY_EYEBALLS=1
)

include("${CMAKE_CURRENT_LIST_DIR}/../../common/bench_project.cmake")
project(bench_happy_eyeballs)
