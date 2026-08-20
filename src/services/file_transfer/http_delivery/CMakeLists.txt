# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/file_transfer/http_delivery
  GATE PROTOCORE_ENABLE_HTTP_DELIVERY
  SOURCES http_delivery.c
  PRIVATE_DEPS
    mmgr/membuild
)

add_subdirectory(http_delivery_routes)


