# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/hash/sha256
  GATE PROTOCORE_ENABLE_SHA256
  OPT 2
  SOURCES sha256.c
  PRIVATE_DEPS
    mmgr/endian
    mmgr/protomem
)
