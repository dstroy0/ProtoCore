// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the UDP transport's multicast receive path (pc_udp_listen_multicast /
// pc_udp_leave_multicast): group validation, datagram delivery to the handler, and teardown.
// The device path joins the group via IGMP; on the host the mock records the join so the same
// call sequence a service makes (join -> receive -> leave) is exercised end to end.

#include "network_drivers/transport/udp.h"

#include <unity.h>

static int g_calls = 0;
static char g_last[64];
static size_t g_last_len = 0;
static char g_src_ip[16];
static uint16_t g_src_port = 0;
static void *g_ctx_seen = NULL;

static void on_datagram(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    g_calls++;
    g_ctx_seen = ctx;
    g_last_len = (len < sizeof(g_last)) ? len : sizeof(g_last) - 1;
    memcpy(g_last, data, g_last_len);
    g_last[g_last_len] = '\0';
    pc_udp_peer_addr(peer, g_src_ip, sizeof(g_src_ip), &g_src_port);
}

void setUp(void)
{
    pc_udp_reset_listeners();
    g_calls = 0;
    g_last_len = 0;
    g_last[0] = '\0';
    g_src_ip[0] = '\0';
    g_src_port = 0;
    g_ctx_seen = NULL;
}
void tearDown(void)
{
}

void test_join_records_the_group()
{
    TEST_ASSERT_TRUE(pc_udp_listen_multicast("224.0.0.251", 5353, on_datagram, NULL));
    TEST_ASSERT_EQUAL_STRING("224.0.0.251", pc_udp_joined_group(5353));
    // A port with no multicast listener has no group.
    TEST_ASSERT_NULL(pc_udp_joined_group(1900));
}

void test_group_datagram_reaches_the_handler()
{
    int marker = 42;
    TEST_ASSERT_TRUE(pc_udp_listen_multicast("224.0.0.251", 5353, on_datagram, &marker));
    const char *pkt = "\x00\x00\x84\x00mdns-announce";
    pc_udp_inject(5353, "192.168.1.77", 5353, (const uint8_t *)pkt, 17);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_UINT(17, (unsigned)g_last_len);
    TEST_ASSERT_EQUAL_PTR(&marker, g_ctx_seen);
    TEST_ASSERT_EQUAL_STRING("192.168.1.77", g_src_ip);
    TEST_ASSERT_EQUAL_UINT16(5353, g_src_port);
}

void test_counts_repeated_announcements()
{
    // The contention-counting use case: many announcements land on one joined group.
    TEST_ASSERT_TRUE(pc_udp_listen_multicast("224.0.0.251", 5353, on_datagram, NULL));
    for (int i = 0; i < 12; i++)
    {
        pc_udp_inject(5353, "192.168.1.5", 5353, (const uint8_t *)"x", 1);
    }
    TEST_ASSERT_EQUAL_INT(12, g_calls);
}

void test_rejects_non_multicast_group()
{
    // A unicast address would bind but never deliver - fail loudly instead.
    TEST_ASSERT_FALSE(pc_udp_listen_multicast("192.168.1.10", 5353, on_datagram, NULL));
    TEST_ASSERT_FALSE(pc_udp_listen_multicast("223.255.255.255", 5353, on_datagram, NULL)); // just below /4
    TEST_ASSERT_FALSE(pc_udp_listen_multicast("240.0.0.1", 5353, on_datagram, NULL));       // just above /4
    TEST_ASSERT_NULL(pc_udp_joined_group(5353));
}

void test_accepts_group_range_edges()
{
    TEST_ASSERT_TRUE(pc_udp_listen_multicast("224.0.0.1", 5000, on_datagram, NULL));
    TEST_ASSERT_TRUE(pc_udp_leave_multicast(5000));
    TEST_ASSERT_TRUE(pc_udp_listen_multicast("239.255.255.250", 1900, on_datagram, NULL)); // SSDP
    TEST_ASSERT_EQUAL_STRING("239.255.255.250", pc_udp_joined_group(1900));
}

void test_rejects_malformed_group()
{
    TEST_ASSERT_FALSE(pc_udp_listen_multicast(NULL, 5353, on_datagram, NULL));
    TEST_ASSERT_FALSE(pc_udp_listen_multicast("", 5353, on_datagram, NULL));
    TEST_ASSERT_FALSE(pc_udp_listen_multicast("224.0.0", 5353, on_datagram, NULL));     // too few octets
    TEST_ASSERT_FALSE(pc_udp_listen_multicast("224.0.0.1.2", 5353, on_datagram, NULL)); // too many
    TEST_ASSERT_FALSE(pc_udp_listen_multicast("224.0.0.256", 5353, on_datagram, NULL)); // octet overflow
    TEST_ASSERT_FALSE(pc_udp_listen_multicast("224.0.0.", 5353, on_datagram, NULL));    // trailing dot
    TEST_ASSERT_FALSE(pc_udp_listen_multicast("224.0..1", 5353, on_datagram, NULL));    // empty octet
    TEST_ASSERT_FALSE(pc_udp_listen_multicast("224.0.0.abc", 5353, on_datagram, NULL)); // non-digit
    TEST_ASSERT_NULL(pc_udp_joined_group(5353));
}

void test_leave_releases_the_slot()
{
    TEST_ASSERT_TRUE(pc_udp_listen_multicast("224.0.0.251", 5353, on_datagram, NULL));
    TEST_ASSERT_TRUE(pc_udp_leave_multicast(5353));
    TEST_ASSERT_NULL(pc_udp_joined_group(5353));
    // Leaving twice, or leaving a port that never joined, is a no-op failure not a crash.
    TEST_ASSERT_FALSE(pc_udp_leave_multicast(5353));
    TEST_ASSERT_FALSE(pc_udp_leave_multicast(9999));
    // After leaving, the group no longer delivers.
    pc_udp_inject(5353, "192.168.1.5", 5353, (const uint8_t *)"x", 1);
    TEST_ASSERT_EQUAL_INT(0, g_calls);
}

void test_leave_ignores_a_plain_listener()
{
    // A non-multicast listener on the same port must not be torn down by a leave.
    TEST_ASSERT_TRUE(pc_udp_listen(5353, on_datagram, NULL));
    TEST_ASSERT_FALSE(pc_udp_leave_multicast(5353));
    pc_udp_inject(5353, "192.168.1.5", 5353, (const uint8_t *)"y", 1);
    TEST_ASSERT_EQUAL_INT(1, g_calls); // still bound
}

// Binding a port already held by a listener rebinds the existing slot in place
// (new handler/ctx win) rather than consuming a second slot.
void test_listen_rebinds_existing_port()
{
    int marker1 = 1, marker2 = 2;
    TEST_ASSERT_TRUE(pc_udp_listen(5353, on_datagram, &marker1));
    TEST_ASSERT_TRUE(pc_udp_listen(5353, on_datagram, &marker2)); // rebind, not a second slot
    pc_udp_inject(5353, "10.0.0.1", 1234, (const uint8_t *)"z", 1);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_PTR(&marker2, g_ctx_seen); // the rebind's ctx, not the original
    // Only one slot was consumed: a second, different port still gets its own slot
    // (PC_MAX_UDP_LISTENERS == 2), proving the rebind didn't also burn a free slot.
    TEST_ASSERT_TRUE(pc_udp_listen(9999, on_datagram, NULL));
}

// PC_MAX_UDP_LISTENERS == 2: once both slots are taken, binding a third distinct
// port evicts slot 0 (host-test-only fallback) rather than failing.
void test_listen_evicts_slot_zero_when_pool_full()
{
    TEST_ASSERT_TRUE(pc_udp_listen(1111, on_datagram, NULL)); // slot 0
    TEST_ASSERT_TRUE(pc_udp_listen(2222, on_datagram, NULL)); // slot 1, pool now full
    TEST_ASSERT_TRUE(pc_udp_listen(3333, on_datagram, NULL)); // evicts slot 0 (1111)

    // The evicted port no longer delivers; the evicting port does.
    pc_udp_inject(1111, "10.0.0.1", 1, (const uint8_t *)"a", 1);
    TEST_ASSERT_EQUAL_INT(0, g_calls);
    pc_udp_inject(3333, "10.0.0.1", 1, (const uint8_t *)"b", 1);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
}

// A multicast group string that is a valid dotted-quad in [224,239]/4 but padded with enough
// leading zeros to exceed the 16-byte group buffer must be rejected, not overflow it.
// parse_mcast_group() only bounds each octet's VALUE (<=255), not its digit count, so a run
// of leading zeros keeps the octet at 0 indefinitely while still growing the string length -
// this is the only way to make a group string this long pass validation.
void test_multicast_group_too_long_for_buffer_rejected()
{
    char group[64];
    size_t n = 0;
    memcpy(group + n, "224.", 4);
    n += 4;
    for (int i = 0; i < 20; i++) // 20 zero digits: pushes total length well past sizeof(group[16])
    {
        group[n++] = '0';
    }
    memcpy(group + n, ".0.0", 4);
    n += 4;
    group[n] = '\0';
    TEST_ASSERT_TRUE(n >= 16); // sanity: the constructed group really is too long

    TEST_ASSERT_FALSE(pc_udp_listen_multicast(group, 5353, on_datagram, NULL));
    TEST_ASSERT_NULL(pc_udp_joined_group(5353));
}

// When the target port's slot is not slot 0, pc_udp_listen_multicast's post-bind lookup loop
// must scan past an unrelated occupied slot to find it (exercises the "used but different port"
// continue-scanning branch, not just an immediate slot-0 match).
void test_multicast_join_finds_slot_past_an_unrelated_listener()
{
    TEST_ASSERT_TRUE(pc_udp_listen(4000, on_datagram, NULL));                              // occupies slot 0
    TEST_ASSERT_TRUE(pc_udp_listen_multicast("239.255.255.250", 1900, on_datagram, NULL)); // lands in slot 1
    TEST_ASSERT_EQUAL_STRING("239.255.255.250", pc_udp_joined_group(1900));
}

// A rejoin after a lower-slot listener has left leaves a hole below the target: the post-bind
// lookup loop then scans an unused slot (used==false) before reaching the rebound one. Drives
// line 540's short-circuit-on-unused arm, which lowest-free-first binding alone never reaches.
void test_multicast_rejoin_scans_past_a_freed_lower_slot()
{
    TEST_ASSERT_TRUE(pc_udp_listen_multicast("224.0.0.251", 5353, on_datagram, NULL));     // slot 0
    TEST_ASSERT_TRUE(pc_udp_listen_multicast("239.255.255.250", 1900, on_datagram, NULL)); // slot 1
    TEST_ASSERT_TRUE(pc_udp_leave_multicast(5353));                                        // slot 0 -> hole
    // Rebind port 1900 (reuses slot 1); post-bind lookup skips the now-unused slot 0 first.
    TEST_ASSERT_TRUE(pc_udp_listen_multicast("239.255.255.250", 1900, on_datagram, NULL));
    TEST_ASSERT_EQUAL_STRING("239.255.255.250", pc_udp_joined_group(1900));
}

// A null peer token is reported, not dereferenced.
void test_peer_addr_rejects_null_peer()
{
    char ip[16];
    uint16_t port = 0;
    TEST_ASSERT_FALSE(pc_udp_peer_addr(NULL, ip, sizeof(ip), &port));
}

// pc_udp_peer_addr copies the address/port and tolerates null out-params (a caller that only
// wants the port, or only the address, passes null for the other).
void test_peer_addr_copies_and_tolerates_null_outparams()
{
    int marker = 7;
    TEST_ASSERT_TRUE(pc_udp_listen(6000, on_datagram, &marker));
    pc_udp_inject(6000, "203.0.113.9", 4242, (const uint8_t *)"q", 1);
    TEST_ASSERT_EQUAL_STRING("203.0.113.9", g_src_ip);
    TEST_ASSERT_EQUAL_UINT16(4242, g_src_port);
}

// The outbound send paths (reply / arbitrary-destination / listener-sourced) all funnel through
// the shared host_capture() mock so a test can inspect exactly what a service tried to send.
void test_send_paths_are_captured()
{
    pc_udp_capture_enable();
    TEST_ASSERT_TRUE(pc_udp_send(NULL, (const uint8_t *)"reply", 5));
    TEST_ASSERT_EQUAL_UINT(5, (unsigned)pc_udp_captured_len());
    TEST_ASSERT_EQUAL_INT(0, memcmp("reply", pc_udp_captured(), 5));

    pc_udp_capture_reset();
    TEST_ASSERT_NULL(pc_udp_captured());
    TEST_ASSERT_TRUE(pc_udp_sendto("192.168.1.10", 514, (const uint8_t *)"syslog!", 7));
    TEST_ASSERT_EQUAL_UINT(7, (unsigned)pc_udp_captured_len());

    TEST_ASSERT_TRUE(pc_udp_listen(5683, on_datagram, NULL));
    pc_udp_capture_reset();
    TEST_ASSERT_TRUE(pc_udp_listener_sendto(5683, "192.168.1.20", 5683, (const uint8_t *)"notify", 6));
    TEST_ASSERT_EQUAL_UINT(6, (unsigned)pc_udp_captured_len());

    // The test knob can force a listener-sourced send to fail (models an unreachable observer).
    pc_udp_set_listener_sendto_result(PROTO_FALSE);
    TEST_ASSERT_FALSE(pc_udp_listener_sendto(5683, "192.168.1.20", 5683, (const uint8_t *)"x", 1));
    pc_udp_set_listener_sendto_result(PROTO_TRUE); // restore for any test that runs after this one
}

// host_capture()'s guard rejects a null payload, a zero length, and a payload larger than the
// capture buffer - none of which are exercised by a normal successful send.
void test_capture_rejects_null_zero_and_oversized_payload()
{
    pc_udp_capture_enable();
    TEST_ASSERT_FALSE(pc_udp_send(NULL, NULL, 5));                 // null data
    TEST_ASSERT_FALSE(pc_udp_send(NULL, (const uint8_t *)"x", 0)); // zero length
    static uint8_t big[3000] = {0};                                // larger than cap_buf (2048)
    TEST_ASSERT_FALSE(pc_udp_send(NULL, big, sizeof(big)));
    TEST_ASSERT_NULL(pc_udp_captured()); // none of the above landed anything
}

// A listener bound with a null handler (a legal pc_udp_listen() call) must not be invoked -
// pc_udp_inject() checks for a handler before calling through.
void test_inject_skips_a_listener_with_no_handler()
{
    TEST_ASSERT_TRUE(pc_udp_listen(7000, NULL, NULL));
    pc_udp_inject(7000, "10.0.0.1", 1, (const uint8_t *)"x", 1); // must not crash or call anything
    TEST_ASSERT_EQUAL_INT(0, g_calls);
}

// A null source IP at injection is reported to the handler as an empty string, not a crash.
void test_inject_null_src_ip_becomes_empty_string()
{
    TEST_ASSERT_TRUE(pc_udp_listen(7001, on_datagram, NULL));
    pc_udp_inject(7001, NULL, 1, (const uint8_t *)"x", 1);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_STRING("", g_src_ip);
}

// pc_udp_joined_group()/pc_udp_leave_multicast() must scan past an unrelated multicast listener
// (used + mcast, but the wrong port) to find or miss the right one - not just past a plain listener.
void test_multicast_lookup_skips_a_different_multicast_group()
{
    TEST_ASSERT_TRUE(pc_udp_listen_multicast("224.0.0.251", 5353, on_datagram, NULL));     // slot 0
    TEST_ASSERT_TRUE(pc_udp_listen_multicast("239.255.255.250", 1900, on_datagram, NULL)); // slot 1
    TEST_ASSERT_EQUAL_STRING("239.255.255.250", pc_udp_joined_group(1900));
    TEST_ASSERT_TRUE(pc_udp_leave_multicast(1900));
    TEST_ASSERT_NULL(pc_udp_joined_group(1900));
    TEST_ASSERT_EQUAL_STRING("224.0.0.251", pc_udp_joined_group(5353)); // untouched
}

static char g_edge_ip[16];
static proto_bool g_edge_had_ip_out = PROTO_FALSE;
static proto_bool g_edge_had_port_out = PROTO_FALSE;

// pc_udp_peer_addr()'s null-outparam tolerance: called from inside the handler where the peer
// token is still valid, covering ip_out==null and ip_cap==0 (independently) and port_out==null.
static void on_datagram_edge_cases(const uint8_t *, size_t, const struct pc_udp_peer *peer, void *)
{
    uint16_t port_tmp = 0;
    g_edge_had_ip_out = pc_udp_peer_addr(peer, NULL, sizeof(g_edge_ip), &port_tmp);           // ip_out null
    g_edge_had_ip_out = g_edge_had_ip_out && pc_udp_peer_addr(peer, g_edge_ip, 0, &port_tmp); // ip_cap == 0
    g_edge_had_port_out = pc_udp_peer_addr(peer, g_edge_ip, sizeof(g_edge_ip), NULL);         // port_out null
}

void test_peer_addr_tolerates_null_ip_out_and_zero_cap_and_null_port_out()
{
    g_edge_had_ip_out = PROTO_FALSE;
    g_edge_had_port_out = PROTO_FALSE;
    TEST_ASSERT_TRUE(pc_udp_listen(7002, on_datagram_edge_cases, NULL));
    pc_udp_inject(7002, "198.51.100.5", 9, (const uint8_t *)"e", 1);
    // pc_udp_peer_addr() still returns true in every case above (peer non-null); the point is
    // that none of the null/zero out-params crashed and the copy was skipped, not performed.
    TEST_ASSERT_TRUE(g_edge_had_ip_out);
    TEST_ASSERT_TRUE(g_edge_had_port_out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_join_records_the_group);
    RUN_TEST(test_group_datagram_reaches_the_handler);
    RUN_TEST(test_counts_repeated_announcements);
    RUN_TEST(test_rejects_non_multicast_group);
    RUN_TEST(test_accepts_group_range_edges);
    RUN_TEST(test_rejects_malformed_group);
    RUN_TEST(test_leave_releases_the_slot);
    RUN_TEST(test_leave_ignores_a_plain_listener);
    RUN_TEST(test_listen_rebinds_existing_port);
    RUN_TEST(test_listen_evicts_slot_zero_when_pool_full);
    RUN_TEST(test_multicast_group_too_long_for_buffer_rejected);
    RUN_TEST(test_multicast_join_finds_slot_past_an_unrelated_listener);
    RUN_TEST(test_multicast_rejoin_scans_past_a_freed_lower_slot);
    RUN_TEST(test_peer_addr_rejects_null_peer);
    RUN_TEST(test_peer_addr_copies_and_tolerates_null_outparams);
    RUN_TEST(test_send_paths_are_captured);
    RUN_TEST(test_capture_rejects_null_zero_and_oversized_payload);
    RUN_TEST(test_inject_skips_a_listener_with_no_handler);
    RUN_TEST(test_inject_null_src_ip_becomes_empty_string);
    RUN_TEST(test_multicast_lookup_skips_a_different_multicast_group);
    RUN_TEST(test_peer_addr_tolerates_null_ip_out_and_zero_cap_and_null_port_out);
    return UNITY_END();
}
