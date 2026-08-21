# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_net_egress: test_net_egress.c, plus the generated Unity runner.

protocore_add_suite(unit/src/network_drivers/physical/test_net_egress
  SOURCES
    test_net_egress.c
)
