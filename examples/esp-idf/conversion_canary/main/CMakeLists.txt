# The application component: one C source, and the library as its only requirement.
#
# The library component's name is the repo-root directory name, whatever the repo was checked out
# as. Derive it the way the project CMakeLists derived the path (main is four levels below the root).
get_filename_component(PC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
get_filename_component(PC_NAME "${PC_ROOT}" NAME)

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES "${PC_NAME}"
)
