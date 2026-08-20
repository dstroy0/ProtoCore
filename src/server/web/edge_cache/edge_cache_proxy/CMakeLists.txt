# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/web/edge_cache/edge_cache_proxy
  GATE PROTOCORE_ENABLE_EDGE_CACHE
  SOURCES edge_cache_proxy.c
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/plaintext
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/application/http_range
    network_drivers/presentation/http
    network_drivers/presentation/http/http_parser
    network_drivers/presentation/http/httpcache
    network_drivers/session
    network_drivers/transport/tcp
    network_drivers/transport/tcp/client
    network_drivers/transport/tcp/protocol
    server/clock
    server/core/proto_handler
    server/web/edge_cache/edge_cache
    server/web/edge_cache/edge_cache_sd
    server/web/edge_cache/edge_fetch
    server/web/edge_cache/edge_mesh
    services/net/http_client
    services/storage/dbm
    shared/http_date
    shared/mime
)
