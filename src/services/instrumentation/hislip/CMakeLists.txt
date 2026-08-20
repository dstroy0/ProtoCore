# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/instrumentation/hislip
  GATE PROTOCORE_ENABLE_HISLIP
  SOURCES hislip.c
  PRIVATE_DEPS
    mmgr/endian
    mmgr/protomem
    mmgr/protostr
)
