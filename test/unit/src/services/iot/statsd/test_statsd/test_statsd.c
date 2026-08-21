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
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "api.hits";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_TRUE(StatsdV.n);
    TEST_ASSERT_EQUAL_STRING("api.hits:1|c", out);
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "temp";
    StatsdV.value.text = "42";
    StatsdV.metric.type = STATSD_GAUGE;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("temp:42|g", out);
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "req.latency";
    StatsdV.value.text = "120";
    StatsdV.metric.type = STATSD_TIMING;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("req.latency:120|ms", out);
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "users";
    StatsdV.value.text = "u42";
    StatsdV.metric.type = STATSD_SET;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("users:u42|s", out);
}

void test_format_sample_rate()
{
    char out[64];
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "x";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 0.1f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c|@0.1", out);
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "x";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 0.5f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c|@0.5", out);
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "x";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 0.01f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c|@0.01", out);
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "x";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c", out);
}

void test_format_tags_and_both()
{
    char out[80];
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "x";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = "env:prod,host:a";
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c|#env:prod,host:a", out);
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "x";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 0.1f;
    StatsdV.tags.metric = "env:prod";
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c|@0.1|#env:prod", out);
}

void test_format_guards()
{
    char out[64];
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "x";
    StatsdV.value.text = "1";
    StatsdV.metric.type = (StatsdType)'z';
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = NULL;
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "x";
    StatsdV.value.text = NULL;
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = 5;
    StatsdV.metric.name = "toolongname";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, StatsdV.n);
}

void test_emit_counter_and_negative()
{
    StatsdV.server.addr = "192.0.2.10";
    StatsdV.server.port = 8125;
    StatsdV.tags.global = NULL;
    Statsd.init(protocore_statsd_span());
    StatsdV.metric.name = "api.hits";
    StatsdV.value.i64 = 3;
    StatsdV.metric.rate = 1.0f;
    Statsd.count(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("api.hits:3|c", captured());
    protocore_net_host_udp_reset();
    StatsdV.metric.name = "api.hits";
    StatsdV.value.i64 = -4;
    StatsdV.metric.rate = 1.0f;
    Statsd.count(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("api.hits:-4|c", captured());
}

void test_emit_gauge_and_delta()
{
    StatsdV.server.addr = "192.0.2.10";
    StatsdV.server.port = 0;
    StatsdV.tags.global = NULL;
    Statsd.init(protocore_statsd_span());
    StatsdV.metric.name = "heap.free";
    StatsdV.value.i64 = 200000;
    Statsd.gauge(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("heap.free:200000|g", captured());
    protocore_net_host_udp_reset();
    StatsdV.metric.name = "conns";
    StatsdV.value.i64 = 5;
    Statsd.gauge_delta(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("conns:+5|g", captured());
    protocore_net_host_udp_reset();
    StatsdV.metric.name = "conns";
    StatsdV.value.i64 = -2;
    Statsd.gauge_delta(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("conns:-2|g", captured());
}

void test_emit_timing_set_sampled()
{
    StatsdV.server.addr = "192.0.2.10";
    StatsdV.server.port = 8125;
    StatsdV.tags.global = NULL;
    Statsd.init(protocore_statsd_span());
    StatsdV.metric.name = "db.query";
    StatsdV.value.ms = 120;
    Statsd.timing(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("db.query:120|ms", captured());
    protocore_net_host_udp_reset();
    StatsdV.metric.name = "uniques";
    StatsdV.value.member = "device-7";
    Statsd.set(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("uniques:device-7|s", captured());
    protocore_net_host_udp_reset();
    StatsdV.metric.name = "rare";
    StatsdV.value.i64 = 1;
    StatsdV.metric.rate = 0.25f;
    Statsd.count(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("rare:1|c|@0.25", captured());
}

void test_emit_global_tags()
{
    StatsdV.server.addr = "192.0.2.10";
    StatsdV.server.port = 8125;
    StatsdV.tags.global = "env:prod,region:us";
    Statsd.init(protocore_statsd_span());
    StatsdV.metric.name = "x";
    StatsdV.value.i64 = 1;
    StatsdV.metric.rate = 1.0f;
    Statsd.count(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c|#env:prod,region:us", captured());
}

void test_emit_noop_until_begin()
{
    StatsdV.server.addr = NULL;
    StatsdV.server.port = 0;
    StatsdV.tags.global = NULL;
    Statsd.init(protocore_statsd_span());
    protocore_net_host_udp_reset();
    StatsdV.metric.name = "x";
    StatsdV.value.i64 = 1;
    StatsdV.metric.rate = 1.0f;
    Statsd.count(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

void test_rate_clamp_and_stage_overflow()
{
    char out[64];

    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "m";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 0.0001f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_TRUE(StatsdV.n > 0);
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "m";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 0.9999f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_TRUE(StatsdV.n > 0);

    StatsdV.line.out = out;
    StatsdV.line.cap = 2;
    StatsdV.metric.name = "metric";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = 4;
    StatsdV.metric.name = "m";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_TIMING;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = 6;
    StatsdV.metric.name = "m";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 0.5f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = 7;
    StatsdV.metric.name = "m";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = "#tag:x";
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, StatsdV.n);
}

void test_format_guard_null_out_and_zero_cap()
{
    char out[64];
    StatsdV.line.out = NULL;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "a";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = 0;
    StatsdV.metric.name = "a";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, StatsdV.n);
}

void test_format_append_chain_overflow_points()
{
    char out[64];
    StatsdV.line.out = out;
    StatsdV.line.cap = 2;
    StatsdV.metric.name = "a";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = 3;
    StatsdV.metric.name = "a";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = 5;
    StatsdV.metric.name = "a";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = 6;
    StatsdV.metric.name = "a";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_TIMING;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = 8;
    StatsdV.metric.name = "a";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 0.5f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, StatsdV.n);
    StatsdV.line.out = out;
    StatsdV.line.cap = 8;
    StatsdV.metric.name = "a";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = "tg";
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_size_t(0, StatsdV.n);
}

void test_format_rate_zero_and_empty_tags()
{
    char out[64];
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "x";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 0.0f;
    StatsdV.tags.metric = NULL;
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c", out);
    StatsdV.line.out = out;
    StatsdV.line.cap = sizeof(out);
    StatsdV.metric.name = "x";
    StatsdV.value.text = "1";
    StatsdV.metric.type = STATSD_COUNTER;
    StatsdV.metric.rate = 1.0f;
    StatsdV.tags.metric = "";
    Statsd.format(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("x:1|c", out);
}

void test_emit_zero_value_and_set_null_member()
{
    StatsdV.server.addr = "192.0.2.10";
    StatsdV.server.port = 8125;
    StatsdV.tags.global = NULL;
    Statsd.init(protocore_statsd_span());
    StatsdV.metric.name = "db.zero";
    StatsdV.value.ms = 0;
    Statsd.timing(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("db.zero:0|ms", captured());
    protocore_net_host_udp_reset();
    StatsdV.metric.name = "uniques";
    StatsdV.value.member = NULL;
    Statsd.set(protocore_statsd_span());
    TEST_ASSERT_EQUAL_STRING("uniques:|s", captured());
}

void test_emit_overlong_name_is_noop()
{
    StatsdV.server.addr = "192.0.2.10";
    StatsdV.server.port = 8125;
    StatsdV.tags.global = NULL;
    Statsd.init(protocore_statsd_span());
    char longname[300];
    for (size_t i = 0; i < sizeof(longname) - 1; i++)
    {
        longname[i] = 'a';
    }
    longname[sizeof(longname) - 1] = '\0';
    protocore_net_host_udp_reset();
    StatsdV.metric.name = longname;
    StatsdV.value.i64 = 1;
    StatsdV.metric.rate = 1.0f;
    Statsd.count(protocore_statsd_span());
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}
