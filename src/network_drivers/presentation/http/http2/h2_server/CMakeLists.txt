# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/http2/h2_server
  GATE PROTOCORE_ENABLE_HTTP2 AND PROTOCORE_ENABLE_TLS
  SOURCES h2_server.c
  PRIVATE_DEPS
    mmgr/protomem
    network_drivers/presentation/http/http2/h2_conn
    network_drivers/presentation/http/http_parser
    network_drivers/session
    network_drivers/tls
    network_drivers/transport/tcp
)
