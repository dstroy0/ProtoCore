# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# What a suite is given, rather than what it tests. Header-only: a fixture, a key, and a feed a
# suite drives a socket from - no definitions of this tree's own.
#
# rx_feed.h reaches tcp/common for the connection type it hands back, so that is a dependency and
# is stated. The two key headers are self-contained.

protocore_add_module(test/support
  HEADER_ONLY
  DEPS
    network_drivers/transport/tcp/common
)
