# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/security/ikev2/ikev2
  GATE PROTOCORE_ENABLE_IKEV2
  SOURCES ikev2.c
  PRIVATE_DEPS
    crypto/aead/aesgcm
    crypto/asymmetric/curve25519
    crypto/asymmetric/ecdsa
    crypto/asymmetric/rsa
    crypto/hash/sha256
    crypto/mac/hmac_sha256
    mmgr/protomem
    mmgr/secure
)
