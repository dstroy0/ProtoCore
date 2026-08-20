# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/peripherals/sen0192
  GATE PROTOCORE_ENABLE_SEN0192
  SOURCES sen0192.c
  PRIVATE_DEPS
    mmgr/plaintext
    server/clock
)
