# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_sigfox: test_sigfox.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/radio/sigfox/test_sigfox
  SOURCES
    test_sigfox.c
)
