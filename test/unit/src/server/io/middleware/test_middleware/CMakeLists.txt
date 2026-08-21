# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_middleware: test_middleware.c, plus the generated Unity runner.

protocore_add_suite(unit/src/server/io/middleware/test_middleware
  SOURCES
    test_middleware.c
)
