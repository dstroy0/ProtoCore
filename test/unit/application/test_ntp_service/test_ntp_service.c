// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The portable SNTP client (network_drivers/application/ntp_service, RFC 4330): the mode-3 request
// it puts on the wire, the mode-4 reply it accepts, and the ones it refuses. Replies are built by
// this file, so the client is checked against the wire rather than against itself.

#include "network_drivers/application/ntp_server/ntp_server.h" // NTP_PACKET_LEN / NTP_UNIX_OFFSET
#include "network_drivers/application/ntp_service/ntp_service.h"
#include "network_drivers/transport/udp.h"
#include "pc_net_host.h"
#include <string.h>

#include <unity.h>

#define SERVER_IP "192.0.2.30"
#define GOOD_EPOCH 1700000000u // well past the 2021 plausibility floor

void setUp()
{
    pc_net_host_reset();
    set_millis(0);
    pc_ntp_set_test_epoch(0); // back to never-synced
}
void tearDown()
{
    Udp.listener->close(PC_NTP_CLIENT_PORT);
}

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

// Start a sync and let the listener put the request on the wire: begin() queues it on the send ring
// and poll() is what drains that ring.
static proto_bool sync_with(const char *server)
{
    pc_net_host_udp_reset();
    proto_bool ok = pc_ntp_begin(NULL, server, NULL);
    Udp.listener->poll();
    return ok;
}

// The request the client just sent, taken off the wire.
static const pc_net_host_dgram *request(void)
{
    TEST_ASSERT_EQUAL_INT(1, (int)pc_net_host_udp_count());
    return pc_net_host_udp_at(0);
}

// A server answer to the request the client sent: mode 4, the given stratum, and the origin field
// echoing whatever transmit stamp the request carried.
static void reply_with(uint8_t mode, uint8_t stratum, uint32_t origin, uint32_t unix_secs)
{
    uint8_t r[NTP_PACKET_LEN];
    memset(r, 0, sizeof(r));
    r[0] = (uint8_t)((4u << 3) | mode);
    r[1] = stratum;
    wr_be32(r + 24, origin);
    wr_be32(r + 40, unix_secs + NTP_UNIX_OFFSET);
    pc_net_host_udp_deliver(PC_NTP_CLIENT_PORT, SERVER_IP, 123, r, sizeof(r));
    Udp.listener->poll();
}

// Ask, and report the cookie the request went out with.
static uint32_t ask(void)
{
    TEST_ASSERT_TRUE(sync_with(SERVER_IP));
    return rd_be32(request()->data + 40);
}

// --- the request ---------------------------------------------------------------------------------

void test_begin_sends_a_client_request()
{
    TEST_ASSERT_TRUE(sync_with(SERVER_IP));

    const pc_net_host_dgram *d = request();
    TEST_ASSERT_EQUAL_UINT16(NTP_PACKET_LEN, d->len);
    TEST_ASSERT_EQUAL_UINT16(123, d->dst_port);
    TEST_ASSERT_EQUAL_UINT16(PC_NTP_CLIENT_PORT, d->src_port);
    TEST_ASSERT_EQUAL_UINT8(30, d->addr[3]);           // 192.0.2.30
    TEST_ASSERT_EQUAL_UINT8(3, d->data[0] & 7);        // mode 3: a client asking
    TEST_ASSERT_EQUAL_UINT8(4, (d->data[0] >> 3) & 7); // version 4
    TEST_ASSERT_NOT_EQUAL(0, rd_be32(d->data + 40));   // a nonzero transmit stamp to echo back
}

// A hostname needs a resolver this client does not have, so it is refused rather than guessed at.
void test_begin_refuses_a_hostname()
{
    TEST_ASSERT_FALSE(sync_with("pool.ntp.org"));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_net_host_udp_sent());
}

// --- the reply -----------------------------------------------------------------------------------

void test_a_good_reply_sets_the_clock()
{
    TEST_ASSERT_FALSE(pc_ntp_synced());
    uint32_t cookie = ask();
    reply_with(4, 3, cookie, GOOD_EPOCH);

    TEST_ASSERT_TRUE(pc_ntp_synced());
    TEST_ASSERT_EQUAL_UINT32(GOOD_EPOCH, (uint32_t)pc_ntp_epoch());
    TEST_ASSERT_EQUAL_UINT32(GOOD_EPOCH, pc_ntp_time_source());
}

// The origin field is what ties a reply to the request it answers; without it any packet arriving
// on the client port could set the clock.
void test_a_reply_that_does_not_echo_the_request_is_refused()
{
    uint32_t cookie = ask();
    reply_with(4, 3, cookie ^ 0xFFFFu, GOOD_EPOCH);
    TEST_ASSERT_FALSE(pc_ntp_synced());
}

void test_a_non_server_mode_is_refused()
{
    uint32_t cookie = ask();
    reply_with(3, 3, cookie, GOOD_EPOCH); // mode 3: another client, not a server
    TEST_ASSERT_FALSE(pc_ntp_synced());
}

// Stratum 0 is a kiss-o'-death packet and carries no time; past 15 is unsynchronized.
void test_an_unusable_stratum_is_refused()
{
    uint32_t cookie = ask();
    reply_with(4, 0, cookie, GOOD_EPOCH);
    TEST_ASSERT_FALSE(pc_ntp_synced());
    reply_with(4, 16, cookie, GOOD_EPOCH);
    TEST_ASSERT_FALSE(pc_ntp_synced());
}

// A server answering with a pre-2021 clock is not one to follow.
void test_an_implausible_epoch_is_refused()
{
    uint32_t cookie = ask();
    reply_with(4, 3, cookie, 946684800u); // 2000-01-01
    TEST_ASSERT_FALSE(pc_ntp_synced());
}

void test_a_short_datagram_is_refused()
{
    ask();
    uint8_t runt[16] = {0x24, 3, 0, 0};
    pc_net_host_udp_deliver(PC_NTP_CLIENT_PORT, SERVER_IP, 123, runt, sizeof(runt));
    Udp.listener->poll();
    TEST_ASSERT_FALSE(pc_ntp_synced());
}

// --- between syncs -------------------------------------------------------------------------------

// One reply fixes an instant; the monotonic clock carries the epoch forward from there.
void test_the_epoch_advances_with_the_tick()
{
    uint32_t cookie = ask();
    reply_with(4, 3, cookie, GOOD_EPOCH);
    TEST_ASSERT_EQUAL_UINT32(GOOD_EPOCH, (uint32_t)pc_ntp_epoch());

    set_millis(millis() + 5000);
    TEST_ASSERT_EQUAL_UINT32(GOOD_EPOCH + 5, (uint32_t)pc_ntp_epoch());

    set_millis(millis() + 999); // less than a second does not move the second
    TEST_ASSERT_EQUAL_UINT32(GOOD_EPOCH + 5, (uint32_t)pc_ntp_epoch());
}

// Never synced means no time at all, not a zero that reads as 1970.
void test_unsynced_reports_no_time()
{
    TEST_ASSERT_FALSE(pc_ntp_synced());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_ntp_epoch());
    TEST_ASSERT_EQUAL_UINT32(0, pc_ntp_time_source());
    char buf[40];
    TEST_ASSERT_EQUAL_UINT(0, pc_ntp_http_date(buf, sizeof(buf)));
}

// The seam a caller with its own clock uses, and what the Date header formats from it.
void test_a_seeded_epoch_formats_a_date()
{
    pc_ntp_set_test_epoch(784111777); // Sun, 06 Nov 1994 08:49:37 GMT
    TEST_ASSERT_TRUE(pc_ntp_synced());
    char buf[40];
    size_t n = pc_ntp_http_date(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("Sun, 06 Nov 1994 08:49:37 GMT", buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_sends_a_client_request);
    RUN_TEST(test_begin_refuses_a_hostname);
    RUN_TEST(test_a_good_reply_sets_the_clock);
    RUN_TEST(test_a_reply_that_does_not_echo_the_request_is_refused);
    RUN_TEST(test_a_non_server_mode_is_refused);
    RUN_TEST(test_an_unusable_stratum_is_refused);
    RUN_TEST(test_an_implausible_epoch_is_refused);
    RUN_TEST(test_a_short_datagram_is_refused);
    RUN_TEST(test_the_epoch_advances_with_the_tick);
    RUN_TEST(test_unsynced_reports_no_time);
    RUN_TEST(test_a_seeded_epoch_formats_a_date);
    return UNITY_END();
}
