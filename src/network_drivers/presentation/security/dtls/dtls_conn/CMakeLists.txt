# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/security/dtls/dtls_conn
  GATE PROTOCORE_ENABLE_DTLS
  SOURCES dtls_conn.c
  DEPS
    network_drivers/presentation/security/dtls/dtls_handshake
    network_drivers/presentation/security/dtls/dtls_record
    network_drivers/tls/key_schedule
  PRIVATE_DEPS
    crypto/asymmetric/curve25519
    crypto/asymmetric/ed25519
    crypto/ct_eq
    mmgr/protomem
    mmgr/secure
    network_drivers/presentation/http/http3/tls13_msg
    network_drivers/presentation/http/http3/tls13_rpk
    server/clock
)
