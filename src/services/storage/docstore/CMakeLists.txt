# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/storage/docstore
  GATE PROTOCORE_ENABLE_DOCSTORE
  SOURCES docstore.c
  DEPS
    services/storage/dbm
  PRIVATE_DEPS
    mmgr/protostr
    network_drivers/presentation/codec/json
)
