// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the edge-cache server glue (server/web/edge_cache/edge_cache_proxy.h).
//
// This translation unit was in no build at all until 2026-08-17, so nothing here had ever been
// compiled, let alone run. These cases cover the surface that is decidable without an origin
// server on the other end: the route map's bounds and refusals, the counters a reset returns to,
// and purge on an empty store. The fetch path needs a real origin and is not driven here.
//
// The map table is PROTOCORE_EDGE_MAP_MAX entries (4), and a prefix is bounded by the map's own
// prefix field, so test_the_map_table_is_bounded is the load-bearing case: it fills the table and
// then asserts the next map is refused rather than overwriting a live route.

#include "server/web/edge_cache/edge_cache/edge_cache.h"
#include "server/web/edge_cache/edge_cache_proxy/edge_cache_proxy.h"

#include <unity.h>

void setUp(void)
{
    EdgeProxy.reset(protocore_edge_cache_proxy_span());
}
void tearDown(void)
{
    EdgeProxy.reset(protocore_edge_cache_proxy_span());
}

// A reset returns every counter to zero, which is what makes the counters below assertable at all.
void test_reset_zeroes_every_counter(void)
{
    EdgeCacheStats st;
    EdgeProxy.stats(protocore_edge_cache_proxy_span(), &st);
    TEST_ASSERT_EQUAL_UINT32(0, st.hits);
    TEST_ASSERT_EQUAL_UINT32(0, st.misses);
    TEST_ASSERT_EQUAL_UINT32(0, st.revalidations_304);
    TEST_ASSERT_EQUAL_UINT32(0, st.replaces_200);
    TEST_ASSERT_EQUAL_UINT32(0, st.stores);
    TEST_ASSERT_EQUAL_UINT32(0, st.evictions);
    TEST_ASSERT_EQUAL_UINT32(0, st.purges);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_stored);
}

// A map needs both a prefix and an origin; neither is optional and a null is refused rather than
// stored as an empty route that would match every path.
void test_a_map_needs_both_a_prefix_and_an_origin(void)
{
    proto_bool edge_proxy_ok = EdgeProxy.map(protocore_edge_cache_proxy_span(), NULL, "http://origin.local");
    TEST_ASSERT_FALSE(edge_proxy_ok);
    proto_bool edge_proxy_ok2 = EdgeProxy.map(protocore_edge_cache_proxy_span(), "/cdn/", NULL);
    TEST_ASSERT_FALSE(edge_proxy_ok2);
    proto_bool edge_proxy_ok3 = EdgeProxy.map(protocore_edge_cache_proxy_span(), NULL, NULL);
    TEST_ASSERT_FALSE(edge_proxy_ok3);
}

// A plain http origin maps. This is the shape the header documents:
// "/cdn/" -> "http://origin.local".
void test_a_plain_http_origin_maps(void)
{
    proto_bool edge_proxy_ok = EdgeProxy.map(protocore_edge_cache_proxy_span(), "/cdn/", "http://origin.local");
    TEST_ASSERT_TRUE(edge_proxy_ok);
}

// PROTOCORE_EDGE_MAP_MAX route maps fit and the next one is refused, so a full table cannot
// silently drop a route the application believes it registered.
void test_the_map_table_is_bounded(void)
{
    static const char *const prefix[PROTOCORE_EDGE_MAP_MAX] = {"/a/", "/b/", "/c/", "/d/"};
    for (uint32_t i = 0; i < PROTOCORE_EDGE_MAP_MAX; i++)
    {
        proto_bool edge_proxy_ok = EdgeProxy.map(protocore_edge_cache_proxy_span(), prefix[i], "http://origin.local");
        TEST_ASSERT_TRUE(edge_proxy_ok);
    }
    proto_bool edge_proxy_ok2 = EdgeProxy.map(protocore_edge_cache_proxy_span(), "/e/", "http://origin.local");
    TEST_ASSERT_FALSE(edge_proxy_ok2);

    // and a reset frees the table again
    EdgeProxy.reset(protocore_edge_cache_proxy_span());
    proto_bool edge_proxy_ok3 = EdgeProxy.map(protocore_edge_cache_proxy_span(), "/e/", "http://origin.local");
    TEST_ASSERT_TRUE(edge_proxy_ok3);
}

// A prefix longer than the map's own field is refused rather than stored truncated: a truncated
// prefix matches more paths than the caller asked for.
void test_an_overlong_prefix_is_refused(void)
{
    char huge[256];
    for (uint32_t i = 0; i < sizeof(huge) - 1u; i++)
    {
        huge[i] = 'x';
    }
    huge[sizeof(huge) - 1u] = '\0';
    proto_bool edge_proxy_ok = EdgeProxy.map(protocore_edge_cache_proxy_span(), huge, "http://origin.local");
    TEST_ASSERT_FALSE(edge_proxy_ok);
}

// A malformed origin URL is refused, so a route can never point at something the fetch path cannot
// dial.
void test_a_malformed_origin_is_refused(void)
{
    proto_bool edge_proxy_ok = EdgeProxy.map(protocore_edge_cache_proxy_span(), "/cdn/", "not-a-url");
    TEST_ASSERT_FALSE(edge_proxy_ok);
    proto_bool edge_proxy_ok2 = EdgeProxy.map(protocore_edge_cache_proxy_span(), "/cdn/", "");
    TEST_ASSERT_FALSE(edge_proxy_ok2);
}

// Purging an empty store reports nothing purged rather than claiming a hit, and the counter agrees.
void test_purging_an_empty_store_purges_nothing(void)
{
    proto_bool edge_proxy_ok = EdgeProxy.purge(protocore_edge_cache_proxy_span(), "/nothing/here");
    TEST_ASSERT_FALSE(edge_proxy_ok);
    uint32_t edge_proxy_n = EdgeProxy.purge_prefix(protocore_edge_cache_proxy_span(), "/nothing/");
    TEST_ASSERT_EQUAL_UINT32(0, edge_proxy_n);
    EdgeCacheStats st;
    EdgeProxy.stats(protocore_edge_cache_proxy_span(), &st);
    TEST_ASSERT_EQUAL_UINT32(0, st.purges);
}

// A null key is refused rather than dereferenced.
void test_purge_refuses_a_null_key(void)
{
    proto_bool edge_proxy_ok = EdgeProxy.purge(protocore_edge_cache_proxy_span(), NULL);
    TEST_ASSERT_FALSE(edge_proxy_ok);
    uint32_t edge_proxy_n = EdgeProxy.purge_prefix(protocore_edge_cache_proxy_span(), NULL);
    TEST_ASSERT_EQUAL_UINT32(0, edge_proxy_n);
}
