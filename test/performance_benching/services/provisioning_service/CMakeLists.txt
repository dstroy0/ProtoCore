# On-device CCOUNT microbenchmark for provisioning_service. See test/performance_benching/README.md.
#
#   idf.py -C test/performance_benching/services/provisioning_service set-target esp32s3
#   idf.py -C test/performance_benching/services/provisioning_service build
#   idf.py -C test/performance_benching/services/provisioning_service -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../../common/bench_project.cmake")
project(bench_provisioning_service)
