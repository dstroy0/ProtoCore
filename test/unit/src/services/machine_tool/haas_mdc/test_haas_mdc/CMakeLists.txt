# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_haas_mdc: test_haas_mdc.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/machine_tool/haas_mdc/test_haas_mdc
  SOURCES
    test_haas_mdc.c
)
