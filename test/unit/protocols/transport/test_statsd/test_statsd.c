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
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "api.hits";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_TRUE(Statsd.n);
    TEST_ASSERT_EQUAL_STRING("api.hits:1|c", out);
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "temp";
    Statsd.value.text = "42";
    Statsd.metric.type = STATSD_GAUGE;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("temp:42|g", out);
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "req.latency";
    Statsd.value.text = "120";
    Statsd.metric.type = STATSD_TIMING;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("req.latency:120|ms", out);
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "users";
    Statsd.value.text = "u42";
    Statsd.metric.type = STATSD_SET;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("users:u42|s", out);
}

void test_format_sample_rate()
{
    char out[64];
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "x";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 0.1f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c|@0.1", out);
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "x";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 0.5f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c|@0.5", out);
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "x";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 0.01f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c|@0.01", out);
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "x";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c", out);
}

void test_format_tags_and_both()
{
    char out[80];
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "x";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = "env:prod,host:a";
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c|#env:prod,host:a", out);
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "x";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 0.1f;
    Statsd.tags.metric = "env:prod";
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c|@0.1|#env:prod", out);
}

void test_format_guards()
{
    char out[64];
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "x";
    Statsd.value.text = "1";
    Statsd.metric.type = (StatsdType)'z';
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = NULL;
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "x";
    Statsd.value.text = NULL;
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = 5;
    Statsd.metric.name = "toolongname";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, Statsd.n);
}

void test_emit_counter_and_negative()
{
    Statsd.server.addr = "192.0.2.10";
    Statsd.server.port = 8125;
    Statsd.tags.global = NULL;
    Statsd.init(protocore_statsd_span());
    Statsd.metric.name = "api.hits";
    Statsd.value.i64 = 3;
    Statsd.metric.rate = 1.0f;
    Statsd.count(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("api.hits:3|c", captured());
    protocore_net_host_udp_reset();
    Statsd.metric.name = "api.hits";
    Statsd.value.i64 = -4;
    Statsd.metric.rate = 1.0f;
    Statsd.count(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("api.hits:-4|c", captured());
}

void test_emit_gauge_and_delta()
{
    Statsd.server.addr = "192.0.2.10";
    Statsd.server.port = 0;
    Statsd.tags.global = NULL;
    Statsd.init(protocore_statsd_span());
    Statsd.metric.name = "heap.free";
    Statsd.value.i64 = 200000;
    Statsd.gauge(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("heap.free:200000|g", captured());
    protocore_net_host_udp_reset();
    Statsd.metric.name = "conns";
    Statsd.value.i64 = 5;
    Statsd.gauge_delta(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("conns:+5|g", captured());
    protocore_net_host_udp_reset();
    Statsd.metric.name = "conns";
    Statsd.value.i64 = -2;
    Statsd.gauge_delta(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("conns:-2|g", captured());
}

void test_emit_timing_set_sampled()
{
    Statsd.server.addr = "192.0.2.10";
    Statsd.server.port = 8125;
    Statsd.tags.global = NULL;
    Statsd.init(protocore_statsd_span());
    Statsd.metric.name = "db.query";
    Statsd.value.ms = 120;
    Statsd.timing(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("db.query:120|ms", captured());
    protocore_net_host_udp_reset();
    Statsd.metric.name = "uniques";
    Statsd.value.member = "device-7";
    Statsd.set(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("uniques:device-7|s", captured());
    protocore_net_host_udp_reset();
    Statsd.metric.name = "rare";
    Statsd.value.i64 = 1;
    Statsd.metric.rate = 0.25f;
    Statsd.count(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("rare:1|c|@0.25", captured());
}

void test_emit_global_tags()
{
    Statsd.server.addr = "192.0.2.10";
    Statsd.server.port = 8125;
    Statsd.tags.global = "env:prod,region:us";
    Statsd.init(protocore_statsd_span());
    Statsd.metric.name = "x";
    Statsd.value.i64 = 1;
    Statsd.metric.rate = 1.0f;
    Statsd.count(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c|#env:prod,region:us", captured());
}

void test_emit_noop_until_begin()
{
    Statsd.server.addr = NULL;
    Statsd.server.port = 0;
    Statsd.tags.global = NULL;
    Statsd.init(protocore_statsd_span());
    protocore_net_host_udp_reset();
    Statsd.metric.name = "x";
    Statsd.value.i64 = 1;
    Statsd.metric.rate = 1.0f;
    Statsd.count(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

void test_rate_clamp_and_stage_overflow()
{
    char out[64];

    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "m";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 0.0001f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_TRUE(Statsd.n > 0);
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "m";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 0.9999f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_TRUE(Statsd.n > 0);

    Statsd.line.out = out;
    Statsd.line.cap = 2;
    Statsd.metric.name = "metric";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = 4;
    Statsd.metric.name = "m";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_TIMING;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = 6;
    Statsd.metric.name = "m";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 0.5f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = 7;
    Statsd.metric.name = "m";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = "#tag:x";
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, Statsd.n);
}

void test_format_guard_null_out_and_zero_cap()
{
    char out[64];
    Statsd.line.out = NULL;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "a";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = 0;
    Statsd.metric.name = "a";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, Statsd.n);
}

void test_format_append_chain_overflow_points()
{
    char out[64];
    Statsd.line.out = out;
    Statsd.line.cap = 2;
    Statsd.metric.name = "a";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = 3;
    Statsd.metric.name = "a";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = 5;
    Statsd.metric.name = "a";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = 6;
    Statsd.metric.name = "a";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_TIMING;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = 8;
    Statsd.metric.name = "a";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 0.5f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, Statsd.n);
    Statsd.line.out = out;
    Statsd.line.cap = 8;
    Statsd.metric.name = "a";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = "tg";
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, Statsd.n);
}

void test_format_rate_zero_and_empty_tags()
{
    char out[64];
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "x";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 0.0f;
    Statsd.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c", out);
    Statsd.line.out = out;
    Statsd.line.cap = sizeof(out);
    Statsd.metric.name = "x";
    Statsd.value.text = "1";
    Statsd.metric.type = STATSD_COUNTER;
    Statsd.metric.rate = 1.0f;
    Statsd.tags.metric = "";
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c", out);
}

void test_emit_zero_value_and_set_null_member()
{
    Statsd.server.addr = "192.0.2.10";
    Statsd.server.port = 8125;
    Statsd.tags.global = NULL;
    Statsd.init(protocore_statsd_span());
    Statsd.metric.name = "db.zero";
    Statsd.value.ms = 0;
    Statsd.timing(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("db.zero:0|ms", captured());
    protocore_net_host_udp_reset();
    Statsd.metric.name = "uniques";
    Statsd.value.member = NULL;
    Statsd.set(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("uniques:|s", captured());
}

void test_emit_overlong_name_is_noop()
{
    Statsd.server.addr = "192.0.2.10";
    Statsd.server.port = 8125;
    Statsd.tags.global = NULL;
    Statsd.init(protocore_statsd_span());
    char longname[300];
    for (size_t i = 0; i < sizeof(longname) - 1; i++)
    {
        longname[i] = 'a';
    }
    longname[sizeof(longname) - 1] = '\0';
    protocore_net_host_udp_reset();
    Statsd.metric.name = longname;
    Statsd.value.i64 = 1;
    Statsd.metric.rate = 1.0f;
    Statsd.count(protocore_statsd_span());
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
