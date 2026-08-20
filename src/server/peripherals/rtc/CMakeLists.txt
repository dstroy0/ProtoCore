# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/peripherals/rtc
  GATE PROTOCORE_ENABLE_RTC
  SOURCES rtc.c
  PRIVATE_DEPS
    mmgr/secure
    server/peripherals/i2c
)
