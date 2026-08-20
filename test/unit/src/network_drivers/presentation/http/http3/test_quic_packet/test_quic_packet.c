// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for QUIC packet headers and packet-number coding
// (network_drivers/presentation/http/http3/quic_packet.h).
//
// RFC 9001 Appendix A prints the unprotected headers of a real client Initial (A.2), server Initial
// (A.3), Retry (A.4) and short-header (A.5) packet as hex strings.
// test_rfc9001_published_headers is the load-bearing case: those four byte strings are parsed and
// two of them rebuilt, so every field offset in RFC 9000 sec 17.2 / 17.3 is checked against packets
// the specification itself wrote out rather than against this module's own idea of the layout. The
// packet-number cases use the three worked examples of RFC 9000 Appendix A.2 and A.3.

#include "network_drivers/presentation/http/http3/quic_packet/quic_packet.h"
#include <string.h>

#include <unity.h>

static uint8_t quic_packet_work[16]; // the borrow an entry takes; QuicPacket never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_out[64];

// A stand-in for an absent connection ID, so a zero length is passed with a real pointer.
static const uint8_t NONE[1] = {0};

// RFC 9001 A.2: "The header includes the connection ID and a packet number of 2:
//   c300000001088394c8f03e5157080000449e00000002"
static const uint8_t A2_CLIENT_INITIAL[22] = {0xc3, 0x00, 0x00, 0x00, 0x01, 0x08, 0x83, 0x94, 0xc8, 0xf0, 0x3e,
                                              0x51, 0x57, 0x08, 0x00, 0x00, 0x44, 0x9e, 0x00, 0x00, 0x00, 0x02};

// RFC 9001 A.3: "The header from the server includes a new connection ID and a 2-byte packet number
// encoding for a packet number of 1:  c1000000010008f067a5502a4262b50040750001"
static const uint8_t A3_SERVER_INITIAL[20] = {0xc1, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0xf0, 0x67, 0xa5,
                                              0x50, 0x2a, 0x42, 0x62, 0xb5, 0x00, 0x40, 0x75, 0x00, 0x01};

// RFC 9001 A.4: the Retry packet, whose first byte carries long packet type 0x03.
static const uint8_t A4_RETRY[36] = {0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0xf0, 0x67, 0xa5, 0x50, 0x2a,
                                     0x42, 0x62, 0xb5, 0x74, 0x6f, 0x6b, 0x65, 0x6e, 0x04, 0xa2, 0x65, 0xba,
                                     0x2e, 0xff, 0x4d, 0x82, 0x90, 0x58, 0xfb, 0x3f, 0x0f, 0x24, 0x96, 0xba};

// The three long headers RFC 9001 Appendix A publishes, read field by field against RFC 9000
// sec 17.2 Figure 13: Header Form, Fixed Bit, Long Packet Type, Type-Specific Bits, Version,
// DCID Length, DCID, SCID Length, SCID.
void test_rfc9001_published_headers(void)
{
    QuicLongHeader h;

    // A.2 client Initial: 0xc3 = long form, Fixed Bit, type 0x00, and 0b11 in the packet-number
    // length bits, which sec 17.2 defines as length - 1, so 4 octets - the RFC says as much.
    QuicPacket.is_long_header_args.first = A2_CLIENT_INITIAL[0];
    QuicPacket.is_long_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacket.ok);
    QuicPacket.parse_long_header_args.buf = A2_CLIENT_INITIAL;
    QuicPacket.parse_long_header_args.len = sizeof(A2_CLIENT_INITIAL);
    QuicPacket.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacket.ok);
    TEST_ASSERT_EQUAL_HEX8(0xc3, h.first);
    TEST_ASSERT_EQUAL_HEX8(QUIC_LP_INITIAL, h.type);
    TEST_ASSERT_EQUAL_HEX32(QUIC_VERSION_1, h.version);
    TEST_ASSERT_EQUAL_UINT8(8, h.dcid_len);
    TEST_ASSERT_EQUAL_MEMORY(A2_CLIENT_INITIAL + 6, h.dcid, 8);
    TEST_ASSERT_EQUAL_UINT8(0, h.scid_len);
    TEST_ASSERT_EQUAL_UINT(15u, h.hdr_len); // 1 + 4 + 1 + 8 + 1 + 0
    TEST_ASSERT_EQUAL_UINT8(4, (h.first & 0x03) + 1);

    // A.3 server Initial: no DCID, an 8-octet SCID, and 0b01 giving a 2-octet packet number
    QuicPacket.parse_long_header_args.buf = A3_SERVER_INITIAL;
    QuicPacket.parse_long_header_args.len = sizeof(A3_SERVER_INITIAL);
    QuicPacket.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacket.ok);
    TEST_ASSERT_EQUAL_HEX8(0xc1, h.first);
    TEST_ASSERT_EQUAL_HEX8(QUIC_LP_INITIAL, h.type);
    TEST_ASSERT_EQUAL_UINT8(0, h.dcid_len);
    TEST_ASSERT_EQUAL_UINT8(8, h.scid_len);
    TEST_ASSERT_EQUAL_MEMORY(A3_SERVER_INITIAL + 7, h.scid, 8);
    TEST_ASSERT_EQUAL_UINT(15u, h.hdr_len); // 1 + 4 + 1 + 0 + 1 + 8
    TEST_ASSERT_EQUAL_UINT8(2, (h.first & 0x03) + 1);

    // A.4 Retry: 0xff carries long packet type 0b11, which Table 5 assigns to Retry
    QuicPacket.parse_long_header_args.buf = A4_RETRY;
    QuicPacket.parse_long_header_args.len = sizeof(A4_RETRY);
    QuicPacket.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacket.ok);
    TEST_ASSERT_EQUAL_HEX8(QUIC_LP_RETRY, h.type);
    TEST_ASSERT_EQUAL_HEX32(QUIC_VERSION_1, h.version);
    TEST_ASSERT_EQUAL_UINT8(0, h.dcid_len);
    TEST_ASSERT_EQUAL_UINT8(8, h.scid_len);
    TEST_ASSERT_EQUAL_UINT(15u, h.hdr_len);
}

// The invariant part of each published header, rebuilt: first byte through Source Connection ID.
void test_build_reproduces_the_published_headers(void)
{
    static const uint8_t A2_DCID[8] = {0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08};
    static const uint8_t A3_SCID[8] = {0xf0, 0x67, 0xa5, 0x50, 0x2a, 0x42, 0x62, 0xb5};

    QuicPacket.build_long_header_args.out = g_out;
    QuicPacket.build_long_header_args.cap = sizeof(g_out);
    QuicPacket.build_long_header_args.type = QUIC_LP_INITIAL;
    QuicPacket.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacket.build_long_header_args.dcid = A2_DCID;
    QuicPacket.build_long_header_args.dcid_len = 8;
    QuicPacket.build_long_header_args.scid = NONE;
    QuicPacket.build_long_header_args.scid_len = 0;
    QuicPacket.build_long_header_args.pn_len = 4;
    QuicPacket.build_long_header(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT(15u, QuicPacket.n);
    TEST_ASSERT_EQUAL_MEMORY(A2_CLIENT_INITIAL, g_out, 15);

    QuicPacket.build_long_header_args.out = g_out;
    QuicPacket.build_long_header_args.cap = sizeof(g_out);
    QuicPacket.build_long_header_args.type = QUIC_LP_INITIAL;
    QuicPacket.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacket.build_long_header_args.dcid = NONE;
    QuicPacket.build_long_header_args.dcid_len = 0;
    QuicPacket.build_long_header_args.scid = A3_SCID;
    QuicPacket.build_long_header_args.scid_len = 8;
    QuicPacket.build_long_header_args.pn_len = 2;
    QuicPacket.build_long_header(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT(15u, QuicPacket.n);
    TEST_ASSERT_EQUAL_MEMORY(A3_SERVER_INITIAL, g_out, 15);
}

// RFC 9000 sec 17.2 Table 5 assigns the four long packet types, and the builder places the type in
// bits 0x30 of the first byte.
void test_rfc9000_long_packet_types(void)
{
    static const uint8_t TYPES[4] = {QUIC_LP_INITIAL, QUIC_LP_0RTT, QUIC_LP_HANDSHAKE, QUIC_LP_RETRY};
    TEST_ASSERT_EQUAL_HEX8(0x00, QUIC_LP_INITIAL);
    TEST_ASSERT_EQUAL_HEX8(0x01, QUIC_LP_0RTT);
    TEST_ASSERT_EQUAL_HEX8(0x02, QUIC_LP_HANDSHAKE);
    TEST_ASSERT_EQUAL_HEX8(0x03, QUIC_LP_RETRY);
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, QUIC_VERSION_1);

    for (size_t i = 0; i < 4; i++)
    {
        QuicLongHeader h;
        QuicPacket.build_long_header_args.out = g_out;
        QuicPacket.build_long_header_args.cap = sizeof(g_out);
        QuicPacket.build_long_header_args.type = TYPES[i];
        QuicPacket.build_long_header_args.version = QUIC_VERSION_1;
        QuicPacket.build_long_header_args.dcid = NONE;
        QuicPacket.build_long_header_args.dcid_len = 0;
        QuicPacket.build_long_header_args.scid = NONE;
        QuicPacket.build_long_header_args.scid_len = 0;
        QuicPacket.build_long_header_args.pn_len = 1;
        QuicPacket.build_long_header(quic_packet_work);
        size_t n = QuicPacket.n;
        TEST_ASSERT_EQUAL_UINT(7u, n);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xC0 | (TYPES[i] << 4)), g_out[0]);
        QuicPacket.parse_long_header_args.buf = g_out;
        QuicPacket.parse_long_header_args.len = n;
        QuicPacket.parse_long_header_args.out = &h;
        QuicPacket.parse_long_header(quic_packet_work);
        TEST_ASSERT_TRUE(QuicPacket.ok);
        TEST_ASSERT_EQUAL_HEX8(TYPES[i], h.type);
    }
}

// RFC 9000 sec 17.2: "Fixed Bit: The next bit (0x40) of byte 0 is set to 1, unless the packet is a
// Version Negotiation packet. Packets containing a zero value for this bit are not valid packets in
// this version and MUST be discarded." sec 17.3.1 says the same of the short header.
void test_rfc9000_fixed_bit_is_required(void)
{
    QuicLongHeader h;
    uint8_t buf[sizeof(A2_CLIENT_INITIAL)];
    memcpy(buf, A2_CLIENT_INITIAL, sizeof(buf));
    buf[0] &= (uint8_t)~0x40;
    QuicPacket.parse_long_header_args.buf = buf;
    QuicPacket.parse_long_header_args.len = sizeof(buf);
    QuicPacket.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_FALSE(QuicPacket.ok);

    QuicShortHeader s;
    uint8_t sh[2] = {0x42, 0x00};
    QuicPacket.parse_short_header_args.buf = sh;
    QuicPacket.parse_short_header_args.len = sizeof(sh);
    QuicPacket.parse_short_header_args.dcid_len = 0;
    QuicPacket.parse_short_header_args.out = &s;
    QuicPacket.parse_short_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacket.ok);
    sh[0] = 0x02; // Fixed Bit clear
    QuicPacket.parse_short_header_args.buf = sh;
    QuicPacket.parse_short_header_args.len = sizeof(sh);
    QuicPacket.parse_short_header_args.dcid_len = 0;
    QuicPacket.parse_short_header_args.out = &s;
    QuicPacket.parse_short_header(quic_packet_work);
    TEST_ASSERT_FALSE(QuicPacket.ok);
    sh[0] = 0xC2; // long form, so not a short header at all
    QuicPacket.parse_short_header_args.buf = sh;
    QuicPacket.parse_short_header_args.len = sizeof(sh);
    QuicPacket.parse_short_header_args.dcid_len = 0;
    QuicPacket.parse_short_header_args.out = &s;
    QuicPacket.parse_short_header(quic_packet_work);
    TEST_ASSERT_FALSE(QuicPacket.ok);
}

// RFC 9001 A.5: "unprotected header = 4200bff4" for a short-header packet with an empty Destination
// Connection ID and a 3-octet packet number. RFC 9000 sec 17.3.1 Figure 19 puts Header Form (0),
// Fixed Bit (1), Spin Bit, two reserved bits, Key Phase and the Packet Number Length in byte 0, so
// 0x42 = 0100 0010 is spin 0, key phase 0 and 0b10 = 3 octets of packet number.
void test_rfc9001_short_header(void)
{
    static const uint8_t A5[4] = {0x42, 0x00, 0xbf, 0xf4};
    QuicShortHeader s;
    QuicPacket.is_long_header_args.first = A5[0];
    QuicPacket.is_long_header(quic_packet_work);
    TEST_ASSERT_FALSE(QuicPacket.ok);
    QuicPacket.parse_short_header_args.buf = A5;
    QuicPacket.parse_short_header_args.len = sizeof(A5);
    QuicPacket.parse_short_header_args.dcid_len = 0;
    QuicPacket.parse_short_header_args.out = &s;
    QuicPacket.parse_short_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacket.ok);
    TEST_ASSERT_EQUAL_HEX8(0x42, s.first);
    TEST_ASSERT_EQUAL_UINT8(0, s.spin);
    TEST_ASSERT_EQUAL_UINT8(0, s.key_phase);
    TEST_ASSERT_EQUAL_UINT8(3, s.pn_len);
    TEST_ASSERT_EQUAL_UINT8(0, s.dcid_len);
    TEST_ASSERT_EQUAL_UINT(1u, s.hdr_len);

    // the spin and key-phase bits, each read off its own mask (0x20 and 0x04)
    static const uint8_t SPUN[6] = {0x64, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    QuicPacket.parse_short_header_args.buf = SPUN;
    QuicPacket.parse_short_header_args.len = sizeof(SPUN);
    QuicPacket.parse_short_header_args.dcid_len = 4;
    QuicPacket.parse_short_header_args.out = &s;
    QuicPacket.parse_short_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacket.ok);
    TEST_ASSERT_EQUAL_UINT8(1, s.spin);
    TEST_ASSERT_EQUAL_UINT8(1, s.key_phase);
    TEST_ASSERT_EQUAL_UINT8(1, s.pn_len);
    TEST_ASSERT_EQUAL_UINT8(4, s.dcid_len);
    TEST_ASSERT_EQUAL_MEMORY(SPUN + 1, s.dcid, 4);
    TEST_ASSERT_EQUAL_UINT(5u, s.hdr_len);
}

// RFC 9000 sec 17.2.1 Figure 14: a Version Negotiation packet is the long form with Version 0, the
// echoed connection IDs, then a list of Supported Versions. sec 17.2.1 also has the server echo the
// received Source Connection ID as its Destination Connection ID.
void test_rfc9000_version_negotiation(void)
{
    static const uint8_t DCID[2] = {0xf0, 0x67};
    static const uint8_t SCID[2] = {0x83, 0x94};
    static const uint32_t VERSIONS[2] = {QUIC_VERSION_1, 0x1a2a3a4au};
    static const uint8_t WANT[19] = {0xc0, 0x00, 0x00, 0x00, 0x00, 0x02, 0xf0, 0x67, 0x02, 0x83,
                                     0x94, 0x00, 0x00, 0x00, 0x01, 0x1a, 0x2a, 0x3a, 0x4a};
    QuicPacket.build_version_negotiation_args.out = g_out;
    QuicPacket.build_version_negotiation_args.cap = sizeof(g_out);
    QuicPacket.build_version_negotiation_args.dcid = DCID;
    QuicPacket.build_version_negotiation_args.dcid_len = 2;
    QuicPacket.build_version_negotiation_args.scid = SCID;
    QuicPacket.build_version_negotiation_args.scid_len = 2;
    QuicPacket.build_version_negotiation_args.versions = VERSIONS;
    QuicPacket.build_version_negotiation_args.nversions = 2;
    QuicPacket.build_version_negotiation(quic_packet_work);
    size_t n = QuicPacket.n;
    TEST_ASSERT_EQUAL_UINT(19u, n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 19);

    // Version 0 is what marks it, and it is the one long header exempt from the Fixed Bit rule
    QuicLongHeader h;
    QuicPacket.parse_long_header_args.buf = g_out;
    QuicPacket.parse_long_header_args.len = n;
    QuicPacket.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacket.ok);
    TEST_ASSERT_EQUAL_HEX32(0u, h.version);
    TEST_ASSERT_EQUAL_UINT8(2, h.dcid_len);
    TEST_ASSERT_EQUAL_UINT8(2, h.scid_len);
    TEST_ASSERT_EQUAL_UINT(11u, h.hdr_len);

    g_out[0] &= (uint8_t)~0x40;
    QuicPacket.parse_long_header_args.buf = g_out;
    QuicPacket.parse_long_header_args.len = n;
    QuicPacket.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacket.ok);
}

// RFC 9000 Appendix A.2 worked example: "if an endpoint has received an acknowledgment for packet
// 0xabe8b3 and is sending a packet with a number of 0xac5c02, there are 29,519 (0x734f) outstanding
// packet numbers. In order to represent at least twice this range (59,038 packets, or 0xe69e), 16
// bits are required." and "sending a packet with a number of 0xace8fe uses the 24-bit encoding".
void test_rfc9000_a2_packet_number_length(void)
{
    QuicPacket.pn_length_args.full_pn = 0xac5c02u;
    QuicPacket.pn_length_args.largest_acked = (int64_t)0xabe8b3;
    QuicPacket.pn_length(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT8(2, QuicPacket.u8);
    QuicPacket.pn_length_args.full_pn = 0xace8feu;
    QuicPacket.pn_length_args.largest_acked = (int64_t)0xabe8b3;
    QuicPacket.pn_length(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT8(3, QuicPacket.u8);

    // A.2's encode step is "truncate to the num_bytes least significant bytes", big-endian
    QuicPacket.pn_encode_args.out = g_out;
    QuicPacket.pn_encode_args.cap = sizeof(g_out);
    QuicPacket.pn_encode_args.full_pn = 0xac5c02u;
    QuicPacket.pn_encode_args.largest_acked = (int64_t)0xabe8b3;
    QuicPacket.pn_encode(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT(2u, QuicPacket.n);
    TEST_ASSERT_EQUAL_HEX8(0x5c, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, g_out[1]);
    QuicPacket.pn_encode_args.out = g_out;
    QuicPacket.pn_encode_args.cap = sizeof(g_out);
    QuicPacket.pn_encode_args.full_pn = 0xace8feu;
    QuicPacket.pn_encode_args.largest_acked = (int64_t)0xabe8b3;
    QuicPacket.pn_encode(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT(3u, QuicPacket.n);
    TEST_ASSERT_EQUAL_HEX8(0xac, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xe8, g_out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xfe, g_out[2]);

    // "if largest_acked is None: num_unacked = full_pn + 1" - packet 0 with nothing acked needs one
    // octet, and the width grows with the count of unacknowledged numbers, not with the value
    QuicPacket.pn_length_args.full_pn = 0u;
    QuicPacket.pn_length_args.largest_acked = -1;
    QuicPacket.pn_length(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT8(1, QuicPacket.u8);
    QuicPacket.pn_length_args.full_pn = 126u;
    QuicPacket.pn_length_args.largest_acked = -1;
    QuicPacket.pn_length(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT8(1, QuicPacket.u8); // 2 * 127 = 254 fits in 8 bits
    QuicPacket.pn_length_args.full_pn = 128u;
    QuicPacket.pn_length_args.largest_acked = -1;
    QuicPacket.pn_length(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT8(2, QuicPacket.u8); // 2 * 129 = 258 does not
    QuicPacket.pn_length_args.full_pn = 1u << 24;
    QuicPacket.pn_length_args.largest_acked = -1;
    QuicPacket.pn_length(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT8(4, QuicPacket.u8);
    QuicPacket.pn_encode_args.out = g_out;
    QuicPacket.pn_encode_args.cap = 1;
    QuicPacket.pn_encode_args.full_pn = 0xac5c02u;
    QuicPacket.pn_encode_args.largest_acked = (int64_t)0xabe8b3;
    QuicPacket.pn_encode(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicPacket.n);
}

// RFC 9000 Appendix A.3 worked example: "if the highest successfully authenticated packet had a
// packet number of 0xa82f30ea, then a packet containing a 16-bit value of 0x9b32 will be decoded as
// 0xa82f9b32."
void test_rfc9000_a3_packet_number_decode(void)
{
    QuicPacket.pn_decode_args.largest_pn = 0xa82f30eaULL;
    QuicPacket.pn_decode_args.truncated_pn = 0x9b32ULL;
    QuicPacket.pn_decode_args.pn_nbits = 16;
    QuicPacket.pn_decode(quic_packet_work);
    TEST_ASSERT_EQUAL_HEX64(0xa82f9b32ULL, QuicPacket.u64);

    // RFC 9001 A.5 states a packet number of 654360564 (0x2700bff4) encoded on 3 octets as 0x00bff4,
    // so decoding that field against the packet before it recovers the same full number
    QuicPacket.pn_decode_args.largest_pn = 0x2700bff3ULL;
    QuicPacket.pn_decode_args.truncated_pn = 0x00bff4ULL;
    QuicPacket.pn_decode_args.pn_nbits = 24;
    QuicPacket.pn_decode(quic_packet_work);
    TEST_ASSERT_EQUAL_HEX64(0x2700bff4ULL, QuicPacket.u64);

    // A.3's window is (expected - pn_hwin, expected + pn_hwin]. With an 8-bit field and a largest of
    // 0x27f, expected is 0x280 and the window is (0x200, 0x300]: a truncated 0x00 names 0x300, since
    // the nearer candidate 0x200 sits on the excluded edge and gets one window added.
    QuicPacket.pn_decode_args.largest_pn = 0x27fULL;
    QuicPacket.pn_decode_args.truncated_pn = 0x00ULL;
    QuicPacket.pn_decode_args.pn_nbits = 8;
    QuicPacket.pn_decode(quic_packet_work);
    TEST_ASSERT_EQUAL_HEX64(0x300ULL, QuicPacket.u64);
    // and the other direction, with a largest of 0x300: expected 0x301, window (0x281, 0x381], so a
    // truncated 0xff names the late 0x2ff rather than the candidate 0x3ff above the window
    QuicPacket.pn_decode_args.largest_pn = 0x300ULL;
    QuicPacket.pn_decode_args.truncated_pn = 0xffULL;
    QuicPacket.pn_decode_args.pn_nbits = 8;
    QuicPacket.pn_decode(quic_packet_work);
    TEST_ASSERT_EQUAL_HEX64(0x2ffULL, QuicPacket.u64);
}

// RFC 9000 sec 17.2: a connection ID is at most 20 octets in this version, so a longer one has no
// valid encoding and a declared length past the end of the datagram is truncation.
void test_connection_id_bounds(void)
{
    QuicLongHeader h;
    uint8_t big[32];
    memset(big, 0xAA, sizeof(big));
    TEST_ASSERT_EQUAL_UINT(20u, (unsigned)QUIC_MAX_CID_LEN);
    QuicPacket.build_long_header_args.out = g_out;
    QuicPacket.build_long_header_args.cap = sizeof(g_out);
    QuicPacket.build_long_header_args.type = QUIC_LP_INITIAL;
    QuicPacket.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacket.build_long_header_args.dcid = big;
    QuicPacket.build_long_header_args.dcid_len = 21;
    QuicPacket.build_long_header_args.scid = NONE;
    QuicPacket.build_long_header_args.scid_len = 0;
    QuicPacket.build_long_header_args.pn_len = 1;
    QuicPacket.build_long_header(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicPacket.n);
    QuicPacket.build_long_header_args.out = g_out;
    QuicPacket.build_long_header_args.cap = sizeof(g_out);
    QuicPacket.build_long_header_args.type = QUIC_LP_INITIAL;
    QuicPacket.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacket.build_long_header_args.dcid = NONE;
    QuicPacket.build_long_header_args.dcid_len = 0;
    QuicPacket.build_long_header_args.scid = big;
    QuicPacket.build_long_header_args.scid_len = 21;
    QuicPacket.build_long_header_args.pn_len = 1;
    QuicPacket.build_long_header(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicPacket.n);
    // pn_len is 1..4 (sec 17.2: the field holds length - 1 in two bits)
    QuicPacket.build_long_header_args.out = g_out;
    QuicPacket.build_long_header_args.cap = sizeof(g_out);
    QuicPacket.build_long_header_args.type = QUIC_LP_INITIAL;
    QuicPacket.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacket.build_long_header_args.dcid = NONE;
    QuicPacket.build_long_header_args.dcid_len = 0;
    QuicPacket.build_long_header_args.scid = NONE;
    QuicPacket.build_long_header_args.scid_len = 0;
    QuicPacket.build_long_header_args.pn_len = 0;
    QuicPacket.build_long_header(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicPacket.n);
    QuicPacket.build_long_header_args.out = g_out;
    QuicPacket.build_long_header_args.cap = sizeof(g_out);
    QuicPacket.build_long_header_args.type = QUIC_LP_INITIAL;
    QuicPacket.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacket.build_long_header_args.dcid = NONE;
    QuicPacket.build_long_header_args.dcid_len = 0;
    QuicPacket.build_long_header_args.scid = NONE;
    QuicPacket.build_long_header_args.scid_len = 0;
    QuicPacket.build_long_header_args.pn_len = 5;
    QuicPacket.build_long_header(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicPacket.n);
    QuicPacket.build_long_header_args.out = g_out;
    QuicPacket.build_long_header_args.cap = 6;
    QuicPacket.build_long_header_args.type = QUIC_LP_INITIAL;
    QuicPacket.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacket.build_long_header_args.dcid = NONE;
    QuicPacket.build_long_header_args.dcid_len = 0;
    QuicPacket.build_long_header_args.scid = NONE;
    QuicPacket.build_long_header_args.scid_len = 0;
    QuicPacket.build_long_header_args.pn_len = 1;
    QuicPacket.build_long_header(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicPacket.n);

    static const uint8_t OVERLONG_DCID[7] = {0xC0, 0x00, 0x00, 0x00, 0x01, 21, 0x00};
    QuicPacket.parse_long_header_args.buf = OVERLONG_DCID;
    QuicPacket.parse_long_header_args.len = sizeof(OVERLONG_DCID);
    QuicPacket.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_FALSE(QuicPacket.ok);

    QuicPacket.build_version_negotiation_args.out = g_out;
    QuicPacket.build_version_negotiation_args.cap = sizeof(g_out);
    QuicPacket.build_version_negotiation_args.dcid = big;
    QuicPacket.build_version_negotiation_args.dcid_len = 21;
    QuicPacket.build_version_negotiation_args.scid = NONE;
    QuicPacket.build_version_negotiation_args.scid_len = 0;
    QuicPacket.build_version_negotiation_args.versions = NULL;
    QuicPacket.build_version_negotiation_args.nversions = 0;
    QuicPacket.build_version_negotiation(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicPacket.n);
    QuicPacket.build_version_negotiation_args.out = g_out;
    QuicPacket.build_version_negotiation_args.cap = 6;
    QuicPacket.build_version_negotiation_args.dcid = NONE;
    QuicPacket.build_version_negotiation_args.dcid_len = 0;
    QuicPacket.build_version_negotiation_args.scid = NONE;
    QuicPacket.build_version_negotiation_args.scid_len = 0;
    QuicPacket.build_version_negotiation_args.versions = NULL;
    QuicPacket.build_version_negotiation_args.nversions = 0;
    QuicPacket.build_version_negotiation(quic_packet_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicPacket.n);
}

// A header cut short of a field it declares is refused rather than read past the datagram.
void test_truncated_headers_are_refused(void)
{
    QuicLongHeader h;
    QuicShortHeader s;
    QuicPacket.parse_long_header_args.buf = A2_CLIENT_INITIAL;
    QuicPacket.parse_long_header_args.len = 6;
    QuicPacket.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_FALSE(QuicPacket.ok); // shorter than the fixed part
    QuicPacket.parse_long_header_args.buf = A2_CLIENT_INITIAL;
    QuicPacket.parse_long_header_args.len = 10;
    QuicPacket.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_FALSE(QuicPacket.ok); // DCID runs past the end
    QuicPacket.parse_long_header_args.buf = A2_CLIENT_INITIAL;
    QuicPacket.parse_long_header_args.len = 14;
    QuicPacket.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_FALSE(QuicPacket.ok); // no room for the SCID length

    static const uint8_t SHORT_ONE[3] = {0x42, 0x11, 0x22};
    QuicPacket.parse_short_header_args.buf = SHORT_ONE;
    QuicPacket.parse_short_header_args.len = sizeof(SHORT_ONE);
    QuicPacket.parse_short_header_args.dcid_len = 4;
    QuicPacket.parse_short_header_args.out = &s;
    QuicPacket.parse_short_header(quic_packet_work);
    TEST_ASSERT_FALSE(QuicPacket.ok);
    QuicPacket.parse_short_header_args.buf = SHORT_ONE;
    QuicPacket.parse_short_header_args.len = sizeof(SHORT_ONE);
    QuicPacket.parse_short_header_args.dcid_len = 21;
    QuicPacket.parse_short_header_args.out = &s;
    QuicPacket.parse_short_header(quic_packet_work);
    TEST_ASSERT_FALSE(QuicPacket.ok);
}
