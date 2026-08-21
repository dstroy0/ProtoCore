# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/x509/x509_verify
  SOURCES x509_verify.c
  DEPS
    crypto/x509/x509_types
  PRIVATE_DEPS
    crypto/asymmetric/ecdsa
    crypto/asymmetric/ed25519
    crypto/asymmetric/rsa
    mmgr/protomem
    mmgr/secure
    shared/der
)
