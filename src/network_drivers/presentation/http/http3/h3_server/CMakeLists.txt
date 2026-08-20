# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/http3/h3_server
  GATE PROTOCORE_ENABLE_HTTP3
  SOURCES h3_server.c
  DEPS
    PRIVATE
    mmgr/protostr
    mmgr/rawmemcpy
    network_drivers/presentation/http
    network_drivers/presentation/http/http3/quic_server
    network_drivers/session
    network_drivers/transport/tcp/protocol
  PRIVATE_DEPS
    mmgr/protostr
    mmgr/rawmemcpy
    network_drivers/presentation/http
    network_drivers/session
    network_drivers/transport/tcp/protocol
)
