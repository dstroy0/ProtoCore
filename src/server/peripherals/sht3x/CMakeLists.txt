# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/peripherals/sht3x
  GATE PROTOCORE_ENABLE_SHT3X
  SOURCES sht3x.c
  PRIVATE_DEPS
    mmgr/endian
    mmgr/secure
    server/clock
    server/peripherals/i2c
    shared/crc
)
