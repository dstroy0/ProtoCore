# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/aead/aesccm
  GATE PROTOCORE_ENABLE_AESCCM
  SOURCES aesccm.c
  PRIVATE_DEPS
    crypto/cipher/aes_block
    crypto/crypto_opt
    crypto/ct_eq
    mmgr/protomem
)
