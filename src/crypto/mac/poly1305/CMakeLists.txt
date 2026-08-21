# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/mac/poly1305
  GATE PROTOCORE_ENABLE_POLY1305
  OPT 2
  SOURCES poly1305.c
  PRIVATE_DEPS
    mmgr/protomem
)
