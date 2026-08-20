# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/cipher/aes256ctr
  GATE PROTOCORE_ENABLE_AES256CTR
  SOURCES aes256ctr.c
  PRIVATE_DEPS
    crypto/cipher/aes_block
    crypto/crypto_opt
    mmgr/protomem
)
