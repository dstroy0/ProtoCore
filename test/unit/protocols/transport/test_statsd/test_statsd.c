// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/udp/udp.h"
#include "services/iot/statsd/statsd.h"
#include <string.h>
#include <unity.h>

static const uint8_t *udp_cap(void)
{
    size_t n = protocore_net_host_udp_count();
    return n ? protocore_net_host_udp_at(n - 1)->data : NULL;
}
static size_t udp_cap_len(void)
{
    size_t n = protocore_net_host_udp_count();
    return n ? protocore_net_host_udp_at(n - 1)->len : 0;
}

static const char *captured(void)
{
    static char buf[512];
    const uint8_t *p = udp_cap();
    size_t n = p ? udp_cap_len() : 0;
    if (n >= sizeof(buf))
    {
        n = sizeof(buf) - 1;
    }
    if (n)
    {
        memcpy(buf, p, n);
    }
    buf[n] = 0;
    return buf;
}

void setUp()
{
    protocore_net_host_udp_reset();
}
void tearDown()
{
}

void test_format_types()
{
    char out[64];
    TEST_ASSERT_TRUE(protocore_statsd_format(out, sizeof(out), "api.hits", "1", STATSD_COUNTER, 1.0f, NULL));
    TEST_ASSERT_EQUAL_STRING("api.hits:1|c", out);
    protocore_statsd_format(out, sizeof(out), "temp", "42", STATSD_GAUGE, 1.0f, NULL);
    TEST_ASSERT_EQUAL_STRING("temp:42|g", out);
    protocore_statsd_format(out, sizeof(out), "req.latency", "120", STATSD_TIMING, 1.0f, NULL);
    TEST_ASSERT_EQUAL_STRING("req.latency:120|ms", out);
    protocore_statsd_format(out, sizeof(out), "users", "u42", STATSD_SET, 1.0f, NULL);
    TEST_ASSERT_EQUAL_STRING("users:u42|s", out);
}

void test_format_sample_rate()
{
    char out[64];
    protocore_statsd_format(out, sizeof(out), "x", "1", STATSD_COUNTER, 0.1f, NULL);
    TEST_ASSERT_EQUAL_STRING("x:1|c|@0.1", out);
    protocore_statsd_format(out, sizeof(out), "x", "1", STATSD_COUNTER, 0.5f, NULL);
    TEST_ASSERT_EQUAL_STRING("x:1|c|@0.5", out);
    protocore_statsd_format(out, sizeof(out), "x", "1", STATSD_COUNTER, 0.01f, NULL);
    TEST_ASSERT_EQUAL_STRING("x:1|c|@0.01", out);
    protocore_statsd_format(out, sizeof(out), "x", "1", STATSD_COUNTER, 1.0f, NULL);
    TEST_ASSERT_EQUAL_STRING("x:1|c", out);
}

void test_format_tags_and_both()
{
    char out[80];
    protocore_statsd_format(out, sizeof(out), "x", "1", STATSD_COUNTER, 1.0f, "env:prod,host:a");
    TEST_ASSERT_EQUAL_STRING("x:1|c|#env:prod,host:a", out);
    protocore_statsd_format(out, sizeof(out), "x", "1", STATSD_COUNTER, 0.1f, "env:prod");
    TEST_ASSERT_EQUAL_STRING("x:1|c|@0.1|#env:prod", out);
}

void test_format_guards()
{
    char out[64];
    TEST_ASSERT_EQUAL_UINT(0, protocore_statsd_format(out, sizeof(out), "x", "1", (StatsdType)'z', 1.0f, NULL));
    TEST_ASSERT_EQUAL_UINT(0, protocore_statsd_format(out, sizeof(out), NULL, "1", STATSD_COUNTER, 1.0f, NULL));
    TEST_ASSERT_EQUAL_UINT(0, protocore_statsd_format(out, sizeof(out), "", "1", STATSD_COUNTER, 1.0f, NULL));
    TEST_ASSERT_EQUAL_UINT(0, protocore_statsd_format(out, sizeof(out), "x", NULL, STATSD_COUNTER, 1.0f, NULL));
    TEST_ASSERT_EQUAL_UINT(0, protocore_statsd_format(out, 5, "toolongname", "1", STATSD_COUNTER, 1.0f, NULL));
}

void test_emit_counter_and_negative()
{
    protocore_statsd_begin("192.0.2.10", 8125, NULL);
    protocore_statsd_count("api.hits", 3);
    TEST_ASSERT_EQUAL_STRING("api.hits:3|c", captured());
    protocore_net_host_udp_reset();
    protocore_statsd_count("api.hits", -4);
    TEST_ASSERT_EQUAL_STRING("api.hits:-4|c", captured());
}

void test_emit_gauge_and_delta()
{
    protocore_statsd_begin("192.0.2.10", 0, NULL);
    protocore_statsd_gauge("heap.free", 200000);
    TEST_ASSERT_EQUAL_STRING("heap.free:200000|g", captured());
    protocore_net_host_udp_reset();
    protocore_statsd_gauge_delta("conns", 5);
    TEST_ASSERT_EQUAL_STRING("conns:+5|g", captured());
    protocore_net_host_udp_reset();
    protocore_statsd_gauge_delta("conns", -2);
    TEST_ASSERT_EQUAL_STRING("conns:-2|g", captured());
}

void test_emit_timing_set_sampled()
{
    protocore_statsd_begin("192.0.2.10", 8125, NULL);
    protocore_statsd_timing("db.query", 120);
    TEST_ASSERT_EQUAL_STRING("db.query:120|ms", captured());
    protocore_net_host_udp_reset();
    protocore_statsd_set("uniques", "device-7");
    TEST_ASSERT_EQUAL_STRING("uniques:device-7|s", captured());
    protocore_net_host_udp_reset();
    protocore_statsd_count_sampled("rare", 1, 0.25f);
    TEST_ASSERT_EQUAL_STRING("rare:1|c|@0.25", captured());
}

void test_emit_global_tags()
{
    protocore_statsd_begin("192.0.2.10", 8125, "env:prod,region:us");
    protocore_statsd_count("x", 1);
    TEST_ASSERT_EQUAL_STRING("x:1|c|#env:prod,region:us", captured());
}

void test_emit_noop_until_begin()
{
    protocore_statsd_begin(NULL, 0, NULL);
    protocore_net_host_udp_reset();
    protocore_statsd_count("x", 1);
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

void test_rate_clamp_and_stage_overflow()
{
    char out[64];

    TEST_ASSERT_TRUE(protocore_statsd_format(out, sizeof(out), "m", "1", STATSD_COUNTER, 0.0001f, NULL) > 0);
    TEST_ASSERT_TRUE(protocore_statsd_format(out, sizeof(out), "m", "1", STATSD_COUNTER, 0.9999f, NULL) > 0);

    TEST_ASSERT_EQUAL_size_t(0, protocore_statsd_format(out, 2, "metric", "1", STATSD_COUNTER, 1.0f, NULL));
    TEST_ASSERT_EQUAL_size_t(0, protocore_statsd_format(out, 4, "m", "1", STATSD_TIMING, 1.0f, NULL));
    TEST_ASSERT_EQUAL_size_t(0, protocore_statsd_format(out, 6, "m", "1", STATSD_COUNTER, 0.5f, NULL));
    TEST_ASSERT_EQUAL_size_t(0, protocore_statsd_format(out, 7, "m", "1", STATSD_COUNTER, 1.0f, "#tag:x"));
}

void test_format_guard_null_out_and_zero_cap()
{
    char out[64];
    TEST_ASSERT_EQUAL_size_t(0, protocore_statsd_format(NULL, sizeof(out), "a", "1", STATSD_COUNTER, 1.0f, NULL));
    TEST_ASSERT_EQUAL_size_t(0, protocore_statsd_format(out, 0, "a", "1", STATSD_COUNTER, 1.0f, NULL));
}

void test_format_append_chain_overflow_points()
{
    char out[64];
    TEST_ASSERT_EQUAL_size_t(0, protocore_statsd_format(out, 2, "a", "1", STATSD_COUNTER, 1.0f, NULL));
    TEST_ASSERT_EQUAL_size_t(0, protocore_statsd_format(out, 3, "a", "1", STATSD_COUNTER, 1.0f, NULL));
    TEST_ASSERT_EQUAL_size_t(0, protocore_statsd_format(out, 5, "a", "1", STATSD_COUNTER, 1.0f, NULL));
    TEST_ASSERT_EQUAL_size_t(0, protocore_statsd_format(out, 6, "a", "1", STATSD_TIMING, 1.0f, NULL));
    TEST_ASSERT_EQUAL_size_t(0, protocore_statsd_format(out, 8, "a", "1", STATSD_COUNTER, 0.5f, NULL));
    TEST_ASSERT_EQUAL_size_t(0, protocore_statsd_format(out, 8, "a", "1", STATSD_COUNTER, 1.0f, "tg"));
}

void test_format_rate_zero_and_empty_tags()
{
    char out[64];
    protocore_statsd_format(out, sizeof(out), "x", "1", STATSD_COUNTER, 0.0f, NULL);
    TEST_ASSERT_EQUAL_STRING("x:1|c", out);
    protocore_statsd_format(out, sizeof(out), "x", "1", STATSD_COUNTER, 1.0f, "");
    TEST_ASSERT_EQUAL_STRING("x:1|c", out);
}

void test_emit_zero_value_and_set_null_member()
{
    protocore_statsd_begin("192.0.2.10", 8125, NULL);
    protocore_statsd_timing("db.zero", 0);
    TEST_ASSERT_EQUAL_STRING("db.zero:0|ms", captured());
    protocore_net_host_udp_reset();
    protocore_statsd_set("uniques", NULL);
    TEST_ASSERT_EQUAL_STRING("uniques:|s", captured());
}

void test_emit_overlong_name_is_noop()
{
    protocore_statsd_begin("192.0.2.10", 8125, NULL);
    char longname[300];
    for (size_t i = 0; i < sizeof(longname) - 1; i++)
    {
        longname[i] = 'a';
    }
    longname[sizeof(longname) - 1] = '\0';
    protocore_net_host_udp_reset();
    protocore_statsd_count(longname, 1);
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_format_types);
    RUN_TEST(test_format_sample_rate);
    RUN_TEST(test_format_tags_and_both);
    RUN_TEST(test_format_guards);
    RUN_TEST(test_emit_counter_and_negative);
    RUN_TEST(test_emit_gauge_and_delta);
    RUN_TEST(test_emit_timing_set_sampled);
    RUN_TEST(test_emit_global_tags);
    RUN_TEST(test_emit_noop_until_begin);
    RUN_TEST(test_rate_clamp_and_stage_overflow);
    RUN_TEST(test_format_guard_null_out_and_zero_cap);
    RUN_TEST(test_format_append_chain_overflow_points);
    RUN_TEST(test_format_rate_zero_and_empty_tags);
    RUN_TEST(test_emit_zero_value_and_set_null_member);
    RUN_TEST(test_emit_overlong_name_is_noop);
    return UNITY_END();
}
