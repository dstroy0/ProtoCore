# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/http3/quic_server
  GATE PROTOCORE_ENABLE_HTTP3
  SOURCES quic_server.c
  DEPS
    PRIVATE
    mmgr/plaintext
    mmgr/protomem
    mmgr/ring
    mmgr/secure
    network_drivers/presentation/http/http3/h3_conn
    network_drivers/presentation/http/http3/quic_conn
    network_drivers/presentation/http/http3/quic_packet
    network_drivers/presentation/http/http3/quic_tls
    network_drivers/presentation/http/http3/quic_tp
    network_drivers/transport/udp/server
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protomem
    mmgr/ring
    mmgr/secure
    network_drivers/presentation/http/http3/quic_packet
    network_drivers/presentation/http/http3/quic_tls
    network_drivers/presentation/http/http3/quic_tp
    network_drivers/transport/udp/server
)
