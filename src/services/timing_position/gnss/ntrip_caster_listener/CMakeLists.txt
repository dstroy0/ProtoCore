# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/timing_position/gnss/ntrip_caster_listener
  GATE PROTOCORE_ENABLE_NTRIP_CASTER
  SOURCES ntrip_caster_listener.c
  DEPS
    PRIVATE
    mmgr/protomem
    mmgr/protostr
    network_drivers/session
    network_drivers/transport/tcp
    network_drivers/transport/tcp/protocol
    server/core/proto_handler
    services/timing_position/gnss/ntrip_caster
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
    network_drivers/session
    network_drivers/transport/tcp
    network_drivers/transport/tcp/protocol
    server/core/proto_handler
)
