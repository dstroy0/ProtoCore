# On-device CCOUNT microbenchmark for i2c. See test/performance_benching/README.md.
#
#   idf.py -C test/performance_benching/services/i2c set-target esp32s3
#   idf.py -C test/performance_benching/services/i2c build
#   idf.py -C test/performance_benching/services/i2c -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../../common/bench_project.cmake")
project(bench_i2c)
