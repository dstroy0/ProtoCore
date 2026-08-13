# The bench application component: one C source, the library, and the shared bench macros on the
# include path (performance_benching/common holds device_bench.h).
#
# The library component's name is the repo-root directory name, whatever the repo was checked out
# as. Derive it the way bench_project.cmake derived the path.
get_filename_component(PROTOCORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
get_filename_component(PROTOCORE_NAME "${PROTOCORE_ROOT}" NAME)

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../../common"
    REQUIRES "${PROTOCORE_NAME}"
)
