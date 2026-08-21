# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/iot/coap/coaps_server
  GATE PROTOCORE_ENABLE_DTLS AND PROTOCORE_ENABLE_COAP
  SOURCES coaps_server.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
    mmgr/ring
    mmgr/secure
    network_drivers/presentation/security/dtls/dtls_conn
    network_drivers/transport/udp/server
    server/clock
    services/iot/coap/coaps
    shared/ip
)
