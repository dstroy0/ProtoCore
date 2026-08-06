// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the accept-time connection gates (network_drivers/transport/listener):
// the global fixed-window accept throttle, the per-source-IP throttle bucket table
// (independent budgets, window rollover, the millis() wrap, and bounded eviction), and
// the CIDR source-IP allowlist. Every address-keyed gate keys on the FULL family-tagged
// address (pc_ip) - never a hash or a uint32 flattening - so IPv4 and IPv6 peers are
// distinct buckets and a v6 peer cannot spray or collide its way past a per-address cap.
// These functions are always compiled so they can be host-tested; this env also compiles
// them with PC_ENABLE_ACCEPT_THROTTLE / PER_IP_THROTTLE / IP_ALLOWLIST set so the
// flag-guarded accept-callback paths build.
//
// The env overrides the budgets to small values so the boundaries are explicit:
//   ACCEPT_THROTTLE_MAX 3 / PC_WINDOW 1000   PER_IP_MAX 2 / PC_WINDOW 1000 / SLOTS 4   ALLOWLIST_SLOTS 4
// Pure host tests.

#include "network_drivers/session/proto_handler.h"
#include "network_drivers/transport/tcp.h"
#include "server/clock/clock.h"
#include "shared_primitives/ip.h"
#include <unity.h>

// A fake tick source for the pc_millis() override tests below.
static uint32_t g_fake_ticks = 0;
static uint32_t fake_ticks(void)
{
    return g_fake_ticks;
}

// Small builders so the tests read in terms of addresses, not byte plumbing.
static pc_ip v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return pc_ip_from_v4_octets(a, b, c, d);
}
static pc_ip v6(const char *s)
{
    pc_ip ip;
    ip.family = PC_IP_NONE;
    Ip.parse(s, &ip);
    return ip;
}

void setUp()
{
}
void tearDown()
{
}

// The global throttle allows up to MAX per window, then denies until the window rolls.
void test_accept_throttle_window()
{
    Tcp.listener->accept_throttle_reset();
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed(0));   // 1
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed(10));  // 2
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed(20));  // 3 == MAX
    TEST_ASSERT_FALSE(Tcp.listener->accept_allowed(30)); // 4 over budget, same window
    // A timestamp a full window later opens a fresh budget.
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed(1000));
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed(1100));
}

// The fixed-window math uses unsigned subtraction, so it survives the millis() rollover.
void test_accept_throttle_rollover()
{
    Tcp.listener->accept_throttle_reset();
    uint32_t base = 0xFFFFFE00u;                                // ~512 ticks before wrap
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed(base));       // window starts here, 1
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed(base + 100)); // 2
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed(5));          // wrapped; elapsed ~517 < 1000, 3
    TEST_ASSERT_FALSE(Tcp.listener->accept_allowed(10));        // over budget, still the same window
}

// Each source IP gets its own budget; one noisy client cannot exhaust another's.
void test_per_ip_independent_budgets()
{
    Tcp.listener->per_ip_throttle_reset();
    pc_ip a = v4(10, 0, 0, 1);
    pc_ip b = v4(10, 0, 0, 2);
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&a, 0));  // a:1
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&a, 1));  // a:2 == MAX
    TEST_ASSERT_FALSE(Tcp.listener->accept_allowed_ip(&a, 2)); // a over budget
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&b, 2));  // b independent, fresh
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&b, 3));  // b:2
    TEST_ASSERT_FALSE(Tcp.listener->accept_allowed_ip(&b, 4)); // b over budget
}

// Distinct IPv6 peers are distinct buckets (no hash collapse); a v4 and a v6 never share one.
void test_per_ip_v6_distinct_buckets()
{
    Tcp.listener->per_ip_throttle_reset();
    pc_ip a = v6("2001:db8::1");
    pc_ip b = v6("2001:db8::2");
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&a, 0));  // a:1
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&a, 1));  // a:2 == MAX
    TEST_ASSERT_FALSE(Tcp.listener->accept_allowed_ip(&a, 2)); // a over budget
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&b, 2));  // b:1 - a different v6 peer, own budget
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&b, 3));  // b:2
    TEST_ASSERT_FALSE(Tcp.listener->accept_allowed_ip(&b, 4)); // b over budget
}

// A per-IP bucket's window rolls over on its own clock.
void test_per_ip_window_rollover()
{
    Tcp.listener->per_ip_throttle_reset();
    pc_ip a = v4(192, 168, 1, 5);
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&a, 0));
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&a, 10));
    TEST_ASSERT_FALSE(Tcp.listener->accept_allowed_ip(&a, 20));  // budget used
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&a, 1000)); // window elapsed -> reset
}

// An unspecified address is untrackable; such sources defer to the global throttle (always true here).
void test_per_ip_unspecified_defers()
{
    Tcp.listener->per_ip_throttle_reset();
    pc_ip none;
    none.family = PC_IP_NONE;
    for (uint32_t i = 0; i < 10; i++)
    {
        TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&none, i));
    }
}

// More distinct addresses than buckets stays bounded: the oldest active bucket is evicted
// and the new address is still admitted (exercises the LRU-eviction branch).
void test_per_ip_eviction_bounded()
{
    Tcp.listener->per_ip_throttle_reset();
    // Fill all 4 buckets at staggered start times, none yet expired at now=500.
    for (uint32_t i = 0; i < 4; i++)
    {
        pc_ip ip = v4(10, 0, 0, (uint8_t)(i + 1));
        TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&ip, i * 100));
    }
    // A 5th distinct address must still be admitted by evicting the least-recently-started.
    pc_ip fresh = v4(10, 0, 0, 99);
    TEST_ASSERT_TRUE(Tcp.listener->accept_allowed_ip(&fresh, 500));
}

// An empty allowlist allows everything, so enabling the feature without rules never locks out.
void test_ip_allowlist_empty_allows_all()
{
    Tcp.listener->ip_allowlist_reset();
    pc_ip any = v4(8, 8, 8, 8);
    TEST_ASSERT_TRUE(Tcp.listener->ip_allowed(&any));
}

// A /24 rule matches its subnet only; host bits in the network argument are masked at compare time.
void test_ip_allowlist_cidr()
{
    Tcp.listener->ip_allowlist_reset();
    pc_ip net = v4(192, 168, 1, 0);
    TEST_ASSERT_TRUE(Tcp.listener->ip_allow_add(&net, 24));
    pc_ip in = v4(192, 168, 1, 55);
    pc_ip out = v4(192, 168, 2, 55);
    TEST_ASSERT_TRUE(Tcp.listener->ip_allowed(&in));
    TEST_ASSERT_FALSE(Tcp.listener->ip_allowed(&out));

    Tcp.listener->ip_allowlist_reset();
    pc_ip net8 = v4(10, 1, 2, 3); // host bits masked -> 10.0.0.0/8
    TEST_ASSERT_TRUE(Tcp.listener->ip_allow_add(&net8, 8));
    pc_ip in8 = v4(10, 255, 255, 255);
    pc_ip out8 = v4(11, 0, 0, 1);
    TEST_ASSERT_TRUE(Tcp.listener->ip_allowed(&in8));
    TEST_ASSERT_FALSE(Tcp.listener->ip_allowed(&out8));
}

// The CIDR-string public entry point parses v4 and v6, bare hosts, and rejects garbage.
void test_ip_allowlist_cidr_string()
{
    Tcp.listener->ip_allowlist_reset();
    TEST_ASSERT_TRUE(Tcp.listener->ip_allow_add_cidr("192.168.1.0/24"));
    TEST_ASSERT_TRUE(Tcp.listener->ip_allow_add_cidr("2001:db8::/32"));
    TEST_ASSERT_TRUE(Tcp.listener->ip_allow_add_cidr("10.0.0.5")); // bare host -> /32

    pc_ip v4in = v4(192, 168, 1, 200);
    pc_ip v4host = v4(10, 0, 0, 5);
    pc_ip v4no = v4(10, 0, 0, 6);
    pc_ip v6in = v6("2001:db8:0:0:1234::abcd");
    pc_ip v6no = v6("2001:db9::1");
    TEST_ASSERT_TRUE(Tcp.listener->ip_allowed(&v4in));
    TEST_ASSERT_TRUE(Tcp.listener->ip_allowed(&v4host));
    TEST_ASSERT_FALSE(Tcp.listener->ip_allowed(&v4no));
    TEST_ASSERT_TRUE(Tcp.listener->ip_allowed(&v6in));
    TEST_ASSERT_FALSE(Tcp.listener->ip_allowed(&v6no)); // v6 peer outside every v6 rule (and v4 rules never match)

    // Malformed input fails closed.
    TEST_ASSERT_FALSE(Tcp.listener->ip_allow_add_cidr("not-an-ip"));
    TEST_ASSERT_FALSE(Tcp.listener->ip_allow_add_cidr("192.168.1.0/33")); // prefix > 32
    TEST_ASSERT_FALSE(Tcp.listener->ip_allow_add_cidr("2001:db8::/129")); // prefix > 128
    TEST_ASSERT_FALSE(Tcp.listener->ip_allow_add_cidr("192.168.1.0/"));   // empty prefix
}

// A v4 allowlist rule must never admit a v6 peer (and vice versa) - families are isolated.
void test_ip_allowlist_family_isolation()
{
    Tcp.listener->ip_allowlist_reset();
    pc_ip v4net = v4(192, 168, 1, 0);
    TEST_ASSERT_TRUE(Tcp.listener->ip_allow_add(&v4net, 24));
    pc_ip v6peer = v6("2001:db8::1");
    TEST_ASSERT_FALSE(Tcp.listener->ip_allowed(&v6peer)); // rules exist but none match this family
}

// /32 is a single host; /0 matches everything (the full-width-shift edge is handled apart).
void test_ip_allowlist_host_and_zero_prefix()
{
    Tcp.listener->ip_allowlist_reset();
    pc_ip host = v4(203, 0, 113, 7);
    TEST_ASSERT_TRUE(Tcp.listener->ip_allow_add(&host, 32));
    pc_ip other = v4(203, 0, 113, 8);
    TEST_ASSERT_TRUE(Tcp.listener->ip_allowed(&host));
    TEST_ASSERT_FALSE(Tcp.listener->ip_allowed(&other));

    Tcp.listener->ip_allowlist_reset();
    pc_ip z = v4(0, 0, 0, 0);
    TEST_ASSERT_TRUE(Tcp.listener->ip_allow_add(&z, 0)); // /0 -> matches all v4
    pc_ip anyone = v4(1, 2, 3, 4);
    TEST_ASSERT_TRUE(Tcp.listener->ip_allowed(&anyone));
}

// add() fails closed on an out-of-range prefix and on a full table.
void test_ip_allowlist_rejects_bad_and_full()
{
    Tcp.listener->ip_allowlist_reset();
    pc_ip bad = v4(1, 0, 0, 0);
    TEST_ASSERT_FALSE(Tcp.listener->ip_allow_add(&bad, 33)); // prefix > 32
    for (int i = 0; i < 4; i++)                              // SLOTS == 4
    {
        pc_ip r = v4(10, 0, 0, (uint8_t)i);
        TEST_ASSERT_TRUE(Tcp.listener->ip_allow_add(&r, 32));
    }
    pc_ip overflow = v4(10, 0, 0, 9);
    TEST_ASSERT_FALSE(Tcp.listener->ip_allow_add(&overflow, 32)); // table full
}

// Session.proto->register_builtins() installs the always-present HTTP handler; this env compiles no
// other protocol module (telnet/ssh/modbus/opcua are all off), so no handler is installed for
// a protocol this build never registers.
void test_proto_register_builtins_installs_http(void)
{
    Session.proto->register_builtins();
    TEST_ASSERT_NOT_NULL(Session.proto->get(PROTO_HTTP));
    TEST_ASSERT_NULL(Session.proto->get(PROTO_TELNET));
}

// With no custom clock installed, pc_millis() falls through to the platform millis() mock.
void test_clock_default_is_platform_millis(void)
{
    pc_set_clock(NULL, 0); // ensure no override from a prior test
    set_millis(4242);
    TEST_ASSERT_EQUAL_UINT32(4242, pc_millis());
}

// A custom clock is divided down to the internal 1000 Hz, and (NULL, 0) reverts to the
// platform default.
void test_clock_custom_and_revert(void)
{
    pc_set_clock(fake_ticks, 8000); // 8 kHz source -> divide by 8
    g_fake_ticks = 8000;
    TEST_ASSERT_EQUAL_UINT32(1000, pc_millis());
    g_fake_ticks = 16000;
    TEST_ASSERT_EQUAL_UINT32(2000, pc_millis());

    pc_set_clock(NULL, 0);
    set_millis(777);
    TEST_ASSERT_EQUAL_UINT32(777, pc_millis());
}

// ====================================================================
// listener_accept_cb() integration: this env compiles PC_ENABLE_ACCEPT_THROTTLE /
// PER_IP_THROTTLE / IP_ALLOWLIST = 1, so the accept callback itself (non-static -
// see listener.cpp) consults these gates before claiming a pool slot, not just the
// standalone functions the tests above drive directly.
// ====================================================================

// Once the global accept-throttle budget for the window is spent, the accept callback
// itself aborts the new pcb and reports ERR_ABRT - not just the standalone gate function.
void test_accept_cb_global_throttle_rejects_over_budget()
{
    Tcp.conn->init(NULL);
    Tcp.listener->accept_throttle_reset();
    Tcp.listener->per_ip_throttle_reset();
    Tcp.listener->ip_allowlist_reset(); // empty: does not interfere with this test
    set_millis(0);

    for (int i = 0; i < PC_ACCEPT_THROTTLE_MAX; i++)
    {
        pc_pcb pcb = {0};
        TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PC_NET_OK));
    }
    pc_pcb over_budget = {0};
    int before_aborts = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(PC_NET_ERR_ABRT, listener_accept_cb((void *)(uintptr_t)0, &over_budget, PC_NET_OK));
    TEST_ASSERT_EQUAL_INT(before_aborts + 1, mock_abort_call_count());
}

// An empty allowlist (the default) allows every accept through the callback itself.
void test_accept_cb_ip_allowlist_allows_when_empty()
{
    Tcp.conn->init(NULL);
    Tcp.listener->accept_throttle_reset();
    Tcp.listener->per_ip_throttle_reset();
    Tcp.listener->ip_allowlist_reset();
    set_millis(0);

    pc_pcb pcb = {0};
    TEST_ASSERT_EQUAL_INT(PC_NET_OK, listener_accept_cb((void *)(uintptr_t)0, &pcb, PC_NET_OK));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
}

// Once any allowlist rule exists, the accept callback rejects every connection: a host
// build has no real lwIP pcb to read the peer's address from, so the synthesized source
// (PC_IP_NONE, "unspecified") never matches a real CIDR rule - the accept-time
// firewall fails closed for a peer address this build cannot actually resolve.
void test_accept_cb_ip_allowlist_rejects_once_a_rule_exists()
{
    Tcp.conn->init(NULL);
    Tcp.listener->accept_throttle_reset();
    Tcp.listener->per_ip_throttle_reset();
    Tcp.listener->ip_allowlist_reset();
    set_millis(0);

    pc_ip rule_net = v4(192, 168, 1, 0);
    TEST_ASSERT_TRUE(Tcp.listener->ip_allow_add(&rule_net, 24));

    pc_pcb pcb = {0};
    int before_aborts = mock_abort_call_count();
    TEST_ASSERT_EQUAL_INT(PC_NET_ERR_ABRT, listener_accept_cb((void *)(uintptr_t)0, &pcb, PC_NET_OK));
    TEST_ASSERT_EQUAL_INT(before_aborts + 1, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state); // no slot claimed
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
    RUN_TEST(test_proto_register_builtins_installs_http);
    RUN_TEST(test_clock_default_is_platform_millis);
    RUN_TEST(test_clock_custom_and_revert);
    RUN_TEST(test_accept_cb_global_throttle_rejects_over_budget);
    RUN_TEST(test_accept_cb_ip_allowlist_allows_when_empty);
    RUN_TEST(test_accept_cb_ip_allowlist_rejects_once_a_rule_exists);
    return UNITY_END();
}
