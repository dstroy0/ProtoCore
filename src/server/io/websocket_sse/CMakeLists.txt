# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/io/websocket_sse
  GATE PROTOCORE_ENABLE_WEBSOCKET
  SOURCES websocket_sse.c
  PRIVATE_DEPS
    crypto/hash/sha1
    mmgr/membuild
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    mmgr/span
    network_drivers/presentation/codec/base64
    network_drivers/presentation/http/http_parser
    network_drivers/presentation/http/sse
    network_drivers/presentation/http/websocket
    network_drivers/session/sse
    network_drivers/session/ws
    network_drivers/transport/tcp
    network_drivers/transport/tcp/protocol
)
