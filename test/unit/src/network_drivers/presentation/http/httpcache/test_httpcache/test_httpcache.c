// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Cache-Control directive builder / parser / freshness helper
// (network_drivers/presentation/http/httpcache/httpcache.h).
//
// RFC 9111 sec 4.2.1 states the freshness-lifetime rule as an ordered "first match" list, and every
// cache between this origin and the client evaluates that same list. test_rfc9111_4_2_1_first_match
// is the load-bearing case: it walks the four bullets in order and pins which one wins where two
// apply, because a s-maxage that loses to max-age on a shared cache silently doubles the TTL a CDN
// keeps. The parser cases lean on sec 5.2's "identified by a token, to be compared
// case-insensitively, and have an optional argument that can use both token and quoted-string
// syntax", which is exactly the tolerance a cache has to have.

#include "network_drivers/presentation/http/httpcache/httpcache.h"
#include <string.h>

#include <unity.h>

static uint8_t httpcache_work[16]; // the borrow an entry takes; Httpcache never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_buf[256];

static const char *build(const protocore_cache_control *cc)
{
    g_buf[0] = 0;
    HttpcacheV.control_build_args.buf = g_buf;
    HttpcacheV.control_build_args.cap = sizeof(g_buf);
    HttpcacheV.control_build_args.cc = cc;
    Httpcache.control_build(httpcache_work);
    (void)HttpcacheV.n;
    return g_buf;
}

// RFC 9111 sec 4.2.1, the rules "and using the first match":
//   1. shared cache + s-maxage present -> its value
//   2. max-age present                 -> its value
//   3. Expires minus Date              -> that difference
//   4. otherwise no explicit expiration
void test_rfc9111_4_2_1_first_match(void)
{
    protocore_cache_control cc;

    // Bullet 1 beats bullet 2, but only when the cache is shared.
    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.s_maxage = 600;
    cc.max_age = 60;
    HttpcacheV.freshness_lifetime_args.cc = &cc;
    HttpcacheV.freshness_lifetime_args.shared = PROTO_TRUE;
    HttpcacheV.freshness_lifetime_args.expires_minus_date = 3600;
    Httpcache.freshness_lifetime(httpcache_work);
    TEST_ASSERT_EQUAL_INT(600, (int)HttpcacheV.value);
    HttpcacheV.freshness_lifetime_args.cc = &cc;
    HttpcacheV.freshness_lifetime_args.shared = PROTO_FALSE;
    HttpcacheV.freshness_lifetime_args.expires_minus_date = 3600;
    Httpcache.freshness_lifetime(httpcache_work);
    TEST_ASSERT_EQUAL_INT(60, (int)HttpcacheV.value);

    // Bullet 2 beats bullet 3.
    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.max_age = 60;
    HttpcacheV.freshness_lifetime_args.cc = &cc;
    HttpcacheV.freshness_lifetime_args.shared = PROTO_TRUE;
    HttpcacheV.freshness_lifetime_args.expires_minus_date = 3600;
    Httpcache.freshness_lifetime(httpcache_work);
    TEST_ASSERT_EQUAL_INT(60, (int)HttpcacheV.value);
    HttpcacheV.freshness_lifetime_args.cc = &cc;
    HttpcacheV.freshness_lifetime_args.shared = PROTO_FALSE;
    HttpcacheV.freshness_lifetime_args.expires_minus_date = 3600;
    Httpcache.freshness_lifetime(httpcache_work);
    TEST_ASSERT_EQUAL_INT(60, (int)HttpcacheV.value);

    // Bullet 3 applies when neither directive is present.
    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    HttpcacheV.freshness_lifetime_args.cc = &cc;
    HttpcacheV.freshness_lifetime_args.shared = PROTO_TRUE;
    HttpcacheV.freshness_lifetime_args.expires_minus_date = 3600;
    Httpcache.freshness_lifetime(httpcache_work);
    TEST_ASSERT_EQUAL_INT(3600, (int)HttpcacheV.value);

    // Bullet 4: nothing explicit, so the caller is told to apply a heuristic (sec 4.2.2).
    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    HttpcacheV.freshness_lifetime_args.cc = &cc;
    HttpcacheV.freshness_lifetime_args.shared = PROTO_TRUE;
    HttpcacheV.freshness_lifetime_args.expires_minus_date = -1;
    Httpcache.freshness_lifetime(httpcache_work);
    TEST_ASSERT_EQUAL_INT(-1, (int)HttpcacheV.value);
    HttpcacheV.freshness_lifetime_args.cc = &cc;
    HttpcacheV.freshness_lifetime_args.shared = PROTO_FALSE;
    HttpcacheV.freshness_lifetime_args.expires_minus_date = -1;
    Httpcache.freshness_lifetime(httpcache_work);
    TEST_ASSERT_EQUAL_INT(-1, (int)HttpcacheV.value);

    // max-age=0 is present, not absent: it means "stale immediately", never "fall through".
    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.max_age = 0;
    HttpcacheV.freshness_lifetime_args.cc = &cc;
    HttpcacheV.freshness_lifetime_args.shared = PROTO_FALSE;
    HttpcacheV.freshness_lifetime_args.expires_minus_date = 3600;
    Httpcache.freshness_lifetime(httpcache_work);
    TEST_ASSERT_EQUAL_INT(0, (int)HttpcacheV.value);

    // s-maxage=0 on a shared cache, likewise.
    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.s_maxage = 0;
    cc.max_age = 60;
    HttpcacheV.freshness_lifetime_args.cc = &cc;
    HttpcacheV.freshness_lifetime_args.shared = PROTO_TRUE;
    HttpcacheV.freshness_lifetime_args.expires_minus_date = 3600;
    Httpcache.freshness_lifetime(httpcache_work);
    TEST_ASSERT_EQUAL_INT(0, (int)HttpcacheV.value);
}

// sec 1.2.2: "delta-seconds = 1*DIGIT ... a non-negative integer". Absent is -1 here, so a fresh
// set carries no directive at all and builds to nothing.
void test_init_is_an_empty_directive_set(void)
{
    protocore_cache_control cc;
    memset(&cc, 0x5A, sizeof(cc));
    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);

    TEST_ASSERT_FALSE(cc.cc_public);
    TEST_ASSERT_FALSE(cc.cc_private);
    TEST_ASSERT_FALSE(cc.no_store);
    TEST_ASSERT_FALSE(cc.no_cache);
    TEST_ASSERT_FALSE(cc.no_transform);
    TEST_ASSERT_FALSE(cc.must_revalidate);
    TEST_ASSERT_FALSE(cc.proxy_revalidate);
    TEST_ASSERT_FALSE(cc.must_understand);
    TEST_ASSERT_FALSE(cc.cc_immutable);
    TEST_ASSERT_FALSE(cc.only_if_cached);
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_age);
    TEST_ASSERT_EQUAL_INT32(-1, cc.s_maxage);
    TEST_ASSERT_EQUAL_INT32(-1, cc.stale_while_revalidate);
    TEST_ASSERT_EQUAL_INT32(-1, cc.stale_if_error);
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_stale);
    TEST_ASSERT_EQUAL_INT32(-1, cc.min_fresh);

    // Nothing set means nothing to send: 0 rather than an empty header value.
    HttpcacheV.control_build_args.buf = g_buf;
    HttpcacheV.control_build_args.cap = sizeof(g_buf);
    HttpcacheV.control_build_args.cc = &cc;
    Httpcache.control_build(httpcache_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpcacheV.n);
}

// sec 5.2: "Cache-Control = #cache-directive"; the RFC 9110 sec 5.6.1 list rule is comma-separated
// with optional whitespace, and sec 5.2.1.1 forbids the quoted form when generating
// ("'max-age=5' not 'max-age=\"5\"'"). Each expected string spells the directive names sec 5.2.1 /
// 5.2.2, RFC 8246 and RFC 5861 define, in this module's documented stable emission order (the list
// rule itself fixes the separator, not the order).
void test_build_emits_the_grammar(void)
{
    protocore_cache_control cc;

    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.no_store = PROTO_TRUE;
    TEST_ASSERT_EQUAL_STRING("no-store", build(&cc));

    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.cc_public = PROTO_TRUE;
    cc.max_age = 31536000;
    cc.cc_immutable = PROTO_TRUE;
    TEST_ASSERT_EQUAL_STRING("public, max-age=31536000, immutable", build(&cc));

    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.cc_private = PROTO_TRUE;
    cc.no_cache = PROTO_TRUE;
    cc.must_revalidate = PROTO_TRUE;
    TEST_ASSERT_EQUAL_STRING("private, no-cache, must-revalidate", build(&cc));

    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.cc_public = PROTO_TRUE;
    cc.max_age = 0;
    TEST_ASSERT_EQUAL_STRING("public, max-age=0", build(&cc));

    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.no_transform = PROTO_TRUE;
    cc.must_understand = PROTO_TRUE;
    cc.proxy_revalidate = PROTO_TRUE;
    TEST_ASSERT_EQUAL_STRING("proxy-revalidate, no-transform, must-understand", build(&cc));

    // The RFC 5861 extensions.
    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.max_age = 60;
    cc.stale_while_revalidate = 30;
    cc.stale_if_error = 86400;
    TEST_ASSERT_EQUAL_STRING("max-age=60, stale-while-revalidate=30, stale-if-error=86400", build(&cc));

    // The request-side directives of sec 5.2.1.
    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.only_if_cached = PROTO_TRUE;
    cc.max_stale = 10;
    cc.min_fresh = 5;
    TEST_ASSERT_EQUAL_STRING("only-if-cached, max-stale=10, min-fresh=5", build(&cc));

    // sec 5.2.1.2: "If no value is assigned to max-stale, then the client will accept a stale
    // response of any age" - the bare token, which this module spells as -2.
    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.max_stale = -2;
    TEST_ASSERT_EQUAL_STRING("max-stale", build(&cc));
}

// The byte count returned is the length of the value it wrote, and the value is NUL-terminated at
// exactly that offset.
void test_build_reports_its_own_length(void)
{
    protocore_cache_control cc;
    HttpcacheV.control_init_args.cc = &cc;
    Httpcache.control_init(httpcache_work);
    cc.cc_public = PROTO_TRUE;
    cc.max_age = 31536000;
    cc.cc_immutable = PROTO_TRUE;

    HttpcacheV.control_build_args.buf = g_buf;
    HttpcacheV.control_build_args.cap = sizeof(g_buf);
    HttpcacheV.control_build_args.cc = &cc;
    Httpcache.control_build(httpcache_work);
    size_t n = HttpcacheV.n;
    TEST_ASSERT_EQUAL_UINT(strlen("public, max-age=31536000, immutable"), n);
    TEST_ASSERT_EQUAL_CHAR('\0', g_buf[n]);
}

// sec 5.2: directives are "identified by a token, to be compared case-insensitively", and the
// argument "can use both token and quoted-string syntax", with the list rule allowing OWS.
void test_parse_is_tolerant_as_sec_5_2_requires(void)
{
    protocore_cache_control cc;
    static const char MIXED[] = "PUBLIC,  Max-Age=\"600\" , IMMUTABLE";
    HttpcacheV.control_parse_args.s = MIXED;
    HttpcacheV.control_parse_args.len = sizeof(MIXED) - 1;
    HttpcacheV.control_parse_args.cc = &cc;
    Httpcache.control_parse(httpcache_work);
    TEST_ASSERT_TRUE(HttpcacheV.ok);
    TEST_ASSERT_TRUE(cc.cc_public);
    TEST_ASSERT_TRUE(cc.cc_immutable);
    TEST_ASSERT_EQUAL_INT32(600, cc.max_age);

    // sec 5.2.3: a directive a cache does not understand is ignored, not an error.
    static const char UNKNOWN[] = "surrogate-control=foo, max-age=42, x-nonsense";
    HttpcacheV.control_parse_args.s = UNKNOWN;
    HttpcacheV.control_parse_args.len = sizeof(UNKNOWN) - 1;
    HttpcacheV.control_parse_args.cc = &cc;
    Httpcache.control_parse(httpcache_work);
    TEST_ASSERT_TRUE(HttpcacheV.ok);
    TEST_ASSERT_EQUAL_INT32(42, cc.max_age);
    TEST_ASSERT_FALSE(cc.cc_public);

    // Nothing known at all: false, and the set is left at its defaults.
    static const char NONE[] = "x-nonsense, another-thing=1";
    HttpcacheV.control_parse_args.s = NONE;
    HttpcacheV.control_parse_args.len = sizeof(NONE) - 1;
    HttpcacheV.control_parse_args.cc = &cc;
    Httpcache.control_parse(httpcache_work);
    TEST_ASSERT_FALSE(HttpcacheV.ok);
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_age);

    // A NULL value is refused rather than read.
    HttpcacheV.control_parse_args.s = NULL;
    HttpcacheV.control_parse_args.len = 10;
    HttpcacheV.control_parse_args.cc = &cc;
    Httpcache.control_parse(httpcache_work);
    TEST_ASSERT_FALSE(HttpcacheV.ok);
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_age);
}

// sec 5.2.1.2: max-stale with a value versus max-stale bare are different requests, so the parser
// has to keep them apart. -1 absent, -2 bare, >= 0 the delta-seconds.
void test_parse_separates_bare_max_stale_from_valued(void)
{
    protocore_cache_control cc;

    static const char BARE[] = "max-stale";
    HttpcacheV.control_parse_args.s = BARE;
    HttpcacheV.control_parse_args.len = sizeof(BARE) - 1;
    HttpcacheV.control_parse_args.cc = &cc;
    Httpcache.control_parse(httpcache_work);
    TEST_ASSERT_TRUE(HttpcacheV.ok);
    TEST_ASSERT_EQUAL_INT32(-2, cc.max_stale);

    static const char VALUED[] = "max-stale=10";
    HttpcacheV.control_parse_args.s = VALUED;
    HttpcacheV.control_parse_args.len = sizeof(VALUED) - 1;
    HttpcacheV.control_parse_args.cc = &cc;
    Httpcache.control_parse(httpcache_work);
    TEST_ASSERT_TRUE(HttpcacheV.ok);
    TEST_ASSERT_EQUAL_INT32(10, cc.max_stale);

    static const char OTHER[] = "min-fresh=5";
    HttpcacheV.control_parse_args.s = OTHER;
    HttpcacheV.control_parse_args.len = sizeof(OTHER) - 1;
    HttpcacheV.control_parse_args.cc = &cc;
    Httpcache.control_parse(httpcache_work);
    TEST_ASSERT_TRUE(HttpcacheV.ok);
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_stale);
    TEST_ASSERT_EQUAL_INT32(5, cc.min_fresh);
}

// sec 1.2.2: "If a cache receives a delta-seconds value greater than the greatest integer it can
// represent ... the cache MUST consider the value to be 2147483648 (2^31) or the greatest positive
// integer it can conveniently represent." The field is int32_t, so that ceiling is 2147483647.
void test_delta_seconds_saturates_rather_than_wrapping(void)
{
    protocore_cache_control cc;

    static const char OVERSIZE[] = "max-age=99999999999999999999";
    HttpcacheV.control_parse_args.s = OVERSIZE;
    HttpcacheV.control_parse_args.len = sizeof(OVERSIZE) - 1;
    HttpcacheV.control_parse_args.cc = &cc;
    Httpcache.control_parse(httpcache_work);
    TEST_ASSERT_TRUE(HttpcacheV.ok);
    TEST_ASSERT_EQUAL_INT32(2147483647, cc.max_age);

    // 2^31-1 itself survives unchanged, so the clamp is not eating a legal value.
    static const char TOP[] = "max-age=2147483647";
    HttpcacheV.control_parse_args.s = TOP;
    HttpcacheV.control_parse_args.len = sizeof(TOP) - 1;
    HttpcacheV.control_parse_args.cc = &cc;
    Httpcache.control_parse(httpcache_work);
    TEST_ASSERT_TRUE(HttpcacheV.ok);
    TEST_ASSERT_EQUAL_INT32(2147483647, cc.max_age);

    // 2^31 itself is one past what the field holds and saturates to the same ceiling.
    static const char OVER[] = "max-age=2147483648";
    HttpcacheV.control_parse_args.s = OVER;
    HttpcacheV.control_parse_args.len = sizeof(OVER) - 1;
    HttpcacheV.control_parse_args.cc = &cc;
    Httpcache.control_parse(httpcache_work);
    TEST_ASSERT_TRUE(HttpcacheV.ok);
    TEST_ASSERT_EQUAL_INT32(2147483647, cc.max_age);

    // "max-age" with no digits carries no delta-seconds at all.
    static const char NODIGITS[] = "max-age=";
    HttpcacheV.control_parse_args.s = NODIGITS;
    HttpcacheV.control_parse_args.len = sizeof(NODIGITS) - 1;
    HttpcacheV.control_parse_args.cc = &cc;
    Httpcache.control_parse(httpcache_work);
    TEST_ASSERT_TRUE(HttpcacheV.ok);
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_age);
}

// Build then parse recovers the same directive set: the identity every downstream cache relies on.
void test_build_parse_round_trip(void)
{
    protocore_cache_control out;
    HttpcacheV.control_init_args.cc = &out;
    Httpcache.control_init(httpcache_work);
    out.cc_public = PROTO_TRUE;
    out.no_transform = PROTO_TRUE;
    out.must_revalidate = PROTO_TRUE;
    out.proxy_revalidate = PROTO_TRUE;
    out.must_understand = PROTO_TRUE;
    out.cc_immutable = PROTO_TRUE;
    out.max_age = 300;
    out.s_maxage = 900;
    out.stale_while_revalidate = 60;
    out.stale_if_error = 120;

    HttpcacheV.control_build_args.buf = g_buf;
    HttpcacheV.control_build_args.cap = sizeof(g_buf);
    HttpcacheV.control_build_args.cc = &out;
    Httpcache.control_build(httpcache_work);
    size_t n = HttpcacheV.n;
    TEST_ASSERT_TRUE(n > 0);

    protocore_cache_control in;
    HttpcacheV.control_parse_args.s = g_buf;
    HttpcacheV.control_parse_args.len = n;
    HttpcacheV.control_parse_args.cc = &in;
    Httpcache.control_parse(httpcache_work);
    TEST_ASSERT_TRUE(HttpcacheV.ok);
    TEST_ASSERT_TRUE(in.cc_public);
    TEST_ASSERT_TRUE(in.no_transform);
    TEST_ASSERT_TRUE(in.must_revalidate);
    TEST_ASSERT_TRUE(in.proxy_revalidate);
    TEST_ASSERT_TRUE(in.must_understand);
    TEST_ASSERT_TRUE(in.cc_immutable);
    TEST_ASSERT_EQUAL_INT32(300, in.max_age);
    TEST_ASSERT_EQUAL_INT32(900, in.s_maxage);
    TEST_ASSERT_EQUAL_INT32(60, in.stale_while_revalidate);
    TEST_ASSERT_EQUAL_INT32(120, in.stale_if_error);
    TEST_ASSERT_FALSE(in.no_store);
    TEST_ASSERT_FALSE(in.cc_private);
}

// The presets are the same directive sets a route would assemble by hand.
void test_presets_match_their_documented_directives(void)
{
    protocore_cache_control cc;

    // RFC 8246: "immutable" tells a cache not to revalidate while fresh.
    HttpcacheV.immutable_asset_args.cc = &cc;
    HttpcacheV.immutable_asset_args.max_age = 31536000u;
    Httpcache.immutable_asset(httpcache_work);
    TEST_ASSERT_EQUAL_STRING("public, max-age=31536000, immutable", build(&cc));

    HttpcacheV.revalidatable_args.cc = &cc;
    HttpcacheV.revalidatable_args.max_age = 60u;
    HttpcacheV.revalidatable_args.stale_while_revalidate = 30;
    Httpcache.revalidatable(httpcache_work);
    TEST_ASSERT_EQUAL_STRING("public, max-age=60, stale-while-revalidate=30", build(&cc));

    // A negative stale-while-revalidate means "do not emit the directive".
    HttpcacheV.revalidatable_args.cc = &cc;
    HttpcacheV.revalidatable_args.max_age = 60u;
    HttpcacheV.revalidatable_args.stale_while_revalidate = -1;
    Httpcache.revalidatable(httpcache_work);
    TEST_ASSERT_EQUAL_STRING("public, max-age=60", build(&cc));

    HttpcacheV.no_store_args.cc = &cc;
    Httpcache.no_store(httpcache_work);
    TEST_ASSERT_EQUAL_STRING("no-store", build(&cc));

    HttpcacheV.shared_args.cc = &cc;
    HttpcacheV.shared_args.max_age = 60u;
    HttpcacheV.shared_args.s_maxage = 600u;
    Httpcache.shared(httpcache_work);
    TEST_ASSERT_EQUAL_STRING("public, max-age=60, s-maxage=600", build(&cc));
    HttpcacheV.freshness_lifetime_args.cc = &cc;
    HttpcacheV.freshness_lifetime_args.shared = PROTO_TRUE;
    HttpcacheV.freshness_lifetime_args.expires_minus_date = -1;
    Httpcache.freshness_lifetime(httpcache_work);
    TEST_ASSERT_EQUAL_INT(600, (int)HttpcacheV.value);
    HttpcacheV.freshness_lifetime_args.cc = &cc;
    HttpcacheV.freshness_lifetime_args.shared = PROTO_FALSE;
    HttpcacheV.freshness_lifetime_args.expires_minus_date = -1;
    Httpcache.freshness_lifetime(httpcache_work);
    TEST_ASSERT_EQUAL_INT(60, (int)HttpcacheV.value);

    // Each preset starts from an empty set, so a reused struct carries nothing across.
    HttpcacheV.immutable_asset_args.cc = &cc;
    HttpcacheV.immutable_asset_args.max_age = 10u;
    Httpcache.immutable_asset(httpcache_work);
    HttpcacheV.no_store_args.cc = &cc;
    Httpcache.no_store(httpcache_work);
    TEST_ASSERT_FALSE(cc.cc_public);
    TEST_ASSERT_FALSE(cc.cc_immutable);
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_age);
}

// A destination too small for the whole value writes nothing: a truncated Cache-Control is a
// different set of directives, and a cache would act on it.
void test_build_refuses_a_short_buffer(void)
{
    protocore_cache_control cc;
    HttpcacheV.immutable_asset_args.cc = &cc;
    HttpcacheV.immutable_asset_args.max_age = 31536000u;
    Httpcache.immutable_asset(httpcache_work);
    const size_t want = strlen("public, max-age=31536000, immutable");

    char small[8];
    small[0] = 'x';
    HttpcacheV.control_build_args.buf = small;
    HttpcacheV.control_build_args.cap = sizeof(small);
    HttpcacheV.control_build_args.cc = &cc;
    Httpcache.control_build(httpcache_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpcacheV.n);

    // One octet short of value + NUL is still a refusal; exactly enough is not.
    char exact[64];
    HttpcacheV.control_build_args.buf = exact;
    HttpcacheV.control_build_args.cap = want;
    HttpcacheV.control_build_args.cc = &cc;
    Httpcache.control_build(httpcache_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpcacheV.n);
    HttpcacheV.control_build_args.buf = exact;
    HttpcacheV.control_build_args.cap = want + 1;
    HttpcacheV.control_build_args.cc = &cc;
    Httpcache.control_build(httpcache_work);
    TEST_ASSERT_EQUAL_UINT(want, HttpcacheV.n);
    TEST_ASSERT_EQUAL_STRING("public, max-age=31536000, immutable", exact);

    HttpcacheV.control_build_args.buf = NULL;
    HttpcacheV.control_build_args.cap = 64;
    HttpcacheV.control_build_args.cc = &cc;
    Httpcache.control_build(httpcache_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpcacheV.n);
    HttpcacheV.control_build_args.buf = exact;
    HttpcacheV.control_build_args.cap = 0;
    HttpcacheV.control_build_args.cc = &cc;
    Httpcache.control_build(httpcache_work);
    TEST_ASSERT_EQUAL_UINT(0u, HttpcacheV.n);
}
