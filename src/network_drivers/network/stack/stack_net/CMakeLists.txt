# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/network/stack/stack_net
  GATE PROTOCORE_ENABLE_IDEMIP
  SOURCES stack_net.c
  PRIVATE_DEPS
    network_drivers/network/stack
    network_drivers/network/stack/stack_tcp
    mmgr
    idemip
)
