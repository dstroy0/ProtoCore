# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/storage/wal/wal_fs
  HEADER_ONLY
  DEPS
    mmgr/protomem
    server/storage/mnt
    services/storage/wal/wal_store
)
