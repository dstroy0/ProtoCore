# On-device CCOUNT microbenchmark for dma. See test/performance_benching/README.md.
#
#   idf.py -C test/performance_benching/mmgr/dma set-target esp32s3
#   idf.py -C test/performance_benching/mmgr/dma build
#   idf.py -C test/performance_benching/mmgr/dma -p COM7 flash monitor
cmake_minimum_required(VERSION 3.16)

# This bench's feature switches, applied to the whole build so they reach the separately-compiled
# library and not just this translation unit. Must precede project().
add_compile_definitions(
    PC_ENABLE_DMA=1
    PC_DMA_BUF_SIZE=8
    PC_DMA_CHANNELS=2
)

include("${CMAKE_CURRENT_LIST_DIR}/../../common/bench_project.cmake")
project(bench_dma)
