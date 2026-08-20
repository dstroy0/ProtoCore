# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/websocket
  GATE PROTOCORE_ENABLE_WEBSOCKET
  SOURCES websocket.c
  DEPS
    PRIVATE
    mmgr/plaintext
    mmgr/protomem
    mmgr/secure
    mmgr/span
    network_drivers/presentation/codec/deflate/deflate
    network_drivers/presentation/codec/inflate
    network_drivers/transport/tcp/protocol
    shared/utf8
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protomem
    mmgr/secure
    mmgr/span
    network_drivers/transport/tcp/protocol
    shared/utf8
)
