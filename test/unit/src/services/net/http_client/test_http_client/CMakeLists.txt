# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_http_client: test_http_client.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/net/http_client/test_http_client
  SOURCES
    test_http_client.c
)
