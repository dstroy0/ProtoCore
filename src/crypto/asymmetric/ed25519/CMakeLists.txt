# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/asymmetric/ed25519
  GATE PROTOCORE_ENABLE_ED25519
  SOURCES ed25519.c
  PRIVATE_DEPS
    crypto/asymmetric/curve25519
    crypto/asymmetric/ed25519_comb_table
    crypto/asymmetric/fe25519
    crypto/crypto_opt
    crypto/ct_eq
    crypto/hash/sha512
)
