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

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

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
    IpV.args.text = s;
    IpV.args.out = &ip;
    Ip.parse(ip_work);
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
    TcpListener.accept_throttle_reset(protocore_tcp_listener_span());
    TcpListenerV.gate.now_ms = 0;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.now_ms = 10;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.now_ms = 20;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.now_ms = 30;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);

    TcpListenerV.gate.now_ms = 1000;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.now_ms = 1100;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
}

void test_accept_throttle_rollover()
{
    TcpListener.accept_throttle_reset(protocore_tcp_listener_span());
    uint32_t base = 0xFFFFFE00u;
    TcpListenerV.gate.now_ms = base;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.now_ms = base + 100;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.now_ms = 5;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.now_ms = 10;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
}

void test_per_ip_independent_budgets()
{
    TcpListener.per_ip_throttle_reset(protocore_tcp_listener_span());
    protocore_ip a = v4(10, 0, 0, 1);
    protocore_ip b = v4(10, 0, 0, 2);
    TcpListenerV.gate.addr = &a;
    TcpListenerV.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &a;
    TcpListenerV.gate.now_ms = 1;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &a;
    TcpListenerV.gate.now_ms = 2;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &b;
    TcpListenerV.gate.now_ms = 2;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &b;
    TcpListenerV.gate.now_ms = 3;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &b;
    TcpListenerV.gate.now_ms = 4;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
}

void test_per_ip_v6_distinct_buckets()
{
    TcpListener.per_ip_throttle_reset(protocore_tcp_listener_span());
    protocore_ip a = v6("2001:db8::1");
    protocore_ip b = v6("2001:db8::2");
    TcpListenerV.gate.addr = &a;
    TcpListenerV.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &a;
    TcpListenerV.gate.now_ms = 1;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &a;
    TcpListenerV.gate.now_ms = 2;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &b;
    TcpListenerV.gate.now_ms = 2;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &b;
    TcpListenerV.gate.now_ms = 3;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &b;
    TcpListenerV.gate.now_ms = 4;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
}

void test_per_ip_window_rollover()
{
    TcpListener.per_ip_throttle_reset(protocore_tcp_listener_span());
    protocore_ip a = v4(192, 168, 1, 5);
    TcpListenerV.gate.addr = &a;
    TcpListenerV.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &a;
    TcpListenerV.gate.now_ms = 10;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &a;
    TcpListenerV.gate.now_ms = 20;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &a;
    TcpListenerV.gate.now_ms = 1000;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
}

void test_per_ip_unspecified_defers()
{
    TcpListener.per_ip_throttle_reset(protocore_tcp_listener_span());
    protocore_ip none;
    none.family = PROTOCORE_IP_NONE;
    for (uint32_t i = 0; i < 10; i++)
    {
        TcpListenerV.gate.addr = &none;
        TcpListenerV.gate.now_ms = i;
        TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
        TEST_ASSERT_TRUE(TcpListenerV.ok);
    }
}

void test_per_ip_eviction_bounded()
{
    TcpListener.per_ip_throttle_reset(protocore_tcp_listener_span());

    for (uint32_t i = 0; i < 4; i++)
    {
        protocore_ip ip = v4(10, 0, 0, (uint8_t)(i + 1));
        TcpListenerV.gate.addr = &ip;
        TcpListenerV.gate.now_ms = i * 100;
        TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
        TEST_ASSERT_TRUE(TcpListenerV.ok);
    }

    protocore_ip fresh = v4(10, 0, 0, 99);
    TcpListenerV.gate.addr = &fresh;
    TcpListenerV.gate.now_ms = 500;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
}

void test_ip_allowlist_empty_allows_all()
{
    TcpListener.ip_allowlist_reset(protocore_tcp_listener_span());
    protocore_ip any = v4(8, 8, 8, 8);
    TcpListenerV.gate.addr = &any;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
}

void test_ip_allowlist_cidr()
{
    TcpListener.ip_allowlist_reset(protocore_tcp_listener_span());
    protocore_ip net = v4(192, 168, 1, 0);
    TcpListenerV.gate.addr = &net;
    TcpListenerV.gate.prefix_len = 24;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    protocore_ip in = v4(192, 168, 1, 55);
    protocore_ip out = v4(192, 168, 2, 55);
    TcpListenerV.gate.addr = &in;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &out;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);

    TcpListener.ip_allowlist_reset(protocore_tcp_listener_span());
    protocore_ip net8 = v4(10, 1, 2, 3);
    TcpListenerV.gate.addr = &net8;
    TcpListenerV.gate.prefix_len = 8;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    protocore_ip in8 = v4(10, 255, 255, 255);
    protocore_ip out8 = v4(11, 0, 0, 1);
    TcpListenerV.gate.addr = &in8;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &out8;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
}

void test_ip_allowlist_cidr_string()
{
    TcpListener.ip_allowlist_reset(protocore_tcp_listener_span());
    TcpListenerV.gate.cidr = "192.168.1.0/24";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.cidr = "2001:db8::/32";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.cidr = "10.0.0.5";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);

    protocore_ip v4in = v4(192, 168, 1, 200);
    protocore_ip v4host = v4(10, 0, 0, 5);
    protocore_ip v4no = v4(10, 0, 0, 6);
    protocore_ip v6in = v6("2001:db8:0:0:1234::abcd");
    protocore_ip v6no = v6("2001:db9::1");
    TcpListenerV.gate.addr = &v4in;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &v4host;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &v4no;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &v6in;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &v6no;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);

    TcpListenerV.gate.cidr = "not-an-ip";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
    TcpListenerV.gate.cidr = "192.168.1.0/33";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
    TcpListenerV.gate.cidr = "2001:db8::/129";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
    TcpListenerV.gate.cidr = "192.168.1.0/";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
}

void test_ip_allowlist_family_isolation()
{
    TcpListener.ip_allowlist_reset(protocore_tcp_listener_span());
    protocore_ip v4net = v4(192, 168, 1, 0);
    TcpListenerV.gate.addr = &v4net;
    TcpListenerV.gate.prefix_len = 24;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    protocore_ip v6peer = v6("2001:db8::1");
    TcpListenerV.gate.addr = &v6peer;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
}

void test_ip_allowlist_host_and_zero_prefix()
{
    TcpListener.ip_allowlist_reset(protocore_tcp_listener_span());
    protocore_ip host = v4(203, 0, 113, 7);
    TcpListenerV.gate.addr = &host;
    TcpListenerV.gate.prefix_len = 32;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    protocore_ip other = v4(203, 0, 113, 8);
    TcpListenerV.gate.addr = &host;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    TcpListenerV.gate.addr = &other;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);

    TcpListener.ip_allowlist_reset(protocore_tcp_listener_span());
    protocore_ip z = v4(0, 0, 0, 0);
    TcpListenerV.gate.addr = &z;
    TcpListenerV.gate.prefix_len = 0;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
    protocore_ip anyone = v4(1, 2, 3, 4);
    TcpListenerV.gate.addr = &anyone;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);
}

void test_ip_allowlist_rejects_bad_and_full()
{
    TcpListener.ip_allowlist_reset(protocore_tcp_listener_span());
    protocore_ip bad = v4(1, 0, 0, 0);
    TcpListenerV.gate.addr = &bad;
    TcpListenerV.gate.prefix_len = 33;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
    for (int i = 0; i < 4; i++)
    {
        protocore_ip r = v4(10, 0, 0, (uint8_t)i);
        TcpListenerV.gate.addr = &r;
        TcpListenerV.gate.prefix_len = 32;
        TcpListener.ip_allow_add(protocore_tcp_listener_span());
        TEST_ASSERT_TRUE(TcpListenerV.ok);
    }
    protocore_ip overflow = v4(10, 0, 0, 9);
    TcpListenerV.gate.addr = &overflow;
    TcpListenerV.gate.prefix_len = 32;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListenerV.ok);
}

void test_protocore_register_builtins_installs_http(void)
{
    Protocols.register_builtins(protocore_session_span());
    Protocols.proto = PROTO_HTTP;
    Protocols.get(protocore_session_span());
    TEST_ASSERT_NOT_NULL(Protocols.handler);
    Protocols.proto = PROTO_TELNET;
    Protocols.get(protocore_session_span());
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
    ConnPoolV.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    TcpListener.accept_throttle_reset(protocore_tcp_listener_span());
    TcpListener.per_ip_throttle_reset(protocore_tcp_listener_span());
    TcpListener.ip_allowlist_reset(protocore_tcp_listener_span());
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
    ConnPoolV.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    TcpListener.accept_throttle_reset(protocore_tcp_listener_span());
    TcpListener.per_ip_throttle_reset(protocore_tcp_listener_span());
    TcpListener.ip_allowlist_reset(protocore_tcp_listener_span());
    set_millis(0);

    protocore_pcb pcb = {0};
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

void test_accept_cb_ip_allowlist_rejects_once_a_rule_exists()
{
    ConnPoolV.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    TcpListener.accept_throttle_reset(protocore_tcp_listener_span());
    TcpListener.per_ip_throttle_reset(protocore_tcp_listener_span());
    TcpListener.ip_allowlist_reset(protocore_tcp_listener_span());
    set_millis(0);

    protocore_ip rule_net = v4(192, 168, 1, 0);
    TcpListenerV.gate.addr = &rule_net;
    TcpListenerV.gate.prefix_len = 24;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListenerV.ok);

    protocore_pcb pcb = {0};
    int before_aborts = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_ERR_ABRT, listener_accept_cb((void *)(uintptr_t)0, &pcb, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL_INT(before_aborts + 1, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}
