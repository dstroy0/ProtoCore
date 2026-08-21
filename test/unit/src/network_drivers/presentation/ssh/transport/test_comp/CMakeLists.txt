# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_comp: test_comp.c.
# Writes its own main(), so it takes no generated Unity runner.

protocore_add_suite(unit/src/network_drivers/presentation/ssh/transport/test_comp
  OWN_MAIN
  SOURCES
    test_comp.c
)
