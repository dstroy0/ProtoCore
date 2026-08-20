# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/codec/hpack_prim
  GATE PROTOCORE_ENABLE_HPACK_PRIM
  SOURCES hpack_prim.c
  PRIVATE_DEPS
    mmgr/protomem
)
