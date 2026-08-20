# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/application/file_serving
  GATE PROTOCORE_ENABLE_FILE_SERVING
  SOURCES file_serving.c
  DEPS
    PRIVATE
    mmgr/membuild
    mmgr/plaintext
    mmgr/protostr
    network_drivers/application/http_range
    network_drivers/presentation/http
    network_drivers/presentation/http/http_parser
    network_drivers/presentation/http/route/http_route
    network_drivers/session
    network_drivers/transport/tcp
    network_drivers/transport/tcp/protocol
    server/storage/filesystem
    server/storage/mnt
    shared/mime
    shared/time_compat
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/plaintext
    mmgr/protostr
    network_drivers/application/http_range
    network_drivers/presentation/http
    network_drivers/session
    network_drivers/transport/tcp
    network_drivers/transport/tcp/protocol
    server/storage/filesystem
    shared/mime
    shared/time_compat
)
