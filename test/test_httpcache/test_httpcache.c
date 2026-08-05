// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the HTTP Cache-Control helpers (services/web/httpcache): the directive
// builder + presets, the tolerant parser, a build->parse round-trip, and the RFC 9111
// freshness-lifetime calculation. Pure host tests.

#include "services/web/httpcache/httpcache.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// --- presets / builder -----------------------------------------------------

void test_preset_immutable()
{
    pc_cache_control cc;
    cache_immutable_asset(&cc, 31536000u); // 1 year
    char b[96];
    size_t n = cache_control_build(b, sizeof(b), &cc);
    TEST_ASSERT_EQUAL_STRING("public, max-age=31536000, immutable", b);
    TEST_ASSERT_EQUAL_size_t(n, strlen(b));
}

void test_preset_no_store_and_shared_and_revalidatable()
{
    pc_cache_control cc;
    char b[96];

    cache_no_store(&cc);
    cache_control_build(b, sizeof(b), &cc);
    TEST_ASSERT_EQUAL_STRING("no-store", b);

    cache_shared(&cc, 60, 600);
    cache_control_build(b, sizeof(b), &cc);
    TEST_ASSERT_EQUAL_STRING("public, max-age=60, s-maxage=600", b);

    cache_revalidatable(&cc, 300, 60);
    cache_control_build(b, sizeof(b), &cc);
    TEST_ASSERT_EQUAL_STRING("public, max-age=300, stale-while-revalidate=60", b);

    cache_revalidatable(&cc, 300, -1); // swr omitted
    cache_control_build(b, sizeof(b), &cc);
    TEST_ASSERT_EQUAL_STRING("public, max-age=300", b);
}

void test_build_manual_and_edges()
{
    pc_cache_control cc;
    cache_control_init(&cc);
    cc.cc_private = PROTO_TRUE;
    cc.no_cache = PROTO_TRUE;
    cc.max_age = 0;
    cc.must_revalidate = PROTO_TRUE;
    char b[96];
    cache_control_build(b, sizeof(b), &cc);
    TEST_ASSERT_EQUAL_STRING("private, no-cache, max-age=0, must-revalidate", b);

    // empty set -> 0 (nothing to emit)
    cache_control_init(&cc);
    TEST_ASSERT_EQUAL_size_t(0, cache_control_build(b, sizeof(b), &cc));

    // overflow -> 0, and never overruns
    cache_immutable_asset(&cc, 31536000u);
    char tiny[10];
    TEST_ASSERT_EQUAL_size_t(0, cache_control_build(tiny, sizeof(tiny), &cc));
}

// --- parser ----------------------------------------------------------------

void test_parse_response_directives()
{
    pc_cache_control cc;
    const char *s = "public, max-age=31536000, immutable";
    TEST_ASSERT_TRUE(cache_control_parse(s, strlen(s), &cc));
    TEST_ASSERT_TRUE(cc.cc_public);
    TEST_ASSERT_TRUE(cc.cc_immutable);
    TEST_ASSERT_EQUAL_INT32(31536000, cc.max_age);
    TEST_ASSERT_EQUAL_INT32(-1, cc.s_maxage); // absent
}

void test_parse_case_insensitive_and_quoted_and_unknown()
{
    pc_cache_control cc;
    // case-insensitive names, a quoted delta, extra OWS, and an unknown directive to ignore
    const char *s = "  No-Store ,  MAX-AGE=\"3600\" , community=cats , s-maxage = 120 ";
    TEST_ASSERT_TRUE(cache_control_parse(s, strlen(s), &cc));
    TEST_ASSERT_TRUE(cc.no_store);
    TEST_ASSERT_EQUAL_INT32(3600, cc.max_age);
    TEST_ASSERT_EQUAL_INT32(120, cc.s_maxage);
    TEST_ASSERT_FALSE(cc.cc_public);

    // a header of only unknown directives -> false
    const char *u = "foo, bar=1";
    TEST_ASSERT_FALSE(cache_control_parse(u, strlen(u), &cc));
}

void test_parse_request_directives()
{
    pc_cache_control cc;
    const char *s = "max-stale, min-fresh=30, only-if-cached";
    TEST_ASSERT_TRUE(cache_control_parse(s, strlen(s), &cc));
    TEST_ASSERT_EQUAL_INT32(-2, cc.max_stale); // present, no value = "any"
    TEST_ASSERT_EQUAL_INT32(30, cc.min_fresh);
    TEST_ASSERT_TRUE(cc.only_if_cached);

    const char *s2 = "max-stale=90";
    TEST_ASSERT_TRUE(cache_control_parse(s2, strlen(s2), &cc));
    TEST_ASSERT_EQUAL_INT32(90, cc.max_stale);
}

// build -> parse -> the response directives survive intact.
void test_build_parse_roundtrip()
{
    pc_cache_control a;
    cache_control_init(&a);
    a.cc_public = PROTO_TRUE;
    a.max_age = 300;
    a.s_maxage = 600;
    a.must_revalidate = PROTO_TRUE;
    a.no_transform = PROTO_TRUE;
    a.stale_if_error = 120;
    char b[128];
    size_t n = cache_control_build(b, sizeof(b), &a);
    TEST_ASSERT_GREATER_THAN_size_t(0, n);

    pc_cache_control c;
    TEST_ASSERT_TRUE(cache_control_parse(b, n, &c));
    TEST_ASSERT_EQUAL(a.cc_public, c.cc_public);
    TEST_ASSERT_EQUAL(a.must_revalidate, c.must_revalidate);
    TEST_ASSERT_EQUAL(a.no_transform, c.no_transform);
    TEST_ASSERT_EQUAL_INT32(a.max_age, c.max_age);
    TEST_ASSERT_EQUAL_INT32(a.s_maxage, c.s_maxage);
    TEST_ASSERT_EQUAL_INT32(a.stale_if_error, c.stale_if_error);
}

// --- freshness (RFC 9111 4.2.1) --------------------------------------------

void test_freshness_precedence()
{
    pc_cache_control cc;
    cache_control_init(&cc);
    cc.max_age = 100;
    cc.s_maxage = 200;

    // shared cache honors s-maxage first
    TEST_ASSERT_EQUAL_INT(200, (int)cache_freshness_lifetime(&cc, PROTO_TRUE, 999));
    // private cache ignores s-maxage, uses max-age
    TEST_ASSERT_EQUAL_INT(100, (int)cache_freshness_lifetime(&cc, PROTO_FALSE, 999));

    // no max-age/s-maxage -> Expires minus Date
    cache_control_init(&cc);
    TEST_ASSERT_EQUAL_INT(50, (int)cache_freshness_lifetime(&cc, PROTO_TRUE, 50));

    // nothing explicit -> -1 (heuristic needed)
    TEST_ASSERT_EQUAL_INT(-1, (int)cache_freshness_lifetime(&cc, PROTO_TRUE, -1));
}

// Build every directive (exercises the less-common emit branches) and the request directives.
void test_build_all_directives()
{
    pc_cache_control cc;
    cache_control_init(&cc);
    cc.cc_private = PROTO_TRUE;
    cc.no_cache = PROTO_TRUE;
    cc.max_age = 10;
    cc.s_maxage = 20;
    cc.must_revalidate = PROTO_TRUE;
    cc.proxy_revalidate = PROTO_TRUE;
    cc.no_transform = PROTO_TRUE;
    cc.must_understand = PROTO_TRUE;
    cc.cc_immutable = PROTO_TRUE;
    cc.stale_while_revalidate = 5;
    cc.stale_if_error = 6;
    cc.only_if_cached = PROTO_TRUE;
    cc.min_fresh = 7;
    cc.max_stale = 8;
    char b[256];
    TEST_ASSERT_GREATER_THAN_size_t(0, cache_control_build(b, sizeof(b), &cc));
    TEST_ASSERT_NOT_NULL(strstr(b, "proxy-revalidate"));
    TEST_ASSERT_NOT_NULL(strstr(b, "must-understand"));
    TEST_ASSERT_NOT_NULL(strstr(b, "only-if-cached"));
    TEST_ASSERT_NOT_NULL(strstr(b, "max-stale=8"));
    TEST_ASSERT_NOT_NULL(strstr(b, "min-fresh=7"));

    cc.max_stale = -2; // present, no value
    cache_control_build(b, sizeof(b), &cc);
    TEST_ASSERT_NOT_NULL(strstr(b, "max-stale"));
    TEST_ASSERT_NULL(strstr(b, "max-stale=")); // emitted bare, no '='
}

void test_parse_all_directives()
{
    pc_cache_control cc;
    const char *s = "private, no-cache, no-transform, must-revalidate, proxy-revalidate, "
                    "must-understand, immutable, only-if-cached, stale-while-revalidate=30";
    TEST_ASSERT_TRUE(cache_control_parse(s, strlen(s), &cc));
    TEST_ASSERT_TRUE(cc.cc_private);
    TEST_ASSERT_TRUE(cc.no_cache);
    TEST_ASSERT_TRUE(cc.no_transform);
    TEST_ASSERT_TRUE(cc.must_revalidate);
    TEST_ASSERT_TRUE(cc.proxy_revalidate);
    TEST_ASSERT_TRUE(cc.must_understand);
    TEST_ASSERT_TRUE(cc.cc_immutable);
    TEST_ASSERT_TRUE(cc.only_if_cached);
    TEST_ASSERT_EQUAL_INT32(30, cc.stale_while_revalidate);
}

void test_parse_and_build_guards()
{
    pc_cache_control cc;
    TEST_ASSERT_FALSE(cache_control_parse(NULL, 0, &cc)); // null input
    // a recognized numeric directive with no value stays absent (-1)
    TEST_ASSERT_TRUE(cache_control_parse("max-age", 7, &cc));
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_age);
    // an out-of-range delta clamps to INT32_MAX
    cache_control_parse("max-age=99999999999", 19, &cc);
    TEST_ASSERT_EQUAL_INT32(2147483647, cc.max_age);
    // trailing separators are skipped
    TEST_ASSERT_TRUE(cache_control_parse("public,,,", 9, &cc));
    TEST_ASSERT_TRUE(cc.cc_public);

    // build guards: null buffer, zero cap, and overflow during a number emit
    char b[8];
    cache_control_init(&cc);
    cc.max_age = 12345;
    TEST_ASSERT_EQUAL_size_t(0, cache_control_build(NULL, 8, &cc));
    TEST_ASSERT_EQUAL_size_t(0, cache_control_build(b, 0, &cc));
    char snug[12]; // "max-age=12345" (13) overflows mid-number
    TEST_ASSERT_EQUAL_size_t(0, cache_control_build(snug, sizeof(snug), &cc));
}

// A delta above INT32_MAX passed to a preset clamps (the `> INT32_MAX` side of each preset).
void test_preset_clamps()
{
    pc_cache_control cc;
    cache_immutable_asset(&cc, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_INT32(2147483647, cc.max_age);
    cache_revalidatable(&cc, 0xFFFFFFFFu, -1);
    TEST_ASSERT_EQUAL_INT32(2147483647, cc.max_age);
    cache_shared(&cc, 0xFFFFFFFFu, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_INT32(2147483647, cc.max_age);
    TEST_ASSERT_EQUAL_INT32(2147483647, cc.s_maxage);
}

// Build boundary guards: null cc, a key that fills the buffer exactly (no room for '='), the key
// token itself overflowing (CC_SENT into cc_kv), and an exact fill with no room for the NUL.
void test_build_boundaries()
{
    pc_cache_control cc;
    char b[32];
    cache_control_init(&cc);
    cc.max_age = 5;
    TEST_ASSERT_EQUAL_size_t(0, cache_control_build(b, sizeof(b), NULL)); // !cc
    char b7[7];                                                           // "max-age" fills cap -> no room for '='
    TEST_ASSERT_EQUAL_size_t(0, cache_control_build(b7, sizeof(b7), &cc));
    char b3[3]; // the key token overflows -> cc_kv sees the CC_SENT sentinel
    TEST_ASSERT_EQUAL_size_t(0, cache_control_build(b3, sizeof(b3), &cc));
    cache_no_store(&cc); // "no-store" (8) fills cap 8 exactly -> no room for the NUL
    char b8[8];
    TEST_ASSERT_EQUAL_size_t(0, cache_control_build(b8, sizeof(b8), &cc));
}

// Case-insensitive compare length edges: a name that is a prefix of a directive (input ends first),
// and one that extends past a directive (target ends first).
void test_parse_ci_length_edges()
{
    pc_cache_control cc;
    TEST_ASSERT_FALSE(cache_control_parse("max", 3, &cc));     // prefix of "max-age": i==len, target[i]!=0
    TEST_ASSERT_FALSE(cache_control_parse("publicX", 7, &cc)); // extends past "public": i<len, target[i]==0
}

// Parser OWS / empty-name / no-digit-value edges (tab separators + trailing tabs, empty name, a
// value present with no digits, a delta ending on a non-digit above '9').
void test_parse_ows_and_empty()
{
    pc_cache_control cc;
    const char *tabs = "public,\tmax-age=\t42"; // tab separator + tab as leading delta OWS
    TEST_ASSERT_TRUE(cache_control_parse(tabs, strlen(tabs), &cc));
    TEST_ASSERT_TRUE(cc.cc_public);
    TEST_ASSERT_EQUAL_INT32(42, cc.max_age);

    const char *trail = "no-store\t, max-age\t=5"; // trailing tab on a directive + tab in the name
    TEST_ASSERT_TRUE(cache_control_parse(trail, strlen(trail), &cc));
    TEST_ASSERT_TRUE(cc.no_store);
    TEST_ASSERT_EQUAL_INT32(5, cc.max_age);

    const char *empty = "=5, max-age=xyz, public"; // empty name skipped; "xyz" has no digits -> absent
    TEST_ASSERT_TRUE(cache_control_parse(empty, strlen(empty), &cc));
    TEST_ASSERT_TRUE(cc.cc_public);
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_age);

    const char *mixed = "max-age=5:"; // digit then ':' (> '9') ends the delta loop
    TEST_ASSERT_TRUE(cache_control_parse(mixed, strlen(mixed), &cc));
    TEST_ASSERT_EQUAL_INT32(5, cc.max_age);

    // value is only quote chars: the leading-OWS-skip loop in cc_parse_delta consumes the whole
    // value and exits because i reaches vlen (not because it hit a non-OWS char) - no digits found.
    const char *quoted_empty = "max-age=\"\"";
    TEST_ASSERT_TRUE(cache_control_parse(quoted_empty, strlen(quoted_empty), &cc));
    TEST_ASSERT_EQUAL_INT32(-1, cc.max_age);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_preset_immutable);
    RUN_TEST(test_preset_no_store_and_shared_and_revalidatable);
    RUN_TEST(test_build_manual_and_edges);
    RUN_TEST(test_parse_response_directives);
    RUN_TEST(test_parse_case_insensitive_and_quoted_and_unknown);
    RUN_TEST(test_parse_request_directives);
    RUN_TEST(test_build_parse_roundtrip);
    RUN_TEST(test_freshness_precedence);
    RUN_TEST(test_build_all_directives);
    RUN_TEST(test_parse_all_directives);
    RUN_TEST(test_parse_and_build_guards);
    RUN_TEST(test_preset_clamps);
    RUN_TEST(test_build_boundaries);
    RUN_TEST(test_parse_ci_length_edges);
    RUN_TEST(test_parse_ows_and_empty);
    return UNITY_END();
}
