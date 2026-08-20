# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/rng
  GATE PROTOCORE_ENABLE_RNG
  SOURCES rng.c
  PRIVATE_DEPS
    crypto/cipher/chacha20
    crypto/crypto_opt
    mmgr/protomem
    mmgr/secure
)
