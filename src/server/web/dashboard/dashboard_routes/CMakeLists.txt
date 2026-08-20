# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/web/dashboard/dashboard_routes
  GATE PROTOCORE_ENABLE_DASHBOARD
  SOURCES dashboard_routes.c
  PRIVATE_DEPS
    network_drivers/application/web_assets
    network_drivers/presentation/http/websocket
    server/web/dashboard
    shared/mime
)
