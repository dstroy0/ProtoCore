# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/http2/h2_conn
  GATE PROTOCORE_ENABLE_HTTP2
  SOURCES h2_conn.c
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/protomem
    mmgr/protostr
    network_drivers/presentation/http/http2/h2_frame
    network_drivers/presentation/http/http2/hpack
)
