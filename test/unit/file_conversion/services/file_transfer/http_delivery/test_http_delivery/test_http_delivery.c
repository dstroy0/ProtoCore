// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/file_transfer/http_delivery/http_delivery.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_rfc5861_worked_example(void)
{
    TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, protocore_delivery_swr(0, 600, 30));
    TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, protocore_delivery_swr(599, 600, 30));
    TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, protocore_delivery_swr(600, 600, 30));
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, protocore_delivery_swr(601, 600, 30));
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, protocore_delivery_swr(629, 600, 30));
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, protocore_delivery_swr(630, 600, 30));
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, protocore_delivery_swr(631, 600, 30));
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, protocore_delivery_swr(100000, 600, 30));
}

void test_rfc5861_example_header(void)
{
    char out[64];
    size_t n = protocore_delivery_cache_control(600, 30, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("public, max-age=600, stale-while-revalidate=30", out);
    TEST_ASSERT_EQUAL_UINT(strlen(out), n);
}

void test_rfc5861_twenty_minute_total(void)
{
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, protocore_delivery_swr(1200, 600, 600));
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, protocore_delivery_swr(1201, 600, 600));
}

void test_no_swr_window_has_no_stale_band(void)
{
    TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, protocore_delivery_swr(600, 600, 0));
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, protocore_delivery_swr(601, 600, 0));

    TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, protocore_delivery_swr(0, 0, 0));
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, protocore_delivery_swr(1, 0, 0));
}

void test_verdict_is_monotonic_in_age(void)
{
    DeliveryVerdict prev = DELIVERY_FRESH;
    for (uint32_t age = 0; age <= 200; age++)
    {
        DeliveryVerdict v = protocore_delivery_swr(age, 100, 50);
        TEST_ASSERT_TRUE(v >= prev);
        prev = v;
    }
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, prev);
}

void test_window_sum_does_not_wrap(void)
{
    const uint32_t huge = 0xFFFFFFFFu;
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, protocore_delivery_swr(huge, huge - 1u, 2u));
    TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, protocore_delivery_swr(huge, huge, 1u));
}

void test_cache_control_omits_a_zero_swr(void)
{
    char out[64];
    TEST_ASSERT_TRUE(protocore_delivery_cache_control(3600, 0, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("public, max-age=3600", out);

    TEST_ASSERT_TRUE(protocore_delivery_cache_control(0, 0, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("public, max-age=0", out);
}

void test_cache_control_renders_the_full_range(void)
{
    char out[80];
    TEST_ASSERT_TRUE(protocore_delivery_cache_control(4294967295u, 4294967295u, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("public, max-age=4294967295, stale-while-revalidate=4294967295", out);
}

void test_cache_control_refuses_a_short_buffer(void)
{
    char out[64];
    size_t need = protocore_delivery_cache_control(600, 30, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(46u, need);

    char exact[47];
    TEST_ASSERT_EQUAL_UINT(46u, protocore_delivery_cache_control(600, 30, exact, sizeof(exact)));
    char tight[46];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_delivery_cache_control(600, 30, tight, sizeof(tight)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_delivery_cache_control(600, 30, out, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_delivery_cache_control(600, 30, NULL, 64));
}

void test_manifest_shape(void)
{
    static const char *const PATHS[] = {"/", "/app.js", "/app.css"};
    char out[PROTOCORE_DELIVERY_MANIFEST_BUF];
    size_t n = protocore_delivery_sw_manifest(PATHS, 3, "v1.2.3", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("{\"version\":\"v1.2.3\",\"precache\":[\"/\",\"/app.js\",\"/app.css\"]}", out);
    TEST_ASSERT_EQUAL_UINT(strlen(out), n);

    TEST_ASSERT_TRUE(protocore_delivery_sw_manifest(NULL, 0, "v1", out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"version\":\"v1\",\"precache\":[]}", out);
}

void test_manifest_escapes_per_rfc8259(void)
{
    static const char *const PATHS[] = {"/a\"b", "/c\\d", "/e\tf"};
    char out[PROTOCORE_DELIVERY_MANIFEST_BUF];
    TEST_ASSERT_TRUE(protocore_delivery_sw_manifest(PATHS, 3, "v\n1", out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"version\":\"v\\n1\",\"precache\":[\"/a\\\"b\",\"/c\\\\d\",\"/e\\tf\"]}", out);

    static const char *const BELL[] = {"/\x07"};
    TEST_ASSERT_TRUE(protocore_delivery_sw_manifest(BELL, 1, "v1", out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"version\":\"v1\",\"precache\":[\"/\\u0007\"]}", out);
}

void test_full_precache_list_fits_the_configured_buffer(void)
{
    const char *paths[PROTOCORE_DELIVERY_PRECACHE_MAX];
    static const char NAMES[PROTOCORE_DELIVERY_PRECACHE_MAX][4] = {
        "/a", "/b", "/c", "/d", "/e", "/f", "/g", "/h", "/i", "/j", "/k", "/l", "/m", "/n", "/o", "/p"};
    char out[PROTOCORE_DELIVERY_MANIFEST_BUF];
    for (size_t i = 0; i < PROTOCORE_DELIVERY_PRECACHE_MAX; i++)
    {
        paths[i] = NAMES[i];
    }
    size_t n = protocore_delivery_sw_manifest(paths, PROTOCORE_DELIVERY_PRECACHE_MAX, "v1", out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT(strlen(out), n);
    TEST_ASSERT_EQUAL_CHAR('}', out[n - 1]);
}

void test_manifest_refuses_rather_than_truncating(void)
{
    static const char *const PATHS[] = {"/aaaaaaaaaa", "/bbbbbbbbbb", "/cccccccccc"};
    char out[32];
    memset(out, 'Z', sizeof(out));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_delivery_sw_manifest(PATHS, 3, "v1", out, sizeof(out)));

    char big[PROTOCORE_DELIVERY_MANIFEST_BUF];
    size_t need = protocore_delivery_sw_manifest(PATHS, 3, "v1", big, sizeof(big));
    TEST_ASSERT_TRUE(need > 0);
    char exact[PROTOCORE_DELIVERY_MANIFEST_BUF];
    TEST_ASSERT_EQUAL_UINT(need, protocore_delivery_sw_manifest(PATHS, 3, "v1", exact, need + 1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_delivery_sw_manifest(PATHS, 3, "v1", exact, need));
}

void test_manifest_refuses_bad_arguments(void)
{
    static const char *const PATHS[] = {"/a"};
    char out[64];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_delivery_sw_manifest(PATHS, 1, "v1", NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_delivery_sw_manifest(PATHS, 1, "v1", out, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_delivery_sw_manifest(NULL, 1, "v1", out, sizeof(out)));

    TEST_ASSERT_TRUE(protocore_delivery_sw_manifest(PATHS, 1, NULL, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"version\":\"\",\"precache\":[\"/a\"]}", out);
}
