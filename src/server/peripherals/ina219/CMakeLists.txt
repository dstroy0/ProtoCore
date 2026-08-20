# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/peripherals/ina219
  GATE PROTOCORE_ENABLE_INA219
  SOURCES ina219.c
  PRIVATE_DEPS
    mmgr/endian
    mmgr/secure
    server/peripherals/i2c
)
