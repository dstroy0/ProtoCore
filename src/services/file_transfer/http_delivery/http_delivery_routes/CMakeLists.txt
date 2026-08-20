# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/file_transfer/http_delivery/http_delivery_routes
  GATE PROTOCORE_ENABLE_HTTP_DELIVERY
  SOURCES http_delivery_routes.c
  PRIVATE_DEPS
    network_drivers/application/web_assets
    services/file_transfer/http_delivery
    shared/mime
)
