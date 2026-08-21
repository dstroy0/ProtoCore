# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/transport/tcp/server
  GATE PROTOCORE_ENABLE_ACCEPT_THROTTLE
  SOURCES server.c
  DEPS
    network_drivers/transport/tcp/evt
    shared/ip
  PRIVATE_DEPS
    mmgr/plaintext
    network_drivers/tls
    network_drivers/transport/diffserv
    network_drivers/transport/net_addr
    network_drivers/transport/tcp
    network_drivers/transport/tcp/common
    network_drivers/transport/tcp/lower
    network_drivers/transport/tcp/protocol
    server/clock
    server/core/worker
)
