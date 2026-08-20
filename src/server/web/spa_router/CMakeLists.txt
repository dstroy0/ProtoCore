# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/web/spa_router
  GATE PROTOCORE_ENABLE_SPA_ROUTER
  SOURCES spa_router.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
)
