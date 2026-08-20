# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/iot/coap/coaps
  SOURCES coaps.c
  DEPS
    PRIVATE
    network_drivers/presentation/security/dtls/dtls_conn
    services/iot/coap/coap
  PRIVATE_DEPS
    services/iot/coap/coap
)
