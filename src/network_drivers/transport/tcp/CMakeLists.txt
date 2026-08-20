# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/transport/tcp
  GATE PROTOCORE_ENABLE_TCP
  SOURCES tcp.c
  DEPS
    network_drivers/transport/tcp/client
    network_drivers/transport/tcp/protocol
    network_drivers/transport/tcp/server
)

add_subdirectory(client)
add_subdirectory(common)
add_subdirectory(evt)
add_subdirectory(lower)
add_subdirectory(protocol)
add_subdirectory(server)


