# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/codec/cbor
  GATE PROTOCORE_ENABLE_CBOR
  SOURCES cbor.c
  DEPS
    PRIVATE
    mmgr/bytes
    mmgr/protomem
    mmgr/protostr
    mmgr/span
    network_drivers/presentation/codec
  PRIVATE_DEPS
    mmgr/bytes
    mmgr/protomem
    mmgr/protostr
)
