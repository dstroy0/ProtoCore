# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/asymmetric/fe25519
  GATE PROTOCORE_ENABLE_FE25519
  SOURCES fe25519.c
  DEPS
    crypto/ct_eq
  PRIVATE_DEPS
    crypto/crypto_opt
)
