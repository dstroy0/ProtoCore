// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for src/network_drivers/transport/tcp/server/server.h - bound ports, their event
// queues, and the accept-time gates.
//
// The RFC anchors, from docs/learn/rfc/text/:
//
//   RFC 9293 sec 3.10.5  ABORT in ESTABLISHED "sends a reset segment" and flushes the queues. A
//                        connection this layer refuses has already completed its handshake in the
//                        stack below, so refusing it is an abort, not a quiet drop.
//   RFC 4632 sec 3.1     CIDR: an address is matched against a prefix of the stated length, and the
//                        bits past that length are not part of the comparison.
//   RFC 2474 sec 3       The DS field carries a 6-bit DSCP in the high bits, with the low two bits
//                        currently unused; so the octet is DSCP << 2.
//
// The accept callback is non-static for exactly this reason: on the host there is no real accept
// event to drive it, so a test calls it with a fabricated control block.

#include "network_drivers/transport/diffserv/diffserv.h" // the DSCP code points the marking tests name
#include "network_drivers/transport/tcp/common.h" // conn_pool, listener_pool, protocore_ap_ip
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include "network_drivers/transport/tcp/server/server.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "shared/ip/ip.h"
#include "server/clock/clock.h" // Clock.millis: what a dispatch pass stamps
#include <string.h>

#include <unity.h>

static protocore_pcb g_newpcb;

// Move the virtual clock and take the stamp a dispatch pass would. Clock.ms is where the last
// reading landed, and service_once() refreshes it once per pass before anything here reads the
// time; a case that drives the accept callback directly stands in for that pass.
static void advance_to(uint32_t ms)
{
    set_millis(ms);
    Clock.millis(Clock.internal);
}

void setUp(void)
{
    advance_to(0);
    queue_stage_reset();
    mock_abort_call_reset();
    memset(&g_newpcb, 0, sizeof(g_newpcb));
    protocore_ap_ip = 0;
    ConnPool.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    TcpListener.stop_all(protocore_tcp_listener_span());
    TcpListener.accept_throttle_reset(protocore_tcp_listener_span());
    TcpListener.per_ip_throttle_reset(protocore_tcp_listener_span());
    TcpListener.ip_allowlist_reset(protocore_tcp_listener_span());
    TcpListener.idx = 0;
    TcpListener.bind.port = 80;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
}

void tearDown(void)
{
    TcpListener.stop_all(protocore_tcp_listener_span());
}

// The accept callback takes its listener index through the control block's user data, the way
// listener_add() stamps it onto the listening pcb.
static protocore_net_err accept_on(uint8_t idx, protocore_pcb *pcb)
{
    return listener_accept_cb((void *)(uintptr_t)idx, pcb, PROTOCORE_NET_OK);
}

static int drain_events(uint8_t idx, TcpEvt *out, int cap)
{
    int n = 0;
    while (n < cap && protocore_platform_queue_recv(listener_pool[idx].queue, &out[n], 0) == PROTOCORE_PLATFORM_OK)
    {
        n++;
    }
    return n;
}

static protocore_ip v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    protocore_ip ip;
    memset(&ip, 0, sizeof(ip));
    ip.family = PROTOCORE_IP_V4;
    ip.bytes[0] = a;
    ip.bytes[1] = b;
    ip.bytes[2] = c;
    ip.bytes[3] = d;
    return ip;
}

// ---------------------------------------------------------------------------
// Accept: claiming and stamping a slot
// ---------------------------------------------------------------------------

// An accept claims the lowest free slot and leaves it ready to receive: cursors at zero, the
// request deadline unarmed, and the stack callbacks wired to the control block.
void test_accept_claims_a_slot_and_wires_the_connection(void)
{
    advance_to(4321);
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(0, &g_newpcb));

    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL_PTR(&g_newpcb, conn_pool[0].pcb);
    TEST_ASSERT_EQUAL_UINT8(0, conn_pool[0].listener_id);
    TEST_ASSERT_EQUAL(PROTO_HTTP, conn_pool[0].proto);
    TEST_ASSERT_EQUAL_UINT32(4321, conn_pool[0].last_activity_ms);
    // The request deadline is not armed here and is not the transport's: it lives in
    // http_req_start_ms[], armed on EVT_DATA where the session drains events (test_session).
    ConnPool.slot = 0;
    ConnPool.available(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT(0, ConnPool.n);

    // The control block now points back at the slot, and carries this layer's callbacks.
    TEST_ASSERT_EQUAL_PTR(&conn_pool[0], g_newpcb.arg);
    TEST_ASSERT_NOT_NULL(g_newpcb.on_recv);
    TEST_ASSERT_NOT_NULL(g_newpcb.on_sent);
    TEST_ASSERT_NOT_NULL(g_newpcb.on_err);
}

// The slot's ring cursors are reset at accept, so a slot reused after a previous connection does
// not inherit its predecessor's unread bytes.
void test_accept_resets_the_ring_of_a_reused_slot(void)
{
    conn_pool[0].rx_head = 500;
    conn_pool[0].rx_tail = 100;
    conn_pool[0].rx_acked = 100;

    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(0, &g_newpcb));
    ConnPool.slot = 0;
    ConnPool.available(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT(0, ConnPool.n);
    TEST_ASSERT_EQUAL_UINT(0, conn_pool[0].rx_acked);
}

// Each accept posts EVT_CONNECT naming the slot it claimed, which is how the session layer learns
// a connection exists at all.
void test_accept_posts_a_connect_event(void)
{
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(0, &g_newpcb));

    TcpEvt evts[4];
    TEST_ASSERT_EQUAL_INT(1, drain_events(0, evts, 4));
    TEST_ASSERT_EQUAL(EVT_CONNECT, evts[0].type);
    TEST_ASSERT_EQUAL_UINT8(0, evts[0].slot_id);
    TEST_ASSERT_EQUAL_UINT(0, evts[0].data_len);
}

// Two accepts take two different slots.
void test_successive_accepts_take_distinct_slots(void)
{
    protocore_pcb second;
    memset(&second, 0, sizeof(second));
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(0, &g_newpcb));
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(0, &second));

    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[1].state);
    TEST_ASSERT_EQUAL_PTR(&g_newpcb, conn_pool[0].pcb);
    TEST_ASSERT_EQUAL_PTR(&second, conn_pool[1].pcb);
}

// RFC 9293 sec 3.10.5: the handshake already completed below this layer, so a connection refused
// for want of a slot is aborted - a reset - rather than left dangling.
void test_accept_resets_the_connection_when_the_pool_is_full(void)
{
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        ConnPool.slot = i;
        ConnPool.st = CONN_ACTIVE;
        ConnPool.set_state(protocore_conn_pool_span());
    }
    int aborts_before = mock_abort_call_count();

    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_ABRT, accept_on(0, &g_newpcb));
    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count());
}

// A failed accept from the stack, or a missing control block, is rejected before any slot is
// touched.
void test_accept_rejects_a_failed_handshake_or_a_null_control_block(void)
{
    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_VAL, listener_accept_cb((void *)0, NULL, PROTOCORE_NET_OK));
    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_VAL, listener_accept_cb((void *)0, &g_newpcb, PROTOCORE_NET_ERR_ABRT));
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

void test_accept_rejects_a_listener_index_past_the_pool(void)
{
    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_VAL, accept_on(MAX_LISTENERS, &g_newpcb));
    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_VAL, accept_on((uint8_t)(MAX_LISTENERS + 3), &g_newpcb));
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

// The connection is tagged with the interface it arrived on by comparing its local address against
// the configured access-point address, which is what per-route interface filters read.
void test_accept_tags_the_ingress_interface(void)
{
    protocore_net_ip4_set(&g_newpcb.local_ip, 192, 168, 4, 1);
    protocore_ap_ip = protocore_net_ip4_u32(&g_newpcb.local_ip);

    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(0, &g_newpcb));
    ConnPool.slot = 0;
    ConnPool.iface(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL(PROTOCORE_IF_WIFI_AP, ConnPool.if_kind);
}

void test_accept_tags_a_station_connection_when_no_access_point_matches(void)
{
    protocore_net_ip4_set(&g_newpcb.local_ip, 10, 0, 0, 7);
    protocore_ap_ip = 0;

    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(0, &g_newpcb));
    ConnPool.slot = 0;
    ConnPool.iface(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL(PROTOCORE_IF_WIFI_STA, ConnPool.if_kind);
}

// The listener's protocol is stamped onto every connection it accepts, so one port's traffic
// cannot be routed to another port's handler.
void test_each_listener_stamps_its_own_protocol(void)
{
    TcpListener.idx = 1;
    TcpListener.bind.port = 23;
    TcpListener.bind.proto = PROTO_TELNET;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    protocore_pcb second;
    memset(&second, 0, sizeof(second));

    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(0, &g_newpcb));
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(1, &second));

    TEST_ASSERT_EQUAL(PROTO_HTTP, conn_pool[0].proto);
    TEST_ASSERT_EQUAL_UINT8(0, conn_pool[0].listener_id);
    TEST_ASSERT_EQUAL(PROTO_TELNET, conn_pool[1].proto);
    TEST_ASSERT_EQUAL_UINT8(1, conn_pool[1].listener_id);
}

// An accept whose event cannot be queued still leaves a usable connection: the event is dropped,
// not the connection, and the idle sweep is the backstop.
void test_accept_survives_a_full_event_queue(void)
{
    mock_queue_send_fail_once();
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(0, &g_newpcb));
    TEST_ASSERT_EQUAL(CONN_ACTIVE, (ConnState)conn_pool[0].state);
    TEST_ASSERT_EQUAL_PTR(&g_newpcb, conn_pool[0].pcb);
}

// ---------------------------------------------------------------------------
// RFC 2474 - DiffServ marking on accepted connections
// ---------------------------------------------------------------------------

// sec 3: the DS field is a 6-bit DSCP in the high bits over two currently-unused bits, so the
// octet a connection carries is the code point shifted left by two.
void test_a_listener_dscp_marks_the_connections_it_accepts(void)
{
    TcpListener.bind.port = 80;
    TcpListener.bind.dscp = PROTOCORE_DSCP_EF;
    TcpListener.set_dscp(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(0, &g_newpcb));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(PROTOCORE_DSCP_EF << 2), g_newpcb.tos);
    TEST_ASSERT_EQUAL_UINT8(0, g_newpcb.tos & 0x03); // the two unused bits stay clear
}

// A code point wider than six bits is masked to the field, so it cannot spill into the low bits.
void test_a_listener_dscp_is_masked_to_six_bits(void)
{
    TcpListener.bind.port = 80;
    TcpListener.bind.dscp = 0x7E;
    TcpListener.set_dscp(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok); // 0b1111110 -> 0b111110
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(0, &g_newpcb));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)((0x7E & 0x3F) << 2), g_newpcb.tos);
}

// The per-listener mark applies only to the port it was set on.
void test_setting_a_dscp_on_an_unbound_port_reports_failure(void)
{
    TcpListener.bind.port = 8080;
    TcpListener.bind.dscp = PROTOCORE_DSCP_EF;
    TcpListener.set_dscp(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
}

// Best effort leaves the field alone rather than writing a zero code point over it.
void test_an_unmarked_listener_leaves_the_ds_field_clear(void)
{
    TEST_ASSERT_EQUAL(PROTOCORE_NET_OK, accept_on(0, &g_newpcb));
    TEST_ASSERT_EQUAL_UINT8(0, g_newpcb.tos);
}

// A live connection is not re-marked, and there is no call that would: RFC 9293 sec 3.9.2
// SHLD-23 says an application should not change the Diffserv field during a connection, so a bound
// port is the finest granularity. TcpListener.set_dscp is that granularity, and test_diffserv
// covers it end to end - the override, the sentinel, and what the accept callback stamps.

// ---------------------------------------------------------------------------
// Accept-rate gates
// ---------------------------------------------------------------------------

// The global throttle is a fixed window: the budget is spent, then refills when the window rolls.
void test_the_global_throttle_spends_and_refills_its_window(void)
{
    for (int i = 0; i < PROTOCORE_ACCEPT_THROTTLE_MAX; i++)
    {
        TcpListener.gate.now_ms = 0;
        TcpListener.accept_allowed(protocore_tcp_listener_span());
        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);

    TcpListener.gate.now_ms = PROTOCORE_ACCEPT_THROTTLE_WINDOW_MS;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
}

// The elapsed test is an unsigned subtraction, so the window still rolls across the counter wrap.
void test_the_global_throttle_survives_the_counter_wrap(void)
{
    uint32_t late = 0xFFFFFF00u;
    for (int i = 0; i < PROTOCORE_ACCEPT_THROTTLE_MAX; i++)
    {
        TcpListener.gate.now_ms = late;
        TcpListener.accept_allowed(protocore_tcp_listener_span());
        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    TcpListener.gate.now_ms = late;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.now_ms = late + PROTOCORE_ACCEPT_THROTTLE_WINDOW_MS;
    TcpListener.accept_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
}

// An accept beyond the budget is reset, not silently dropped, and claims no slot.
void test_an_accept_over_the_global_budget_is_reset(void)
{
    for (int i = 0; i < PROTOCORE_ACCEPT_THROTTLE_MAX; i++)
    {
        TcpListener.gate.now_ms = 0;
        TcpListener.accept_allowed(protocore_tcp_listener_span());
        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    int aborts_before = mock_abort_call_count();

    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_ABRT, accept_on(0, &g_newpcb));
    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

// Each source address gets its own budget, so one noisy client cannot spend another's.
void test_each_address_has_its_own_budget(void)
{
    protocore_ip a = v4(10, 0, 0, 1);
    protocore_ip b = v4(10, 0, 0, 2);

    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_MAX; i++)
    {
        TcpListener.gate.addr = &a;
        TcpListener.gate.now_ms = 0;
        TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok); // a is spent
    TcpListener.gate.addr = &b;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);  // b is not
}

// The bucket is keyed on the whole address, so a v6 peer cannot be confused with a v4 one.
void test_an_address_family_does_not_share_a_bucket(void)
{
    protocore_ip v4a = v4(0, 0, 0, 1);
    protocore_ip v6a;
    memset(&v6a, 0, sizeof(v6a));
    v6a.family = PROTOCORE_IP_V6;
    v6a.bytes[15] = 1; // ::1, the same trailing octet as the v4 address

    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_MAX; i++)
    {
        TcpListener.gate.addr = &v4a;
        TcpListener.gate.now_ms = 0;
        TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    TcpListener.gate.addr = &v4a;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.addr = &v6a;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok); // its own bucket
}

// An address that cannot be attributed is left to the global throttle rather than sharing one
// bucket with every other untrackable peer.
void test_an_unspecified_address_defers_to_the_global_throttle(void)
{
    protocore_ip none;
    memset(&none, 0, sizeof(none));
    none.family = PROTOCORE_IP_NONE;
    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_MAX + 4; i++)
    {
        TcpListener.gate.addr = &none;
        TcpListener.gate.now_ms = 0;
        TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
        TEST_ASSERT_TRUE(TcpListener.ok);
    }
}

// The bucket table is bounded, so a flood of distinct addresses evicts rather than growing.
void test_the_bucket_table_is_bounded_and_evicts(void)
{
    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_SLOTS + 4; i++)
    {
        protocore_ip ip = v4(10, 0, 1, (uint8_t)i);
        TcpListener.gate.addr = &ip;
        TcpListener.gate.now_ms = (uint32_t)i;
        TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
        TEST_ASSERT_TRUE(TcpListener.ok);
    }
}

void test_a_per_address_window_refills(void)
{
    protocore_ip a = v4(10, 0, 0, 1);
    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_MAX; i++)
    {
        TcpListener.gate.addr = &a;
        TcpListener.gate.now_ms = 0;
        TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = 0;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.addr = &a;
    TcpListener.gate.now_ms = PROTOCORE_PER_IP_THROTTLE_WINDOW_MS;
    TcpListener.accept_allowed_ip(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
}

// ---------------------------------------------------------------------------
// RFC 4632 - the source-address allowlist
// ---------------------------------------------------------------------------

// No rules means no filter: enabling the feature before adding any rule cannot lock the device out.
void test_an_empty_allowlist_admits_everything(void)
{
    protocore_ip any = v4(203, 0, 113, 9);
    TcpListener.gate.addr = &any;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
}

// sec 3.1: only the leading prefix_len bits take part in the comparison; the host bits do not.
void test_a_prefix_matches_on_its_network_bits_alone(void)
{
    protocore_ip net = v4(192, 168, 1, 0);
    TcpListener.gate.addr = &net;
    TcpListener.gate.prefix_len = 24;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);

    protocore_ip inside = v4(192, 168, 1, 200); // differs only in host bits
    protocore_ip outside = v4(192, 168, 2, 1);  // differs inside the prefix
    TcpListener.gate.addr = &inside;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &outside;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
}

// Host bits set in the rule itself are not part of the comparison either.
void test_host_bits_in_a_rule_are_ignored(void)
{
    protocore_ip sloppy = v4(192, 168, 1, 77); // a /24 written with a host address
    TcpListener.gate.addr = &sloppy;
    TcpListener.gate.prefix_len = 24;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);

    protocore_ip other = v4(192, 168, 1, 5);
    TcpListener.gate.addr = &other;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
}

// A zero-length prefix covers the whole address space.
void test_a_zero_length_prefix_matches_every_address(void)
{
    protocore_ip net = v4(0, 0, 0, 0);
    TcpListener.gate.addr = &net;
    TcpListener.gate.prefix_len = 0;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip anything = v4(198, 51, 100, 4);
    TcpListener.gate.addr = &anything;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
}

// A full-length prefix is a single host.
void test_a_full_length_prefix_matches_one_host(void)
{
    protocore_ip host = v4(10, 1, 2, 3);
    TcpListener.gate.addr = &host;
    TcpListener.gate.prefix_len = 32;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &host;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip neighbour = v4(10, 1, 2, 4);
    TcpListener.gate.addr = &neighbour;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
}

// A rule of one family never admits an address of the other.
void test_a_rule_never_matches_across_families(void)
{
    protocore_ip net = v4(0, 0, 0, 0);
    TcpListener.gate.addr = &net;
    TcpListener.gate.prefix_len = 0;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok); // "everything", in v4

    protocore_ip v6a;
    memset(&v6a, 0, sizeof(v6a));
    v6a.family = PROTOCORE_IP_V6;
    v6a.bytes[15] = 1;
    TcpListener.gate.addr = &v6a;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
}

// Any of several rules admitting the address is enough.
void test_any_matching_rule_admits_the_address(void)
{
    protocore_ip a = v4(10, 0, 0, 0);
    protocore_ip b = v4(192, 168, 0, 0);
    TcpListener.gate.addr = &a;
    TcpListener.gate.prefix_len = 8;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &b;
    TcpListener.gate.prefix_len = 16;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);

    protocore_ip in_a = v4(10, 9, 9, 9);
    protocore_ip in_b = v4(192, 168, 5, 5);
    protocore_ip in_neither = v4(172, 16, 0, 1);
    TcpListener.gate.addr = &in_a;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &in_b;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &in_neither;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
}

// The textual form parses address and prefix, and a bare address is a host route.
void test_a_cidr_string_parses_its_address_and_prefix(void)
{
    TcpListener.gate.cidr = "10.0.0.0/8";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip inside = v4(10, 255, 255, 1);
    protocore_ip outside = v4(11, 0, 0, 1);
    TcpListener.gate.addr = &inside;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &outside;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_a_bare_address_is_a_host_route(void)
{
    TcpListener.gate.cidr = "172.16.3.4";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_ip exact = v4(172, 16, 3, 4);
    protocore_ip near = v4(172, 16, 3, 5);
    TcpListener.gate.addr = &exact;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    TcpListener.gate.addr = &near;
    TcpListener.ip_allowed(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
}

// A prefix longer than the family allows is not a valid rule.
void test_a_prefix_wider_than_the_family_is_rejected(void)
{
    protocore_ip net = v4(10, 0, 0, 0);
    TcpListener.gate.addr = &net;
    TcpListener.gate.prefix_len = 33;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.cidr = "10.0.0.0/33";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.cidr = "10.0.0.0/999";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_malformed_rules_are_rejected(void)
{
    TcpListener.gate.addr = NULL;
    TcpListener.gate.prefix_len = 8;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.cidr = NULL;
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.gate.cidr = "10.0.0.0/";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);   // no prefix digits
    TcpListener.gate.cidr = "10.0.0.0/8x";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok); // not a number
    TcpListener.gate.cidr = "not-an-address/8";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);

    protocore_ip none;
    memset(&none, 0, sizeof(none));
    none.family = PROTOCORE_IP_NONE;
    TcpListener.gate.addr = &none;
    TcpListener.gate.prefix_len = 0;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok); // no family to size a prefix against
}

// The rule table is bounded; once it is full further rules are refused rather than overwriting.
void test_the_rule_table_is_bounded(void)
{
    for (int i = 0; i < PROTOCORE_IP_ALLOWLIST_SLOTS; i++)
    {
        protocore_ip net = v4(10, (uint8_t)i, 0, 0);
        TcpListener.gate.addr = &net;
        TcpListener.gate.prefix_len = 16;
        TcpListener.ip_allow_add(protocore_tcp_listener_span());
        TEST_ASSERT_TRUE(TcpListener.ok);
    }
    protocore_ip extra = v4(10, 200, 0, 0);
    TcpListener.gate.addr = &extra;
    TcpListener.gate.prefix_len = 16;
    TcpListener.ip_allow_add(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
}

// An address outside every rule is refused at accept time, with a reset.
void test_an_address_outside_the_allowlist_is_reset_at_accept(void)
{
    TcpListener.gate.cidr = "10.0.0.0/8";
    TcpListener.ip_allow_add_cidr(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);
    protocore_net_ip4_set(&g_newpcb.remote_ip, 203, 0, 113, 5); // outside the rule
    int aborts_before = mock_abort_call_count();

    TEST_ASSERT_EQUAL(PROTOCORE_NET_ERR_ABRT, accept_on(0, &g_newpcb));
    TEST_ASSERT_EQUAL_INT(aborts_before + 1, mock_abort_call_count());
    TEST_ASSERT_EQUAL(CONN_FREE, (ConnState)conn_pool[0].state);
}

// ---------------------------------------------------------------------------
// Listener lifecycle
// ---------------------------------------------------------------------------

void test_add_binds_a_port_and_creates_its_queue(void)
{
    TcpListener.idx = 1;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);
    TEST_ASSERT_TRUE(listener_pool[1].active);
    TEST_ASSERT_NOT_NULL(listener_pool[1].queue);
    TEST_ASSERT_NOT_NULL(listener_pool[1].listen_pcb);
    TEST_ASSERT_EQUAL_UINT16(8080, listener_pool[1].port);
}

void test_add_rejects_an_index_past_the_pool(void)
{
    TcpListener.idx = MAX_LISTENERS;
    TcpListener.bind.port = 80;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);
    TcpListener.idx = MAX_LISTENERS;
    TcpListener.bind.port = 80;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.add_dynamic(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);
}

void test_stop_deactivates_the_listener_and_releases_its_queue(void)
{
    TEST_ASSERT_TRUE(listener_pool[0].active);
    TcpListener.idx = 0;
    TcpListener.stop(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(listener_pool[0].active);
    TEST_ASSERT_NULL(listener_pool[0].queue);
    TEST_ASSERT_NULL(listener_pool[0].listen_pcb);
}

void test_stop_tolerates_an_index_past_the_pool_and_an_inactive_row(void)
{
    TcpListener.idx = MAX_LISTENERS;
    TcpListener.stop(protocore_tcp_listener_span());
    TcpListener.idx = MAX_LISTENERS;
    TcpListener.stop_dynamic(protocore_tcp_listener_span());
    TcpListener.idx = 1;
    TcpListener.stop(protocore_tcp_listener_span()); // never added
    TcpListener.idx = 0;
    TcpListener.stop(protocore_tcp_listener_span());
    TcpListener.idx = 0;
    TcpListener.stop(protocore_tcp_listener_span()); // already stopped
}

void test_stop_all_clears_every_listener(void)
{
    TcpListener.idx = 1;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TcpListener.idx = 2;
    TcpListener.bind.port = 8081;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TcpListener.stop_all(protocore_tcp_listener_span());
    for (uint8_t i = 0; i < MAX_LISTENERS; i++)
    {
        TEST_ASSERT_FALSE(listener_pool[i].active);
        TEST_ASSERT_NULL(listener_pool[i].queue);
    }
}

// Re-adding on an occupied index tears the old listener down first, so a port is never bound twice
// through one row.
void test_add_replaces_an_active_listener(void)
{
    TcpListener.idx = 0;
    TcpListener.bind.port = 9090;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);
    TEST_ASSERT_TRUE(listener_pool[0].active);
    TEST_ASSERT_EQUAL_UINT16(9090, listener_pool[0].port);
}

// A queue the kernel cannot create unwinds the add rather than leaving a half-built listener.
void test_add_unwinds_when_the_queue_cannot_be_created(void)
{
    TcpListener.idx = 1;
    TcpListener.stop(protocore_tcp_listener_span());
    mock_queue_create_fail_once();
    TcpListener.idx = 1;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);
    TEST_ASSERT_FALSE(listener_pool[1].active);
}

// A port already in use unwinds the add and releases the queue it had built.
void test_add_unwinds_and_releases_its_queue_when_the_bind_fails(void)
{
    TcpListener.idx = 1;
    TcpListener.stop(protocore_tcp_listener_span());
    mock_bind_fail_once();
    TcpListener.idx = 1;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);
    TEST_ASSERT_FALSE(listener_pool[1].active);
    TEST_ASSERT_NULL(listener_pool[1].queue);
}

void test_add_unwinds_when_the_control_block_pool_is_spent(void)
{
    TcpListener.idx = 1;
    TcpListener.stop(protocore_tcp_listener_span());
    mock_new_pcb_fail_once();
    TcpListener.idx = 1;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);
    TEST_ASSERT_FALSE(listener_pool[1].active);
    TEST_ASSERT_NULL(listener_pool[1].queue);
}

void test_add_unwinds_when_the_listen_call_fails(void)
{
    TcpListener.idx = 1;
    TcpListener.stop(protocore_tcp_listener_span());
    mock_listen_fail_once();
    TcpListener.idx = 1;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL_INT32(-1, TcpListener.i32);
    TEST_ASSERT_FALSE(listener_pool[1].active);
}

// A dynamically added listener is a plaintext bridge and lives on the same row machinery.
void test_a_dynamic_listener_binds_and_stops(void)
{
    TcpListener.idx = 1;
    TcpListener.bind.port = 2222;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.add_dynamic(protocore_tcp_listener_span());
    TEST_ASSERT_EQUAL_INT32(1, TcpListener.i32);
    TEST_ASSERT_TRUE(listener_pool[1].active);
    TEST_ASSERT_FALSE(listener_pool[1].tls); // forwarded ports are never TLS
    TcpListener.idx = 1;
    TcpListener.stop_dynamic(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(listener_pool[1].active);
    TEST_ASSERT_NULL(listener_pool[1].queue);
}

// ---------------------------------------------------------------------------
// Event queue
// ---------------------------------------------------------------------------

void test_enqueue_delivers_an_event_to_the_listener_queue(void)
{
    TcpEvt evt = {EVT_DATA, 0, 42};
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);

    TcpEvt got[2];
    TEST_ASSERT_EQUAL_INT(1, drain_events(0, got, 2));
    TEST_ASSERT_EQUAL(EVT_DATA, got[0].type);
    TEST_ASSERT_EQUAL_UINT(42, got[0].data_len);
}

void test_enqueue_rejects_a_null_event_and_a_slot_past_the_pool(void)
{
    TcpListener.idx = 0;
    TcpListener.q.evt = NULL;
    TcpListener.enqueue(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpEvt bad = {EVT_DATA, (uint8_t)CONN_POOL_SLOTS, 0};
    TcpListener.idx = 0;
    TcpListener.q.evt = &bad;
    TcpListener.enqueue(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_enqueue_rejects_an_unknown_or_inactive_listener(void)
{
    TcpEvt evt = {EVT_DATA, 0, 0};
    TcpListener.idx = MAX_LISTENERS;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.idx = 0;
    TcpListener.stop(protocore_tcp_listener_span());
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
}

void test_enqueue_reports_a_full_queue(void)
{
    TcpEvt evt = {EVT_DATA, 0, 0};
    mock_queue_send_fail_once();
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(protocore_tcp_listener_span());
    TEST_ASSERT_FALSE(TcpListener.ok);
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok); // the latch was one-shot
}

// Each listener drains its own queue, so an event posted for one port is not seen on another.
void test_each_listener_owns_its_own_queue(void)
{
    TcpListener.idx = 1;
    TcpListener.bind.port = 8080;
    TcpListener.bind.proto = PROTO_HTTP;
    TcpListener.bind.tls = PROTO_FALSE;
    TcpListener.add(protocore_tcp_listener_span());
    TcpEvt evt = {EVT_DATA, 0, 7};
    TcpListener.idx = 0;
    TcpListener.q.evt = &evt;
    TcpListener.enqueue(protocore_tcp_listener_span());
    TEST_ASSERT_TRUE(TcpListener.ok);

    TcpEvt got[2];
    TEST_ASSERT_EQUAL_INT(0, drain_events(1, got, 2)); // nothing on the other port
    TEST_ASSERT_EQUAL_INT(1, drain_events(0, got, 2));
}

// The runner is generated: Unity's auto/generate_test_runner.rb scans this file for
// void test_*(void) and emits main() with every case registered, stamped with the line each test
// is defined on. See test/gen_test_runners.py.
