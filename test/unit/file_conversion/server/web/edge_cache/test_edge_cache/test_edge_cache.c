// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/application/http_range.h"
#include "network_drivers/presentation/http/httpcache/httpcache.h"
#include "server/web/edge_cache/edge_cache.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

#define NOV6_1994 ((int64_t)784111777)

static int64_t date_of(const char *s)
{
    return edge_parse_http_date(s, strlen(s));
}

void test_rfc9110_three_spellings_of_one_instant(void)
{
    TEST_ASSERT_EQUAL_INT64(NOV6_1994, date_of("Sun, 06 Nov 1994 08:49:37 GMT"));
    TEST_ASSERT_EQUAL_INT64(NOV6_1994, date_of("Sunday, 06-Nov-94 08:49:37 GMT"));
    TEST_ASSERT_EQUAL_INT64(NOV6_1994, date_of("Sun Nov  6 08:49:37 1994"));
}

void test_http_date_anchor_instants(void)
{
    TEST_ASSERT_EQUAL_INT64((int64_t)0, date_of("Thu, 01 Jan 1970 00:00:00 GMT"));
    TEST_ASSERT_EQUAL_INT64((int64_t)2147483647, date_of("Tue, 19 Jan 2038 03:14:07 GMT"));

    TEST_ASSERT_EQUAL_INT64((int64_t)951782400, date_of("Tue, 29 Feb 2000 00:00:00 GMT"));
}

void test_http_date_refuses_malformed_text(void)
{
    static const char *const BAD[] = {
        "",
        "not a date",
        "Sun, 06 Xxx 1994 08:49:37 GMT",
        "Sun, 06 Nov 1994 08:49 GMT",
        "Sun, 32 Nov 1994 08:49:37 GMT",
        "Sun, 06 Nov 1994 24:49:37 GMT",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT64_MESSAGE((int64_t)-1, date_of(BAD[i]), BAD[i]);
    }
}

void test_rfc9110_published_range_examples(void)
{
    size_t s = 0;
    size_t e = 0;

    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=0-499", 10000, &s, &e));
    TEST_ASSERT_EQUAL_UINT(0u, s);
    TEST_ASSERT_EQUAL_UINT(499u, e);

    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=500-999", 10000, &s, &e));
    TEST_ASSERT_EQUAL_UINT(500u, s);
    TEST_ASSERT_EQUAL_UINT(999u, e);

    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=-500", 10000, &s, &e));
    TEST_ASSERT_EQUAL_UINT(9500u, s);
    TEST_ASSERT_EQUAL_UINT(9999u, e);
    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=9500-", 10000, &s, &e));
    TEST_ASSERT_EQUAL_UINT(9500u, s);
    TEST_ASSERT_EQUAL_UINT(9999u, e);
}

void test_range_last_pos_and_suffix_clamp_to_the_representation(void)
{
    size_t s = 0;
    size_t e = 0;
    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=0-99999", 10000, &s, &e));
    TEST_ASSERT_EQUAL_UINT(0u, s);
    TEST_ASSERT_EQUAL_UINT(9999u, e);

    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=-99999", 10000, &s, &e));
    TEST_ASSERT_EQUAL_UINT(0u, s);
    TEST_ASSERT_EQUAL_UINT(9999u, e);

    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=0-0", 10000, &s, &e));
    TEST_ASSERT_EQUAL_UINT(0u, s);
    TEST_ASSERT_EQUAL_UINT(0u, e);
    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=-1", 10000, &s, &e));
    TEST_ASSERT_EQUAL_UINT(9999u, s);
    TEST_ASSERT_EQUAL_UINT(9999u, e);
}

void test_rfc9110_unsatisfiable_ranges(void)
{
    size_t s = 0;
    size_t e = 0;
    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=10000-", 10000, &s, &e));
    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=10500-11000", 10000, &s, &e));
    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=-0", 10000, &s, &e));
    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=500-499", 10000, &s, &e));

    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=-1", 0, &s, &e));
    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=0-0", 0, &s, &e));
}

void test_unusable_range_headers_fall_back_to_a_full_response(void)
{
    size_t s = 0;
    size_t e = 0;
    static const char *const IGNORED[] = {
        "bytes=0-0,-1",
        "bytes= 0-999, 4500-5499, -1000",
        "items=0-499",
        "bytes=abc",       "bytes=",     "bytes=0-499x", "0-499",
    };
    for (size_t i = 0; i < sizeof(IGNORED) / sizeof(IGNORED[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, http_parse_byte_range(IGNORED[i], 10000, &s, &e), IGNORED[i]);
    }
    TEST_ASSERT_EQUAL_INT(0, http_parse_byte_range(NULL, 10000, &s, &e));

    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("BYTES=0-9", 10000, &s, &e));
    TEST_ASSERT_EQUAL_UINT(9u, e);
}

void test_range_overflow_saturates_past_eof(void)
{
    size_t s = 0;
    size_t e = 0;
    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=99999999999999999999999-", 10000, &s, &e));

    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=10-99999999999999999999999", 10000, &s, &e));
    TEST_ASSERT_EQUAL_UINT(10u, s);
    TEST_ASSERT_EQUAL_UINT(9999u, e);
}

static const char *const HEAD = "HTTP/1.1 200 OK\r\n"
                                "ETag: \"abc123\"\r\n"
                                "Cache-Control:   max-age=60  \r\n"
                                "Last-Modified: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                                "Content-Type: text/html\r\n"
                                "ETag: \"second\"\r\n"
                                "\r\n";

void test_field_lookup_is_case_insensitive_and_ows_trimmed(void)
{
    char out[64];
    size_t n = strlen(HEAD);
    TEST_ASSERT_TRUE(edge_header_value(HEAD, n, "ETag", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("\"abc123\"", out);
    TEST_ASSERT_TRUE(edge_header_value(HEAD, n, "cache-CONTROL", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("max-age=60", out);
    TEST_ASSERT_TRUE(edge_header_value(HEAD, n, "Content-Type", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("text/html", out);

    TEST_ASSERT_FALSE(edge_header_value(HEAD, n, "HTTP/1.1 200 OK", out, sizeof(out)));
}

void test_field_lookup_refuses_rather_than_truncates(void)
{
    char out[64];
    size_t n = strlen(HEAD);
    TEST_ASSERT_FALSE(edge_header_value(HEAD, n, "X-Missing", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);

    char tiny[4];
    TEST_ASSERT_FALSE(edge_header_value(HEAD, n, "Content-Type", tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("", tiny);
}

static void parse_cc(const char *s, protocore_cache_control *cc)
{
    cache_control_parse(s, strlen(s), cc);
}

void test_rfc9111_freshness_lifetime_precedence(void)
{
    protocore_cache_control cc;
    parse_cc("public, max-age=50, s-maxage=100", &cc);
    TEST_ASSERT_EQUAL_INT32(100, edge_freshness_lifetime(&cc, PROTO_TRUE, -1, -1));
    TEST_ASSERT_EQUAL_INT32(50, edge_freshness_lifetime(&cc, PROTO_FALSE, -1, -1));

    protocore_cache_control none;
    cache_control_init(&none);
    TEST_ASSERT_EQUAL_INT32(600, edge_freshness_lifetime(&none, PROTO_TRUE, NOV6_1994, NOV6_1994 + 600));
    TEST_ASSERT_EQUAL_INT32(-1, edge_freshness_lifetime(&none, PROTO_TRUE, -1, -1));

    protocore_cache_control ma;
    parse_cc("max-age=30", &ma);
    TEST_ASSERT_EQUAL_INT32(30, edge_freshness_lifetime(&ma, PROTO_TRUE, NOV6_1994, NOV6_1994 + 600));

    TEST_ASSERT_EQUAL_INT32(-100, edge_freshness_lifetime(&none, PROTO_TRUE, NOV6_1994, NOV6_1994 - 100));
}

void test_rfc9111_heuristic_is_a_tenth_of_the_last_modified_interval(void)
{
    TEST_ASSERT_EQUAL_INT32(8640, edge_heuristic_lifetime(NOV6_1994, NOV6_1994 - 86400));
    TEST_ASSERT_EQUAL_INT32(0, edge_heuristic_lifetime(NOV6_1994, NOV6_1994 - 9));

    TEST_ASSERT_EQUAL_INT32(-1, edge_heuristic_lifetime(-1, NOV6_1994));
    TEST_ASSERT_EQUAL_INT32(-1, edge_heuristic_lifetime(NOV6_1994, -1));
    TEST_ASSERT_EQUAL_INT32(-1, edge_heuristic_lifetime(NOV6_1994, NOV6_1994));
    TEST_ASSERT_EQUAL_INT32(-1, edge_heuristic_lifetime(NOV6_1994, NOV6_1994 + 10));
}

void test_rfc9111_corrected_initial_age(void)
{

    TEST_ASSERT_EQUAL_INT32(40, edge_initial_age(0, NOV6_1994, NOV6_1994 + 40));

    TEST_ASSERT_EQUAL_INT32(500, edge_initial_age(500, NOV6_1994, NOV6_1994 + 40));

    TEST_ASSERT_EQUAL_INT32(0, edge_initial_age(0, NOV6_1994, NOV6_1994 - 40));

    TEST_ASSERT_EQUAL_INT32(77, edge_initial_age(77, NOV6_1994, -1));
    TEST_ASSERT_EQUAL_INT32(0, edge_initial_age(-1, -1, -1));
}

void test_rfc9111_current_age_and_the_fresh_predicate(void)
{
    TEST_ASSERT_EQUAL_INT32(10, edge_current_age(0, 1000u, 11000u));
    TEST_ASSERT_EQUAL_INT32(35, edge_current_age(30, 1000u, 6000u));

    TEST_ASSERT_EQUAL_INT32(10, edge_current_age(0, 0xFFFFF000u, 0xFFFFF000u + 10000u));

    TEST_ASSERT_TRUE(edge_is_fresh_at(60, 59));
    TEST_ASSERT_FALSE(edge_is_fresh_at(60, 60));
    TEST_ASSERT_FALSE(edge_is_fresh_at(60, 61));
    TEST_ASSERT_FALSE(edge_is_fresh_at(-1, 0));
}

void test_cache_key_is_canonical(void)
{
    char a[PROTOCORE_EDGE_KEY_MAX];
    char b[PROTOCORE_EDGE_KEY_MAX];

    size_t n = edge_key_canon("GET", "Example.COM", "/a/B", "q=1", PROTO_TRUE, a, sizeof(a));
    TEST_ASSERT_EQUAL_STRING("GET\nexample.com\n/a/B\nq=1", a);
    TEST_ASSERT_EQUAL_UINT(strlen("GET\nexample.com\n/a/B\nq=1"), n);

    TEST_ASSERT_TRUE(edge_key_canon("GET", "EXAMPLE.com", "/a/B", "q=1", PROTO_TRUE, b, sizeof(b)) > 0);
    TEST_ASSERT_EQUAL_STRING(a, b);
    TEST_ASSERT_TRUE(edge_key_canon("GET", "example.com", "/a/b", "q=1", PROTO_TRUE, b, sizeof(b)) > 0);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a, b));

    TEST_ASSERT_TRUE(edge_key_canon("GET", "example.com", "/a/B", "q=1", PROTO_FALSE, a, sizeof(a)) > 0);
    TEST_ASSERT_TRUE(edge_key_canon("GET", "example.com", "/a/B", "q=2", PROTO_FALSE, b, sizeof(b)) > 0);
    TEST_ASSERT_EQUAL_STRING(a, b);
    TEST_ASSERT_EQUAL_STRING("GET\nexample.com\n/a/B", a);

    char small[8];
    TEST_ASSERT_EQUAL_UINT(0u, edge_key_canon("GET", "example.com", "/a/B", NULL, PROTO_FALSE, small, sizeof(small)));
}

void test_key_digest_matches_the_fips_180_4_vector(void)
{
    static const uint8_t WANT[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                     0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                     0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    static uint8_t work[PROTOCORE_SHA256_BORROW + 1];
    uint8_t got[32];
    edge_key_digest(work, "abc", 3, got);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, got, 32);
}

static const char *lookup_accept_gzip(void *ctx, const char *name)
{
    (void)ctx;
    return strcmp(name, "accept-encoding") == 0 ? "gzip" : NULL;
}

static const char *lookup_accept_br(void *ctx, const char *name)
{
    (void)ctx;
    return strcmp(name, "accept-encoding") == 0 ? "br" : NULL;
}

static const char *lookup_nothing(void *ctx, const char *name)
{
    (void)ctx;
    (void)name;
    return NULL;
}

static const char *lookup_empty(void *ctx, const char *name)
{
    (void)ctx;
    (void)name;
    return "";
}

void test_rfc9111_vary_secondary_key(void)
{
    char gzip[PROTOCORE_EDGE_VARY_MAX];
    char br[PROTOCORE_EDGE_VARY_MAX];
    char absent[PROTOCORE_EDGE_VARY_MAX];
    char present_empty[PROTOCORE_EDGE_VARY_MAX];

    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding", lookup_accept_gzip, NULL, gzip, sizeof(gzip)));
    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding", lookup_accept_br, NULL, br, sizeof(br)));
    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding", lookup_nothing, NULL, absent, sizeof(absent)));
    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding", lookup_empty, NULL, present_empty, sizeof(present_empty)));

    TEST_ASSERT_NOT_EQUAL(0, strcmp(gzip, br));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(gzip, absent));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(absent, present_empty));

    char spelled[PROTOCORE_EDGE_VARY_MAX];
    TEST_ASSERT_TRUE(edge_vary_serialize("accept-encoding", lookup_accept_gzip, NULL, spelled, sizeof(spelled)));
    TEST_ASSERT_EQUAL_STRING(gzip, spelled);

    char nothing[PROTOCORE_EDGE_VARY_MAX];
    TEST_ASSERT_TRUE(edge_vary_serialize(NULL, lookup_accept_gzip, NULL, nothing, sizeof(nothing)));
    TEST_ASSERT_EQUAL_STRING("", nothing);
    TEST_ASSERT_TRUE(edge_vary_serialize("", lookup_accept_gzip, NULL, nothing, sizeof(nothing)));
    TEST_ASSERT_EQUAL_STRING("", nothing);

    TEST_ASSERT_FALSE(edge_vary_serialize("*", lookup_accept_gzip, NULL, nothing, sizeof(nothing)));

    char two[PROTOCORE_EDGE_VARY_MAX];
    char one[PROTOCORE_EDGE_VARY_MAX];
    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding, Accept", lookup_accept_gzip, NULL, two, sizeof(two)));
    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding", lookup_accept_gzip, NULL, one, sizeof(one)));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(two, one));
}

static EdgeCacheStore g_store;

static EdgeEntry *store(const char *canon, const char *vary_key)
{
    return edge_store_alloc(&g_store, canon, vary_key);
}

void test_store_alloc_and_lookup(void)
{
    edge_store_init(&g_store);
    EdgeEntry *a = store("GET\nexample.com\n/a", "");
    TEST_ASSERT_NOT_NULL(a);
    a->status = 200;

    TEST_ASSERT_EQUAL_PTR(a, edge_store_lookup(&g_store, "GET\nexample.com\n/a", "", 0u));
    TEST_ASSERT_NULL(edge_store_lookup(&g_store, "GET\nexample.com\n/b", "", 0u));
    TEST_ASSERT_NULL(edge_store_lookup(&g_store, "GET\nexample.com\n/a", "gzip", 0u));

    EdgeEntry *b = store("GET\nexample.com\n/a", "gzip");
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(a != b);
    TEST_ASSERT_EQUAL_PTR(b, edge_store_lookup(&g_store, "GET\nexample.com\n/a", "gzip", 0u));
    TEST_ASSERT_EQUAL_UINT32(2u, g_store.stats.stores);

    char huge[PROTOCORE_EDGE_KEY_MAX + 8];
    memset(huge, 'k', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';
    TEST_ASSERT_NULL(store(huge, ""));
}

void test_store_evicts_the_least_recently_used_slot(void)
{
    char key[32];
    edge_store_init(&g_store);
    for (int i = 0; i < PROTOCORE_EDGE_CACHE_SLOTS; i++)
    {
        key[0] = '/';
        key[1] = (char)('0' + i);
        key[2] = '\0';
        TEST_ASSERT_NOT_NULL(store(key, ""));
    }
    TEST_ASSERT_EQUAL_UINT32(0u, g_store.stats.evictions);

    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "/0", "", 100u));

    TEST_ASSERT_NOT_NULL(store("/new", ""));
    TEST_ASSERT_EQUAL_UINT32(1u, g_store.stats.evictions);
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "/0", "", 200u));
    TEST_ASSERT_NULL(edge_store_lookup(&g_store, "/1", "", 200u));
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "/new", "", 200u));
}

void test_store_find_resolves_the_vary_variant(void)
{
    edge_store_init(&g_store);
    char vk[PROTOCORE_EDGE_VARY_MAX];

    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding", lookup_accept_gzip, NULL, vk, sizeof(vk)));
    EdgeEntry *g = store("GET\nexample.com\n/a", vk);
    TEST_ASSERT_NOT_NULL(g);
    memcpy(g->vary_names, "Accept-Encoding", sizeof("Accept-Encoding"));

    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding", lookup_accept_br, NULL, vk, sizeof(vk)));
    EdgeEntry *b = store("GET\nexample.com\n/a", vk);
    TEST_ASSERT_NOT_NULL(b);
    memcpy(b->vary_names, "Accept-Encoding", sizeof("Accept-Encoding"));

    TEST_ASSERT_EQUAL_PTR(g, edge_store_find(&g_store, "GET\nexample.com\n/a", lookup_accept_gzip, NULL, 0u));
    TEST_ASSERT_EQUAL_PTR(b, edge_store_find(&g_store, "GET\nexample.com\n/a", lookup_accept_br, NULL, 0u));
    TEST_ASSERT_NULL(edge_store_find(&g_store, "GET\nexample.com\n/a", lookup_nothing, NULL, 0u));
}

void test_store_purge_by_key_and_by_path_prefix(void)
{
    edge_store_init(&g_store);
    TEST_ASSERT_NOT_NULL(store("GET\nexample.com\n/img/a.png", ""));
    TEST_ASSERT_NOT_NULL(store("GET\nexample.com\n/img/a.png", "gzip"));
    TEST_ASSERT_NOT_NULL(store("GET\nexample.com\n/img/b.png", ""));
    TEST_ASSERT_NOT_NULL(store("GET\nexample.com\n/css/c.css", ""));

    TEST_ASSERT_EQUAL_UINT32(2u, edge_store_purge(&g_store, "GET\nexample.com\n/img/a.png"));
    TEST_ASSERT_NULL(edge_store_lookup(&g_store, "GET\nexample.com\n/img/a.png", "gzip", 0u));

    TEST_ASSERT_EQUAL_UINT32(1u, edge_store_purge_prefix(&g_store, "/img/"));
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "GET\nexample.com\n/css/c.css", "", 0u));
    TEST_ASSERT_EQUAL_UINT32(3u, g_store.stats.purges);
}

void test_sweep_drops_only_unrevalidatable_stale_entries(void)
{
    edge_store_init(&g_store);
    EdgeEntry *dead = store("/dead", "");
    dead->lifetime_s = 10;
    dead->initial_age = 0;
    dead->insert_ms = 0u;

    EdgeEntry *keep = store("/keep", "");
    keep->lifetime_s = 10;
    keep->initial_age = 0;
    keep->insert_ms = 0u;
    memcpy(keep->etag, "\"v1\"", sizeof("\"v1\""));

    EdgeEntry *fresh = store("/fresh", "");
    fresh->lifetime_s = 1000;
    fresh->initial_age = 0;
    fresh->insert_ms = 0u;

    TEST_ASSERT_TRUE(edge_entry_fresh(dead, 9000u));
    TEST_ASSERT_FALSE(edge_entry_fresh(dead, 10000u));
    TEST_ASSERT_TRUE(edge_entry_has_validator(keep));
    TEST_ASSERT_FALSE(edge_entry_has_validator(dead));

    TEST_ASSERT_EQUAL_UINT32(1u, edge_store_sweep(&g_store, 20000u));
    TEST_ASSERT_NULL(edge_store_lookup(&g_store, "/dead", "", 0u));
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "/keep", "", 0u));
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "/fresh", "", 0u));
}

void test_rfc9111_storeability(void)
{
    protocore_cache_control cc;
    cache_control_init(&cc);
    TEST_ASSERT_TRUE(edge_is_storeable(200, "GET", &cc, NULL, 100));
    TEST_ASSERT_TRUE(edge_is_storeable(200, "GET", NULL, "Accept-Encoding", 100));

    TEST_ASSERT_FALSE(edge_is_storeable(200, "POST", &cc, NULL, 100));
    TEST_ASSERT_FALSE(edge_is_storeable(404, "GET", &cc, NULL, 100));
    TEST_ASSERT_FALSE(edge_is_storeable(200, "GET", &cc, "*", 100));
    TEST_ASSERT_FALSE(edge_is_storeable(200, "GET", &cc, NULL, PROTOCORE_EDGE_BODY_MAX + 1));
    TEST_ASSERT_TRUE(edge_is_storeable(200, "GET", &cc, NULL, PROTOCORE_EDGE_BODY_MAX));

    protocore_cache_control ns;
    parse_cc("no-store", &ns);
    TEST_ASSERT_FALSE(edge_is_storeable(200, "GET", &ns, NULL, 100));

    protocore_cache_control pv;
    parse_cc("private, max-age=60", &pv);
    TEST_ASSERT_FALSE(edge_is_storeable(200, "GET", &pv, NULL, 100));
}

void test_conditional_request_carries_the_stored_validators(void)
{
    edge_store_init(&g_store);
    EdgeEntry *e = store("/x", "");
    char out[256];

    TEST_ASSERT_EQUAL_UINT(0u, edge_build_conditional(e, out, sizeof(out)));

    memcpy(e->etag, "\"abc123\"", sizeof("\"abc123\""));
    TEST_ASSERT_TRUE(edge_build_conditional(e, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("If-None-Match: \"abc123\"\r\n", out);

    memcpy(e->last_modified, "Sun, 06 Nov 1994 08:49:37 GMT", sizeof("Sun, 06 Nov 1994 08:49:37 GMT"));
    TEST_ASSERT_TRUE(edge_build_conditional(e, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("If-None-Match: \"abc123\"\r\n"
                             "If-Modified-Since: Sun, 06 Nov 1994 08:49:37 GMT\r\n",
                             out);

    char small[16];
    TEST_ASSERT_EQUAL_UINT(0u, edge_build_conditional(e, small, sizeof(small)));
}

void test_apply_304_refreshes_freshness_and_adopts_validators(void)
{
    edge_store_init(&g_store);
    EdgeEntry *e = store("/x", "");
    memcpy(e->etag, "\"old\"", sizeof("\"old\""));
    e->body_len = 3;
    e->body[0] = 'a';
    e->lifetime_s = 0;
    e->insert_ms = 0u;
    TEST_ASSERT_FALSE(edge_entry_fresh(e, 1000u));

    static const char *const NOT_MODIFIED = "HTTP/1.1 304 Not Modified\r\n"
                                            "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                                            "Cache-Control: max-age=120\r\n"
                                            "ETag: \"new\"\r\n"
                                            "\r\n";
    edge_apply_304(e, NOT_MODIFIED, strlen(NOT_MODIFIED), NOV6_1994, 5000u);

    TEST_ASSERT_EQUAL_STRING("\"new\"", e->etag);
    TEST_ASSERT_EQUAL_INT32(120, e->lifetime_s);
    TEST_ASSERT_EQUAL_INT64(NOV6_1994, e->date_epoch);
    TEST_ASSERT_EQUAL_INT32(0, e->initial_age);
    TEST_ASSERT_TRUE(edge_entry_fresh(e, 5000u));
    TEST_ASSERT_EQUAL_UINT(3u, e->body_len);
    TEST_ASSERT_EQUAL_CHAR('a', (char)e->body[0]);

    static const char *const AGED = "HTTP/1.1 304 Not Modified\r\n"
                                    "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                                    "Cache-Control: max-age=120\r\n"
                                    "Age: 100\r\n"
                                    "\r\n";
    edge_apply_304(e, AGED, strlen(AGED), NOV6_1994, 5000u);
    TEST_ASSERT_EQUAL_INT32(100, e->initial_age);
    TEST_ASSERT_TRUE(edge_entry_fresh(e, 5000u));
    TEST_ASSERT_FALSE(edge_entry_fresh(e, 5000u + 20000u));
}

void test_freshness_falls_back_to_the_default_ttl(void)
{
    edge_store_init(&g_store);
    EdgeEntry *e = store("/x", "");
    protocore_cache_control cc;
    cache_control_init(&cc);

    edge_entry_set_freshness(e, &cc, PROTO_TRUE, -1, -1, -1, 0, -1, 0u);
    TEST_ASSERT_EQUAL_INT32(PROTOCORE_EDGE_DEFAULT_TTL_S, e->lifetime_s);

    edge_entry_set_freshness(e, &cc, PROTO_TRUE, NOV6_1994, -1, NOV6_1994 - 3600, 0, -1, 0u);
    TEST_ASSERT_EQUAL_INT32(360, e->lifetime_s);
}

void test_an_expires_in_the_past_stores_as_stale(void)
{
    edge_store_init(&g_store);
    EdgeEntry *e = store("/x", "");
    protocore_cache_control cc;
    cache_control_init(&cc);

    edge_entry_set_freshness(e, &cc, PROTO_TRUE, NOV6_1994, NOV6_1994 - 100, NOV6_1994 - 86400, 0, -1, 0u);
    TEST_ASSERT_EQUAL_INT32(0, e->lifetime_s);
    TEST_ASSERT_FALSE(edge_entry_fresh(e, 0u));

    edge_entry_set_freshness(e, &cc, PROTO_TRUE, NOV6_1994, NOV6_1994 + 100, NOV6_1994 - 86400, 0, -1, 0u);
    TEST_ASSERT_EQUAL_INT32(100, e->lifetime_s);
    TEST_ASSERT_TRUE(edge_entry_fresh(e, 99000u));
    TEST_ASSERT_FALSE(edge_entry_fresh(e, 100000u));
}
