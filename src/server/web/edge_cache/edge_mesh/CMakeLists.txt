# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/web/edge_cache/edge_mesh
  GATE PROTOCORE_ENABLE_EDGE_MESH
  SOURCES edge_mesh.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
    server/web/edge_cache/edge_cache
    server/web/edge_cache/edge_cache_sd
    server/web/edge_cache/edge_fetch
    services/storage/dbm
)
