# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_oauth2_exchange: test_oauth2_exchange.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/security/oauth2/test_oauth2_exchange
  SOURCES
    test_oauth2_exchange.c
)
