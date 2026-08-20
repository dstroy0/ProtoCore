# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/session/ws
  GATE PROTOCORE_ENABLE_WEBSOCKET
  SOURCES ws.c
  PRIVATE_DEPS
    network_drivers/presentation/http/websocket
)
