# ESP-IDF component registration for ProtoCore.
#
# Makes the library buildable with the ESP-IDF CMake toolchain (idf.py) as a component,
# alongside the existing PlatformIO (library.json) and Arduino (library.properties) build
# paths - both of which ignore this file.
#
# Which IDF flavor is the consumer's choice, not this file's:
#
#   idf.py build                          Arduino as an ESP-IDF component (default)
#   idf.py -DPC_WITH_ARDUINO=OFF build    plain ESP-IDF, no Arduino layer
#
# The two differ only in whether arduino-esp32 is required; the IDF components below are needed
# either way, because the library #includes them directly across its feature set (transport ->
# lwip, TLS/crypto -> mbedtls, OTA -> app_update, config -> nvs_flash, sensors -> esp_driver_*).
# arduino-esp32 re-exports most of them, but declaring the direct dependencies is IDF best
# practice and keeps a feature build from missing an include path.
option(PC_WITH_ARDUINO "Build against arduino-esp32 (Arduino-as-component). OFF = plain ESP-IDF." ON)

# Every feature source under src/ is compiled; the ones whose PC_ENABLE_* flag is off strip
# to an empty translation unit via their guard - the same whole-library-compiles model the
# Arduino build uses (esp32dev builds with no extra -D flags, only protocore_config.h defaults).
#
# Plain GLOB (no CONFIGURE_DEPENDS): IDF evaluates a component's CMakeLists in script mode
# (cmake -P) to extract REQUIRES, and CONFIGURE_DEPENDS is illegal there. The glob is therefore
# evaluated at configure time - run `idf.py reconfigure` after adding or removing a source file.
file(GLOB_RECURSE PC_SRCS "src/*.c" "src/*.cpp")

set(PC_REQUIRES
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

if(PC_WITH_ARDUINO)
    list(APPEND PC_REQUIRES arduino-esp32)
endif()

idf_component_register(
    SRCS ${PC_SRCS}
    INCLUDE_DIRS "src"
    REQUIRES ${PC_REQUIRES}
)

if(PC_WITH_ARDUINO)
    # arduino-esp32 sets -Werror=all as a global build property, which the library is never built
    # under elsewhere (Arduino/PlatformIO use -Wall, not -Werror). Only -Werror=comment is demoted:
    # it fires on legitimate wildcard-route examples written in doc comments (e.g. `"/api/*"` - the
    # `/*` substring reads as a nested comment open), which are documentation, not defects. A bare
    # -Wno-error would NOT cancel a specific -Werror=comment promotion, so the disable must name the
    # warning. Every other warning keeps the project's -Werror, so real issues still fail the build.
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=comment)
endif()
