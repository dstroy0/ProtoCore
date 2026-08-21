# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/southbound/sb_modbus
  GATE PROTOCORE_ENABLE_SOUTHBOUND AND PROTOCORE_ENABLE_MODBUS_MASTER
  SOURCES sb_modbus.c
  DEPS
    services/fieldbus/modbus/modbus
    services/southbound/southbound
  PRIVATE_DEPS
    services/fieldbus/modbus/modbus_master
)
