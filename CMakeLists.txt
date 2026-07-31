# ESP-IDF component registration for ProtoCore.
#
# Makes the library buildable with the ESP-IDF CMake toolchain (idf.py) as a component,
# alongside the existing PlatformIO (library.json) and Arduino (library.properties) build
# paths - both of which ignore this file. The library calls Arduino APIs (WiFi, ESP, millis,
# Serial, ...), so it is built with arduino-esp32 as a component; idf_component.yml declares
# that dependency so the ESP Component Manager pulls it automatically.
#
# Every feature source under src/ is compiled; the ones whose PC_ENABLE_* flag is off strip
# to an empty translation unit via their guard - the same whole-library-compiles model the
# Arduino build uses (esp32dev builds with no extra -D flags, only protocore_config.h defaults).
#
# Plain GLOB (no CONFIGURE_DEPENDS): IDF evaluates a component's CMakeLists in script mode
# (cmake -P) to extract REQUIRES, and CONFIGURE_DEPENDS is illegal there. The glob is therefore
# evaluated at configure time - run `idf.py reconfigure` after adding or removing a source file.
file(GLOB_RECURSE PC_SRCS "src/*.c" "src/*.cpp")

# REQUIRES lists arduino-esp32 plus the IDF components the library #includes directly across its
# feature set (transport -> lwip, TLS/crypto -> mbedtls, OTA -> app_update, config -> nvs_flash,
# sensors -> esp_driver_*, ...). arduino-esp32 re-exports most of these, but declaring the direct
# dependencies is the IDF best practice and keeps a feature build from missing an include path.
idf_component_register(
    SRCS ${PC_SRCS}
    INCLUDE_DIRS "src"
    REQUIRES
        arduino-esp32
        lwip
        mbedtls
        esp_netif
        esp_wifi
        nvs_flash
        app_update
        esp_timer
        esp_hw_support
        esp_system
        spi_flash
        esp_partition
        vfs
        fatfs
        freertos
        esp_driver_gpio
        esp_driver_spi
        esp_driver_i2c
        esp_driver_uart
)

# An IDF project compiles every component with -Werror=all (arduino-esp32 sets it as a global build
# property), which our library is never built under elsewhere (Arduino/PlatformIO use -Wall, not
# -Werror). Only -Werror=comment is demoted here: it fires on legitimate wildcard-route examples
# written in doc comments (e.g. `"/api/*"` - the `/*` substring reads as a nested comment open), which
# are documentation, not defects. A bare -Wno-error would NOT cancel a specific -Werror=comment
# promotion, so the disable must name the warning. Every other warning keeps the project's -Werror,
# so real issues still fail the build.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=comment)
