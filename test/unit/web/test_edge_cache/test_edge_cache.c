// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// Pure host tests for the CDN edge-cache engine (services/web/edge_cache): response header-field access,
// HTTP-date parsing, RFC 9111 freshness, and the cache key + digest + Vary secondary key.

#include "network_drivers/application/http_range.h" // http_parse_byte_range (Range/206 serving from the cache)
#include "services/web/edge_cache/edge_cache.h"
#include "services/web/httpcache/httpcache.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static const char *RESP_HEAD = "HTTP/1.1 200 OK\r\n"
                               "ETag: \"abc123\"\r\n"
                               "Cache-Control:   max-age=60  \r\n"
                               "Last-Modified: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                               "Content-Type: text/html\r\n"
                               "\r\n";

// --- header field access -------------------------------------------------------------------------

static void test_header_value_found()
{
    char out[64];
    TEST_ASSERT_TRUE(edge_header_value(RESP_HEAD, strlen(RESP_HEAD), "ETag", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("\"abc123\"", out);
    TEST_ASSERT_TRUE(edge_header_value(RESP_HEAD, strlen(RESP_HEAD), "Content-Type", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("text/html", out);
}

static void test_header_value_case_insensitive_and_ows_trim()
{
    char out[64];
    // case-insensitive name; leading + trailing OWS on the value is stripped
    TEST_ASSERT_TRUE(edge_header_value(RESP_HEAD, strlen(RESP_HEAD), "cache-control", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("max-age=60", out);
}

static void test_header_value_absent_and_too_small()
{
    char out[64];
    TEST_ASSERT_FALSE(edge_header_value(RESP_HEAD, strlen(RESP_HEAD), "X-Missing", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out); // emptied on miss
    char tiny[4];
    // "text/html" does not fit 4 bytes -> refuse (never truncate a validator)
    TEST_ASSERT_FALSE(edge_header_value(RESP_HEAD, strlen(RESP_HEAD), "Content-Type", tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("", tiny);
}

// --- HTTP-date parsing ---------------------------------------------------------------------------

static void test_http_date_all_three_formats()
{
    // RFC 9110 sec 5.6.7 worked example: all three encode 1994-11-06 08:49:37 UTC = 784111777.
    const int64_t expect = 784111777;
    const char *imf = "Sun, 06 Nov 1994 08:49:37 GMT";
    const char *r850 = "Sunday, 06-Nov-94 08:49:37 GMT";
    const char *asc = "Sun Nov  6 08:49:37 1994";
    TEST_ASSERT_EQUAL_INT64(expect, edge_parse_http_date(imf, strlen(imf)));
    TEST_ASSERT_EQUAL_INT64(expect, edge_parse_http_date(r850, strlen(r850)));
    TEST_ASSERT_EQUAL_INT64(expect, edge_parse_http_date(asc, strlen(asc)));
}

static void test_http_date_epoch_zero_and_invalid()
{
    const char *epoch = "Thu, 01 Jan 1970 00:00:00 GMT";
    TEST_ASSERT_EQUAL_INT64(0, edge_parse_http_date(epoch, strlen(epoch)));
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("not a date", 10));
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Zzz 1994 08:49:37 GMT", 29));
}

// --- freshness -----------------------------------------------------------------------------------

static void test_freshness_lifetime_precedence()
{
    pc_cache_control cc;
    cache_control_init(&cc);
    cc.max_age = 100;
    cc.s_maxage = 50;
    TEST_ASSERT_EQUAL_INT32(50, edge_freshness_lifetime(&cc, /*shared=*/PROTO_TRUE, -1, -1)); // s-maxage wins (shared)
    TEST_ASSERT_EQUAL_INT32(100,
                            edge_freshness_lifetime(&cc, /*shared=*/PROTO_FALSE, -1, -1)); // private ignores s-maxage

    pc_cache_control empty;
    cache_control_init(&empty);
    TEST_ASSERT_EQUAL_INT32(100, edge_freshness_lifetime(&empty, PROTO_TRUE, 1000, 1100)); // Expires - Date
    TEST_ASSERT_EQUAL_INT32(-1, edge_freshness_lifetime(&empty, PROTO_TRUE, -1, -1));      // nothing explicit
}

static void test_heuristic_lifetime()
{
    TEST_ASSERT_EQUAL_INT32(100, edge_heuristic_lifetime(1000000, 1000000 - 1000)); // 10% of 1000
    TEST_ASSERT_EQUAL_INT32(-1, edge_heuristic_lifetime(-1, 5));                    // Date absent
    TEST_ASSERT_EQUAL_INT32(-1, edge_heuristic_lifetime(1000, 2000));               // Last-Modified newer than Date
}

static void test_initial_and_current_age()
{
    // no wall clock (response_time_epoch < 0) -> the Age header alone
    TEST_ASSERT_EQUAL_INT32(10, edge_initial_age(10, -1, -1));
    // with a clock: max(apparent = response-date, age header)
    TEST_ASSERT_EQUAL_INT32(50, edge_initial_age(10, 1000, 1050));
    TEST_ASSERT_EQUAL_INT32(0, edge_initial_age(-5, -1, -1)); // negative Age clamped

    TEST_ASSERT_EQUAL_INT32(15, edge_current_age(10, 1000, 6000)); // +5 s resident
    // monotonic wrap: insert near UINT32_MAX, now just past 0
    TEST_ASSERT_EQUAL_INT32(2, edge_current_age(0, 0xFFFFFC18u, 1000u)); // 2000 ms across the wrap
}

static void test_is_fresh()
{
    TEST_ASSERT_TRUE(edge_is_fresh_at(100, 50));
    TEST_ASSERT_FALSE(edge_is_fresh_at(100, 100)); // reached the lifetime
    TEST_ASSERT_FALSE(edge_is_fresh_at(-1, 0));    // no known lifetime
}

// --- cache key + digest --------------------------------------------------------------------------

static void test_key_canon()
{
    char out[128];
    size_t n = edge_key_canon("GET", "Example.COM", "/a/b", "x=1", /*include_query=*/PROTO_TRUE, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("GET\nexample.com\n/a/b\nx=1", out); // host lowercased
    TEST_ASSERT_EQUAL_UINT(strlen("GET\nexample.com\n/a/b\nx=1"), n);

    n = edge_key_canon("GET", "example.com", "/a/b", "x=1", /*include_query=*/PROTO_FALSE, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("GET\nexample.com\n/a/b", out); // query excluded

    char tiny[8];
    TEST_ASSERT_EQUAL_UINT(
        0, edge_key_canon("GET", "example.com", "/a/b", "", PROTO_FALSE, tiny, sizeof(tiny))); // overflow
}

static void test_key_digest_deterministic_and_distinct()
{
    const char *a = "GET\nexample.com\n/a";
    const char *b = "GET\nexample.com\n/b";
    uint8_t d1[32], d2[32], d3[32];
    edge_key_digest(a, strlen(a), d1);
    edge_key_digest(a, strlen(a), d2);
    edge_key_digest(b, strlen(b), d3);
    TEST_ASSERT_EQUAL_MEMORY(d1, d2, 32);      // deterministic
    TEST_ASSERT_TRUE(memcmp(d1, d3, 32) != 0); // distinct inputs -> distinct digests
}

// --- Vary secondary key --------------------------------------------------------------------------

// Mock request-header lookup: a tiny name->value table.
typedef struct
{
    const char *name;
    const char *value;
} MockHdr;
static const MockHdr *g_hdrs = NULL;
static size_t g_hdr_count = 0;

static const char *mock_lookup(void *ctx, const char *name)
{
    (void)ctx;
    for (size_t i = 0; i < g_hdr_count; i++)
    {
        if (strcasecmp(g_hdrs[i].name, name) == 0)
        {
            return g_hdrs[i].value;
        }
    }
    return NULL;
}

static void test_vary_serialize_match_and_differ()
{
    static const MockHdr gz_en[] = {{"accept-encoding", "gzip"}, {"accept-language", "en-US"}};
    static const MockHdr br_en[] = {{"accept-encoding", "br"}, {"accept-language", "en-US"}};

    char a[128], b[128];
    g_hdrs = gz_en;
    g_hdr_count = 2;
    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding, Accept-Language", mock_lookup, NULL, a, sizeof(a)));
    char a2[128];
    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding, Accept-Language", mock_lookup, NULL, a2, sizeof(a2)));
    TEST_ASSERT_EQUAL_STRING(a, a2); // same request values -> same key

    g_hdrs = br_en;
    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding, Accept-Language", mock_lookup, NULL, b, sizeof(b)));
    TEST_ASSERT_TRUE(strcmp(a, b) != 0); // different Accept-Encoding -> different key
}

static void test_vary_serialize_star_and_empty()
{
    char out[64];
    TEST_ASSERT_FALSE(edge_vary_serialize("*", mock_lookup, NULL, out, sizeof(out))); // uncacheable
    TEST_ASSERT_TRUE(edge_vary_serialize(NULL, mock_lookup, NULL, out, sizeof(out))); // no Vary
    TEST_ASSERT_EQUAL_STRING("", out);
}

// --- L1 store ------------------------------------------------------------------------------------

static EdgeCacheStore g_store;

static void test_store_alloc_lookup()
{
    edge_store_init(&g_store);
    EdgeEntry *e = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    TEST_ASSERT_NOT_NULL(e);
    e->status = 200;
    e->body_len = 3;
    memcpy(e->body, "abc", 3);
    TEST_ASSERT_EQUAL_PTR(e, edge_store_lookup(&g_store, "GET\nh\n/a", "", 100));
    TEST_ASSERT_NULL(edge_store_lookup(&g_store, "GET\nh\n/b", "", 100));  // different key
    TEST_ASSERT_NULL(edge_store_lookup(&g_store, "GET\nh\n/a", "x", 100)); // Vary mismatch
}

static void test_store_lru_evict()
{
    edge_store_init(&g_store);
    char key[32];
    for (int i = 0; i < PC_EDGE_CACHE_SLOTS; i++)
    {
        snprintf(key, sizeof(key), "GET\nh\n/%d", i);
        TEST_ASSERT_NOT_NULL(edge_store_alloc(&g_store, key, ""));
    }
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "GET\nh\n/3", "", 1)); // touch /3 -> MRU (LRU is /0)
    TEST_ASSERT_NOT_NULL(edge_store_alloc(&g_store, "GET\nh\n/new", ""));   // evicts the true LRU = /0
    TEST_ASSERT_NULL(edge_store_lookup(&g_store, "GET\nh\n/0", "", 1));     // evicted
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "GET\nh\n/3", "", 1)); // survived (touched)
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "GET\nh\n/new", "", 1));
    TEST_ASSERT_EQUAL_UINT32(1, g_store.stats.evictions);
}

static void test_store_ttl_sweep()
{
    edge_store_init(&g_store);
    pc_cache_control s10, s1000;
    cache_control_init(&s10);
    s10.max_age = 10;
    cache_control_init(&s1000);
    s1000.max_age = 1000;

    EdgeEntry *stale_noval = edge_store_alloc(&g_store, "GET\nh\n/x", "");
    edge_entry_set_freshness(stale_noval, &s10, PROTO_TRUE, -1, -1, -1, 0, -1, 1000);
    EdgeEntry *stale_val = edge_store_alloc(&g_store, "GET\nh\n/y", "");
    edge_entry_set_freshness(stale_val, &s10, PROTO_TRUE, -1, -1, -1, 0, -1, 1000);
    strcpy(stale_val->etag, "\"v\"");
    EdgeEntry *fresh = edge_store_alloc(&g_store, "GET\nh\n/z", "");
    edge_entry_set_freshness(fresh, &s1000, PROTO_TRUE, -1, -1, -1, 0, -1, 1000);

    // 20 s later: /x stale+no-validator (swept), /y stale+validator (kept), /z fresh (kept)
    TEST_ASSERT_EQUAL_UINT32(1, edge_store_sweep(&g_store, 21000));
    TEST_ASSERT_NULL(edge_store_lookup(&g_store, "GET\nh\n/x", "", 21000));
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "GET\nh\n/y", "", 21000));
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "GET\nh\n/z", "", 21000));
}

static void test_store_purge()
{
    edge_store_init(&g_store);
    edge_store_alloc(&g_store, "GET\nh\n/img/a", "");
    edge_store_alloc(&g_store, "GET\nh\n/img/b", "");
    edge_store_alloc(&g_store, "GET\nh\n/css/c", "");
    TEST_ASSERT_EQUAL_UINT32(1, edge_store_purge(&g_store, "GET\nh\n/img/a")); // single
    TEST_ASSERT_NULL(edge_store_lookup(&g_store, "GET\nh\n/img/a", "", 1));
    TEST_ASSERT_EQUAL_UINT32(1, edge_store_purge_prefix(&g_store, "/img/")); // prefix -> /img/b
    TEST_ASSERT_NULL(edge_store_lookup(&g_store, "GET\nh\n/img/b", "", 1));
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "GET\nh\n/css/c", "", 1)); // untouched
    TEST_ASSERT_EQUAL_UINT32(0, edge_store_purge(&g_store, "GET\nh\n/nope"));   // miss no-op
}

static void test_store_free_entry()
{
    edge_store_init(&g_store);
    EdgeEntry *a = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    edge_store_alloc(&g_store, "GET\nh\n/b", "");
    edge_store_free_entry(&g_store, a);
    TEST_ASSERT_NULL(edge_store_lookup(&g_store, "GET\nh\n/a", "", 1));
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "GET\nh\n/b", "", 1));
    EdgeEntry *c = edge_store_alloc(&g_store, "GET\nh\n/c", ""); // the freed slot is reusable
    TEST_ASSERT_EQUAL_PTR(a, c);
}

static void test_store_find_vary()
{
    static const MockHdr gz_hdr[] = {{"accept-encoding", "gzip"}};
    static const MockHdr br_hdr[] = {{"accept-encoding", "br"}};

    edge_store_init(&g_store);
    // two variants of the same resource, keyed by Accept-Encoding
    EdgeEntry *gz = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    strcpy(gz->vary_names, "Accept-Encoding");
    g_hdrs = gz_hdr;
    g_hdr_count = 1;
    edge_vary_serialize("Accept-Encoding", mock_lookup, NULL, gz->vary_vals, sizeof(gz->vary_vals));

    EdgeEntry *br = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    strcpy(br->vary_names, "Accept-Encoding");
    g_hdrs = br_hdr;
    edge_vary_serialize("Accept-Encoding", mock_lookup, NULL, br->vary_vals, sizeof(br->vary_vals));

    g_hdrs = gz_hdr; // a gzip request selects the gzip variant
    TEST_ASSERT_EQUAL_PTR(gz, edge_store_find(&g_store, "GET\nh\n/a", mock_lookup, NULL, 1));
    g_hdrs = br_hdr; // a br request selects the br variant
    TEST_ASSERT_EQUAL_PTR(br, edge_store_find(&g_store, "GET\nh\n/a", mock_lookup, NULL, 1));
    g_hdrs = NULL; // identity (no Accept-Encoding) matches neither variant
    g_hdr_count = 0;
    TEST_ASSERT_NULL(edge_store_find(&g_store, "GET\nh\n/a", mock_lookup, NULL, 1));

    // an entry with no Vary matches any request
    EdgeEntry *plain = edge_store_alloc(&g_store, "GET\nh\n/b", "");
    g_hdrs = gz_hdr;
    g_hdr_count = 1;
    TEST_ASSERT_EQUAL_PTR(plain, edge_store_find(&g_store, "GET\nh\n/b", mock_lookup, NULL, 1));
}

static void test_entry_freshness_resolution()
{
    edge_store_init(&g_store);
    pc_cache_control cc, empty;
    cache_control_init(&cc);
    cc.max_age = 100;
    cache_control_init(&empty);

    EdgeEntry *e = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    edge_entry_set_freshness(e, &cc, PROTO_TRUE, -1, -1, -1, 0, -1, 1000);
    TEST_ASSERT_EQUAL_INT32(100, (int32_t)e->lifetime_s);
    TEST_ASSERT_TRUE(edge_entry_fresh(e, 1000 + 99000));
    TEST_ASSERT_FALSE(edge_entry_fresh(e, 1000 + 101000));

    EdgeEntry *dflt = edge_store_alloc(&g_store, "GET\nh\n/b", "");
    edge_entry_set_freshness(dflt, &empty, PROTO_TRUE, -1, -1, -1, 0, -1, 1000); // no directive -> default TTL
    TEST_ASSERT_EQUAL_INT32(PC_EDGE_DEFAULT_TTL_S, (int32_t)dflt->lifetime_s);

    EdgeEntry *heur = edge_store_alloc(&g_store, "GET\nh\n/c", "");
    edge_entry_set_freshness(heur, &empty, PROTO_TRUE, 1000000, -1, 1000000 - 1000, 0, -1, 1000); // 10% heuristic
    TEST_ASSERT_EQUAL_INT32(100, (int32_t)heur->lifetime_s);

    TEST_ASSERT_FALSE(edge_entry_has_validator(e));
    strcpy(e->last_modified, "Sun, 06 Nov 1994 08:49:37 GMT");
    TEST_ASSERT_TRUE(edge_entry_has_validator(e));
}

static void test_storeability()
{
    pc_cache_control cc, ns, pv;
    cache_control_init(&cc);
    cache_control_init(&ns);
    ns.no_store = PROTO_TRUE;
    cache_control_init(&pv);
    pv.cc_private = PROTO_TRUE;

    TEST_ASSERT_TRUE(edge_is_storeable(200, "GET", &cc, NULL, 100));
    TEST_ASSERT_TRUE(edge_is_storeable(200, "GET", NULL, "Accept-Encoding", PC_EDGE_BODY_MAX));
    TEST_ASSERT_FALSE(edge_is_storeable(200, "POST", &cc, NULL, 100)); // not GET
    TEST_ASSERT_FALSE(edge_is_storeable(404, "GET", &cc, NULL, 100));  // not 200
    TEST_ASSERT_FALSE(edge_is_storeable(200, "GET", &ns, NULL, 100));  // no-store
    TEST_ASSERT_FALSE(edge_is_storeable(200, "GET", &pv, NULL, 100));  // private
    TEST_ASSERT_FALSE(edge_is_storeable(200, "GET", &cc, "*", 100));   // Vary: *
    TEST_ASSERT_FALSE(edge_is_storeable(200, "GET", &cc, "Accept-Encoding, *", 100));
    TEST_ASSERT_FALSE(edge_is_storeable(200, "GET", &cc, NULL, PC_EDGE_BODY_MAX + 1)); // oversize
}

// --- conditional revalidation --------------------------------------------------------------------

static void test_build_conditional()
{
    edge_store_init(&g_store);
    EdgeEntry *e = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    strcpy(e->etag, "\"v1\"");
    strcpy(e->last_modified, "Sun, 06 Nov 1994 08:49:37 GMT");
    char out[128];
    size_t n = edge_build_conditional(e, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("If-None-Match: \"v1\"\r\nIf-Modified-Since: Sun, 06 Nov 1994 08:49:37 GMT\r\n", out);
    TEST_ASSERT_EQUAL_UINT(strlen(out), n);

    EdgeEntry *no_val = edge_store_alloc(&g_store, "GET\nh\n/b", "");
    TEST_ASSERT_EQUAL_UINT(0, edge_build_conditional(no_val, out, sizeof(out))); // no validator
}

static void test_apply_304()
{
    edge_store_init(&g_store);
    pc_cache_control s10;
    cache_control_init(&s10);
    s10.max_age = 10;
    EdgeEntry *e = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    e->status = 200;
    e->body_len = 3;
    memcpy(e->body, "abc", 3);
    strcpy(e->etag, "\"v1\"");
    edge_entry_set_freshness(e, &s10, PROTO_TRUE, -1, -1, -1, 0, -1, 1000);
    TEST_ASSERT_FALSE(edge_entry_fresh(e, 21000)); // stale 20 s later

    // origin answers the revalidation with 304 + a fresh max-age and a stronger validator
    const char *r304 = "HTTP/1.1 304 Not Modified\r\nETag: \"v2\"\r\nCache-Control: max-age=100\r\n\r\n";
    edge_apply_304(e, r304, strlen(r304), -1, 21000);
    TEST_ASSERT_TRUE(edge_entry_fresh(e, 21000));         // refreshed
    TEST_ASSERT_TRUE(edge_entry_fresh(e, 21000 + 99000)); // for the new lifetime
    TEST_ASSERT_EQUAL_STRING("\"v2\"", e->etag);          // validator adopted
    TEST_ASSERT_EQUAL_MEMORY("abc", e->body, 3);          // body kept
    TEST_ASSERT_EQUAL_INT32(100, (int32_t)e->lifetime_s);
}

// --- Range parsing (http_parse_byte_range) - the math behind serving 206 Partial Content from a cached
// body. Return: 0 = no usable range (full 200), 1 = satisfiable [start,end], -1 = unsatisfiable (416). ---

void test_range_explicit_and_open_ended()
{
    size_t s = 0;
    size_t e = 0;
    // bytes=A-B -> inclusive window.
    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=10-40", 100, &s, &e));
    TEST_ASSERT_EQUAL_UINT(10, s);
    TEST_ASSERT_EQUAL_UINT(40, e);
    // bytes=A- -> A to the last byte.
    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=90-", 100, &s, &e));
    TEST_ASSERT_EQUAL_UINT(90, s);
    TEST_ASSERT_EQUAL_UINT(99, e);
    // end past EOF clamps to the last byte.
    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=10-9999", 100, &s, &e));
    TEST_ASSERT_EQUAL_UINT(10, s);
    TEST_ASSERT_EQUAL_UINT(99, e);
    // the whole body.
    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=0-99", 100, &s, &e));
    TEST_ASSERT_EQUAL_UINT(0, s);
    TEST_ASSERT_EQUAL_UINT(99, e);
    // leading whitespace after "bytes=" is skipped.
    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=  10-40", 100, &s, &e));
    TEST_ASSERT_EQUAL_UINT(10, s);
    TEST_ASSERT_EQUAL_UINT(40, e);
}

void test_range_suffix()
{
    size_t s = 0;
    size_t e = 0;
    // bytes=-N -> the last N bytes.
    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=-20", 100, &s, &e));
    TEST_ASSERT_EQUAL_UINT(80, s);
    TEST_ASSERT_EQUAL_UINT(99, e);
    // N >= size -> the whole body.
    TEST_ASSERT_EQUAL_INT(1, http_parse_byte_range("bytes=-500", 100, &s, &e));
    TEST_ASSERT_EQUAL_UINT(0, s);
    TEST_ASSERT_EQUAL_UINT(99, e);
}

void test_range_unsatisfiable()
{
    size_t s = 0;
    size_t e = 0;
    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=100-200", 100, &s, &e)); // start past EOF
    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=50-10", 100, &s, &e));   // start > end
    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=-0", 100, &s, &e));      // last 0 bytes
    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=-", 100, &s, &e));       // "-" alone
    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=0-0", 0, &s, &e));       // empty body
    // Overflow saturates (never wraps) -> resolves as past-EOF -> unsatisfiable.
    TEST_ASSERT_EQUAL_INT(-1, http_parse_byte_range("bytes=99999999999999999999-", 100, &s, &e));
}

void test_range_ignored_forms()
{
    size_t s = 0;
    size_t e = 0;
    TEST_ASSERT_EQUAL_INT(0, http_parse_byte_range(NULL, 100, &s, &e));               // absent
    TEST_ASSERT_EQUAL_INT(0, http_parse_byte_range("bytes=0-10,20-30", 100, &s, &e)); // multi-range -> full 200
    TEST_ASSERT_EQUAL_INT(0, http_parse_byte_range("items=0-10", 100, &s, &e));       // wrong unit
    TEST_ASSERT_EQUAL_INT(0, http_parse_byte_range("bytes=10", 100, &s, &e));         // no dash -> malformed
    TEST_ASSERT_EQUAL_INT(0, http_parse_byte_range("bytes=1a-5", 100, &s, &e)); // >'9' ends the start scan -> malformed
    TEST_ASSERT_EQUAL_INT(
        0, http_parse_byte_range("bytes=1-a", 100, &s, &e)); // >'9' where the end digit is expected -> trailing garbage
    TEST_ASSERT_EQUAL_INT(
        0, http_parse_byte_range("bytes=1-2a", 100, &s, &e)); // >'9' ends the end scan -> trailing garbage
    TEST_ASSERT_EQUAL_INT(0, http_parse_byte_range("bytes=10-40x", 100, &s, &e)); // trailing garbage
}

// --- header field access: guards, overflow, odd line shapes --------------------------------------

static void test_header_value_null_guards()
{
    char out[32];
    TEST_ASSERT_FALSE(edge_header_value(NULL, 0, "ETag", out, sizeof(out)));
    TEST_ASSERT_FALSE(edge_header_value(RESP_HEAD, strlen(RESP_HEAD), NULL, out, sizeof(out)));
    TEST_ASSERT_FALSE(edge_header_value(RESP_HEAD, strlen(RESP_HEAD), "ETag", NULL, sizeof(out)));
    TEST_ASSERT_FALSE(edge_header_value(RESP_HEAD, strlen(RESP_HEAD), "ETag", out, 0));
}

static void test_header_value_overflow_fails_whole_lookup()
{
    // The name matches exactly but its value will not fit: fail the lookup outright rather than
    // truncate a validator, and do not fall through to a later header.
    char five[5]; // "ETag" is 4 so the name still matches; "\"abc123\"" (8) does not fit
    TEST_ASSERT_FALSE(edge_header_value(RESP_HEAD, strlen(RESP_HEAD), "ETag", five, sizeof(five)));
    TEST_ASSERT_EQUAL_STRING("", five);
}

static void test_header_value_colonless_line_skipped()
{
    static const char *head = "HTTP/1.1 200 OK\r\n"
                              "NoColonHere\r\n"
                              "ETag: \"z\"\r\n"
                              "\r\n";
    char out[32];
    TEST_ASSERT_TRUE(edge_header_value(head, strlen(head), "ETag", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("\"z\"", out);
}

static void test_header_value_lf_only_and_htab_ows()
{
    // Bare-LF line endings (no CR to strip) and HTAB as the OWS around the value.
    static const char *head = "HTTP/1.1 200 OK\n"
                              "ETag:\t\"z\"\t\n"
                              "\n";
    char out[32];
    TEST_ASSERT_TRUE(edge_header_value(head, strlen(head), "ETag", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("\"z\"", out);
    // an absent name walks to the bare-LF blank line and stops there
    TEST_ASSERT_FALSE(edge_header_value(head, strlen(head), "X-Absent", out, sizeof(out)));
}

static void test_header_value_unterminated_blocks()
{
    char out[32];
    // A head with no newline at all: nothing follows the status line.
    static const char *status_only = "HTTP/1.1 200 OK";
    TEST_ASSERT_FALSE(edge_header_value(status_only, strlen(status_only), "ETag", out, sizeof(out)));
    // A final header line with no trailing newline is still parsed, and ends the scan.
    static const char *no_eol = "HTTP/1.1 200 OK\nX-Foo: bar";
    TEST_ASSERT_TRUE(edge_header_value(no_eol, strlen(no_eol), "X-Foo", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("bar", out);
    TEST_ASSERT_FALSE(edge_header_value(no_eol, strlen(no_eol), "ETag", out, sizeof(out)));
}

// --- HTTP-date parsing: guards, per-field failures, range checks ---------------------------------

static void test_http_date_null_and_length_bounds()
{
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date(NULL, 10));
    // leading OWS (SP and HTAB) is trimmed before parsing
    const char *ows = "  \tSun, 06 Nov 1994 08:49:37 GMT";
    TEST_ASSERT_EQUAL_INT64(784111777, edge_parse_http_date(ows, strlen(ows)));
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("   ", 3)); // all OWS -> nothing left
    char big[80];
    memset(big, 'x', sizeof(big));
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date(big, 70)); // past the 64-byte parse scratch
}

static void test_http_date_field_failures()
{
    // Each early-out of the IMF-fixdate / RFC 850 parse in turn.
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun,", 4));                           // no day-of-month
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06Nov 1994 08:49:37 GMT", 28));  // no day/month separator
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Nov xx94 08:49:37 GMT", 29)); // no year
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Nov 1994 xx:49:37 GMT", 29)); // hour not a digit
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Nov 1994 -8:49:37 GMT", 29)); // hour below '0'
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Nov 1994 08-49:37 GMT", 29)); // ':' expected after hh
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Nov 1994 08:xx:37 GMT", 29)); // no minute
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Nov 1994 08:49-37 GMT", 29)); // ':' expected after mm
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Nov 1994 08:49:xx GMT", 29)); // no second
}

static void test_http_date_asctime_field_failures()
{
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun Nov xx 08:49:37 1994", 24)); // no day-of-month
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun Nov  6 xx:49:37 1994", 24)); // no time-of-day
}

static void test_http_date_field_range_checks()
{
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 0 Nov 1994 08:49:37 GMT", 28));  // day 0
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 32 Nov 1994 08:49:37 GMT", 29)); // day 32
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Nov 1994 24:49:37 GMT", 29)); // hour 24
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Nov 1994 08:60:37 GMT", 29)); // minute 60
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Nov 1994 08:49:61 GMT", 29)); // second 61
    // a leap second (ss == 60) is in range
    TEST_ASSERT_EQUAL_INT64(784111800, edge_parse_http_date("Sun, 06 Nov 1994 08:49:60 GMT", 29));
}

static void test_http_date_rfc850_year_windows()
{
    // A 2-digit year below 70 windows into the 2000s.
    const char *y05 = "Sunday, 06-Nov-05 08:49:37 GMT";
    TEST_ASSERT_EQUAL_INT64(1131266977, edge_parse_http_date(y05, strlen(y05)));
    // A 4-digit year in the obsolete format is taken as written (no windowing).
    const char *y1994 = "Sunday, 06-Nov-1994 08:49:37 GMT";
    TEST_ASSERT_EQUAL_INT64(784111777, edge_parse_http_date(y1994, strlen(y1994)));
}

static void test_http_date_pre_epoch_and_year_zero()
{
    const char *pre = "Thu, 06 Nov 1969 08:49:37 GMT";
    TEST_ASSERT_EQUAL_INT64(-4806623, edge_parse_http_date(pre, strlen(pre))); // before 1970 -> negative
    // Jan/Feb of year 0 drives the civil-date algorithm's negative-era path.
    const char *y0 = "Sat, 06 Jan 0000 00:00:00 GMT";
    TEST_ASSERT_EQUAL_INT64(-62166787200LL, edge_parse_http_date(y0, strlen(y0)));
}

// --- freshness edges -----------------------------------------------------------------------------

static void test_heuristic_and_initial_age_edges()
{
    TEST_ASSERT_EQUAL_INT32(-1, edge_heuristic_lifetime(1000, -1)); // Last-Modified absent
    TEST_ASSERT_EQUAL_INT32(10, edge_initial_age(10, 1000, -1));    // Date known but no wall clock
    TEST_ASSERT_EQUAL_INT32(10, edge_initial_age(10, 1000, 900));   // response older than Date -> no apparent age
    TEST_ASSERT_EQUAL_INT32(0, edge_initial_age(0, 1000, 1000));    // same instant -> no apparent age
}

// --- cache key: guards and every append point ----------------------------------------------------

static void test_key_canon_null_guards()
{
    char out[64];
    TEST_ASSERT_EQUAL_UINT(0, edge_key_canon(NULL, "h", "/a", "", PROTO_FALSE, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, edge_key_canon("GET", NULL, "/a", "", PROTO_FALSE, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, edge_key_canon("GET", "h", NULL, "", PROTO_FALSE, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, edge_key_canon("GET", "h", "/a", "", PROTO_FALSE, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, edge_key_canon("GET", "h", "/a", "", PROTO_FALSE, out, 0));
}

static void test_key_canon_overflow_at_each_append()
{
    // "GET\nexample.com\n/a/b\nx=1" - a cap that stops at each piece in turn must yield 0, never a
    // truncated key (two resources would collide).
    char out[64];
    TEST_ASSERT_EQUAL_UINT(0, edge_key_canon("GET", "example.com", "/a/b", "x=1", PROTO_TRUE, out, 2));  // method
    TEST_ASSERT_EQUAL_UINT(0, edge_key_canon("GET", "example.com", "/a/b", "x=1", PROTO_TRUE, out, 4));  // separator
    TEST_ASSERT_EQUAL_UINT(0, edge_key_canon("GET", "example.com", "/a/b", "x=1", PROTO_TRUE, out, 8));  // host
    TEST_ASSERT_EQUAL_UINT(0, edge_key_canon("GET", "example.com", "/a/b", "x=1", PROTO_TRUE, out, 16)); // separator
    TEST_ASSERT_EQUAL_UINT(0, edge_key_canon("GET", "example.com", "/a/b", "x=1", PROTO_TRUE, out, 18)); // path
    TEST_ASSERT_EQUAL_UINT(0, edge_key_canon("GET", "example.com", "/a/b", "x=1", PROTO_TRUE, out, 21)); // separator
    TEST_ASSERT_EQUAL_UINT(0, edge_key_canon("GET", "example.com", "/a/b", "x=1", PROTO_TRUE, out, 22)); // query
    TEST_ASSERT_EQUAL_UINT(24, edge_key_canon("GET", "example.com", "/a/b", "x=1", PROTO_TRUE, out, 25));
}

static void test_key_canon_query_requested_but_empty()
{
    char out[64];
    // include_query with nothing to include is the same key as excluding it.
    TEST_ASSERT_EQUAL_UINT(20, edge_key_canon("GET", "example.com", "/a/b", NULL, PROTO_TRUE, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("GET\nexample.com\n/a/b", out);
    TEST_ASSERT_EQUAL_UINT(20, edge_key_canon("GET", "example.com", "/a/b", "", PROTO_TRUE, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("GET\nexample.com\n/a/b", out);
}

// --- Vary secondary key: guards, overflow, token edges -------------------------------------------

static void test_vary_serialize_null_out_and_null_lookup()
{
    char out[64];
    TEST_ASSERT_FALSE(edge_vary_serialize("Accept-Encoding", mock_lookup, NULL, NULL, sizeof(out)));
    TEST_ASSERT_FALSE(edge_vary_serialize("Accept-Encoding", mock_lookup, NULL, out, 0));
    // With no lookup at all every name still emits its record, with an empty value.
    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding", NULL, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("accept-encoding\x1e\x1f", out);
}

static void test_vary_serialize_overflow_at_each_append()
{
    static const MockHdr gz[] = {{"accept-encoding", "gzip"}};
    g_hdrs = gz;
    g_hdr_count = 1;
    char out[64];
    TEST_ASSERT_FALSE(edge_vary_serialize("Accept-Encoding", mock_lookup, NULL, out, 8));  // the name
    TEST_ASSERT_FALSE(edge_vary_serialize("Accept-Encoding", mock_lookup, NULL, out, 16)); // its separator
    TEST_ASSERT_FALSE(edge_vary_serialize("Accept-Encoding", mock_lookup, NULL, out, 20)); // the value
    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding", mock_lookup, NULL, out, 22));
}

static void test_vary_serialize_long_name_and_separator_runs()
{
    char out[128];
    // A field name past the internal token buffer is clamped, not overflowed.
    static const char *longname = "Aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-Long";
    TEST_ASSERT_TRUE(strlen(longname) > 47);
    TEST_ASSERT_TRUE(edge_vary_serialize(longname, NULL, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(47 + 2, strlen(out)); // 47 name bytes + the two record separators

    // Leading, repeated, and trailing separators (SP, HTAB, comma) are all skipped.
    char a[128];
    char b[128];
    TEST_ASSERT_TRUE(edge_vary_serialize(" ,\tAccept-Encoding,\t , ", NULL, NULL, a, sizeof(a)));
    TEST_ASSERT_TRUE(edge_vary_serialize("Accept-Encoding", NULL, NULL, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING(b, a);
}

// --- L1 store edges ------------------------------------------------------------------------------

static void test_store_alloc_key_too_long()
{
    edge_store_init(&g_store);
    char big[PC_EDGE_KEY_MAX + 8];
    memset(big, 'k', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    TEST_ASSERT_NULL(edge_store_alloc(&g_store, big, "")); // will not fit the key field -> non-cacheable
    TEST_ASSERT_EQUAL_UINT32(0, g_store.stats.stores);
}

static void test_store_alloc_null_and_oversize_vary_key()
{
    edge_store_init(&g_store);
    EdgeEntry *e = edge_store_alloc(&g_store, "GET\nh\n/a", NULL); // no secondary key
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("", e->vary_vals);
    TEST_ASSERT_EQUAL_PTR(e, edge_store_lookup(&g_store, "GET\nh\n/a", NULL, 5)); // NULL matches ""

    char big[PC_EDGE_VARY_MAX + 16];
    memset(big, 'v', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    EdgeEntry *t = edge_store_alloc(&g_store, "GET\nh\n/b", big);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT(PC_EDGE_VARY_MAX - 1, strlen(t->vary_vals)); // clamped, still terminated
}

static void test_store_alloc_no_free_slot_and_empty_lru()
{
    // Every slot marked used with an empty LRU list (the PC_EDGE_CACHE_SLOTS == 0 shape): alloc must
    // fail closed rather than index the PC_EDGE_LRU_NONE sentinel.
    edge_store_init(&g_store);
    for (uint16_t i = 0; i < PC_EDGE_CACHE_SLOTS; i++)
    {
        g_store.entries[i].used = PROTO_TRUE;
    }
    TEST_ASSERT_EQUAL_UINT16(PC_EDGE_LRU_NONE, g_store.lru_tail);
    TEST_ASSERT_NULL(edge_store_alloc(&g_store, "GET\nh\n/x", ""));
}

static void test_store_purge_prefix_key_without_a_path()
{
    // A key that is not the canonical "METHOD\nhost\npath" shape has no path portion, so a prefix
    // purge must skip it instead of matching against the raw key.
    edge_store_init(&g_store);
    TEST_ASSERT_NOT_NULL(edge_store_alloc(&g_store, "malformed-key", ""));
    TEST_ASSERT_NOT_NULL(edge_store_alloc(&g_store, "GET\nh\n/img/a", ""));
    TEST_ASSERT_EQUAL_UINT32(0, edge_store_purge_prefix(&g_store, "malformed"));
    TEST_ASSERT_EQUAL_UINT32(1, edge_store_purge_prefix(&g_store, "/img/"));
    TEST_ASSERT_NOT_NULL(edge_store_lookup(&g_store, "malformed-key", "", 1)); // untouched
}

static void test_store_find_skips_unserializable_variant()
{
    // A stored variant whose Vary names cannot be serialized (a "*" that should never have been
    // stored) is skipped rather than matching every request.
    static const MockHdr gz[] = {{"accept-encoding", "gzip"}};
    g_hdrs = gz;
    g_hdr_count = 1;
    edge_store_init(&g_store);
    EdgeEntry *e = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    TEST_ASSERT_NOT_NULL(e);
    strcpy(e->vary_names, "*");
    TEST_ASSERT_NULL(edge_store_find(&g_store, "GET\nh\n/a", mock_lookup, NULL, 1));
}

static void test_store_free_entry_foreign_pointer()
{
    edge_store_init(&g_store);
    EdgeEntry *a = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    TEST_ASSERT_NOT_NULL(a);
    static EdgeEntry outside; // not one of the store's slots
    memset(&outside, 0, sizeof(outside));
    edge_store_free_entry(&g_store, &outside); // no-op
    TEST_ASSERT_EQUAL_PTR(a, edge_store_lookup(&g_store, "GET\nh\n/a", "", 1));
}

static void test_storeability_null_method()
{
    pc_cache_control cc;
    cache_control_init(&cc);
    TEST_ASSERT_FALSE(edge_is_storeable(200, NULL, &cc, NULL, 100));
}

// --- conditional revalidation edges --------------------------------------------------------------

static void test_build_conditional_guards_and_overflow_points()
{
    edge_store_init(&g_store);
    EdgeEntry *e = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    TEST_ASSERT_NOT_NULL(e);
    strcpy(e->etag, "\"v1\"");
    char out[128];
    TEST_ASSERT_EQUAL_UINT(0, edge_build_conditional(e, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, edge_build_conditional(e, out, 0));
    // "If-None-Match: \"v1\"\r\n" is 21 bytes; each append point in turn is one byte short
    TEST_ASSERT_EQUAL_UINT(0, edge_build_conditional(e, out, 5));  // the field name
    TEST_ASSERT_EQUAL_UINT(0, edge_build_conditional(e, out, 14)); // the ": " separator
    TEST_ASSERT_EQUAL_UINT(0, edge_build_conditional(e, out, 16)); // the value
    TEST_ASSERT_EQUAL_UINT(0, edge_build_conditional(e, out, 20)); // the trailing CRLF
    TEST_ASSERT_EQUAL_UINT(21, edge_build_conditional(e, out, 22));

    // Last-Modified only: the If-Modified-Since line overflows on its own.
    EdgeEntry *lm = edge_store_alloc(&g_store, "GET\nh\n/b", "");
    TEST_ASSERT_NOT_NULL(lm);
    strcpy(lm->last_modified, "Sun, 06 Nov 1994 08:49:37 GMT");
    TEST_ASSERT_EQUAL_UINT(0, edge_build_conditional(lm, out, 8));
}

static void test_apply_304_date_expires_and_age()
{
    edge_store_init(&g_store);
    EdgeEntry *e = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    TEST_ASSERT_NOT_NULL(e);
    e->status = 200;
    strcpy(e->etag, "\"old\"");
    // No Cache-Control: freshness comes from Expires - Date, and Age propagates.
    static const char *r304 = "HTTP/1.1 304 Not Modified\r\n"
                              "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                              "Expires: Sun, 06 Nov 1994 08:59:37 GMT\r\n"
                              "Age: 30\r\n"
                              "ETag: \"new\"\r\n"
                              "Last-Modified: Sun, 06 Nov 1994 08:00:00 GMT\r\n"
                              "\r\n";
    edge_apply_304(e, r304, strlen(r304), 784111777 + 30, 5000);
    TEST_ASSERT_EQUAL_STRING("\"new\"", e->etag);                                // validator adopted
    TEST_ASSERT_EQUAL_STRING("Sun, 06 Nov 1994 08:00:00 GMT", e->last_modified); // and the newer one
    TEST_ASSERT_EQUAL_INT64(784111777, e->date_epoch);
    TEST_ASSERT_EQUAL_INT64(784111777 + 600, e->expires_epoch);
    TEST_ASSERT_EQUAL_INT32(600, (int32_t)e->lifetime_s);
    TEST_ASSERT_EQUAL_INT32(30, e->age_hdr);
    TEST_ASSERT_EQUAL_INT32(30, (int32_t)e->initial_age); // max(apparent 30, Age 30)
}

static void test_apply_304_non_numeric_age_and_oversize_validators()
{
    edge_store_init(&g_store);
    EdgeEntry *e = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    TEST_ASSERT_NOT_NULL(e);
    strcpy(e->etag, "\"keep\"");
    strcpy(e->last_modified, "Sun, 06 Nov 1994 08:49:37 GMT");

    char etag_long[80];
    memset(etag_long, 'a', sizeof(etag_long));
    etag_long[0] = '"';
    etag_long[70] = '"';
    etag_long[71] = '\0'; // a 71-byte ETag: readable, but past the entry's 64-byte field
    char r304[512];
    snprintf(r304, sizeof(r304),
             "HTTP/1.1 304 Not Modified\r\n"
             "Cache-Control: max-age=50\r\n"
             "Age: none\r\n"
             "ETag: %s\r\n"
             "Last-Modified: not a date but quite long so it will not fit\r\n"
             "\r\n",
             etag_long);
    edge_apply_304(e, r304, strlen(r304), -1, 9000);
    TEST_ASSERT_EQUAL_STRING("\"keep\"", e->etag);                               // too long -> not adopted
    TEST_ASSERT_EQUAL_STRING("Sun, 06 Nov 1994 08:49:37 GMT", e->last_modified); // too long -> not adopted
    TEST_ASSERT_EQUAL_INT32(0, e->age_hdr);                                      // "none" carries no digits
    TEST_ASSERT_EQUAL_INT32(50, (int32_t)e->lifetime_s);
}

static void test_apply_304_reuses_stored_last_modified()
{
    edge_store_init(&g_store);
    EdgeEntry *e = edge_store_alloc(&g_store, "GET\nh\n/a", "");
    TEST_ASSERT_NOT_NULL(e);
    strcpy(e->last_modified, "Sun, 06 Nov 1994 08:00:00 GMT");
    // The 304 carries neither Cache-Control nor Last-Modified, so the stored validator supplies the
    // heuristic lifetime: 10% of (Date - Last-Modified) = 10% of 2977 s.
    static const char *r304 = "HTTP/1.1 304 Not Modified\r\n"
                              "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                              "\r\n";
    edge_apply_304(e, r304, strlen(r304), -1, 1000);
    TEST_ASSERT_EQUAL_STRING("Sun, 06 Nov 1994 08:00:00 GMT", e->last_modified); // kept
    TEST_ASSERT_EQUAL_INT32(297, (int32_t)e->lifetime_s);
}

// The month table is matched three letters at a time; a token that shares its first two letters
// with a real month but not the third must not match it.
static void test_http_date_month_prefix_near_miss()
{
    // "Jum" shares "ju" with both June and July; the third letter rules both out.
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Jum 1994 08:49:37 GMT", 29));
    // "Mah" shares "ma" with March and May.
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun, 06 Mah 1994 08:49:37 GMT", 29));
    // asctime form takes the same path.
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun Jum  6 08:49:37 1994", 24));
}

// The asctime scanner skips the day-name by running to the first space; a string with no space at
// all must run out rather than read past the end.
static void test_http_date_asctime_no_space_at_all()
{
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("Sun", 3));
    TEST_ASSERT_EQUAL_INT64(-1, edge_parse_http_date("X", 1));
}

// A header value that is nothing but optional whitespace: the leading-OWS skip runs to the end of
// the line and the trailing-OWS trim then has nothing left to walk back over.
static void test_header_value_all_whitespace()
{
    char out[32];
    // The first line is the status line and is skipped, so the field under test is the second.
    const char *blk = "HTTP/1.1 200 OK\r\nAge:    \r\nVary: accept\r\n\r\n";
    TEST_ASSERT_TRUE(edge_header_value(blk, strlen(blk), "Age", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out); // present but empty
    const char *tabs = "HTTP/1.1 200 OK\r\nAge: \t \t\r\n\r\n";
    TEST_ASSERT_TRUE(edge_header_value(tabs, strlen(tabs), "Age", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// Vary field names end at a comma, a space or a tab; an empty element (a stray separator) emits
// nothing and the walk simply moves on.
static void test_vary_serialize_space_tab_and_empty_elements()
{
    char out[128];
    TEST_ASSERT_TRUE(edge_vary_serialize("accept encoding", mock_lookup, NULL, out, sizeof(out)));
    TEST_ASSERT_TRUE(edge_vary_serialize("accept\tencoding", mock_lookup, NULL, out, sizeof(out)));
    // A leading comma is an empty element: skipped, and the rest still serialises.
    char a[128];
    char b[128];
    TEST_ASSERT_TRUE(edge_vary_serialize(",accept", mock_lookup, NULL, a, sizeof(a)));
    TEST_ASSERT_TRUE(edge_vary_serialize("accept", mock_lookup, NULL, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING(b, a);
    // Nothing but separators serialises to an empty key rather than failing.
    TEST_ASSERT_TRUE(edge_vary_serialize(", ,\t,", mock_lookup, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static int g_evicted;
static char g_evicted_key[PC_EDGE_KEY_MAX];
static void count_evict(void *ctx, const EdgeEntry *e)
{
    (void)ctx;
    g_evicted++;
    memcpy(g_evicted_key, e->key, sizeof(g_evicted_key));
}

// The L2 write-back hook is offered the LRU victim, but only when that victim actually holds
// something: a transient passthrough slot (empty key) is recycled silently.
static void test_store_evict_hook_skips_empty_victim()
{
    edge_store_init(&g_store);
    g_store.on_evict = count_evict;
    g_store.evict_ctx = NULL;
    g_evicted = 0;
    g_evicted_key[0] = '\0';

    // Fill every slot, then force one more allocation so the LRU tail is recycled.
    char key[64];
    for (uint16_t i = 0; i < PC_EDGE_CACHE_SLOTS; i++)
    {
        snprintf(key, sizeof(key), "GET\nh\n/e%u", (unsigned)i);
        TEST_ASSERT_NOT_NULL(edge_store_alloc(&g_store, key, ""));
    }
    TEST_ASSERT_NOT_NULL(edge_store_alloc(&g_store, "GET\nh\n/overflow", ""));
    TEST_ASSERT_EQUAL_INT(1, g_evicted); // the populated victim was handed over
    TEST_ASSERT_EQUAL_STRING("GET\nh\n/e0", g_evicted_key);

    // Now blank the next victim's key (a passthrough slot) - it must be recycled without a callback.
    uint16_t victim = g_store.lru_tail;
    g_store.entries[victim].key[0] = '\0';
    g_evicted = 0;
    TEST_ASSERT_NOT_NULL(edge_store_alloc(&g_store, "GET\nh\n/overflow2", ""));
    TEST_ASSERT_EQUAL_INT(0, g_evicted);

    g_store.on_evict = NULL; // leave the shared store as the other tests expect it
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_header_value_found);
    RUN_TEST(test_header_value_case_insensitive_and_ows_trim);
    RUN_TEST(test_header_value_absent_and_too_small);
    RUN_TEST(test_http_date_all_three_formats);
    RUN_TEST(test_http_date_epoch_zero_and_invalid);
    RUN_TEST(test_freshness_lifetime_precedence);
    RUN_TEST(test_heuristic_lifetime);
    RUN_TEST(test_initial_and_current_age);
    RUN_TEST(test_is_fresh);
    RUN_TEST(test_key_canon);
    RUN_TEST(test_key_digest_deterministic_and_distinct);
    RUN_TEST(test_vary_serialize_match_and_differ);
    RUN_TEST(test_vary_serialize_star_and_empty);
    RUN_TEST(test_store_alloc_lookup);
    RUN_TEST(test_store_lru_evict);
    RUN_TEST(test_store_ttl_sweep);
    RUN_TEST(test_store_purge);
    RUN_TEST(test_store_free_entry);
    RUN_TEST(test_store_find_vary);
    RUN_TEST(test_entry_freshness_resolution);
    RUN_TEST(test_storeability);
    RUN_TEST(test_build_conditional);
    RUN_TEST(test_apply_304);
    RUN_TEST(test_range_explicit_and_open_ended);
    RUN_TEST(test_range_suffix);
    RUN_TEST(test_range_unsatisfiable);
    RUN_TEST(test_range_ignored_forms);
    RUN_TEST(test_header_value_null_guards);
    RUN_TEST(test_header_value_overflow_fails_whole_lookup);
    RUN_TEST(test_header_value_colonless_line_skipped);
    RUN_TEST(test_header_value_lf_only_and_htab_ows);
    RUN_TEST(test_header_value_unterminated_blocks);
    RUN_TEST(test_http_date_null_and_length_bounds);
    RUN_TEST(test_http_date_field_failures);
    RUN_TEST(test_http_date_asctime_field_failures);
    RUN_TEST(test_http_date_field_range_checks);
    RUN_TEST(test_http_date_rfc850_year_windows);
    RUN_TEST(test_http_date_pre_epoch_and_year_zero);
    RUN_TEST(test_heuristic_and_initial_age_edges);
    RUN_TEST(test_key_canon_null_guards);
    RUN_TEST(test_key_canon_overflow_at_each_append);
    RUN_TEST(test_key_canon_query_requested_but_empty);
    RUN_TEST(test_vary_serialize_null_out_and_null_lookup);
    RUN_TEST(test_vary_serialize_overflow_at_each_append);
    RUN_TEST(test_vary_serialize_long_name_and_separator_runs);
    RUN_TEST(test_store_alloc_key_too_long);
    RUN_TEST(test_store_alloc_null_and_oversize_vary_key);
    RUN_TEST(test_store_alloc_no_free_slot_and_empty_lru);
    RUN_TEST(test_store_purge_prefix_key_without_a_path);
    RUN_TEST(test_store_find_skips_unserializable_variant);
    RUN_TEST(test_store_free_entry_foreign_pointer);
    RUN_TEST(test_storeability_null_method);
    RUN_TEST(test_build_conditional_guards_and_overflow_points);
    RUN_TEST(test_apply_304_date_expires_and_age);
    RUN_TEST(test_apply_304_non_numeric_age_and_oversize_validators);
    RUN_TEST(test_apply_304_reuses_stored_last_modified);
    RUN_TEST(test_http_date_month_prefix_near_miss);
    RUN_TEST(test_http_date_asctime_no_space_at_all);
    RUN_TEST(test_header_value_all_whitespace);
    RUN_TEST(test_vary_serialize_space_tab_and_empty_elements);
    RUN_TEST(test_store_evict_hook_skips_empty_victim);
    return UNITY_END();
}
