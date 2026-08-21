# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_client: test_client.c.
# Writes its own main(), so it takes no generated Unity runner.

protocore_add_suite(unit/src/network_drivers/presentation/ssh/app/test_client
  OWN_MAIN
  SOURCES
    test_client.c
)
