# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_tcp_evt: test_tcp_evt.c, plus the generated Unity runner.

protocore_add_suite(unit/src/network_drivers/transport/tcp/test_tcp_evt
  SOURCES
    test_tcp_evt.c
)
