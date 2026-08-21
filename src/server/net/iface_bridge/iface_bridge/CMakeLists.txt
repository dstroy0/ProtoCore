# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/net/iface_bridge/iface_bridge
  GATE PROTOCORE_ENABLE_IFACE_BRIDGE
  SOURCES iface_bridge.c
  DEPS
    shared/ip
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/secure
)
