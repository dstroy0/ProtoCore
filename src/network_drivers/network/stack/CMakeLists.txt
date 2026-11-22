# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/network/stack
  GATE PROTOCORE_ENABLE_IDEMIP
  SOURCES stack.c stack_tcp.c stack_net.c
  PRIVATE_DEPS
    server/core/worker
    mmgr
    idemip
)
