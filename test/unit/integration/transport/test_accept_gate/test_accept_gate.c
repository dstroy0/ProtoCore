// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/session/session.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "server/clock/clock.h"
#include "server/core/proto_handler.h"
#include "shared/ip/ip.h"
#include <unity.h>

// The library's millisecond clock, read through the namespace.
static uint32_t clock_ms(void)
{
    Clock.millis(Clock.internal);
    return Clock.ms;
}

static uint32_t g_fake_ticks = 0;
static uint32_t fake_ticks(void)
{
    return g_fake_ticks;
}

static protocore_ip v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return protocore_ip_from_v4_octets(a, b, c, d);
}
static protocore_ip v6(const char *s)
{
    protocore_ip ip;
    ip.family = PROTOCORE_IP_NONE;
    Ip.args.text = s;
    Ip.args.out = &ip;
    Ip.parse(Ip.internal);
    return ip;
}

void setUp()
{
}
void tearDown()
{
}

void test_accept_throttle_window()
{
    TcpListener.accept_throttle_reset(TcpListener.internal);
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.now_ms = 10;
    TcpListener.accept_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.now_ms = 20;
    TcpListener.accept_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.now_ms = 30;
    TcpListener.accept_allowed(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);

    TcpListener.gate.now_ms = 1000;
    TcpListener.accept_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.now_ms = 1100;
    TcpListener.accept_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_accept_throttle_rollover()
{
    TcpListener.accept_throttle_reset(TcpListener.internal);
    uint32_t base = 0xFFFFFE00u;
    TcpListener.gate.now_ms = base;
    TcpListener.accept_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.now_ms = base + 100;
    TcpListener.accept_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.now_ms = 5;
    TcpListener.accept_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.now_ms = 10;
    TcpListener.accept_allowed(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_per_ip_independent_budgets()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    protocore_ip a = v4(10, 0, 0, 1);
    protocore_ip b = v4(10, 0, 0, 2);
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 1;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 2;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.addr = &b;
    TcpListener.gate.now_ms = 2;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &b;
    TcpListener.gate.now_ms = 3;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &b;
    TcpListener.gate.now_ms = 4;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_per_ip_v6_distinct_buckets()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    protocore_ip a = v6("2001:db8::1");
    protocore_ip b = v6("2001:db8::2");
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 1;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 2;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.addr = &b;
    TcpListener.gate.now_ms = 2;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &b;
    TcpListener.gate.now_ms = 3;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &b;
    TcpListener.gate.now_ms = 4;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_per_ip_window_rollover()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    protocore_ip a = v4(192, 168, 1, 5);
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 10;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 20;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 1000;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_per_ip_unspecified_defers()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    protocore_ip none;
    none.family = PROTOCORE_IP_NONE;
    for (uint32_t i = 0; i < 10; i++)
    {
        TcpListener.gate.addr = &none;
        TcpListener.gate.now_ms = i;
        TcpListener.accept_allowed_ip(TcpListener.internal);
        TEST_ASSERT_TRUE(TcpListener.ok);
    }
}

void test_per_ip_eviction_bounded()
{
    TcpListener.per_ip_throttle_reset(TcpListener.internal);

    for (uint32_t i = 0; i < 4; i++)
    {
        protocore_ip ip = v4(10, 0, 0, (uint8_t)(i + 1));
        TcpListener.gate.addr = &ip;
        TcpListener.gate.now_ms = i * 100;
        TcpListener.accept_allowed_ip(TcpListener.internal);
        TEST_ASSERT_TRUE(TcpListener.ok);
    }

    protocore_ip fresh = v4(10, 0, 0, 99);
    TcpListener.gate.addr = &fresh;
    TcpListener.gate.now_ms = 500;
    TcpListener.accept_allowed_ip(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_ip_allowlist_empty_allows_all()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip any = v4(8, 8, 8, 8);
    TcpListener.gate.addr = &any;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_ip_allowlist_cidr()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip net = v4(192, 168, 1, 0);
    TcpListener.gate.addr = &net;
    TcpListener.gate.prefix_len = 24;
    TcpListener.ip_allow_add(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip in = v4(192, 168, 1, 55);
    protocore_ip out = v4(192, 168, 2, 55);
    TcpListener.gate.addr = &in;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &out;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);

    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip net8 = v4(10, 1, 2, 3);
    TcpListener.gate.addr = &net8;
    TcpListener.gate.prefix_len = 8;
    TcpListener.ip_allow_add(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip in8 = v4(10, 255, 255, 255);
    protocore_ip out8 = v4(11, 0, 0, 1);
    TcpListener.gate.addr = &in8;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &out8;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_ip_allowlist_cidr_string()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    TcpListener.gate.cidr = "192.168.1.0/24";
    TcpListener.ip_allow_add_cidr(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.cidr = "2001:db8::/32";
    TcpListener.ip_allow_add_cidr(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.cidr = "10.0.0.5";
    TcpListener.ip_allow_add_cidr(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);

    protocore_ip v4in = v4(192, 168, 1, 200);
    protocore_ip v4host = v4(10, 0, 0, 5);
    protocore_ip v4no = v4(10, 0, 0, 6);
    protocore_ip v6in = v6("2001:db8:0:0:1234::abcd");
    protocore_ip v6no = v6("2001:db9::1");
    TcpListener.gate.addr = &v4in;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &v4host;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &v4no;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.addr = &v6in;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &v6no;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);

    TcpListener.gate.cidr = "not-an-ip";
    TcpListener.ip_allow_add_cidr(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.cidr = "192.168.1.0/33";
    TcpListener.ip_allow_add_cidr(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.cidr = "2001:db8::/129";
    TcpListener.ip_allow_add_cidr(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.cidr = "192.168.1.0/";
    TcpListener.ip_allow_add_cidr(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_ip_allowlist_family_isolation()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip v4net = v4(192, 168, 1, 0);
    TcpListener.gate.addr = &v4net;
    TcpListener.gate.prefix_len = 24;
    TcpListener.ip_allow_add(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip v6peer = v6("2001:db8::1");
    TcpListener.gate.addr = &v6peer;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_ip_allowlist_host_and_zero_prefix()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip host = v4(203, 0, 113, 7);
    TcpListener.gate.addr = &host;
    TcpListener.gate.prefix_len = 32;
    TcpListener.ip_allow_add(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip other = v4(203, 0, 113, 8);
    TcpListener.gate.addr = &host;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &other;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);

    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip z = v4(0, 0, 0, 0);
    TcpListener.gate.addr = &z;
    TcpListener.gate.prefix_len = 0;
    TcpListener.ip_allow_add(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip anyone = v4(1, 2, 3, 4);
    TcpListener.gate.addr = &anyone;
    TcpListener.ip_allowed(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);
}

void test_ip_allowlist_rejects_bad_and_full()
{
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    protocore_ip bad = v4(1, 0, 0, 0);
    TcpListener.gate.addr = &bad;
    TcpListener.gate.prefix_len = 33;
    TcpListener.ip_allow_add(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
    for (int i = 0; i < 4; i++)
    {
        protocore_ip r = v4(10, 0, 0, (uint8_t)i);
        TcpListener.gate.addr = &r;
        TcpListener.gate.prefix_len = 32;
        TcpListener.ip_allow_add(TcpListener.internal);
        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    protocore_ip overflow = v4(10, 0, 0, 9);
    TcpListener.gate.addr = &overflow;
    TcpListener.gate.prefix_len = 32;
    TcpListener.ip_allow_add(TcpListener.internal);
    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_protocore_register_builtins_installs_http(void)
{
    Protocols.register_builtins(Protocols.internal);
    Protocols.proto = PROTO_HTTP;
    Protocols.get(Protocols.internal);
    TEST_ASSERT_NOT_NULL(Protocols.handler);
    Protocols.proto = PROTO_TELNET;
    Protocols.get(Protocols.internal);
    TEST_ASSERT_NULL(Protocols.handler);
}

void test_clock_default_is_platform_millis(void)
{
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 0;
    Clock.set_ms(Clock.internal);
    set_millis(4242);
    TEST_ASSERT_EQUAL_UINT32(4242, clock_ms());
}

void test_clock_custom_and_revert(void)
{
    Clock.src.fn = fake_ticks;
    Clock.src.ticks_per_second = 8000;
    Clock.set_ms(Clock.internal);
    g_fake_ticks = 8000;
    TEST_ASSERT_EQUAL_UINT32(1000, clock_ms());
    g_fake_ticks = 16000;
    TEST_ASSERT_EQUAL_UINT32(2000, clock_ms());

    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 0;
    Clock.set_ms(Clock.internal);
    set_millis(777);
    TEST_ASSERT_EQUAL_UINT32(777, clock_ms());
}

void test_accept_cb_global_throttle_rejects_over_budget()
{
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(ConnPool.internal);
    TcpListener.accept_throttle_reset(TcpListener.internal);
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    set_millis(0);

    for (int i = 0; i < PROTOCORE_ACCEPT_THROTTLE_MAX; i++)
    {
        protocore_pcb pcb = {0};
        TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    }
    protocore_pcb over_budget = {0};
    int before_aborts = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_ERR_ABRT,
                          listener_accept_cb((void *)(uintptr_t)0, &over_budget, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_INT(before_aborts + 1, mock_abort_call_count());
}

void test_accept_cb_ip_allowlist_allows_when_empty()
{
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(ConnPool.internal);
    TcpListener.accept_throttle_reset(TcpListener.internal);
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    set_millis(0);

    protocore_pcb pcb = {0};
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_accept_cb_ip_allowlist_rejects_once_a_rule_exists()
{
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(ConnPool.internal);
    TcpListener.accept_throttle_reset(TcpListener.internal);
    TcpListener.per_ip_throttle_reset(TcpListener.internal);
    TcpListener.ip_allowlist_reset(TcpListener.internal);
    set_millis(0);

    protocore_ip rule_net = v4(192, 168, 1, 0);
    TcpListener.gate.addr = &rule_net;
    TcpListener.gate.prefix_len = 24;
    TcpListener.ip_allow_add(TcpListener.internal);
    TEST_ASSERT_TRUE(TcpListener.ok);

    protocore_pcb pcb = {0};
    int before_aborts = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_ERR_ABRT, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_INT(before_aborts + 1, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_accept_throttle_window);
    RUN_TEST(test_accept_throttle_rollover);
    RUN_TEST(test_per_ip_independent_budgets);
    RUN_TEST(test_per_ip_v6_distinct_buckets);
    RUN_TEST(test_per_ip_window_rollover);
    RUN_TEST(test_per_ip_unspecified_defers);
    RUN_TEST(test_per_ip_eviction_bounded);
    RUN_TEST(test_ip_allowlist_empty_allows_all);
    RUN_TEST(test_ip_allowlist_cidr);
    RUN_TEST(test_ip_allowlist_cidr_string);
    RUN_TEST(test_ip_allowlist_family_isolation);
    RUN_TEST(test_ip_allowlist_host_and_zero_prefix);
    RUN_TEST(test_ip_allowlist_rejects_bad_and_full);
    RUN_TEST(test_protocore_register_builtins_installs_http);
    RUN_TEST(test_clock_default_is_platform_millis);
    RUN_TEST(test_clock_custom_and_revert);
    RUN_TEST(test_accept_cb_global_throttle_rejects_over_budget);
    RUN_TEST(test_accept_cb_ip_allowlist_allows_when_empty);
    RUN_TEST(test_accept_cb_ip_allowlist_rejects_once_a_rule_exists);
    return UNITY_END();
}
