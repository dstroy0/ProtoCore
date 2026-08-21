# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_stomp: test_stomp.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/iot/stomp/test_stomp
  SOURCES
    test_stomp.c
)
