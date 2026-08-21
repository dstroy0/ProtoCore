# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_southbound: test_southbound.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/southbound/southbound/test_southbound
  SOURCES
    test_southbound.c
)
