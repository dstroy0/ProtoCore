# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/codec/multipart
  GATE PROTOCORE_ENABLE_MULTIPART
  SOURCES multipart.c
  DEPS
    PRIVATE
    mmgr/protomem
    mmgr/protostr
    network_drivers/presentation/http/http_parser
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
)
