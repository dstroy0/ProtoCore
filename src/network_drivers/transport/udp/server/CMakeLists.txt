# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/transport/udp/server
  GATE PROTOCORE_ENABLE_DIFFSERV
  SOURCES server.c
  DEPS
    shared/ip
  PRIVATE_DEPS
    mmgr/plaintext
    network_drivers/transport/diffserv
    network_drivers/transport/net_addr
    network_drivers/transport/udp/common
)
