# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_edge_cache_proxy: test_edge_cache_proxy.c, plus the generated Unity runner.

protocore_add_suite(unit/src/server/web/edge_cache/test_edge_cache_proxy
  SOURCES
    test_edge_cache_proxy.c
)
