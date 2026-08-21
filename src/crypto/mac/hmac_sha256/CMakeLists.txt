# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/mac/hmac_sha256
  GATE PROTOCORE_ENABLE_HMAC_SHA256
  OPT 2
  SOURCES hmac_sha256.c
  PRIVATE_DEPS
    crypto/hash/sha256
    mmgr/protomem
)
