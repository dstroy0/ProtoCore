# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_modbus: test_modbus.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/fieldbus/modbus/test_modbus
  SOURCES
    test_modbus.c
)
