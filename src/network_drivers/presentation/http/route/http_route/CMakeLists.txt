# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/route/http_route
  GATE PROTOCORE_ENABLE_HTTP_ROUTE
  SOURCES http_route.c
  DEPS
    network_drivers/presentation/http
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/secure
)
