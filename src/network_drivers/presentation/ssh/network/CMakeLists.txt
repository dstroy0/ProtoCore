# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/ssh/network
  GATE PROTOCORE_ENABLE_SSH_ZLIB
  SOURCES network.c
  DEPS
    PRIVATE
    mmgr/plaintext
    network_drivers/presentation/ssh
    network_drivers/presentation/ssh/auth
    network_drivers/presentation/ssh/common
    network_drivers/presentation/ssh/connection
    network_drivers/presentation/ssh/transport/comp
    network_drivers/presentation/ssh/transport/transport
    network_drivers/transport/tcp/client
    network_drivers/transport/tcp/protocol
    server/clock
    server/core/proto_handler
    server/core/worker
  PRIVATE_DEPS
    mmgr/plaintext
    network_drivers/presentation/ssh
    network_drivers/presentation/ssh/auth
    network_drivers/presentation/ssh/connection
    network_drivers/presentation/ssh/transport/comp
    network_drivers/presentation/ssh/transport/transport
    network_drivers/transport/tcp/client
    network_drivers/transport/tcp/protocol
    server/clock
    server/core/proto_handler
    server/core/worker
)
