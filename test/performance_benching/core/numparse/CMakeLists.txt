# On-device CCOUNT microbenchmark for numparse. See test/performance_benching/README.md.
#
#   idf.py -C test/performance_benching/core/numparse set-target esp32s3
#   idf.py -C test/performance_benching/core/numparse build
#   idf.py -C test/performance_benching/core/numparse -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../../common/bench_project.cmake")
project(bench_numparse)
