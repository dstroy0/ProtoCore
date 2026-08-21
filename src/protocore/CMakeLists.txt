# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(protocore
  GATE PROTOCORE_ENABLE_HTTP_ROUTE
  SOURCES protocore.c
  PRIVATE_DEPS
    crypto/rng
    mmgr/membuild
    mmgr/plaintext
    mmgr/protoframe
    mmgr/protostr
    mmgr/rawmemcpy
    network_drivers/application/web_assets
    network_drivers/application/webdav
    network_drivers/presentation
    network_drivers/presentation/http
    network_drivers/presentation/http/http2/h2_server
    network_drivers/presentation/http/http3/h3_server
    network_drivers/presentation/http/http3/quic_server
    network_drivers/presentation/http/route/http_route
    network_drivers/session
    network_drivers/tls
    network_drivers/transport/tcp
    network_drivers/transport/tcp/common
    network_drivers/transport/tcp/protocol
    server/clock
    server/core/proto_handler
    server/core/worker
    server/io/webdav_handler
    server/security/csrf
    services/file_transfer/http_delivery
    shared/hex
    shared/mime
)
