# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/network/forward
  GATE PROTOCORE_ENABLE_FORWARD
  SOURCES forward.c
  DEPS
    PRIVATE
    mmgr/plaintext
    mmgr/protomem
    network_drivers/physical/physical
    server/clock
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protomem
    server/clock
)
