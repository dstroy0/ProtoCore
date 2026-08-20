# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/instrumentation/scpi
  GATE PROTOCORE_ENABLE_SCPI
  SOURCES scpi.c
  PRIVATE_DEPS
    mmgr/protoframe
    mmgr/protomem
    mmgr/protostr
)
