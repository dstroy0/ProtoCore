# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/mac/aes_cmac
  GATE PROTOCORE_ENABLE_AES_CMAC
  OPT 2
  SOURCES aes_cmac.c
  PRIVATE_DEPS
    crypto/cipher/aes_block
    mmgr/protomem
)
