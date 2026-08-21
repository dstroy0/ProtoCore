# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/transport/tcp/lower
  GATE PROTOCORE_ENABLE_DIFFSERV
  SOURCES lower.c
  DEPS
    network_drivers/transport/tcp/common
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protomem
    network_drivers/tls
    network_drivers/transport/diffserv
    network_drivers/transport/tcp/protocol
)
