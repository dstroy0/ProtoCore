# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http
  GATE PROTOCORE_ENABLE_AUTH_LOCKOUT
  SOURCES http.c
  DEPS
    network_drivers/presentation/http/http_parser
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/plaintext
    mmgr/protostr
    mmgr/rawmemcpy
    network_drivers/presentation/http/auth
    network_drivers/presentation/http/route/http_route
    network_drivers/session
    network_drivers/transport/tcp/common
    network_drivers/transport/tcp/protocol
    server/clock
    server/io/webdav_handler
    server/security/auth_lockout
    server/security/csrf
    server/security/forwarded_trust
)

add_subdirectory(auth)
add_subdirectory(http2)
add_subdirectory(http3)
add_subdirectory(http_parser)
add_subdirectory(httpcache)
add_subdirectory(route)
add_subdirectory(sse)
add_subdirectory(websocket)


