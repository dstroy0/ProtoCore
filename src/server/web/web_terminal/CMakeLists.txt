# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/web/web_terminal
  GATE PROTOCORE_ENABLE_WEB_TERMINAL
  SOURCES web_terminal.c
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/secure
    network_drivers/application/web_assets
    shared/mime
)
