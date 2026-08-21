# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/storage/dbm
  GATE PROTOCORE_ENABLE_DBM
  SOURCES dbm.c
  DEPS
    services/storage/wal/wal_store
  PRIVATE_DEPS
    mmgr/protomem
)
