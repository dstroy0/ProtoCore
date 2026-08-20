# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/kdf/hkdf
  GATE PROTOCORE_ENABLE_HKDF
  SOURCES hkdf.c
  PRIVATE_DEPS
    crypto/crypto_opt
    crypto/mac/hmac_sha256
    mmgr/protomem
    mmgr/protostr
)
