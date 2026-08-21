# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/asymmetric/rsa
  GATE PROTOCORE_ENABLE_RSA
  OPT 2
  SOURCES rsa.c
  PRIVATE_DEPS
    crypto/asymmetric/bignum
    crypto/ct_eq
    crypto/hash/sha256
    crypto/hash/sha512
    mmgr/protomem
)
