# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/io/webdav_handler
  GATE PROTOCORE_ENABLE_WEBDAV
  SOURCES webdav_handler.c
  PRIVATE_DEPS
    crypto/rng
    mmgr/membuild
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/application/webdav
    network_drivers/presentation/http
    network_drivers/presentation/http/http_parser
    network_drivers/presentation/http/route/http_route
    network_drivers/transport/tcp
    server/clock
    shared/mime
)
