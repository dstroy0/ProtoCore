# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation
  GATE PROTOCORE_ENABLE_WEBSOCKET
  SOURCES presentation.c
  DEPS
    PRIVATE
    mmgr/plaintext
    mmgr/protostr
    network_drivers/presentation/http/http2/h2_server
    network_drivers/presentation/http/http_parser
    network_drivers/presentation/http/sse
    network_drivers/presentation/http/websocket
    network_drivers/session
    network_drivers/session/sse
    network_drivers/session/ws
    network_drivers/tls
    network_drivers/transport/tcp/evt
    network_drivers/transport/tcp/protocol
    server/core/proto_handler
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protostr
    network_drivers/presentation/http/http2/h2_server
    network_drivers/presentation/http/sse
    network_drivers/presentation/http/websocket
    network_drivers/session/sse
    network_drivers/session/ws
    network_drivers/tls
    network_drivers/transport/tcp/protocol
    server/core/proto_handler
)

add_subdirectory(codec)
add_subdirectory(http)
add_subdirectory(security)
add_subdirectory(ssh)
add_subdirectory(telnet)


