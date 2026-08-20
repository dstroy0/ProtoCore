# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/net/relay/relay_listener
  GATE PROTOCORE_ENABLE_RELAY
  SOURCES relay_listener.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/physical/radio_power
    network_drivers/session
    network_drivers/transport/tcp
    network_drivers/transport/tcp/client
    network_drivers/transport/tcp/protocol
    server/core/proto_handler
    server/net/relay/relay
)
