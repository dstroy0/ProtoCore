# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/http3/tls13_rpk
  GATE PROTOCORE_ENABLE_TLS_RPK
  SOURCES tls13_rpk.c
  PRIVATE_DEPS
    mmgr/protomem
    network_drivers/presentation/http/http3/tls13_msg
)
