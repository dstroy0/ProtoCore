# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/http3/quic_tls
  GATE PROTOCORE_ENABLE_HTTP3
  SOURCES quic_tls.c
  DEPS
    PRIVATE
    crypto/asymmetric/curve25519
    crypto/ct_eq
    crypto/pqc/mlkem
    mmgr/protomem
    network_drivers/presentation/http/http3/quic_crypto
    network_drivers/presentation/http/http3/quic_tp
    network_drivers/presentation/http/http3/tls13_msg
    network_drivers/tls/key_schedule
  PRIVATE_DEPS
    crypto/asymmetric/curve25519
    crypto/ct_eq
    crypto/pqc/mlkem
    mmgr/protomem
    network_drivers/presentation/http/http3/tls13_msg
)
