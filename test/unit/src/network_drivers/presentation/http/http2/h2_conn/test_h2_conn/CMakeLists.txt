# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_h2_conn: test_h2_conn.c, plus the generated Unity runner.

protocore_add_suite(unit/src/network_drivers/presentation/http/http2/h2_conn/test_h2_conn
  SOURCES
    test_h2_conn.c
)
