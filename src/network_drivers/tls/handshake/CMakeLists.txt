# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/tls/handshake
  GATE PROTOCORE_ENABLE_TLS
  SOURCES handshake.c
  PRIVATE_DEPS
    crypto/asymmetric/curve25519
    crypto/asymmetric/ed25519
    crypto/ct_eq
    crypto/x509/x509
    crypto/x509/x509_types
    crypto/x509/x509_verify
    mmgr/protomem
    mmgr/protostr
    mmgr/rawmemcpy
    mmgr/secure
    network_drivers/presentation/http/http3/tls13_rpk
    network_drivers/tls
)
