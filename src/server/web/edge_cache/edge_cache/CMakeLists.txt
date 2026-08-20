# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/web/edge_cache/edge_cache
  GATE PROTOCORE_ENABLE_EDGE_CACHE
  SOURCES edge_cache.c
  PRIVATE_DEPS
    crypto/hash/sha256
    mmgr/protomem
    mmgr/protostr
    network_drivers/presentation/http/httpcache
    shared/http_date
)
