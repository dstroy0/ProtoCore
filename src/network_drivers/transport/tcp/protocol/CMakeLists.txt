# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/transport/tcp/protocol
  GATE PROTOCORE_ENABLE_TLS
  SOURCES protocol.c
  DEPS
    network_drivers/transport/tcp/evt
    shared/ip
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/rawmemcpy
    network_drivers/tls
    network_drivers/transport/net_addr
    network_drivers/transport/tcp/common
    network_drivers/transport/tcp/lower
    network_drivers/transport/tcp/server
    server/clock
    server/core/worker
)
