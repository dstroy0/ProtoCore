// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
    (void)cache_control_build(g_buf, sizeof(g_buf), cc);
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
    cache_control_init(&cc);
    cc.s_maxage = 600;
    cc.max_age = 60;
    TEST_ASSERT_EQUAL_INT(600, (int)cache_freshness_lifetime(&cc, PROTO_TRUE, 3600));
    TEST_ASSERT_EQUAL_INT(60, (int)cache_freshness_lifetime(&cc, PROTO_FALSE, 3600));

    // Bullet 2 beats bullet 3.
    cache_control_init(&cc);
    cc.max_age = 60;
    TEST_ASSERT_EQUAL_INT(60, (int)cache_freshness_lifetime(&cc, PROTO_TRUE, 3600));
    TEST_ASSERT_EQUAL_INT(60, (int)cache_freshness_lifetime(&cc, PROTO_FALSE, 3600));

    // Bullet 3 applies when neither directive is present.
    cache_control_init(&cc);
    TEST_ASSERT_EQUAL_INT(3600, (int)cache_freshness_lifetime(&cc, PROTO_TRUE, 3600));

    // Bullet 4: nothing explicit, so the caller is told to apply a heuristic (sec 4.2.2).
    cache_control_init(&cc);
    TEST_ASSERT_EQUAL_INT(-1, (int)cache_freshness_lifetime(&cc, PROTO_TRUE, -1));
    TEST_ASSERT_EQUAL_INT(-1, (int)cache_freshness_lifetime(&cc, PROTO_FALSE, -1));

    // max-age=0 is present, not absent: it means "stale immediately", never "fall through".
    cache_control_init(&cc);
    cc.max_age = 0;
    TEST_ASSERT_EQUAL_INT(0, (int)cache_freshness_lifetime(&cc, PROTO_FALSE, 3600));

    // s-maxage=0 on a shared cache, likewise.
    cache_control_init(&cc);
    cc.s_maxage = 0;
    cc.max_age = 60;
    TEST_ASSERT_EQUAL_INT(0, (int)cache_freshness_lifetime(&cc, PROTO_TRUE, 3600));
}

// sec 1.2.2: "delta-seconds = 1*DIGIT ... a non-negative integer". Absent is -1 here, so a fresh
// set carries no directive at all and builds to nothing.
void test_init_is_an_empty_directive_set(void)
{
    protocore_cache_control cc;
    memset(&cc, 0x5A, sizeof(cc));
    cache_control_init(&cc);

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
    TEST_ASSERT_EQUAL_UINT(0u, cache_control_build(g_buf, sizeof(g_buf), &cc));
}

// sec 5.2: "Cache-Control = #cache-directive"; the RFC 9110 sec 5.6.1 list rule is comma-separated
// with optional whitespace, and sec 5.2.1.1 forbids the quoted form when generating
// ("'max-age=5' not 'max-age=\"5\"'"). Each expected string spells the directive names sec 5.2.1 /
// 5.2.2, RFC 8246 and RFC 5861 define, in this module's documented stable emission order (the list
// rule itself fixes the separator, not the order).
void test_build_emits_the_grammar(void)
{
    protocore_cache_control cc;

    cache_control_init(&cc);
    cc.no_store = PROTO_TRUE;
    TEST_ASSERT_EQUAL_STRING("no-store", build(&cc));

    cache_control_init(&cc);
    cc.cc_public = PROTO_TRUE;
    cc.max_age = 31536000;
    cc.cc_immutable = PROTO_TRUE;
    TEST_ASSERT_EQUAL_STRING("public, max-age=31536000, immutable", build(&cc));

    cache_control_init(&cc);
    cc.cc_private = PROTO_TRUE;
    cc.no_cache = PROTO_TRUE;
    cc.must_revalidate = PROTO_TRUE;
    TEST_ASSERT_EQUAL_STRING("private, no-cache, must-revalidate", build(&cc));

    cache_control_init(&cc);
    cc.cc_public = PROTO_TRUE;
    cc.max_age = 0;
    TEST_ASSERT_EQUAL_STRING("public, max-age=0", build(&cc));

    cache_control_init(&cc);
    cc.no_transform = PROTO_TRUE;
    cc.must_understand = PROTO_TRUE;
    cc.proxy_revalidate = PROTO_TRUE;
    TEST_ASSERT_EQUAL_STRING("proxy-revalidate, no-transform, must-understand", build(&cc));

    // The RFC 5861 extensions.
    cache_control_init(&cc);
    cc.max_age = 60;
    cc.stale_while_revalidate = 30;
    cc.stale_if_error = 86400;
    TEST_ASSERT_EQUAL_STRING("max-age=60, stale-while-revalidate=30, stale-if-error=86400", build(&cc));

    // The request-side directives of sec 5.2.1.
    cache_control_init(&cc);
    cc.only_if_cached = PROTO_TRUE;
    cc.max_stale = 10;
    cc.min_fresh = 5;
    TEST_ASSERT_EQUAL_STRING("only-if-cached, max-stale=10, min-fresh=5", build(&cc));

    // sec 5.2.1.2: "If no value is assigned to max-stale, then the client will accept a stale
    // response of any age" - the bare token, which this module spells as -2.
    cache_control_init(&cc);
    cc.max_stale = -2;
    TEST_ASSERT_EQUAL_STRING("max-stale", build(&cc));
}

// The byte count returned is the length of the value it wrote, and the value is NUL-terminated at
// exactly that offset.
void test_build_reports_its_own_length(void)
{
    protocore_cache_control cc;
    cache_control_init(&cc);
    cc.cc_public = PROTO_TRUE;
    cc.max_age = 31536000;
    cc.cc_immutable = PROTO_TRUE;

    size_t n = cache_control_build(g_buf, sizeof(g_buf), &cc);
    TEST_ASSERT_EQUAL_UINT(strlen("public, max-age=31536000, immutable"), n);
    TEST_ASSERT_EQUAL_CHAR('\0', g_buf[n]);
}

// sec 5.2: directives are "identified by a token, to be compared case-insensitively", and the
// argument "can use both token and quoted-string syntax", with the list rule allowing OWS.
void test_parse_is_tolerant_as_sec_5_2_requires(void)
{
    protocore_cache_control cc;
    static const char MIXED[] = "PUBLIC,  Max-Age=\"600\" , IMMUTABLE";
    TEST_ASSERT_TRUE(cache_control_parse(MIXED, sizeof(MIXED) - 1, &cc));
    TEST_ASSERT_TRUE(cc.cc_public);
    TEST_ASSERT_TRUE(cc.cc_immutable);
    TEST_ASSERT_EQUAL_INT32(600, cc.max_age);

    // sec 5.2.3: a directive a cache does not understand is ignored, not an error.
    static const char UNKNOWN[] = "surrogate-control=foo, max-age=42, x-nonsense";
    TEST_ASSERT_TRUE(cache_control_parse(UNKNOWN, sizeof(UNKNOWN) - 1, &cc));
    TEST_ASSERT_EQUAL_INT32(42, cc.max_age);
    TEST_ASSERT_FALSE(cc.cc_public);

    // Nothing known at all: false, and the set is left at its defaults.
    static const char NONE[] = "x-nonsense, another-thing=1";
    TEST_ASSERT_FALSE(cache_control_parse(NONE, sizeof(NONE) - 1, &cc));
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_age);

    // A NULL value is refused rather than read.
    TEST_ASSERT_FALSE(cache_control_parse(NULL, 10, &cc));
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_age);
}

// sec 5.2.1.2: max-stale with a value versus max-stale bare are different requests, so the parser
// has to keep them apart. -1 absent, -2 bare, >= 0 the delta-seconds.
void test_parse_separates_bare_max_stale_from_valued(void)
{
    protocore_cache_control cc;

    static const char BARE[] = "max-stale";
    TEST_ASSERT_TRUE(cache_control_parse(BARE, sizeof(BARE) - 1, &cc));
    TEST_ASSERT_EQUAL_INT32(-2, cc.max_stale);

    static const char VALUED[] = "max-stale=10";
    TEST_ASSERT_TRUE(cache_control_parse(VALUED, sizeof(VALUED) - 1, &cc));
    TEST_ASSERT_EQUAL_INT32(10, cc.max_stale);

    static const char OTHER[] = "min-fresh=5";
    TEST_ASSERT_TRUE(cache_control_parse(OTHER, sizeof(OTHER) - 1, &cc));
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_stale);
    TEST_ASSERT_EQUAL_INT32(5, cc.min_fresh);
}

// sec 1.2.2: "If a cache receives a delta-seconds value greater than the greatest integer it can
// represent ... the cache MUST consider the value to be 2147483648 (2^31) or the greatest positive
// integer it can conveniently represent." The field is int32_t, so that ceiling is 2147483647.
void test_delta_seconds_saturates_rather_than_wrapping(void)
{
    protocore_cache_control cc;

    static const char HUGE[] = "max-age=99999999999999999999";
    TEST_ASSERT_TRUE(cache_control_parse(HUGE, sizeof(HUGE) - 1, &cc));
    TEST_ASSERT_EQUAL_INT32(2147483647, cc.max_age);

    // 2^31-1 itself survives unchanged, so the clamp is not eating a legal value.
    static const char TOP[] = "max-age=2147483647";
    TEST_ASSERT_TRUE(cache_control_parse(TOP, sizeof(TOP) - 1, &cc));
    TEST_ASSERT_EQUAL_INT32(2147483647, cc.max_age);

    // 2^31 itself is one past what the field holds and saturates to the same ceiling.
    static const char OVER[] = "max-age=2147483648";
    TEST_ASSERT_TRUE(cache_control_parse(OVER, sizeof(OVER) - 1, &cc));
    TEST_ASSERT_EQUAL_INT32(2147483647, cc.max_age);

    // "max-age" with no digits carries no delta-seconds at all.
    static const char NODIGITS[] = "max-age=";
    TEST_ASSERT_TRUE(cache_control_parse(NODIGITS, sizeof(NODIGITS) - 1, &cc));
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_age);
}

// Build then parse recovers the same directive set: the identity every downstream cache relies on.
void test_build_parse_round_trip(void)
{
    protocore_cache_control out;
    cache_control_init(&out);
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

    size_t n = cache_control_build(g_buf, sizeof(g_buf), &out);
    TEST_ASSERT_TRUE(n > 0);

    protocore_cache_control in;
    TEST_ASSERT_TRUE(cache_control_parse(g_buf, n, &in));
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
    cache_immutable_asset(&cc, 31536000u);
    TEST_ASSERT_EQUAL_STRING("public, max-age=31536000, immutable", build(&cc));

    cache_revalidatable(&cc, 60u, 30);
    TEST_ASSERT_EQUAL_STRING("public, max-age=60, stale-while-revalidate=30", build(&cc));

    // A negative stale-while-revalidate means "do not emit the directive".
    cache_revalidatable(&cc, 60u, -1);
    TEST_ASSERT_EQUAL_STRING("public, max-age=60", build(&cc));

    cache_no_store(&cc);
    TEST_ASSERT_EQUAL_STRING("no-store", build(&cc));

    cache_shared(&cc, 60u, 600u);
    TEST_ASSERT_EQUAL_STRING("public, max-age=60, s-maxage=600", build(&cc));
    TEST_ASSERT_EQUAL_INT(600, (int)cache_freshness_lifetime(&cc, PROTO_TRUE, -1));
    TEST_ASSERT_EQUAL_INT(60, (int)cache_freshness_lifetime(&cc, PROTO_FALSE, -1));

    // Each preset starts from an empty set, so a reused struct carries nothing across.
    cache_immutable_asset(&cc, 10u);
    cache_no_store(&cc);
    TEST_ASSERT_FALSE(cc.cc_public);
    TEST_ASSERT_FALSE(cc.cc_immutable);
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_age);
}

// A destination too small for the whole value writes nothing: a truncated Cache-Control is a
// different set of directives, and a cache would act on it.
void test_build_refuses_a_short_buffer(void)
{
    protocore_cache_control cc;
    cache_immutable_asset(&cc, 31536000u);
    const size_t want = strlen("public, max-age=31536000, immutable");

    char small[8];
    small[0] = 'x';
    TEST_ASSERT_EQUAL_UINT(0u, cache_control_build(small, sizeof(small), &cc));

    // One octet short of value + NUL is still a refusal; exactly enough is not.
    char exact[64];
    TEST_ASSERT_EQUAL_UINT(0u, cache_control_build(exact, want, &cc));
    TEST_ASSERT_EQUAL_UINT(want, cache_control_build(exact, want + 1, &cc));
    TEST_ASSERT_EQUAL_STRING("public, max-age=31536000, immutable", exact);

    TEST_ASSERT_EQUAL_UINT(0u, cache_control_build(NULL, 64, &cc));
    TEST_ASSERT_EQUAL_UINT(0u, cache_control_build(exact, 0, &cc));
}
