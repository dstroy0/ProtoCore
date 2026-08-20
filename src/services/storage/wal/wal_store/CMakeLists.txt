# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/storage/wal/wal_store
  GATE PROTOCORE_ENABLE_WAL
  SOURCES wal_store.c
  DEPS
    PRIVATE
    mmgr/endian
    mmgr/protomem
    services/storage/wal/wal
  PRIVATE_DEPS
    mmgr/endian
    mmgr/protomem
)
