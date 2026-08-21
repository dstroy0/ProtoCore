# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_dshot: test_dshot.c, plus the generated Unity runner.

protocore_add_suite(unit/src/server/peripherals/dshot/test_dshot
  SOURCES
    test_dshot.c
)
