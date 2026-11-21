# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/physical/wifi_link
  GATE PROTOCORE_ENABLE_IDEMIP
  SOURCES wifi_link.c
  PRIVATE_DEPS
    mmgr
    idemip
)
