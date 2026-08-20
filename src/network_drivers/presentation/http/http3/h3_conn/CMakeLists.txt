# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/http3/h3_conn
  GATE PROTOCORE_ENABLE_HTTP3
  SOURCES h3_conn.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
    network_drivers/presentation/http/http3/h3_frame
    network_drivers/presentation/http/http3/qpack
    network_drivers/presentation/http/http3/quic_conn
    network_drivers/presentation/http/http3/quic_varint
)
