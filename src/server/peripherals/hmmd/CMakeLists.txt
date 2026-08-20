# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/peripherals/hmmd
  GATE PROTOCORE_ENABLE_HMMD
  SOURCES hmmd.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/secure
    server/peripherals/uart
)
