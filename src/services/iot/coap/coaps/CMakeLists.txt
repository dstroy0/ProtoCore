# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/iot/coap/coaps
  GATE PROTOCORE_ENABLE_DTLS AND PROTOCORE_ENABLE_COAP
  SOURCES coaps.c
  DEPS
    network_drivers/presentation/security/dtls/dtls_conn
  PRIVATE_DEPS
    services/iot/coap/coap
)
