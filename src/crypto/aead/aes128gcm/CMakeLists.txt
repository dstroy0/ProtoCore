# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/aead/aes128gcm
  GATE PROTOCORE_ENABLE_AES128GCM
  OPT 2
  SOURCES aes128gcm.c
  PRIVATE_DEPS
    crypto/cipher/aes_block
    crypto/ct_eq
    crypto/mac/ghash
    mmgr/protomem
    mmgr/rawmemcpy
    mmgr/secure
)
