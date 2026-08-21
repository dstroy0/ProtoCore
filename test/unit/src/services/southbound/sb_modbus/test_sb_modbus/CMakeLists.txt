# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_sb_modbus: test_sb_modbus.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/southbound/sb_modbus/test_sb_modbus
  SOURCES
    test_sb_modbus.c
)
