# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/http3/h3_frame
  GATE PROTOCORE_ENABLE_HTTP3
  SOURCES h3_frame.c
  PRIVATE_DEPS
    mmgr/protomem
    network_drivers/presentation/http/http3/quic_varint
)
