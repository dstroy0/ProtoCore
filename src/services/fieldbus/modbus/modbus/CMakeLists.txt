# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/fieldbus/modbus/modbus
  GATE PROTOCORE_ENABLE_MODBUS
  SOURCES modbus.c
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protomem
    network_drivers/transport/tcp
    network_drivers/transport/tcp/common
    network_drivers/transport/tcp/protocol
    server/core/proto_handler
    shared/crc
)
