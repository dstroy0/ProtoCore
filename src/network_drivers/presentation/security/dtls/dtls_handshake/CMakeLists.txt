# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/security/dtls/dtls_handshake
  GATE PROTOCORE_ENABLE_DTLS
  SOURCES dtls_handshake.c
  PRIVATE_DEPS
    crypto/ct_eq
    crypto/mac/hmac_sha256
    mmgr/protomem
)
