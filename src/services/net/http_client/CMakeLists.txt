# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/net/http_client
  GATE PROTOCORE_ENABLE_HTTP_CLIENT
  SOURCES http_client.c
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/transport/tcp/client
    server/clock
    shared/mime
)
