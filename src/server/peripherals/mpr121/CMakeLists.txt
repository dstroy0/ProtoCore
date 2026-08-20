# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/peripherals/mpr121
  GATE PROTOCORE_ENABLE_MPR121
  SOURCES mpr121.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/secure
    server/clock
    server/peripherals/i2c
)
