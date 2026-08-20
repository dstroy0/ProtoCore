# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/ssh/server
  GATE PROTOCORE_ENABLE_SSH_ZLIB
  SOURCES server.c
  DEPS
    PRIVATE
    mmgr/plaintext
    mmgr/secure
    network_drivers/presentation/ssh
    network_drivers/presentation/ssh/auth
    network_drivers/presentation/ssh/common
    network_drivers/presentation/ssh/connection
    network_drivers/presentation/ssh/network
    network_drivers/presentation/ssh/transport/comp
    network_drivers/presentation/ssh/transport/transport
    network_drivers/transport/tcp
    network_drivers/transport/tcp/common
    network_drivers/transport/tcp/protocol
    network_drivers/transport/tcp/server
    server/clock
    server/core/proto_handler
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/secure
    network_drivers/presentation/ssh
    network_drivers/presentation/ssh/auth
    network_drivers/presentation/ssh/connection
    network_drivers/presentation/ssh/network
    network_drivers/presentation/ssh/transport/comp
    network_drivers/presentation/ssh/transport/transport
    network_drivers/transport/tcp
    network_drivers/transport/tcp/common
    network_drivers/transport/tcp/protocol
    network_drivers/transport/tcp/server
    server/clock
    server/core/proto_handler
)
