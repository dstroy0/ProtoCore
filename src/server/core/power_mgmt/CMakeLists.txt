# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/core/power_mgmt
  GATE PROTOCORE_ENABLE_POWER_MGMT
  SOURCES power_mgmt.c
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/plaintext
)
