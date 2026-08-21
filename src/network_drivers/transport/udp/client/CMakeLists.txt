# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/transport/udp/client
  GATE PROTOCORE_ENABLE_DIFFSERV
  SOURCES client.c
  DEPS
    shared/ip
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/rawmemcpy
    network_drivers/transport/diffserv
    network_drivers/transport/net_addr
)
