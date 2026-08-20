# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/web/dashboard
  GATE PROTOCORE_ENABLE_DASHBOARD
  SOURCES dashboard.c
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/protoframe
    mmgr/protostr
    mmgr/secure
)

add_subdirectory(dashboard_routes)


