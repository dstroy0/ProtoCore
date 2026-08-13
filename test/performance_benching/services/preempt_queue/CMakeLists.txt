# On-device CCOUNT microbenchmark for preempt_queue. See test/performance_benching/README.md.
#
#   idf.py -C test/performance_benching/services/preempt_queue set-target esp32s3
#   idf.py -C test/performance_benching/services/preempt_queue build
#   idf.py -C test/performance_benching/services/preempt_queue -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

# This bench's feature switches, applied to the whole build so they reach the separately-compiled
# library and not just this translation unit. Must precede project().
add_compile_definitions(
    PROTOCORE_ENABLE_PREEMPT_QUEUE=1
    PROTOCORE_PQ_DEPTH=4
    PROTOCORE_PQ_ITEM_SIZE=4
)

include("${CMAKE_CURRENT_LIST_DIR}/../../common/bench_project.cmake")
project(bench_preempt_queue)
