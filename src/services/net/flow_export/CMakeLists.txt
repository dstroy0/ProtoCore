# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/net/flow_export
  GATE PROTOCORE_ENABLE_FLOW_EXPORT
  SOURCES flow_export.c
  PRIVATE_DEPS
    mmgr/endian
    mmgr/plaintext
    mmgr/protomem
)
