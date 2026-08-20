# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/cipher/aes_block
  GATE PROTOCORE_ENABLE_AES_BLOCK
  SOURCES aes_block.c
  DEPS
    PRIVATE
    crypto/cipher/aes_sbox
    crypto/crypto_opt
    mmgr/protomem
  PRIVATE_DEPS
    crypto/crypto_opt
)
