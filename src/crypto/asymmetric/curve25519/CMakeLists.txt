# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/asymmetric/curve25519
  GATE PROTOCORE_ENABLE_CURVE25519
  SOURCES curve25519.c
  PRIVATE_DEPS
    crypto/asymmetric/fe25519
    crypto/crypto_opt
    mmgr/protomem
)
