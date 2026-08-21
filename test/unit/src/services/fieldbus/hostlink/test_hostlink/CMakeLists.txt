# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_hostlink: test_hostlink.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/fieldbus/hostlink/test_hostlink
  SOURCES
    test_hostlink.c
)
