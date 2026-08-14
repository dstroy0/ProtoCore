// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SNTP client (network_drivers/application/ntp_service/ntp_service.h), over the
// packet in network_drivers/application/ntp/ntp.h.
//
// RFC 4330 governs both halves of this. Section 5 fixes the request a unicast client sends (Mode 3,
// a version the server supports, every other header field zero) and the sanity checks a client
// applies to the reply; section 8 defines the Kiss-o'-Death packet a stratum-0 reply carries. The
// 2208988800-second gap between the NTP prime epoch and the Unix epoch is RFC 5905 sec 6.
//
// test_a_reply_whose_origin_does_not_echo_the_request_is_ignored is the load-bearing case. RFC 4330
// sec 5 sanity check 3 requires the Originate Timestamp in the reply to match the Transmit Timestamp
// the client sent, and says of that field "It is important that this field be copied intact, as an
// NTP or SNTP client uses it to avoid bogus messages". Without that check any off-path datagram
// arriving on the client port sets the wall clock, and every other assertion here still passes.

#include "network_drivers/application/ntp/ntp.h"
#include "network_drivers/application/ntp_service/ntp_service.h"
#include "network_drivers/transport/udp/server/server.h"
#include "protocore_net_host.h"
#include <Arduino.h> // set_millis: the host virtual clock this module's epoch advances off
#include <string.h>

#include <unity.h>

#define SERVER_IP "192.0.2.10"

// 2026-01-01T00:00:00Z, comfortably past the 2021 plausibility floor the client applies.
#define GOOD_EPOCH 1767225600u

void setUp(void)
{
    set_millis(1000u);
    protocore_net_host_reset();
    protocore_ntp_set_test_epoch(0); // back to never-synced
}

void tearDown(void)
{
    UdpListener.port = PROTOCORE_NTP_CLIENT_PORT;
    UdpListener.close(UdpListener.internal);
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

// Start the client and return the request it put on the wire.
static const protocore_net_host_dgram *ask(const char *server)
{
    protocore_net_host_udp_reset();
    TEST_ASSERT_TRUE(protocore_ntp_begin(NULL, server, NULL));
    TEST_ASSERT_EQUAL_size_t(1u, protocore_net_host_udp_count());
    const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
    TEST_ASSERT_NOT_NULL(d);
    return d;
}

// A server reply: mode 4, the given stratum, the given origin cookie, and a transmit stamp in NTP
// seconds. Delivered to the client port and drained by one poll.
static void reply_with(uint8_t li, uint8_t mode, uint8_t stratum, uint32_t origin_sec, uint32_t unix_epoch)
{
    uint8_t r[PROTOCORE_NTP_PACKET_LEN];
    memset(r, 0, sizeof r);
    r[PROTOCORE_NTP_OFF_LI_VN_MODE] = PROTOCORE_NTP_LI_VN_MODE(li, PROTOCORE_NTP_VERSION, mode);
    r[PROTOCORE_NTP_OFF_STRATUM] = stratum;
    wr_be32(r + PROTOCORE_NTP_OFF_ORIGIN_SEC, origin_sec);
    wr_be32(r + PROTOCORE_NTP_OFF_TX_SEC, unix_epoch + PROTOCORE_NTP_UNIX_OFFSET);
    protocore_net_host_udp_deliver(PROTOCORE_NTP_CLIENT_PORT, SERVER_IP, PROTOCORE_NTP_PORT, r, sizeof r);
    UdpListener.poll(UdpListener.internal);
}

// The cookie the client sent, which a reply has to echo.
static uint32_t cookie_of(const protocore_net_host_dgram *req)
{
    return rd_be32(req->data + PROTOCORE_NTP_OFF_TX_SEC);
}

// RFC 4330 sec 5: "all the NTP header fields shown above are set to 0, except the Mode, VN, and
// optional Transmit Timestamp fields", the mode is 3 for a unicast client, and the packet goes to
// the NTP port.
void test_the_request_is_an_rfc4330_client_packet(void)
{
    const protocore_net_host_dgram *d = ask(SERVER_IP);

    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_NTP_PACKET_LEN, d->len);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_NTP_PORT, d->dst_port);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_NTP_CLIENT_PORT, d->src_port);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_NTP_MODE_CLIENT, PROTOCORE_NTP_MODE_OF(d->data[PROTOCORE_NTP_OFF_LI_VN_MODE]));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_NTP_VERSION, PROTOCORE_NTP_VN_OF(d->data[PROTOCORE_NTP_OFF_LI_VN_MODE]));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_LI_OF(d->data[PROTOCORE_NTP_OFF_LI_VN_MODE]));

    // Every octet between the first and the transmit timestamp is zero.
    for (size_t i = 1; i < PROTOCORE_NTP_OFF_TX_SEC; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0, d->data[i]);
    }
    // The transmit stamp is set, which is what the reply has to echo back.
    TEST_ASSERT_TRUE(cookie_of(d) != 0u);
}

// A reply that answers the question this client asked sets the clock, and the NTP seconds it carries
// become Unix seconds by subtracting the 2208988800-second gap between the two epochs.
void test_a_matching_reply_sets_the_clock(void)
{
    const protocore_net_host_dgram *d = ask(SERVER_IP);
    TEST_ASSERT_FALSE(protocore_ntp_synced());

    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, 2, cookie_of(d), GOOD_EPOCH);

    TEST_ASSERT_TRUE(protocore_ntp_synced());
    TEST_ASSERT_EQUAL_UINT32(GOOD_EPOCH, (uint32_t)protocore_ntp_epoch());
    TEST_ASSERT_EQUAL_UINT32(GOOD_EPOCH, protocore_ntp_time_source());
}

// RFC 4330 sec 5 sanity check 3: the Originate Timestamp in the reply must match the Transmit
// Timestamp the request carried. A datagram that arrives on the client port with any other origin is
// not an answer to this client's question and must not move the clock.
void test_a_reply_whose_origin_does_not_echo_the_request_is_ignored(void)
{
    const protocore_net_host_dgram *d = ask(SERVER_IP);
    const uint32_t cookie = cookie_of(d);

    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, 2, cookie ^ 0xFFFFFFFFu, GOOD_EPOCH);
    TEST_ASSERT_FALSE(protocore_ntp_synced());

    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, 2, 0u, GOOD_EPOCH);
    TEST_ASSERT_FALSE(protocore_ntp_synced());

    // Off by one in the cookie is still not an answer.
    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, 2, cookie + 1u, GOOD_EPOCH);
    TEST_ASSERT_FALSE(protocore_ntp_synced());

    // The right cookie, and the same packet is taken.
    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, 2, cookie, GOOD_EPOCH);
    TEST_ASSERT_TRUE(protocore_ntp_synced());
}

// RFC 4330 sec 5: a unicast reply is Mode 4. A client request reflected back, or a symmetric or
// broadcast packet, is not one.
void test_a_packet_that_is_not_a_server_reply_is_ignored(void)
{
    const protocore_net_host_dgram *d = ask(SERVER_IP);
    const uint32_t cookie = cookie_of(d);

    static const uint8_t NOT_SERVER[] = {PROTOCORE_NTP_MODE_RESERVED,  PROTOCORE_NTP_MODE_SYM_ACTIVE,
                                         PROTOCORE_NTP_MODE_SYM_PASSIVE, PROTOCORE_NTP_MODE_CLIENT,
                                         PROTOCORE_NTP_MODE_BROADCAST, PROTOCORE_NTP_MODE_CONTROL,
                                         PROTOCORE_NTP_MODE_PRIVATE};
    for (size_t i = 0; i < sizeof NOT_SERVER / sizeof NOT_SERVER[0]; i++)
    {
        reply_with(PROTOCORE_NTP_LI_NONE, NOT_SERVER[i], 2, cookie, GOOD_EPOCH);
        TEST_ASSERT_FALSE(protocore_ntp_synced());
    }

    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, 2, cookie, GOOD_EPOCH);
    TEST_ASSERT_TRUE(protocore_ntp_synced());
}

// RFC 4330 sec 8: a reply with the Stratum field 0 is a Kiss-o'-Death packet carrying an ASCII kiss
// code in the Reference Identifier, not a time. Past 15 the sender is unsynchronized (sec 5).
void test_a_kiss_o_death_or_unsynchronized_stratum_is_ignored(void)
{
    const protocore_net_host_dgram *d = ask(SERVER_IP);
    const uint32_t cookie = cookie_of(d);

    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, PROTOCORE_NTP_STRATUM_KOD, cookie, GOOD_EPOCH);
    TEST_ASSERT_FALSE(protocore_ntp_synced());

    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, PROTOCORE_NTP_STRATUM_UNSYNC, cookie, GOOD_EPOCH);
    TEST_ASSERT_FALSE(protocore_ntp_synced());

    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, 255u, cookie, GOOD_EPOCH);
    TEST_ASSERT_FALSE(protocore_ntp_synced());

    // Stratum 1 (a primary reference) and 15 (the last secondary) are both taken.
    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, PROTOCORE_NTP_STRATUM_PRIMARY, cookie, GOOD_EPOCH);
    TEST_ASSERT_TRUE(protocore_ntp_synced());

    protocore_ntp_set_test_epoch(0);
    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, PROTOCORE_NTP_STRATUM_MAX, cookie, GOOD_EPOCH);
    TEST_ASSERT_TRUE(protocore_ntp_synced());
}

// A transmit stamp at or below the NTP/Unix offset is a zero (or pre-1970) Unix time, and anything
// before 2021 is a server whose own clock has not been set. Neither is followed.
void test_an_implausible_clock_is_ignored(void)
{
    const protocore_net_host_dgram *d = ask(SERVER_IP);
    const uint32_t cookie = cookie_of(d);

    // All-zero timestamps: RFC 4330 sec 5 says an unsynchronized server sends exactly this.
    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, 2, cookie, 0u);
    TEST_ASSERT_FALSE(protocore_ntp_synced());

    // 2020-12-31T23:59:59Z, one second before the 2021-01-01 floor.
    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, 2, cookie, 1609459199u);
    TEST_ASSERT_FALSE(protocore_ntp_synced());

    // 2021-01-01T00:00:01Z clears it.
    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, 2, cookie, 1609459201u);
    TEST_ASSERT_TRUE(protocore_ntp_synced());
    TEST_ASSERT_EQUAL_UINT32(1609459201u, (uint32_t)protocore_ntp_epoch());
}

// A datagram shorter than the 48-octet packet cannot carry the fields the checks read, so it is
// dropped before any of them run.
void test_a_reply_short_of_48_octets_is_ignored(void)
{
    (void)ask(SERVER_IP);
    uint8_t runt[PROTOCORE_NTP_PACKET_LEN - 1];
    memset(runt, 0, sizeof runt);
    runt[PROTOCORE_NTP_OFF_LI_VN_MODE] =
        PROTOCORE_NTP_LI_VN_MODE(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_VERSION, PROTOCORE_NTP_MODE_SERVER);
    protocore_net_host_udp_deliver(PROTOCORE_NTP_CLIENT_PORT, SERVER_IP, PROTOCORE_NTP_PORT, runt, sizeof runt);
    UdpListener.poll(UdpListener.internal);
    TEST_ASSERT_FALSE(protocore_ntp_synced());
}

// One reply fixes one instant; the monotonic clock carries the epoch forward from there, so the time
// advances between syncs without another round trip.
void test_the_epoch_advances_off_the_monotonic_clock(void)
{
    const protocore_net_host_dgram *d = ask(SERVER_IP);
    reply_with(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_MODE_SERVER, 2, cookie_of(d), GOOD_EPOCH);
    TEST_ASSERT_EQUAL_UINT32(GOOD_EPOCH, (uint32_t)protocore_ntp_epoch());

    set_millis(1000u + 5000u); // five seconds of ticks
    TEST_ASSERT_EQUAL_UINT32(GOOD_EPOCH + 5u, (uint32_t)protocore_ntp_epoch());

    // Sub-second ticks do not move the reported second.
    set_millis(1000u + 5999u);
    TEST_ASSERT_EQUAL_UINT32(GOOD_EPOCH + 5u, (uint32_t)protocore_ntp_epoch());
    set_millis(1000u + 6000u);
    TEST_ASSERT_EQUAL_UINT32(GOOD_EPOCH + 6u, (uint32_t)protocore_ntp_epoch());
}

// This client has no resolver of its own, so a server given as a name is refused rather than sent to
// an address parsed out of nothing.
void test_a_server_name_is_refused(void)
{
    protocore_net_host_udp_reset();
    TEST_ASSERT_FALSE(protocore_ntp_begin(NULL, "pool.ntp.org", NULL));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_net_host_udp_sent());

    TEST_ASSERT_FALSE(protocore_ntp_begin(NULL, "", NULL));
    TEST_ASSERT_FALSE(protocore_ntp_begin(NULL, "999.1.1.1", NULL));
}

// Never synced means no Date header rather than a false one. Once synced the shared IMF-fixdate
// formatter renders the instant: RFC 9110 sec 5.6.7's own example, at its Unix epoch 784111777.
void test_the_http_date_is_empty_until_synced(void)
{
    char buf[40];
    memset(buf, 'x', sizeof buf);
    TEST_ASSERT_FALSE(protocore_ntp_synced());
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ntp_http_date(buf, sizeof buf));

    protocore_ntp_set_test_epoch((time_t)784111777);
    TEST_ASSERT_TRUE(protocore_ntp_synced());
    TEST_ASSERT_EQUAL_size_t(29u, protocore_ntp_http_date(buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("Sun, 06 Nov 1994 08:49:37 GMT", buf);

    // Seeding 0 puts the client back to never-synced.
    protocore_ntp_set_test_epoch(0);
    TEST_ASSERT_FALSE(protocore_ntp_synced());
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)protocore_ntp_epoch());
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_ntp_time_source());
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ntp_http_date(buf, sizeof buf));
}
