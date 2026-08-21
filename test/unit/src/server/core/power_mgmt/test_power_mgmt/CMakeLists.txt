# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_power_mgmt: test_power_mgmt.c, plus the generated Unity runner.

protocore_add_suite(unit/src/server/core/power_mgmt/test_power_mgmt
  SOURCES
    test_power_mgmt.c
)
