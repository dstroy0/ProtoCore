// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the RTCM 3 framing and station-reference codec
// (services/timing_position/gnss/rtcm3.h).
//
// RTCM 10403 is a paid document, but the two things this file must get right are both checkable
// without it. The transport frame is 0xD3, six reserved bits, a 10-bit payload length, the payload,
// and a CRC-24Q whose parameters (polynomial 0x1864CFB, initial value 0, no reflection, no final
// XOR) make the CRC of a frame with its own CRC appended identically zero - test_crc24q_residue_is_zero
// is that load-bearing case, and it holds for every payload only if all four parameters are right.
// The 1005/1006 field layout is the published DF numbering (DF002 12, DF003 12, DF021 6, DF022-024
// and DF141 1 each, DF025 38, DF142 1, DF001 1, DF026 38, DF364 2, DF027 38 = 152 bits; 1006 adds
// DF028 16 for 168), which test_message_1005_field_offsets reads back bit offset by bit offset.

#include "services/timing_position/gnss/rtcm3.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The load-bearing case: with initial value 0 and no final XOR, a CRC-24Q run over a message
// followed by its own CRC is zero. That is the identity a receiver checks, and it fails if the
// polynomial, the initial value, the bit order or the final XOR is wrong.
void test_crc24q_residue_is_zero(void)
{
    static const uint16_t LENGTHS[6] = {1, 2, 3, 19, 21, 200};
    for (unsigned i = 0; i < 6; i++)
    {
        uint8_t payload[256];
        for (uint16_t k = 0; k < LENGTHS[i]; k++)
        {
            payload[k] = (uint8_t)(k * 7u + 1u);
        }
        uint8_t frame[RTCM3_MAX_FRAME];
        size_t n = protocore_rtcm3_frame_build(frame, sizeof(frame), payload, LENGTHS[i]);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)(RTCM3_HDR_LEN + LENGTHS[i] + RTCM3_CRC_LEN), (uint32_t)n);
        TEST_ASSERT_EQUAL_HEX32(0u, protocore_rtcm3_crc24q(frame, n));
    }
}

// CRC-24Q with a zero initial value and no final XOR is a linear map over GF(2): the CRC of two
// buffers XORed together is the XOR of their CRCs, and the CRC of all zeros is zero.
void test_crc24q_is_linear_with_a_zero_seed(void)
{
    static const uint8_t ZEROS[16] = {0};
    TEST_ASSERT_EQUAL_HEX32(0u, protocore_rtcm3_crc24q(ZEROS, 0));
    TEST_ASSERT_EQUAL_HEX32(0u, protocore_rtcm3_crc24q(ZEROS, 1));
    TEST_ASSERT_EQUAL_HEX32(0u, protocore_rtcm3_crc24q(ZEROS, 16));

    uint8_t a[16];
    uint8_t b[16];
    uint8_t x[16];
    for (unsigned i = 0; i < 16; i++)
    {
        a[i] = (uint8_t)(i * 13u + 5u);
        b[i] = (uint8_t)(i * 29u + 91u);
        x[i] = (uint8_t)(a[i] ^ b[i]);
    }
    uint32_t ca = protocore_rtcm3_crc24q(a, 16);
    uint32_t cb = protocore_rtcm3_crc24q(b, 16);
    TEST_ASSERT_EQUAL_HEX32(ca ^ cb, protocore_rtcm3_crc24q(x, 16));
    // The result never spills past 24 bits.
    TEST_ASSERT_EQUAL_HEX32(0u, ca & 0xFF000000u);
}

// The transport frame: 0xD3, six reserved bits set to zero, a 10-bit big-endian payload length, the
// payload, then three CRC octets.
void test_frame_header_layout(void)
{
    static const uint16_t LENGTHS[5] = {0, 1, 19, 255, 1023};
    for (unsigned i = 0; i < 5; i++)
    {
        uint8_t payload[RTCM3_MAX_PAYLOAD];
        memset(payload, 0x5A, LENGTHS[i]);
        uint8_t frame[RTCM3_MAX_FRAME];
        size_t n = protocore_rtcm3_frame_build(frame, sizeof(frame), LENGTHS[i] ? payload : NULL, LENGTHS[i]);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)(RTCM3_HDR_LEN + LENGTHS[i] + RTCM3_CRC_LEN), (uint32_t)n);

        TEST_ASSERT_EQUAL_HEX8(RTCM3_PREAMBLE, frame[0]);
        TEST_ASSERT_EQUAL_HEX8(0x00, frame[1] & 0xFC); // the six reserved bits
        uint16_t declared = (uint16_t)(((frame[1] & 0x03u) << 8) | frame[2]);
        TEST_ASSERT_EQUAL_UINT16(LENGTHS[i], declared);
        TEST_ASSERT_EQUAL_HEX32(0u, protocore_rtcm3_crc24q(frame, n));
    }

    // 1023 is the widest a 10-bit length can name; one more does not fit the field.
    uint8_t big[RTCM3_MAX_PAYLOAD + 1];
    uint8_t out[RTCM3_MAX_FRAME + 8];
    memset(big, 0, sizeof(big));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_rtcm3_frame_build(out, sizeof(out), big, RTCM3_MAX_PAYLOAD + 1));
    // A buffer one octet short of the frame writes nothing.
    TEST_ASSERT_EQUAL_UINT32(
        0, (uint32_t)protocore_rtcm3_frame_build(out, RTCM3_HDR_LEN + 19 + RTCM3_CRC_LEN - 1, big, 19));
}

// Parsing reports the frame length, the message number from the first twelve payload bits, and
// whether the trailing CRC matched.
void test_frame_parse_round_trip(void)
{
    uint8_t payload[19];
    memset(payload, 0, sizeof(payload));
    payload[0] = 0x3E; // 1005 = 0x3ED: the top twelve bits are 0011 1110 1101
    payload[1] = 0xD0;

    uint8_t frame[RTCM3_MAX_FRAME];
    size_t n = protocore_rtcm3_frame_build(frame, sizeof(frame), payload, sizeof(payload));
    Rtcm3Frame f;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, (uint32_t)protocore_rtcm3_frame_parse(frame, n, &f));
    TEST_ASSERT_TRUE(f.crc_ok);
    TEST_ASSERT_EQUAL_UINT16(1005, f.msg_type);
    TEST_ASSERT_EQUAL_UINT16(sizeof(payload), f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, sizeof(payload));
    TEST_ASSERT_EQUAL_PTR(frame + RTCM3_HDR_LEN, f.payload);

    // Extra bytes after the frame belong to the next one and are not consumed.
    uint8_t stream[RTCM3_MAX_FRAME + 4];
    memcpy(stream, frame, n);
    stream[n] = 0xD3;
    stream[n + 1] = 0x00;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, (uint32_t)protocore_rtcm3_frame_parse(stream, n + 2, &f));
}

// A frame that is not fully buffered yet is reported as "need more", not as an error.
void test_frame_parse_waits_for_the_whole_frame(void)
{
    uint8_t payload[19] = {0};
    uint8_t frame[RTCM3_MAX_FRAME];
    size_t n = protocore_rtcm3_frame_build(frame, sizeof(frame), payload, sizeof(payload));
    Rtcm3Frame f;
    for (size_t have = 0; have < n; have++)
    {
        TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_rtcm3_frame_parse(frame, have, &f));
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, (uint32_t)protocore_rtcm3_frame_parse(frame, n, &f));
}

// A corrupted frame still frames - the length field is intact - but its CRC no longer matches, which
// is exactly what crc_ok reports.
void test_frame_parse_reports_a_bad_crc(void)
{
    uint8_t payload[19];
    for (unsigned i = 0; i < sizeof(payload); i++)
    {
        payload[i] = (uint8_t)i;
    }
    uint8_t good[RTCM3_MAX_FRAME];
    size_t n = protocore_rtcm3_frame_build(good, sizeof(good), payload, sizeof(payload));

    for (size_t byte = RTCM3_HDR_LEN; byte < n; byte++)
    {
        uint8_t frame[RTCM3_MAX_FRAME];
        memcpy(frame, good, n);
        frame[byte] ^= 0x01;
        Rtcm3Frame f;
        TEST_ASSERT_EQUAL_UINT32((uint32_t)n, (uint32_t)protocore_rtcm3_frame_parse(frame, n, &f));
        TEST_ASSERT_FALSE(f.crc_ok);
    }
}

// Sync scans forward to the next preamble so a stream that starts mid-frame can be realigned.
void test_sync_finds_the_next_preamble(void)
{
    static const uint8_t NOISE[6] = {0x00, 0xFF, 0x12, 0xD3, 0x34, 0xD3};
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)protocore_rtcm3_sync(NOISE, sizeof(NOISE)));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_rtcm3_sync(NOISE + 3, 3));
    static const uint8_t NONE[4] = {0x00, 0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)protocore_rtcm3_sync(NONE, sizeof(NONE)));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_rtcm3_sync(NONE, 0));
}

// RTCM3 packs fields back to back, most significant bit first, with no byte alignment. Writing the
// message number 1005 (0x3ED = 0011 1110 1101) as twelve bits puts 0011 1110 in the first octet and
// 1101 in the top half of the second.
void test_bit_writer_is_msb_first(void)
{
    uint8_t buf[8];
    memset(buf, 0, sizeof(buf));
    RtcmBitWriter w;
    protocore_rtcm_bw_init(&w, buf, sizeof(buf));
    protocore_rtcm_bw_u(&w, 1005u, 12);
    TEST_ASSERT_EQUAL_HEX8(0x3E, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xD0, buf[1]);
    TEST_ASSERT_TRUE(w.ok);
    TEST_ASSERT_EQUAL_UINT32(12, (uint32_t)w.pos);

    // The next field continues in the same octet: four more bits of 0b1010 fill the low nibble.
    protocore_rtcm_bw_u(&w, 0x0Au, 4);
    TEST_ASSERT_EQUAL_HEX8(0xDA, buf[1]);
    TEST_ASSERT_EQUAL_UINT32(16, (uint32_t)w.pos);

    size_t pos = 0;
    TEST_ASSERT_EQUAL_UINT64(1005u, protocore_rtcm_br_u(buf, &pos, 12));
    TEST_ASSERT_EQUAL_UINT32(12, (uint32_t)pos);
    TEST_ASSERT_EQUAL_UINT64(0x0Au, protocore_rtcm_br_u(buf, &pos, 4));

    // A single bit, and the widest field the cursor takes.
    memset(buf, 0, sizeof(buf));
    protocore_rtcm_bw_init(&w, buf, sizeof(buf));
    protocore_rtcm_bw_u(&w, 1u, 1);
    TEST_ASSERT_EQUAL_HEX8(0x80, buf[0]);
    memset(buf, 0, sizeof(buf));
    protocore_rtcm_bw_init(&w, buf, sizeof(buf));
    protocore_rtcm_bw_u(&w, 0xFFFFFFFFFFFFFFFFull, 64);
    for (unsigned i = 0; i < 8; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xFF, buf[i]);
    }
    pos = 0;
    TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFFFFFFull, protocore_rtcm_br_u(buf, &pos, 64));

    // A write past the buffer clears ok rather than running off the end.
    uint8_t tiny[1];
    memset(tiny, 0, sizeof(tiny));
    protocore_rtcm_bw_init(&w, tiny, sizeof(tiny));
    protocore_rtcm_bw_u(&w, 0u, 8);
    TEST_ASSERT_TRUE(w.ok);
    protocore_rtcm_bw_u(&w, 1u, 1);
    TEST_ASSERT_FALSE(w.ok);
}

// Signed fields are two's complement at the field's own width, so -1 is all ones however wide it is
// and the sign bit is the field's most significant bit.
void test_bit_cursor_signed_fields(void)
{
    static const int64_t VALUES[7] = {0, 1, -1, 137438953471ll, -137438953472ll, 8388607ll, -8388608ll};
    static const uint8_t WIDTHS[7] = {38, 38, 38, 38, 38, 24, 24};
    for (unsigned i = 0; i < 7; i++)
    {
        uint8_t buf[16];
        memset(buf, 0, sizeof(buf));
        RtcmBitWriter w;
        protocore_rtcm_bw_init(&w, buf, sizeof(buf));
        protocore_rtcm_bw_s(&w, VALUES[i], WIDTHS[i]);
        TEST_ASSERT_TRUE(w.ok);
        size_t pos = 0;
        TEST_ASSERT_EQUAL_INT64(VALUES[i], protocore_rtcm_br_s(buf, &pos, WIDTHS[i]));
        TEST_ASSERT_EQUAL_UINT32(WIDTHS[i], (uint32_t)pos);
    }

    // -1 in a 38-bit field is 38 set bits: the first four octets are 0xFF and the next six bits are
    // set too, leaving the low two bits of octet 4 clear.
    uint8_t buf[8];
    memset(buf, 0, sizeof(buf));
    RtcmBitWriter w;
    protocore_rtcm_bw_init(&w, buf, sizeof(buf));
    protocore_rtcm_bw_s(&w, -1, 38);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, buf[4]);
}

// Message 1005's published field layout, read back off the built payload at the bit offset each DF
// starts at: DF002 at 0, DF003 at 12, DF021 at 24, the four indicator bits at 30..33, DF025 at 34,
// DF142 at 72, DF001 at 73, DF026 at 74, DF364 at 112, DF027 at 114 - 152 bits, 19 octets.
void test_message_1005_field_offsets(void)
{
    const uint16_t station = 0x0ABC;
    const int64_t x = 123456789ll;
    const int64_t y = -987654321ll;
    const int64_t z = 42ll;

    uint8_t frame[RTCM3_MAX_FRAME];
    size_t n = protocore_rtcm3_build_1005(frame, sizeof(frame), station, x, y, z);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(RTCM3_HDR_LEN + 19 + RTCM3_CRC_LEN), (uint32_t)n);

    Rtcm3Frame f;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, (uint32_t)protocore_rtcm3_frame_parse(frame, n, &f));
    TEST_ASSERT_TRUE(f.crc_ok);
    TEST_ASSERT_EQUAL_UINT16(1005, f.msg_type);
    TEST_ASSERT_EQUAL_UINT16(19, f.payload_len); // 152 bits

    size_t pos = 0;
    TEST_ASSERT_EQUAL_UINT64(1005u, protocore_rtcm_br_u(f.payload, &pos, 12));   // DF002
    TEST_ASSERT_EQUAL_UINT64(station, protocore_rtcm_br_u(f.payload, &pos, 12)); // DF003
    pos = 34;
    TEST_ASSERT_EQUAL_INT64(x, protocore_rtcm_br_s(f.payload, &pos, 38)); // DF025
    TEST_ASSERT_EQUAL_UINT32(72, (uint32_t)pos);
    pos = 74;
    TEST_ASSERT_EQUAL_INT64(y, protocore_rtcm_br_s(f.payload, &pos, 38)); // DF026
    TEST_ASSERT_EQUAL_UINT32(112, (uint32_t)pos);
    pos = 114;
    TEST_ASSERT_EQUAL_INT64(z, protocore_rtcm_br_s(f.payload, &pos, 38)); // DF027
    TEST_ASSERT_EQUAL_UINT32(152, (uint32_t)pos);

    Rtcm3StationArp arp;
    TEST_ASSERT_TRUE(protocore_rtcm3_parse_1005(f.payload, f.payload_len, &arp));
    TEST_ASSERT_EQUAL_UINT16(station, arp.station_id);
    TEST_ASSERT_EQUAL_INT64(x, arp.ecef_x_01mm);
    TEST_ASSERT_EQUAL_INT64(y, arp.ecef_y_01mm);
    TEST_ASSERT_EQUAL_INT64(z, arp.ecef_z_01mm);
    TEST_ASSERT_FALSE(arp.has_height);
    TEST_ASSERT_EQUAL_UINT16(0, arp.antenna_height_01mm);
}

// Message 1006 is 1005 plus DF028, a 16-bit antenna height at bit 152, so its payload is 168 bits.
void test_message_1006_adds_the_antenna_height(void)
{
    const uint16_t height = 1500; // 0.15 m in 0.1 mm units
    uint8_t frame[RTCM3_MAX_FRAME];
    size_t n = protocore_rtcm3_build_1006(frame, sizeof(frame), 7, 1ll, -1ll, 0ll, height);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(RTCM3_HDR_LEN + 21 + RTCM3_CRC_LEN), (uint32_t)n);

    Rtcm3Frame f;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, (uint32_t)protocore_rtcm3_frame_parse(frame, n, &f));
    TEST_ASSERT_TRUE(f.crc_ok);
    TEST_ASSERT_EQUAL_UINT16(1006, f.msg_type);
    TEST_ASSERT_EQUAL_UINT16(21, f.payload_len); // 168 bits

    size_t pos = 152;
    TEST_ASSERT_EQUAL_UINT64(height, protocore_rtcm_br_u(f.payload, &pos, 16)); // DF028
    TEST_ASSERT_EQUAL_UINT32(168, (uint32_t)pos);

    Rtcm3StationArp arp;
    TEST_ASSERT_TRUE(protocore_rtcm3_parse_1005(f.payload, f.payload_len, &arp));
    TEST_ASSERT_EQUAL_UINT16(7, arp.station_id);
    TEST_ASSERT_EQUAL_INT64(1ll, arp.ecef_x_01mm);
    TEST_ASSERT_EQUAL_INT64(-1ll, arp.ecef_y_01mm);
    TEST_ASSERT_EQUAL_INT64(0ll, arp.ecef_z_01mm);
    TEST_ASSERT_TRUE(arp.has_height);
    TEST_ASSERT_EQUAL_UINT16(height, arp.antenna_height_01mm);
}

// The ECEF fields are 38-bit signed, so they carry -2^37 through 2^37-1 (about +/- 13743.9 km at
// 0.1 mm resolution) and every one of those values survives the round trip.
void test_ecef_coordinates_span_the_38_bit_range(void)
{
    static const int64_t CASES[6] = {0ll, 1ll, -1ll, 137438953471ll, -137438953472ll, -63711234567ll};
    for (unsigned i = 0; i < 6; i++)
    {
        uint8_t frame[RTCM3_MAX_FRAME];
        size_t n = protocore_rtcm3_build_1005(frame, sizeof(frame), 4095, CASES[i], -CASES[i] - 1, CASES[i]);
        TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);
        Rtcm3Frame f;
        TEST_ASSERT_EQUAL_UINT32((uint32_t)n, (uint32_t)protocore_rtcm3_frame_parse(frame, n, &f));
        TEST_ASSERT_TRUE(f.crc_ok);
        Rtcm3StationArp arp;
        TEST_ASSERT_TRUE(protocore_rtcm3_parse_1005(f.payload, f.payload_len, &arp));
        TEST_ASSERT_EQUAL_UINT16(4095, arp.station_id); // the widest a 12-bit DF003 names
        TEST_ASSERT_EQUAL_INT64(CASES[i], arp.ecef_x_01mm);
        TEST_ASSERT_EQUAL_INT64(-CASES[i] - 1, arp.ecef_y_01mm);
        TEST_ASSERT_EQUAL_INT64(CASES[i], arp.ecef_z_01mm);
    }
}

// A payload that is neither 19 nor 21 octets, or whose message number is not 1005 or 1006, is not a
// station reference point.
void test_parse_1005_rejects_what_it_is_not(void)
{
    uint8_t frame[RTCM3_MAX_FRAME];
    size_t n = protocore_rtcm3_build_1005(frame, sizeof(frame), 1, 0, 0, 0);
    Rtcm3Frame f;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, (uint32_t)protocore_rtcm3_frame_parse(frame, n, &f));

    uint8_t payload[24];
    memcpy(payload, f.payload, f.payload_len);
    Rtcm3StationArp arp;
    TEST_ASSERT_TRUE(protocore_rtcm3_parse_1005(payload, 19, &arp));
    TEST_ASSERT_FALSE(protocore_rtcm3_parse_1005(payload, 18, &arp)); // one octet short of 1005
    TEST_ASSERT_FALSE(protocore_rtcm3_parse_1005(payload, 0, &arp));

    // Rewrite DF002 to 1004 and it is no longer a 1005: 1004 = 0x3EC.
    payload[0] = 0x3E;
    payload[1] = (uint8_t)((payload[1] & 0x0F) | 0xC0);
    TEST_ASSERT_FALSE(protocore_rtcm3_parse_1005(payload, 19, &arp));
}

// Build refuses rather than writing past the caller's buffer.
void test_build_capacity_is_respected(void)
{
    uint8_t out[RTCM3_MAX_FRAME];
    const size_t want_1005 = RTCM3_HDR_LEN + 19 + RTCM3_CRC_LEN;
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_rtcm3_build_1005(out, want_1005 - 1, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)want_1005, (uint32_t)protocore_rtcm3_build_1005(out, want_1005, 1, 0, 0, 0));

    const size_t want_1006 = RTCM3_HDR_LEN + 21 + RTCM3_CRC_LEN;
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_rtcm3_build_1006(out, want_1006 - 1, 1, 0, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)want_1006, (uint32_t)protocore_rtcm3_build_1006(out, want_1006, 1, 0, 0, 0, 0));
}
