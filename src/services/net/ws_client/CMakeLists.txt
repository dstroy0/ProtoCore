# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/net/ws_client
  GATE PROTOCORE_ENABLE_WS_CLIENT
  SOURCES ws_client.c
  PRIVATE_DEPS
    crypto/hash/sha1
    crypto/rng
    mmgr/membuild
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    mmgr/span
    network_drivers/presentation/codec/base64
    network_drivers/transport/tcp/client
    server/clock
)
