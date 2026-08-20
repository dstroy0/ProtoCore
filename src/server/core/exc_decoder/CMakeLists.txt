# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/core/exc_decoder
  GATE PROTOCORE_ENABLE_EXC_DECODER
  SOURCES exc_decoder.c
  DEPS
    PRIVATE
    mmgr/membuild
    mmgr/protostr
    server/storage/mnt
    shared/hex
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/protostr
    shared/hex
)
