# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/transport/udp
  GATE PROTOCORE_ENABLE_UDP
  SOURCES udp.c
  DEPS
    network_drivers/transport/udp/client
    network_drivers/transport/udp/server
)

add_subdirectory(client)
add_subdirectory(common)
add_subdirectory(server)


