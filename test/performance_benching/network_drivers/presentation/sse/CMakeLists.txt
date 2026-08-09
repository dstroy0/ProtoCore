# On-device CCOUNT microbenchmark for sse. See test/performance_benching/README.md.
#
#   idf.py -C test/performance_benching/network_drivers/presentation/sse set-target esp32s3
#   idf.py -C test/performance_benching/network_drivers/presentation/sse build
#   idf.py -C test/performance_benching/network_drivers/presentation/sse -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../../../common/bench_project.cmake")
project(bench_sse)
