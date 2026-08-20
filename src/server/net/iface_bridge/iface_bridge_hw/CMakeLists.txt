# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/net/iface_bridge/iface_bridge_hw
  GATE PROTOCORE_ENABLE_IFACE_BRIDGE
  SOURCES iface_bridge_hw.c
  PRIVATE_DEPS
    mmgr/secure
    network_drivers/session
    network_drivers/transport/tcp
    network_drivers/transport/tcp/protocol
    server/clock
    server/core/proto_handler
    server/net/iface_bridge/iface_bridge
    server/peripherals/i2c
    server/peripherals/spi
    server/peripherals/uart
    shared/ip
)
