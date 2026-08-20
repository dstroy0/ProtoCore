// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NTP/SNTP server reply builder (network_drivers/application/ntp_server/ntp_server.h).
//
// The load-bearing case is test_rfc5905_first_octet_packing: RFC 5905 sec 7.3 Figure 8 numbers the
// bits of the first octet (LI 0-1, VN 2-4, Mode 5-7) and Figure 10 assigns client = 3, server = 4,
// so a request octet and its reply octet are arithmetic from the diagram, shown in the comment. No
// expectation here is read off the module: request octets are written as raw literals derived from
// Figure 8 and reply octets are decoded by shift and mask in the test, not by ntp.h's macros.
//
// The reply rules come from RFC 4330 sec 5, which prints them as sentences and again as a table:
// "Unicast and manycast servers copy the VN and Poll fields of the request intact to the reply",
// "the Transmit Timestamp field of the request is copied unchanged to the Originate Timestamp field
// of the reply", "The Root Delay and Root Dispersion fields are set to 0 for a primary server", and
// "If the Mode field of the request is 3 (client), the reply is set to 4 (server). If this field is
// set to 1 (symmetric active), the reply is set to 2 (symmetric passive). For any other value in
// the Mode field, the request is discarded." RFC 5905 sec 14 Figure 31 (fast_xmit) repeats the
// copies as x.version <- r.version, x.poll <- r.poll, x.mode <- 4, x.org <- r.xmt.
//
// FOUR CASES FAIL AGAINST THE SHIPPED CODE, each asserting published text:
//   test_rfc4330_a_zero_poll_is_copied_intact_too            ntp_server.c:36 substitutes 6 for poll 0
//   test_rfc4330_a_primary_server_zeroes_delay_and_dispersion ntp_server.c:39 writes 1.0 s dispersion
//   test_rfc4330_a_non_client_mode_request_is_discarded      the handler answers every mode
//   test_rfc4330_symmetric_active_is_answered_symmetric_passive ntp_server.c:34 always writes mode 4
//
// The precision octet has no published value, only a published range (RFC 4330 sec 4: "-6 for
// mains-frequency clocks to -20 for microsecond clocks"), so it is asserted as a range.

#include "network_drivers/application/ntp_server/ntp_server.h"
#include "network_drivers/transport/udp/server/server.h"
#include "protocore_net_host.h"
#include "services/timing_position/time_source/time_source.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}

// Release the UDP/123 bind after every case, so a case that stops early leaves the port free.
void tearDown(void)
{
    UdpListenerV.port = PROTOCORE_NTP_PORT;
    UdpListener.close(protocore_udp_listener_span());
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

// Fill a 48-octet request. @p first_octet is written raw so the LI/VN/Mode packing is spelled by
// the caller, and the transmit stamp lands at octets 40..47 (Figure 8 word 10 and word 11).
static void make_request(uint8_t *req, uint8_t first_octet, uint8_t poll, uint32_t tx_sec, uint32_t tx_frac)
{
    memset(req, 0, PROTOCORE_NTP_PACKET_LEN);
    req[0] = first_octet;
    req[2] = poll;
    wr_be32(req + 40, tx_sec);
    wr_be32(req + 44, tx_frac);
}

// LI = 0, VN = 4, Mode = 3: 0*64 + 4*8 + 3 = 35 = 0x23.
#define REQ_V4_CLIENT 0x23u

static uint8_t li_of(uint8_t b)
{
    return (uint8_t)(b >> 6);
}
static uint8_t vn_of(uint8_t b)
{
    return (uint8_t)((b >> 3) & 0x07u);
}
static uint8_t mode_of(uint8_t b)
{
    return (uint8_t)(b & 0x07u);
}

// RFC 5905 sec 7.3 Figure 8 draws the header as twelve 32-bit words, so it is 12 * 4 = 48 octets and
// word n begins at octet 4n: word 0 carries LI/VN/Mode, Stratum, Poll, Precision at 0,1,2,3; word 1
// Root Delay at 4; word 2 Root Dispersion at 8; word 3 Reference ID at 12; words 4-5 Reference at
// 16,20; words 6-7 Origin at 24,28; words 8-9 Receive at 32,36; words 10-11 Transmit at 40,44.
//
// Port 123 is RFC 5905 sec 7.2 Figure 10, "PORT | 123 | NTP port number".
//
// The prime epoch is 0 h 1 January 1900 UTC (RFC 5905 sec 6), so the offset to the Unix epoch is
// 70 years of 365 days plus the leap days of 1904..1968, one every fourth year and 1900 not a leap
// year under the Gregorian century rule:
//   (1968 - 1904) / 4 + 1                        = 17 leap days
//   (70 * 365 + 17) * 86400 = 25567 * 86400      = 2208988800
void test_rfc5905_header_layout(void)
{
    TEST_ASSERT_EQUAL_UINT(48u, PROTOCORE_NTP_PACKET_LEN);
    TEST_ASSERT_EQUAL_UINT(0u, PROTOCORE_NTP_OFF_LI_VN_MODE);
    TEST_ASSERT_EQUAL_UINT(1u, PROTOCORE_NTP_OFF_STRATUM);
    TEST_ASSERT_EQUAL_UINT(2u, PROTOCORE_NTP_OFF_POLL);
    TEST_ASSERT_EQUAL_UINT(3u, PROTOCORE_NTP_OFF_PRECISION);
    TEST_ASSERT_EQUAL_UINT(4u, PROTOCORE_NTP_OFF_ROOT_DELAY);
    TEST_ASSERT_EQUAL_UINT(8u, PROTOCORE_NTP_OFF_ROOT_DISP);
    TEST_ASSERT_EQUAL_UINT(12u, PROTOCORE_NTP_OFF_REFID);
    TEST_ASSERT_EQUAL_UINT(16u, PROTOCORE_NTP_OFF_REF_SEC);
    TEST_ASSERT_EQUAL_UINT(20u, PROTOCORE_NTP_OFF_REF_FRAC);
    TEST_ASSERT_EQUAL_UINT(24u, PROTOCORE_NTP_OFF_ORIGIN_SEC);
    TEST_ASSERT_EQUAL_UINT(28u, PROTOCORE_NTP_OFF_ORIGIN_FRAC);
    TEST_ASSERT_EQUAL_UINT(32u, PROTOCORE_NTP_OFF_RX_SEC);
    TEST_ASSERT_EQUAL_UINT(36u, PROTOCORE_NTP_OFF_RX_FRAC);
    TEST_ASSERT_EQUAL_UINT(40u, PROTOCORE_NTP_OFF_TX_SEC);
    TEST_ASSERT_EQUAL_UINT(44u, PROTOCORE_NTP_OFF_TX_FRAC);
    TEST_ASSERT_EQUAL_UINT(123u, PROTOCORE_NTP_PORT);
    TEST_ASSERT_EQUAL_HEX32(2208988800u, PROTOCORE_NTP_UNIX_OFFSET);
}

// Figure 8 puts LI in the two most significant bits of octet 0, VN in the next three, Mode in the
// low three, so the octet is LI * 64 + VN * 8 + Mode. Figure 10 assigns client = 3, server = 4;
// Figure 9 assigns LI 0 = no warning; Figure 31 copies the version.
//
//   VN 3 client request  0 * 64 + 3 * 8 + 3 = 27 = 0x1B  ->  reply 0 * 64 + 3 * 8 + 4 = 28 = 0x1C
//   VN 4 client request  0 * 64 + 4 * 8 + 3 = 35 = 0x23  ->  reply 0 * 64 + 4 * 8 + 4 = 36 = 0x24
void test_rfc5905_first_octet_packing(void)
{
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];

    make_request(req, 0x1Bu, 6, 1, 1);
    NtpServer.build_response_args.req = req;
    NtpServer.build_response_args.req_len = sizeof req;
    NtpServer.build_response_args.stratum = 1;
    NtpServer.build_response_args.refid = 0x4C4F434Cu;
    NtpServer.build_response_args.protocore_ntp_secs = 1;
    NtpServer.build_response_args.protocore_ntp_frac = 1;
    NtpServer.build_response_args.out = out;
    NtpServer.build_response_args.out_cap = sizeof out;
    NtpServer.build_response(protocore_ntp_server_span());
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_NTP_PACKET_LEN, NtpServer.n);
    TEST_ASSERT_EQUAL_HEX8(0x1Cu, out[0]);

    make_request(req, 0x23u, 6, 1, 1);
    NtpServer.build_response_args.req = req;
    NtpServer.build_response_args.req_len = sizeof req;
    NtpServer.build_response_args.stratum = 1;
    NtpServer.build_response_args.refid = 0x4C4F434Cu;
    NtpServer.build_response_args.protocore_ntp_secs = 1;
    NtpServer.build_response_args.protocore_ntp_frac = 1;
    NtpServer.build_response_args.out = out;
    NtpServer.build_response_args.out_cap = sizeof out;
    NtpServer.build_response(protocore_ntp_server_span());
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_NTP_PACKET_LEN, NtpServer.n);
    TEST_ASSERT_EQUAL_HEX8(0x24u, out[0]);
}

// RFC 4330 sec 5: "Unicast and manycast servers copy the VN and Poll fields of the request intact to
// the reply". RFC 5905 sec 14 Figure 31: x.version <- r.version, x.poll <- r.poll.
// RFC 4330 sec 4 gives the poll field of a server message the range 4 (16 s) to 17 (131,072 s).
void test_rfc4330_version_and_poll_are_copied_intact(void)
{
    static const uint8_t POLL[4] = {4u, 6u, 10u, 17u};
    for (uint8_t vn = 1; vn <= 4; vn++)
    {
        for (unsigned i = 0; i < 4; i++)
        {
            uint8_t req[PROTOCORE_NTP_PACKET_LEN];
            uint8_t out[PROTOCORE_NTP_PACKET_LEN];
            // LI 0, VN vn, Mode 3: 0 * 64 + vn * 8 + 3.
            make_request(req, (uint8_t)((vn * 8u) + 3u), POLL[i], 1, 1);
            NtpServer.build_response_args.req = req;
            NtpServer.build_response_args.req_len = sizeof req;
            NtpServer.build_response_args.stratum = 1;
            NtpServer.build_response_args.refid = 0x4C4F434Cu;
            NtpServer.build_response_args.protocore_ntp_secs = 1;
            NtpServer.build_response_args.protocore_ntp_frac = 1;
            NtpServer.build_response_args.out = out;
            NtpServer.build_response_args.out_cap = sizeof out;
            NtpServer.build_response(protocore_ntp_server_span());
            TEST_ASSERT_EQUAL_UINT(PROTOCORE_NTP_PACKET_LEN, NtpServer.n);
            TEST_ASSERT_EQUAL_UINT8(vn, vn_of(out[0]));
            TEST_ASSERT_EQUAL_UINT8(4u, mode_of(out[0]));
            TEST_ASSERT_EQUAL_UINT8(POLL[i], out[PROTOCORE_NTP_OFF_POLL]);
        }
    }
}

// "Copy ... intact" and "x.poll <- r.poll" carry no exception for a poll of zero, and neither the
// sec 5 table nor Figure 31 names a substitute value.
void test_rfc4330_a_zero_poll_is_copied_intact_too(void)
{
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];

    make_request(req, REQ_V4_CLIENT, 0, 1, 1);
    NtpServer.build_response_args.req = req;
    NtpServer.build_response_args.req_len = sizeof req;
    NtpServer.build_response_args.stratum = 1;
    NtpServer.build_response_args.refid = 0x4C4F434Cu;
    NtpServer.build_response_args.protocore_ntp_secs = 1;
    NtpServer.build_response_args.protocore_ntp_frac = 1;
    NtpServer.build_response_args.out = out;
    NtpServer.build_response_args.out_cap = sizeof out;
    NtpServer.build_response(protocore_ntp_server_span());
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_NTP_PACKET_LEN, NtpServer.n);
    TEST_ASSERT_EQUAL_UINT8(0u, out[PROTOCORE_NTP_OFF_POLL]);
}

// RFC 4330 sec 4: the Precision field "is significant only in server messages, where the values
// range from -6 for mains-frequency clocks to -20 for microsecond clocks". The exact exponent is a
// property of the clock and is published nowhere, so only the range is asserted.
void test_rfc4330_precision_lies_in_the_published_range(void)
{
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];
    make_request(req, REQ_V4_CLIENT, 6, 1, 1);
    NtpServer.build_response_args.req = req;
    NtpServer.build_response_args.req_len = sizeof req;
    NtpServer.build_response_args.stratum = 1;
    NtpServer.build_response_args.refid = 0x4C4F434Cu;
    NtpServer.build_response_args.protocore_ntp_secs = 1;
    NtpServer.build_response_args.protocore_ntp_frac = 1;
    NtpServer.build_response_args.out = out;
    NtpServer.build_response_args.out_cap = sizeof out;
    NtpServer.build_response(protocore_ntp_server_span());
    (void)NtpServer.n;

    int prec = (int)(int8_t)out[PROTOCORE_NTP_OFF_PRECISION];
    TEST_ASSERT_LESS_OR_EQUAL_INT(-6, prec);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(-20, prec);
}

// RFC 4330 sec 5: "The Root Delay and Root Dispersion fields are set to 0 for a primary server."
// The sec 5 reply table repeats it: Root Delay 0, Root Dispersion 0. Sec 4 bounds the dispersion of
// a server message at "zero to several hundred milliseconds"; both fields are NTP short format,
// 16 bits of seconds then 16 of fraction (RFC 5905 sec 6), so 0x00010000 is 1.0 s.
void test_rfc4330_a_primary_server_zeroes_delay_and_dispersion(void)
{
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];
    make_request(req, REQ_V4_CLIENT, 6, 1, 1);
    NtpServer.build_response_args.req = req;
    NtpServer.build_response_args.req_len = sizeof req;
    NtpServer.build_response_args.stratum = PROTOCORE_NTP_STRATUM_PRIMARY;
    NtpServer.build_response_args.refid = 0x4C4F434Cu;
    NtpServer.build_response_args.protocore_ntp_secs = 1;
    NtpServer.build_response_args.protocore_ntp_frac = 1;
    NtpServer.build_response_args.out = out;
    NtpServer.build_response_args.out_cap = sizeof out;
    NtpServer.build_response(protocore_ntp_server_span());
    (void)NtpServer.n;

    TEST_ASSERT_EQUAL_HEX32(0u, rd_be32(out + PROTOCORE_NTP_OFF_ROOT_DELAY));
    TEST_ASSERT_EQUAL_HEX32(0u, rd_be32(out + PROTOCORE_NTP_OFF_ROOT_DISP));
}

// RFC 4330 sec 5: "the Transmit Timestamp field of the request is copied unchanged to the Originate
// Timestamp field of the reply. It is important that this field be copied intact". Figure 8 puts
// the request's transmit stamp at 40..47 and the reply's origin at 24..31.
void test_rfc4330_origin_is_the_request_transmit_stamp(void)
{
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];
    make_request(req, REQ_V4_CLIENT, 6, 0xCAFEF00Du, 0x0000FFFFu);
    NtpServer.build_response_args.req = req;
    NtpServer.build_response_args.req_len = sizeof req;
    NtpServer.build_response_args.stratum = 1;
    NtpServer.build_response_args.refid = 0x4C4F434Cu;
    NtpServer.build_response_args.protocore_ntp_secs = 0xE4A2C1F0u;
    NtpServer.build_response_args.protocore_ntp_frac = 0x80000000u;
    NtpServer.build_response_args.out = out;
    NtpServer.build_response_args.out_cap = sizeof out;
    NtpServer.build_response(protocore_ntp_server_span());
    (void)NtpServer.n;

    TEST_ASSERT_EQUAL_HEX8_ARRAY(req + 40, out + 24, 8);
    TEST_ASSERT_EQUAL_HEX32(0xCAFEF00Du, rd_be32(out + PROTOCORE_NTP_OFF_ORIGIN_SEC));
    TEST_ASSERT_EQUAL_HEX32(0x0000FFFFu, rd_be32(out + PROTOCORE_NTP_OFF_ORIGIN_FRAC));
}

// RFC 5905 sec 14 Figure 31: x.stratum <- s.stratum, x.refid <- s.refid. Figure 11 gives stratum 1
// to a primary server and 2-15 to a secondary server. Sec 7.3 makes a stratum-1 Reference ID "a
// four-octet, left-justified, zero-padded ASCII string"; RFC 4330 Figure 2 publishes the code LOCL
// for an uncalibrated local clock, so its four octets are 'L' 'O' 'C' 'L' = 0x4C4F434C, big-endian
// at offset 12.
void test_rfc5905_stratum_and_reference_id_are_written_verbatim(void)
{
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];

    for (uint8_t stratum = 1; stratum <= 15; stratum++)
    {
        make_request(req, REQ_V4_CLIENT, 6, 1, 1);
        NtpServer.build_response_args.req = req;
        NtpServer.build_response_args.req_len = sizeof req;
        NtpServer.build_response_args.stratum = stratum;
        NtpServer.build_response_args.refid = 0x4C4F434Cu;
        NtpServer.build_response_args.protocore_ntp_secs = 1;
        NtpServer.build_response_args.protocore_ntp_frac = 1;
        NtpServer.build_response_args.out = out;
        NtpServer.build_response_args.out_cap = sizeof out;
        NtpServer.build_response(protocore_ntp_server_span());
        (void)NtpServer.n;
        TEST_ASSERT_EQUAL_UINT8(stratum, out[PROTOCORE_NTP_OFF_STRATUM]);
    }

    TEST_ASSERT_EQUAL_HEX8('L', out[12]);
    TEST_ASSERT_EQUAL_HEX8('O', out[13]);
    TEST_ASSERT_EQUAL_HEX8('C', out[14]);
    TEST_ASSERT_EQUAL_HEX8('L', out[15]);
    TEST_ASSERT_EQUAL_HEX32(0x4C4F434Cu, PROTOCORE_NTP_REFID_LOCL);
}

// RFC 5905 sec 6: in the timestamp format "a value of zero is a special case representing unknown or
// unsynchronized time", and RFC 4330 sec 5 sets every timestamp to zero only when the server is
// unsynchronized. RFC 5905 Figure 9 gives LI 3 the meaning "clock unsynchronized". A reply built
// from a real instant therefore carries no zero timestamp and does not announce LI 3.
//
// The three stamps name the last reference update, the arrival, and the departure, in that order,
// so reference <= receive <= transmit whatever instant a stateless server reads.
void test_rfc5905_a_synchronized_reply_carries_no_zero_timestamp(void)
{
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];
    const uint32_t secs = 0xE4A2C1F0u;
    const uint32_t frac = 0x80000000u;

    make_request(req, REQ_V4_CLIENT, 6, 1, 1);
    NtpServer.build_response_args.req = req;
    NtpServer.build_response_args.req_len = sizeof req;
    NtpServer.build_response_args.stratum = 1;
    NtpServer.build_response_args.refid = 0x4C4F434Cu;
    NtpServer.build_response_args.protocore_ntp_secs = secs;
    NtpServer.build_response_args.protocore_ntp_frac = frac;
    NtpServer.build_response_args.out = out;
    NtpServer.build_response_args.out_cap = sizeof out;
    NtpServer.build_response(protocore_ntp_server_span());
    (void)NtpServer.n;

    TEST_ASSERT_TRUE(li_of(out[0]) != PROTOCORE_NTP_LI_UNSYNC);
    TEST_ASSERT_NOT_EQUAL(0u, rd_be32(out + PROTOCORE_NTP_OFF_REF_SEC));
    TEST_ASSERT_NOT_EQUAL(0u, rd_be32(out + PROTOCORE_NTP_OFF_RX_SEC));
    TEST_ASSERT_NOT_EQUAL(0u, rd_be32(out + PROTOCORE_NTP_OFF_TX_SEC));

    TEST_ASSERT_LESS_OR_EQUAL_UINT32(rd_be32(out + PROTOCORE_NTP_OFF_RX_SEC), rd_be32(out + PROTOCORE_NTP_OFF_REF_SEC));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(rd_be32(out + PROTOCORE_NTP_OFF_TX_SEC), rd_be32(out + PROTOCORE_NTP_OFF_RX_SEC));

    // Figure 31: x.xmt <- clock, the field a client reads the time from.
    TEST_ASSERT_EQUAL_HEX32(secs, rd_be32(out + PROTOCORE_NTP_OFF_TX_SEC));
    TEST_ASSERT_EQUAL_HEX32(frac, rd_be32(out + PROTOCORE_NTP_OFF_TX_FRAC));
}

// The header is 48 octets (Figure 8), so a shorter request cannot be read and a shorter buffer
// cannot hold a reply. A null on either side is the same refusal.
void test_a_packet_short_of_the_48_octet_header_is_refused(void)
{
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];
    make_request(req, REQ_V4_CLIENT, 6, 1, 1);

    NtpServer.build_response_args.req = req;
    NtpServer.build_response_args.req_len = PROTOCORE_NTP_PACKET_LEN - 1;
    NtpServer.build_response_args.stratum = 1;
    NtpServer.build_response_args.refid = 0x4C4F434Cu;
    NtpServer.build_response_args.protocore_ntp_secs = 1;
    NtpServer.build_response_args.protocore_ntp_frac = 1;
    NtpServer.build_response_args.out = out;
    NtpServer.build_response_args.out_cap = sizeof out;
    NtpServer.build_response(protocore_ntp_server_span());
    TEST_ASSERT_EQUAL_UINT(0, NtpServer.n);
    NtpServer.build_response_args.req = req;
    NtpServer.build_response_args.req_len = sizeof req;
    NtpServer.build_response_args.stratum = 1;
    NtpServer.build_response_args.refid = 0x4C4F434Cu;
    NtpServer.build_response_args.protocore_ntp_secs = 1;
    NtpServer.build_response_args.protocore_ntp_frac = 1;
    NtpServer.build_response_args.out = out;
    NtpServer.build_response_args.out_cap = PROTOCORE_NTP_PACKET_LEN - 1;
    NtpServer.build_response(protocore_ntp_server_span());
    TEST_ASSERT_EQUAL_UINT(0, NtpServer.n);
    NtpServer.build_response_args.req = NULL;
    NtpServer.build_response_args.req_len = PROTOCORE_NTP_PACKET_LEN;
    NtpServer.build_response_args.stratum = 1;
    NtpServer.build_response_args.refid = 0x4C4F434Cu;
    NtpServer.build_response_args.protocore_ntp_secs = 1;
    NtpServer.build_response_args.protocore_ntp_frac = 1;
    NtpServer.build_response_args.out = out;
    NtpServer.build_response_args.out_cap = sizeof out;
    NtpServer.build_response(protocore_ntp_server_span());
    TEST_ASSERT_EQUAL_UINT(0, NtpServer.n);
    NtpServer.build_response_args.req = req;
    NtpServer.build_response_args.req_len = sizeof req;
    NtpServer.build_response_args.stratum = 1;
    NtpServer.build_response_args.refid = 0x4C4F434Cu;
    NtpServer.build_response_args.protocore_ntp_secs = 1;
    NtpServer.build_response_args.protocore_ntp_frac = 1;
    NtpServer.build_response_args.out = NULL;
    NtpServer.build_response_args.out_cap = PROTOCORE_NTP_PACKET_LEN;
    NtpServer.build_response(protocore_ntp_server_span());
    TEST_ASSERT_EQUAL_UINT(0, NtpServer.n);
    NtpServer.build_response_args.req = req;
    NtpServer.build_response_args.req_len = PROTOCORE_NTP_PACKET_LEN;
    NtpServer.build_response_args.stratum = 1;
    NtpServer.build_response_args.refid = 0x4C4F434Cu;
    NtpServer.build_response_args.protocore_ntp_secs = 1;
    NtpServer.build_response_args.protocore_ntp_frac = 1;
    NtpServer.build_response_args.out = out;
    NtpServer.build_response_args.out_cap = PROTOCORE_NTP_PACKET_LEN;
    NtpServer.build_response(protocore_ntp_server_span());
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_NTP_PACKET_LEN, NtpServer.n);
}

static uint32_t g_epoch = 0;
static uint32_t fake_clock(void)
{
    return g_epoch;
}

static void poll_once(void)
{
    UdpListener.poll(protocore_udp_listener_span());
}

static void serve(const void *req, uint16_t len, const char *from_ip, uint16_t from_port)
{
    protocore_net_host_udp_reset();
    TEST_ASSERT_TRUE(protocore_net_host_udp_deliver(PROTOCORE_NTP_PORT, from_ip, from_port, (void *)req, len));
    poll_once();
}

static void start_server(uint32_t epoch, uint8_t stratum, uint32_t refid)
{
    protocore_net_host_reset();
    protocore_time_source_reset();
    g_epoch = epoch;
    TEST_ASSERT_TRUE(protocore_time_source_add("test", 0, fake_clock));
    NtpServer.begin_args.stratum = stratum;
    NtpServer.begin_args.refid = refid;
    NtpServer.begin(protocore_ntp_server_span());
    TEST_ASSERT_TRUE(NtpServer.ok);
}

// RFC 5905 sec 7.2 Figure 10: PORT = 123.
void test_rfc5905_begin_binds_the_published_port(void)
{
    protocore_net_host_reset();
    NtpServer.begin_args.stratum = 1;
    NtpServer.begin_args.refid = 0x4C4F434Cu;
    NtpServer.begin(protocore_ntp_server_span());
    TEST_ASSERT_TRUE(NtpServer.ok);
    TEST_ASSERT_NOT_NULL(protocore_net_host_udp_pcb(123u));
}

// The reply leaves from port 123 for the requester's port, carries mode 4, echoes the origin stamp,
// and carries the local clock advanced to the prime epoch by PROTOCORE_NTP_UNIX_OFFSET.
void test_a_client_request_is_answered_on_the_wire(void)
{
    start_server(1700000000u, 1, 0x4C4F434Cu);

    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    make_request(req, REQ_V4_CLIENT, 6, 0xCAFEF00Du, 0x0000FFFFu);
    serve(req, PROTOCORE_NTP_PACKET_LEN, "192.0.2.5", 40000);

    TEST_ASSERT_EQUAL_size_t(1u, protocore_net_host_udp_count());
    const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_NTP_PACKET_LEN, d->len);
    TEST_ASSERT_EQUAL_UINT16(123u, d->src_port);
    TEST_ASSERT_EQUAL_UINT16(40000, d->dst_port);
    TEST_ASSERT_EQUAL_HEX8(0x24u, d->data[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, d->data[PROTOCORE_NTP_OFF_STRATUM]);
    TEST_ASSERT_EQUAL_HEX32(0x4C4F434Cu, rd_be32(d->data + PROTOCORE_NTP_OFF_REFID));
    TEST_ASSERT_EQUAL_HEX32(0xCAFEF00Du, rd_be32(d->data + PROTOCORE_NTP_OFF_ORIGIN_SEC));
    TEST_ASSERT_EQUAL_HEX32(1700000000u + 2208988800u, rd_be32(d->data + PROTOCORE_NTP_OFF_TX_SEC));
}

// RFC 4330 sec 5: "For any other value in the Mode field, the request is discarded." Figure 10
// numbers the modes: 0 reserved, 2 symmetric passive, 4 server, 5 broadcast, 6 control, 7 private.
// Mode 3 (client) and mode 1 (symmetric active) are the two the section answers.
void test_rfc4330_a_non_client_mode_request_is_discarded(void)
{
    static const uint8_t MODE[6] = {0u, 2u, 4u, 5u, 6u, 7u};
    start_server(1700000000u, 1, 0x4C4F434Cu);

    for (unsigned i = 0; i < 6; i++)
    {
        uint8_t req[PROTOCORE_NTP_PACKET_LEN];
        // LI 0, VN 4, Mode MODE[i]: 0 * 64 + 4 * 8 + MODE[i] = 32 + MODE[i].
        make_request(req, (uint8_t)(32u + MODE[i]), 6, 1, 1);
        serve(req, PROTOCORE_NTP_PACKET_LEN, "192.0.2.5", 40000);
        TEST_ASSERT_EQUAL_size_t(0u, protocore_net_host_udp_sent());
    }
}

// RFC 4330 sec 5: "If this field is set to 1 (symmetric active), the reply is set to 2 (symmetric
// passive)." RFC 5905 Appendix A.5.1 case NEWPS mobilizes the answering association as M_PASV,
// which sec 7.3 Figure 10 numbers 2. Request octet: 0 * 64 + 4 * 8 + 1 = 33 = 0x21.
void test_rfc4330_symmetric_active_is_answered_symmetric_passive(void)
{
    start_server(1700000000u, 1, 0x4C4F434Cu);

    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    make_request(req, 0x21u, 6, 1, 1);
    serve(req, PROTOCORE_NTP_PACKET_LEN, "192.0.2.5", 40000);

    TEST_ASSERT_EQUAL_size_t(1u, protocore_net_host_udp_count());
    const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_UINT8(2u, mode_of(d->data[0]));
}

// RFC 5905 sec 6: a zero timestamp represents unknown or unsynchronized time, and Figure 9 gives
// LI 3 the same meaning. A server with no clock therefore either stays silent or answers with LI 3
// and zero timestamps; what it must not do is put an invented instant on the wire.
void test_a_server_with_no_clock_puts_no_time_on_the_wire(void)
{
    start_server(0u, 1, 0x4C4F434Cu);

    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    make_request(req, REQ_V4_CLIENT, 6, 1, 1);
    serve(req, PROTOCORE_NTP_PACKET_LEN, "192.0.2.5", 40000);

    if (protocore_net_host_udp_count() != 0u)
    {
        const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
        TEST_ASSERT_NOT_NULL(d);
        TEST_ASSERT_EQUAL_UINT8(PROTOCORE_NTP_LI_UNSYNC, li_of(d->data[0]));
        TEST_ASSERT_EQUAL_HEX32(0u, rd_be32(d->data + PROTOCORE_NTP_OFF_TX_SEC));
    }
}

// A datagram shorter than the 48-octet header is not an NTP packet, so nothing is sent back.
void test_a_runt_datagram_is_dropped(void)
{
    start_server(1700000000u, 1, 0x4C4F434Cu);

    uint8_t runt[8] = {REQ_V4_CLIENT, 0, 6, 0, 0, 0, 0, 0};
    serve(runt, (uint16_t)sizeof runt, "192.0.2.5", 40000);

    TEST_ASSERT_EQUAL_size_t(0u, protocore_net_host_udp_sent());
}
