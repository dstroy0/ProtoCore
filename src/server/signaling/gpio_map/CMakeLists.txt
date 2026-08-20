# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/signaling/gpio_map
  GATE PROTOCORE_ENABLE_GPIO_MAP
  SOURCES gpio_map.c
  PRIVATE_DEPS
    mmgr/protoframe
    mmgr/protomem
    mmgr/protostr
    server/clock
)
