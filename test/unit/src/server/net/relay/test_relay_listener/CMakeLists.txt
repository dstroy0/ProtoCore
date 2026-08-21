# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_relay_listener: test_relay_listener.c, plus the generated Unity runner.

protocore_add_suite(unit/src/server/net/relay/test_relay_listener
  SOURCES
    test_relay_listener.c
)
