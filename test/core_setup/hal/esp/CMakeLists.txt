# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The esp arm: the seams answered by the part. The AES, SHA and bignum accelerators, the I2C and SPI
# buses, and the platform facts the IDF reports - MAC, partitions, OTA, core dump, heap, TWAI.
#
# Entered only when ESP_PLATFORM is set, from hal/CMakeLists.txt. Every source here includes an
# ESP-IDF header, so this directory is not a module that a host build gates off - it is one a host
# build has no compiler for.
#
# The accelerator files each take a FreeRTOS semaphore around the peripheral, because the peripheral
# is one block and two tasks hashing at once would interleave in it.

protocore_add_module(test/core_setup/hal/esp
  SOURCES
    esp_aes_hal.c
    esp_bignum.c
    esp_bus.c
    esp_crypto_hal.c
    esp_platform.c
    esp_sha_hal.c
  DEPS
    crypto/asymmetric/bignum
    mmgr/secure
)
