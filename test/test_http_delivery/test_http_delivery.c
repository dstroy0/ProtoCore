// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/http_delivery: RFC 5861 stale-while-revalidate (decision + header) and
// the service-worker precache manifest. Byte-range serving is network_drivers/application/http_range.h's job.

#include "services/file_transfer/http_delivery/http_delivery.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_swr_decision(void)
{
    // max-age=60, swr=30.
    TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, pc_delivery_swr(0, 60, 30));
    TEST_ASSERT_EQUAL_INT(DELIVERY_FRESH, pc_delivery_swr(60, 60, 30)); // boundary: <= max-age
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, pc_delivery_swr(61, 60, 30));
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, pc_delivery_swr(90, 60, 30)); // boundary: max+swr
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, pc_delivery_swr(91, 60, 30));
    // No swr window.
    TEST_ASSERT_EQUAL_INT(DELIVERY_EXPIRED, pc_delivery_swr(61, 60, 0));
    // Overflow guard: age > max-age, but max-age + swr wraps uint32 (0xFFFFFFF0 + 0x20 = 0x100000010).
    // A 32-bit add would wrap to 0x10 and wrongly report EXPIRED; the uint64 window keeps it STALE.
    TEST_ASSERT_EQUAL_INT(DELIVERY_STALE_REVALIDATE, pc_delivery_swr(0xFFFFFFF8u, 0xFFFFFFF0u, 0x20u));
}

void test_cache_control(void)
{
    char buf[64];
    size_t n = pc_delivery_cache_control(60, 30, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_EQUAL_STRING("public, max-age=60, stale-while-revalidate=30", buf);
    pc_delivery_cache_control(3600, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("public, max-age=3600", buf);
    // Overflow.
    char tiny[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_delivery_cache_control(3600, 30, tiny, sizeof(tiny)));
}

void test_sw_manifest(void)
{
    const char *paths[3] = {"/", "/app.js", "/style.css"};
    char buf[128];
    size_t n = pc_delivery_sw_manifest(paths, 3, "v42", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_EQUAL_STRING("{\"version\":\"v42\",\"precache\":[\"/\",\"/app.js\",\"/style.css\"]}", buf);
    // Empty list.
    pc_delivery_sw_manifest(NULL, 0, "v1", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"version\":\"v1\",\"precache\":[]}", buf);
}

void test_delivery_guards_and_escape()
{
    char buf[256];
    TEST_ASSERT_EQUAL_size_t(0, pc_delivery_cache_control(60, 30, NULL, sizeof(buf))); // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_delivery_cache_control(60, 30, buf, 0));            // zero cap
    const char *paths[1] = {"a\"b\\c"};                                                // quote + backslash
    size_t n = pc_delivery_sw_manifest(paths, 1, "1.0", buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\\\""));                                     // escaped quote present
    TEST_ASSERT_EQUAL_size_t(0, pc_delivery_sw_manifest(paths, 1, "1.0", buf, 4)); // tiny cap fails closed
}

// Null-output and capacity guards on the cache-control / service-worker-manifest builders.
// (Byte-range parsing lives in network_drivers/application/http_range.h and is tested by the file-serving suite.)
void test_builder_edge_guards(void)
{
    char buf[64];
    const char *paths[1] = {"/a"};
    TEST_ASSERT_EQUAL_size_t(0, pc_delivery_sw_manifest(paths, 1, "v", NULL, sizeof(buf))); // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_delivery_sw_manifest(NULL, 2, "v", buf, sizeof(buf)));   // n>0, null paths
    TEST_ASSERT_EQUAL_size_t(0, pc_delivery_sw_manifest(paths, 1, "v", buf, 0));            // zero cap, non-null out
    // Null version falls back to "" rather than dereferencing.
    size_t n = pc_delivery_sw_manifest(paths, 1, NULL, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"version\":\"\""));
}

// What the /precache.json route depends on: a full-size manifest fits the shipped buffer, and an
// oversized one fails closed (the route answers 500 rather than serving truncated JSON a service
// worker would choke on).
void test_manifest_fits_the_served_buffer(void)
{
    const char *paths[PC_DELIVERY_PRECACHE_MAX];
    for (size_t i = 0; i < PC_DELIVERY_PRECACHE_MAX; i++)
    {
        paths[i] = "/assets/chunk-0000.js"; // a realistic worst-case path length
    }

    char buf[PC_DELIVERY_MANIFEST_BUF];
    size_t n = pc_delivery_sw_manifest(paths, PC_DELIVERY_PRECACHE_MAX, "7.15.0", buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_EQUAL_CHAR('{', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('}', buf[n - 1]); // complete JSON, not truncated
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"version\":\"7.15.0\""));

    // One byte short of what it needs -> 0, never a partial document.
    TEST_ASSERT_EQUAL_size_t(0, pc_delivery_sw_manifest(paths, PC_DELIVERY_PRECACHE_MAX, "7.15.0", buf, n));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_builder_edge_guards);
    RUN_TEST(test_swr_decision);
    RUN_TEST(test_cache_control);
    RUN_TEST(test_sw_manifest);
    RUN_TEST(test_manifest_fits_the_served_buffer);
    RUN_TEST(test_delivery_guards_and_escape);
    return UNITY_END();
}
