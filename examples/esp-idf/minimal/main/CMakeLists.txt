# The application component. It uses Arduino APIs (WiFi) and the library's public header
# (protocore.h), so it REQUIRES both arduino-esp32 and the library component.
#
# The library component's name is the repo-root directory name (whatever the repo was checked out
# as). Derive it the same way the project CMakeLists derived the path, so REQUIRES matches
# regardless of the checkout folder name/case (main is four levels below the repo root).
get_filename_component(PC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
get_filename_component(PC_NAME "${PC_ROOT}" NAME)

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES arduino-esp32 "${PC_NAME}"
)
