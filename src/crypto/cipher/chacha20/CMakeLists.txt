# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/cipher/chacha20
  GATE PROTOCORE_ENABLE_CHACHA20
  SOURCES chacha20.c
  PRIVATE_DEPS
    crypto/crypto_opt
)
