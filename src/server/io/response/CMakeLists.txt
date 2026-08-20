# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/io/response
  GATE PROTOCORE_ENABLE_STATS
  SOURCES response.c
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/protomem
    mmgr/protostr
    network_drivers/application/web_assets
    network_drivers/presentation/http
    network_drivers/session
    network_drivers/transport/tcp
    network_drivers/transport/tcp/common
    network_drivers/transport/tcp/protocol
    server/clock
    server/io/http_clock
    shared/hex
    shared/mime
)
