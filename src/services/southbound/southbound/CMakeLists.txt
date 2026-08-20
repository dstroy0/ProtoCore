# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/southbound/southbound
  GATE PROTOCORE_ENABLE_SOUTHBOUND
  SOURCES southbound.c
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protostr
)
