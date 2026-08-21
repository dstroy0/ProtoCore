# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/codec/deflate/rfc1951
  GATE PROTOCORE_ENABLE_DEFLATE_RFC1951
  SOURCES rfc1951.c
  DEPS
    mmgr/bitio
  PRIVATE_DEPS
    mmgr/protomem
)
