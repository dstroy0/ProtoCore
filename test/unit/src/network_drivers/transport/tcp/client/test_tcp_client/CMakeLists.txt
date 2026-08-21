# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_tcp_client: test_tcp_client.c, plus the generated Unity runner.

protocore_add_suite(unit/src/network_drivers/transport/tcp/client/test_tcp_client
  SOURCES
    test_tcp_client.c
)
