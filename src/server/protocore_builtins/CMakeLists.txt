# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/protocore_builtins
  GATE PROTOCORE_ENABLE_TELNET
  SOURCES protocore_builtins.c
  PRIVATE_DEPS
    network_drivers/presentation
    network_drivers/presentation/ssh/server
    network_drivers/presentation/telnet
    network_drivers/session
    server/core/proto_handler
    services/fieldbus/modbus/modbus
    services/opcua
)
