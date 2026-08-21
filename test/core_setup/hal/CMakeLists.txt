# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The seams themselves - nvs.h is here rather than in either arm, because it is the interface both
# answer and three modules under src/ include it by that name.
#
# The arms follow. Each is a module of its own so a consumer names the one it took.

protocore_add_module(test/core_setup/hal
  HEADER_ONLY
)

add_subdirectory(host)
add_subdirectory(portable)

# The esp arm compiles only under the ESP-IDF: its sources include driver/i2c.h, esp_partition.h,
# freertos/semphr.h and mbedtls/bignum.h, none of which exist in a host build. ESP_PLATFORM is the
# IDF's own variable, set by its build system - so this is not a flag this tree invented, and a
# host build simply does not enter the directory rather than declaring a module it cannot compile.
if(ESP_PLATFORM)
  add_subdirectory(esp)
endif()
