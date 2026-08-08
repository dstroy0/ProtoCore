// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the UDP transport's multicast receive path (Udp.listener->listen_multicast /
// Udp.listener->leave_multicast): group validation, datagram delivery to the handler, and teardown.
// These are the lines that ship to silicon: the stack underneath is test/mocks/pc_net_host.h, so a
// datagram arrives through the recv callback the listener armed and a send is read out of the
// driver's datagram log.

#include "network_drivers/transport/udp.h"
#include <string.h>

#include <unity.h>

// The sends take an address; a test spelling a literal turns it into one here.
static const pc_ip *addr(const char *s)
{
    static pc_ip a;
    a = (pc_ip){PC_IP_NONE, {0}};
    Ip.parse(s, &a);
    return &a;
}

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
    Udp.listener->peer_addr(peer, g_src_ip, sizeof(g_src_ip), &g_src_port);
}

// Deliver one datagram to the pcb bound to @p port, then drain: the callback fills the receive ring
// and poll() runs the handler.
static void inject(uint16_t port, const char *src_ip, uint16_t src_port, const uint8_t *data, size_t len)
{
    pc_net_host_udp_deliver(port, src_ip, src_port, (void *)(uintptr_t)data, (uint16_t)len);
    Udp.listener->poll();
}

// The last datagram the listener put on the wire, and its length; 0 / NULL when it sent none.
static size_t sent_len(void)
{
    size_t n = pc_net_host_udp_count();
    return n ? pc_net_host_udp_at(n - 1)->len : 0;
}

static const uint8_t *sent_bytes(void)
{
    size_t n = pc_net_host_udp_count();
    return n ? pc_net_host_udp_at(n - 1)->data : NULL;
}

// Every port this suite binds. close() frees the slot and drops the stack's control block; a port
// that was never bound reports false, so closing them all is how a case starts from no listeners.
static void close_all_ports(void)
{
    static const uint16_t ports[] = {1111, 1900, 2222, 3333, 4000, 5000, 5353,
                                     5683, 6000, 6100, 7000, 7001, 7002, 9999};
    for (size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); i++)
    {
        (void)Udp.listener->close(ports[i]);
    }
}

void setUp(void)
{
    close_all_ports();
    pc_net_host_udp_reset();
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
    TEST_ASSERT_TRUE(Udp.listener->listen_multicast("224.0.0.251", 5353, on_datagram, NULL));
    TEST_ASSERT_EQUAL_STRING("224.0.0.251", Udp.listener->joined_group(5353));
    // A port with no multicast listener has no group.
    TEST_ASSERT_NULL(Udp.listener->joined_group(1900));
}

void test_group_datagram_reaches_the_handler()
{
    int marker = 42;
    TEST_ASSERT_TRUE(Udp.listener->listen_multicast("224.0.0.251", 5353, on_datagram, &marker));
    const char *pkt = "\x00\x00\x84\x00mdns-announce";
    inject(5353, "192.168.1.77", 5353, (const uint8_t *)pkt, 17);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_UINT(17, (unsigned)g_last_len);
    TEST_ASSERT_EQUAL_PTR(&marker, g_ctx_seen);
    TEST_ASSERT_EQUAL_STRING("192.168.1.77", g_src_ip);
    TEST_ASSERT_EQUAL_UINT16(5353, g_src_port);
}

void test_counts_repeated_announcements()
{
    // The contention-counting use case: many announcements land on one joined group.
    TEST_ASSERT_TRUE(Udp.listener->listen_multicast("224.0.0.251", 5353, on_datagram, NULL));
    for (int i = 0; i < 12; i++)
    {
        inject(5353, "192.168.1.5", 5353, (const uint8_t *)"x", 1);
    }
    TEST_ASSERT_EQUAL_INT(12, g_calls);
}

void test_rejects_non_multicast_group()
{
    // A unicast address would bind but never deliver - fail loudly instead.
    TEST_ASSERT_FALSE(Udp.listener->listen_multicast("192.168.1.10", 5353, on_datagram, NULL));
    TEST_ASSERT_FALSE(Udp.listener->listen_multicast("223.255.255.255", 5353, on_datagram, NULL)); // just below /4
    TEST_ASSERT_FALSE(Udp.listener->listen_multicast("240.0.0.1", 5353, on_datagram, NULL));       // just above /4
    TEST_ASSERT_NULL(Udp.listener->joined_group(5353));
}

void test_accepts_group_range_edges()
{
    TEST_ASSERT_TRUE(Udp.listener->listen_multicast("224.0.0.1", 5000, on_datagram, NULL));
    TEST_ASSERT_TRUE(Udp.listener->leave_multicast(5000));
    TEST_ASSERT_TRUE(Udp.listener->listen_multicast("239.255.255.250", 1900, on_datagram, NULL)); // SSDP
    TEST_ASSERT_EQUAL_STRING("239.255.255.250", Udp.listener->joined_group(1900));
}

void test_rejects_malformed_group()
{
    TEST_ASSERT_FALSE(Udp.listener->listen_multicast(NULL, 5353, on_datagram, NULL));
    TEST_ASSERT_FALSE(Udp.listener->listen_multicast("", 5353, on_datagram, NULL));
    TEST_ASSERT_FALSE(Udp.listener->listen_multicast("224.0.0", 5353, on_datagram, NULL));     // too few octets
    TEST_ASSERT_FALSE(Udp.listener->listen_multicast("224.0.0.1.2", 5353, on_datagram, NULL)); // too many
    TEST_ASSERT_FALSE(Udp.listener->listen_multicast("224.0.0.256", 5353, on_datagram, NULL)); // octet overflow
    TEST_ASSERT_FALSE(Udp.listener->listen_multicast("224.0.0.", 5353, on_datagram, NULL));    // trailing dot
    TEST_ASSERT_FALSE(Udp.listener->listen_multicast("224.0..1", 5353, on_datagram, NULL));    // empty octet
    TEST_ASSERT_FALSE(Udp.listener->listen_multicast("224.0.0.abc", 5353, on_datagram, NULL)); // non-digit
    TEST_ASSERT_NULL(Udp.listener->joined_group(5353));
}

void test_leave_releases_the_slot()
{
    TEST_ASSERT_TRUE(Udp.listener->listen_multicast("224.0.0.251", 5353, on_datagram, NULL));
    TEST_ASSERT_TRUE(Udp.listener->leave_multicast(5353));
    TEST_ASSERT_NULL(Udp.listener->joined_group(5353));
    // Leaving twice, or leaving a port that never joined, is a no-op failure not a crash.
    TEST_ASSERT_FALSE(Udp.listener->leave_multicast(5353));
    TEST_ASSERT_FALSE(Udp.listener->leave_multicast(9999));
    // After leaving, the group no longer delivers.
    inject(5353, "192.168.1.5", 5353, (const uint8_t *)"x", 1);
    TEST_ASSERT_EQUAL_INT(0, g_calls);
}

void test_leave_ignores_a_plain_listener()
{
    // A non-multicast listener on the same port must not be torn down by a leave.
    TEST_ASSERT_TRUE(Udp.listener->listen(5353, on_datagram, NULL));
    TEST_ASSERT_FALSE(Udp.listener->leave_multicast(5353));
    inject(5353, "192.168.1.5", 5353, (const uint8_t *)"y", 1);
    TEST_ASSERT_EQUAL_INT(1, g_calls); // still bound
}

// Binding a port already held by a listener rebinds the existing slot in place
// (new handler/ctx win) rather than consuming a second slot.
void test_listen_rebinds_existing_port()
{
    int marker1 = 1, marker2 = 2;
    TEST_ASSERT_TRUE(Udp.listener->listen(5353, on_datagram, &marker1));
    TEST_ASSERT_TRUE(Udp.listener->listen(5353, on_datagram, &marker2)); // rebind, not a second slot
    inject(5353, "10.0.0.1", 1234, (const uint8_t *)"z", 1);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_PTR(&marker2, g_ctx_seen); // the rebind's ctx, not the original
    // Only one slot was consumed: a second, different port still gets its own slot
    // (PC_MAX_UDP_LISTENERS == 2), proving the rebind didn't also burn a free slot.
    TEST_ASSERT_TRUE(Udp.listener->listen(9999, on_datagram, NULL));
}

// PC_MAX_UDP_LISTENERS == 2: once both slots are taken, a third distinct port is refused. Taking
// a bound port's slot would stop a service that is still using it, with nothing to tell it so.
void test_listen_refuses_a_third_port_when_the_pool_is_full()
{
    TEST_ASSERT_TRUE(Udp.listener->listen(1111, on_datagram, NULL));  // slot 0
    TEST_ASSERT_TRUE(Udp.listener->listen(2222, on_datagram, NULL));  // slot 1, pool now full
    TEST_ASSERT_FALSE(Udp.listener->listen(3333, on_datagram, NULL)); // refused, nothing evicted

    // Both bound ports still deliver, and the refused one does not.
    inject(1111, "10.0.0.1", 1, (const uint8_t *)"a", 1);
    inject(2222, "10.0.0.1", 1, (const uint8_t *)"b", 1);
    TEST_ASSERT_EQUAL_INT(2, g_calls);
    inject(3333, "10.0.0.1", 1, (const uint8_t *)"c", 1);
    TEST_ASSERT_EQUAL_INT(2, g_calls);
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

    TEST_ASSERT_FALSE(Udp.listener->listen_multicast(group, 5353, on_datagram, NULL));
    TEST_ASSERT_NULL(Udp.listener->joined_group(5353));
}

// When the target port's slot is not slot 0, Udp.listener->listen_multicast's post-bind lookup loop
// must scan past an unrelated occupied slot to find it (exercises the "used but different port"
// continue-scanning branch, not just an immediate slot-0 match).
void test_multicast_join_finds_slot_past_an_unrelated_listener()
{
    TEST_ASSERT_TRUE(Udp.listener->listen(4000, on_datagram, NULL));                              // occupies slot 0
    TEST_ASSERT_TRUE(Udp.listener->listen_multicast("239.255.255.250", 1900, on_datagram, NULL)); // lands in slot 1
    TEST_ASSERT_EQUAL_STRING("239.255.255.250", Udp.listener->joined_group(1900));
}

// A rejoin after a lower-slot listener has left leaves a hole below the target: the post-bind
// lookup loop then scans an unused slot (used==false) before reaching the rebound one. Drives
// line 540's short-circuit-on-unused arm, which lowest-free-first binding alone never reaches.
void test_multicast_rejoin_scans_past_a_freed_lower_slot()
{
    TEST_ASSERT_TRUE(Udp.listener->listen_multicast("224.0.0.251", 5353, on_datagram, NULL));     // slot 0
    TEST_ASSERT_TRUE(Udp.listener->listen_multicast("239.255.255.250", 1900, on_datagram, NULL)); // slot 1
    TEST_ASSERT_TRUE(Udp.listener->leave_multicast(5353));                                        // slot 0 -> hole
    // Rebind port 1900 (reuses slot 1); post-bind lookup skips the now-unused slot 0 first.
    TEST_ASSERT_TRUE(Udp.listener->listen_multicast("239.255.255.250", 1900, on_datagram, NULL));
    TEST_ASSERT_EQUAL_STRING("239.255.255.250", Udp.listener->joined_group(1900));
}

// A null peer token is reported, not dereferenced.
void test_peer_addr_rejects_null_peer()
{
    char ip[16];
    uint16_t port = 0;
    TEST_ASSERT_FALSE(Udp.listener->peer_addr(NULL, ip, sizeof(ip), &port));
}

// Udp.listener->peer_addr copies the address/port and tolerates null out-params (a caller that only
// wants the port, or only the address, passes null for the other).
void test_peer_addr_copies_and_tolerates_null_outparams()
{
    int marker = 7;
    TEST_ASSERT_TRUE(Udp.listener->listen(6000, on_datagram, &marker));
    inject(6000, "203.0.113.9", 4242, (const uint8_t *)"q", 1);
    TEST_ASSERT_EQUAL_STRING("203.0.113.9", g_src_ip);
    TEST_ASSERT_EQUAL_UINT16(4242, g_src_port);
}

// A reply needs the peer token its handler was given, so the reply path is driven from inside a
// handler. The reply lands on the same slot's send ring, which the poll that ran the handler
// flushes on its way out.
static void reply_handler(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)data;
    (void)len;
    (void)ctx;
    g_calls++;
    Udp.listener->reply(peer, (const uint8_t *)"reply", 5);
}

// Each side captures what it sent: the listener holds replies and sends from a bound port, the
// client holds sends from the ephemeral port. Both capture at the wire, so a poll stands between
// queueing a datagram and reading it back.
void test_send_paths_are_captured()
{
    TEST_ASSERT_TRUE(Udp.listener->listen(5683, reply_handler, NULL));
    inject(5683, "192.168.1.30", 5683, (const uint8_t *)"q", 1);
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_UINT(5, (unsigned)sent_len());
    TEST_ASSERT_EQUAL_INT(0, memcmp("reply", sent_bytes(), 5));
    // The reply goes back to the peer the handler was given, on the port it came from.
    TEST_ASSERT_EQUAL_UINT16(5683, pc_net_host_udp_at(pc_net_host_udp_count() - 1)->dst_port);

    pc_net_host_udp_reset();
    TEST_ASSERT_TRUE(Udp.client->sendto(addr("192.168.1.10"), 514, (const uint8_t *)"syslog!", 7));
    Udp.client->poll();
    TEST_ASSERT_EQUAL_UINT(7, (unsigned)sent_len());
    TEST_ASSERT_EQUAL_UINT16(514, pc_net_host_udp_at(pc_net_host_udp_count() - 1)->dst_port);

    pc_net_host_udp_reset();
    TEST_ASSERT_TRUE(Udp.listener->sendto(5683, addr("192.168.1.20"), 5683, (const uint8_t *)"notify", 6));
    Udp.listener->poll();
    TEST_ASSERT_EQUAL_UINT(6, (unsigned)sent_len());
    TEST_ASSERT_EQUAL_INT(0, memcmp("notify", sent_bytes(), 6));
}

// sendto() reports that the ring took the datagram, so a stack that refuses it shows up at the wire
// during poll() and not in the call that queued it.
void test_a_refused_send_still_queues_and_drains()
{
    TEST_ASSERT_TRUE(Udp.listener->listen(5683, on_datagram, NULL));
    mock_udp_send_fail_after(0); // the stack refuses every datagram from here
    TEST_ASSERT_TRUE(Udp.listener->sendto(5683, addr("192.168.1.20"), 5683, (const uint8_t *)"x", 1)); // queued
    Udp.listener->poll();
    TEST_ASSERT_EQUAL_size_t(0, pc_net_host_udp_count()); // refused, so nothing reached the wire

    // The refusal does not wedge the ring: the next send goes out once the stack accepts again.
    mock_udp_send_fail_after(-1);
    TEST_ASSERT_TRUE(Udp.listener->sendto(5683, addr("192.168.1.20"), 5683, (const uint8_t *)"y", 1));
    Udp.listener->poll();
    TEST_ASSERT_EQUAL_UINT(1, (unsigned)sent_len());
    TEST_ASSERT_EQUAL_UINT8('y', sent_bytes()[0]);
}

// queue_tx() rejects a null payload, a zero length, and a payload larger than a ring frame, so
// none of them reach the ring and the capture stays empty across the poll.
void test_send_rejects_null_zero_and_oversized_payload()
{
    TEST_ASSERT_TRUE(Udp.listener->listen(6100, on_datagram, NULL));
    TEST_ASSERT_FALSE(Udp.listener->sendto(6100, addr("192.168.1.20"), 6100, NULL, 5));                 // null data
    TEST_ASSERT_FALSE(Udp.listener->sendto(6100, addr("192.168.1.20"), 6100, (const uint8_t *)"x", 0)); // zero length
    static uint8_t big[3000] = {0}; // larger than one ring frame's payload
    TEST_ASSERT_FALSE(Udp.listener->sendto(6100, addr("192.168.1.20"), 6100, big, sizeof(big)));
    TEST_ASSERT_FALSE(Udp.listener->reply(NULL, (const uint8_t *)"x", 1)); // no peer token
    Udp.listener->poll();
    TEST_ASSERT_EQUAL_size_t(0, pc_net_host_udp_count()); // none of the above landed anything
}

// A listener bound with a null handler (a legal Udp.listener->listen() call) must not be invoked -
// poll() checks for a handler before calling through.
void test_inject_skips_a_listener_with_no_handler()
{
    TEST_ASSERT_TRUE(Udp.listener->listen(7000, NULL, NULL));
    inject(7000, "10.0.0.1", 1, (const uint8_t *)"x", 1); // must not crash or call anything
    TEST_ASSERT_EQUAL_INT(0, g_calls);
}

// A source address the stack tagged with no family carries no address: it converts to PC_IP_NONE,
// which peer_addr refuses to format, rather than to a 0.0.0.0 the sender never had.
void test_an_untagged_source_address_carries_no_address()
{
    TEST_ASSERT_TRUE(Udp.listener->listen(7001, on_datagram, NULL));
    inject(7001, NULL, 1, (const uint8_t *)"x", 1); // NULL: the driver hands over a zeroed address
    TEST_ASSERT_EQUAL_INT(1, g_calls);              // the payload still reaches the handler
    TEST_ASSERT_EQUAL_STRING("", g_src_ip);
}

// Udp.listener->joined_group()/Udp.listener->leave_multicast() must scan past an unrelated multicast listener
// (used + mcast, but the wrong port) to find or miss the right one - not just past a plain listener.
void test_multicast_lookup_skips_a_different_multicast_group()
{
    TEST_ASSERT_TRUE(Udp.listener->listen_multicast("224.0.0.251", 5353, on_datagram, NULL));     // slot 0
    TEST_ASSERT_TRUE(Udp.listener->listen_multicast("239.255.255.250", 1900, on_datagram, NULL)); // slot 1
    TEST_ASSERT_EQUAL_STRING("239.255.255.250", Udp.listener->joined_group(1900));
    TEST_ASSERT_TRUE(Udp.listener->leave_multicast(1900));
    TEST_ASSERT_NULL(Udp.listener->joined_group(1900));
    TEST_ASSERT_EQUAL_STRING("224.0.0.251", Udp.listener->joined_group(5353)); // untouched
}

static char g_edge_ip[16];
static proto_bool g_edge_had_ip_out = PROTO_FALSE;
static proto_bool g_edge_had_port_out = PROTO_FALSE;

// Udp.listener->peer_addr()'s null-outparam tolerance: called from inside the handler where the peer
// token is still valid, covering ip_out==null and ip_cap==0 (independently) and port_out==null.
static void on_datagram_edge_cases(const uint8_t *, size_t, const struct pc_udp_peer *peer, void *)
{
    uint16_t port_tmp = 0;
    // No buffer, or one too small to hold any address, is refused rather than half-filled.
    g_edge_had_ip_out = Udp.listener->peer_addr(peer, NULL, sizeof(g_edge_ip), &port_tmp) ||
                        Udp.listener->peer_addr(peer, g_edge_ip, 0, &port_tmp) ||
                        Udp.listener->peer_addr(peer, g_edge_ip, 4, &port_tmp);
    // A null port_out is the one out-param that is genuinely optional: the address still copies.
    g_edge_had_port_out = Udp.listener->peer_addr(peer, g_edge_ip, sizeof(g_edge_ip), NULL);
}

void test_peer_addr_refuses_a_buffer_it_cannot_fill_and_allows_a_null_port_out()
{
    g_edge_had_ip_out = PROTO_TRUE;
    g_edge_had_port_out = PROTO_FALSE;
    TEST_ASSERT_TRUE(Udp.listener->listen(7002, on_datagram_edge_cases, NULL));
    inject(7002, "198.51.100.5", 9, (const uint8_t *)"e", 1);
    TEST_ASSERT_FALSE(g_edge_had_ip_out);  // every unwritable buffer was refused
    TEST_ASSERT_TRUE(g_edge_had_port_out); // the port is the optional half
    TEST_ASSERT_EQUAL_STRING("198.51.100.5", g_edge_ip);
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
    RUN_TEST(test_listen_refuses_a_third_port_when_the_pool_is_full);
    RUN_TEST(test_multicast_group_too_long_for_buffer_rejected);
    RUN_TEST(test_multicast_join_finds_slot_past_an_unrelated_listener);
    RUN_TEST(test_multicast_rejoin_scans_past_a_freed_lower_slot);
    RUN_TEST(test_peer_addr_rejects_null_peer);
    RUN_TEST(test_peer_addr_copies_and_tolerates_null_outparams);
    RUN_TEST(test_send_paths_are_captured);
    RUN_TEST(test_a_refused_send_still_queues_and_drains);
    RUN_TEST(test_send_rejects_null_zero_and_oversized_payload);
    RUN_TEST(test_inject_skips_a_listener_with_no_handler);
    RUN_TEST(test_an_untagged_source_address_carries_no_address);
    RUN_TEST(test_multicast_lookup_skips_a_different_multicast_group);
    RUN_TEST(test_peer_addr_refuses_a_buffer_it_cannot_fill_and_allows_a_null_port_out);
    return UNITY_END();
}
