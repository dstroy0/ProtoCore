# The bench application component: one C source, the library, and the shared bench macros on the
# include path (performance_benching/common holds device_bench.h).
#
# The library component's name is the repo-root directory name, whatever the repo was checked out
# as. Derive it the way bench_project.cmake derived the path (main is five levels below the root).
get_filename_component(PC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
get_filename_component(PC_NAME "${PC_ROOT}" NAME)

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../../common"
    REQUIRES "${PC_NAME}"
)
