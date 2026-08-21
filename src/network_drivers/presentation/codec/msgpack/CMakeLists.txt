# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/codec/msgpack
  GATE PROTOCORE_ENABLE_MSGPACK
  SOURCES msgpack.c
  DEPS
    mmgr/span
    network_drivers/presentation/codec
  PRIVATE_DEPS
    mmgr/bytes
    mmgr/protomem
    mmgr/protostr
)
