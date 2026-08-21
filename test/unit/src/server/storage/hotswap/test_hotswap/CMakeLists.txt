# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_hotswap: test_hotswap.c, plus the generated Unity runner.

protocore_add_suite(unit/src/server/storage/hotswap/test_hotswap
  SOURCES
    test_hotswap.c
)
