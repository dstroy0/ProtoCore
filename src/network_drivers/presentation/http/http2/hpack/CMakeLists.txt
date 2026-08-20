# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/http2/hpack
  GATE PROTOCORE_ENABLE_HTTP2
  SOURCES hpack.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
    network_drivers/presentation/codec/hpack_prim
)
