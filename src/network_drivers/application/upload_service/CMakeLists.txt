# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/application/upload_service
  GATE PROTOCORE_ENABLE_UPLOAD
  SOURCES upload_service.c
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protoframe
    mmgr/protostr
    network_drivers/presentation/http/http_parser
    server/storage/mnt
    shared/mime
)
