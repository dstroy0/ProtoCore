# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/hash/sha1
  GATE PROTOCORE_ENABLE_SHA1
  SOURCES sha1.c
  PRIVATE_DEPS
    crypto/crypto_opt
    mmgr/endian
    mmgr/protomem
)
