# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/session
  GATE PROTOCORE_ENABLE_HTTP2
  SOURCES session.c
  DEPS
    PRIVATE
    mmgr/plaintext
    network_drivers/presentation
    network_drivers/presentation/ssh/network
    network_drivers/transport/tcp/evt
    network_drivers/transport/tcp/protocol
    network_drivers/transport/tcp/server
    network_drivers/transport/udp/server
    server/clock
    server/core/proto_handler
    server/core/worker
    server/storage/filesystem
  PRIVATE_DEPS
    mmgr/plaintext
    network_drivers/presentation
    network_drivers/presentation/ssh/network
    network_drivers/transport/tcp/protocol
    network_drivers/transport/tcp/server
    network_drivers/transport/udp/server
    server/clock
    server/storage/filesystem
)

add_subdirectory(scp)
add_subdirectory(sse)
add_subdirectory(ws)


