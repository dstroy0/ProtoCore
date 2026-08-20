# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/storage/config_io
  GATE PROTOCORE_ENABLE_CONFIG_IO
  SOURCES config_io.c
  PRIVATE_DEPS
    mmgr/protoframe
    mmgr/protomem
    mmgr/protostr
    server/storage/config_store
)
