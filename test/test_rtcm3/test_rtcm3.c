// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the RTCM 3.x pure codec (services/timing_position/gnss/rtcm3): CRC-24Q, MSB-first bit I/O, the
// transport frame (preamble sync / length / CRC), and the 1005/1006 Stationary Antenna Reference Point encode + decode.
// The 1005/1006 reference frames below were produced by an independent packer and confirmed byte-for-byte by
// pyrtcm 1.2.0 (fields decode to the given DF values; RTCMReader validates the CRC).

#include "services/timing_position/gnss/rtcm3.h"
#include <string.h>

#include <unity.h>

// Base antenna reference point (ECEF, 0.1 mm units) used for both reference frames.
static const uint16_t SID = 2003;
static const int64_t EX = 11149789412LL;
static const int64_t EY = -48500020000LL;
static const int64_t EZ = 39752890000LL;
static const uint16_t AH = 12500; // antenna height (0.1 mm) for 1006

// pyrtcm-confirmed 1005 frame (25 bytes) and 1006 frame (27 bytes).
static const uint8_t FRAME_1005[] = {0xD3, 0x00, 0x13, 0x3E, 0xD7, 0xD3, 0x02, 0x02, 0x98, 0x94, 0x48, 0xE4, 0x34,
                                     0xB5, 0x2C, 0x6C, 0xE0, 0x09, 0x41, 0x74, 0xF6, 0x90, 0x1E, 0xA5, 0x47};
static const uint8_t FRAME_1006[] = {0xD3, 0x00, 0x15, 0x3E, 0xE7, 0xD3, 0x02, 0x02, 0x98, 0x94, 0x48, 0xE4, 0x34, 0xB5,
                                     0x2C, 0x6C, 0xE0, 0x09, 0x41, 0x74, 0xF6, 0x90, 0x30, 0xD4, 0xC5, 0xBF, 0x97};

void setUp()
{
}
void tearDown()
{
}

void test_build_1005_matches_pyrtcm()
{
    uint8_t out[RTCM3_MAX_FRAME];
    size_t n = pc_rtcm3_build_1005(out, sizeof(out), SID, EX, EY, EZ);
    TEST_ASSERT_EQUAL_size_t(sizeof(FRAME_1005), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FRAME_1005, out, n);
}

void test_build_1006_matches_pyrtcm()
{
    uint8_t out[RTCM3_MAX_FRAME];
    size_t n = pc_rtcm3_build_1006(out, sizeof(out), SID, EX, EY, EZ, AH);
    TEST_ASSERT_EQUAL_size_t(sizeof(FRAME_1006), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FRAME_1006, out, n);
}

void test_parse_frame_and_1005()
{
    Rtcm3Frame f;
    memset(&f, 0, sizeof(f));
    size_t n = pc_rtcm3_frame_parse(FRAME_1005, sizeof(FRAME_1005), &f);
    TEST_ASSERT_EQUAL_size_t(sizeof(FRAME_1005), n);
    TEST_ASSERT_EQUAL_UINT16(1005, f.msg_type);
    TEST_ASSERT_EQUAL_UINT16(19, f.payload_len);
    TEST_ASSERT_TRUE(f.crc_ok);

    Rtcm3StationArp arp;
    memset(&arp, 0, sizeof(arp));
    TEST_ASSERT_TRUE(pc_rtcm3_parse_1005(f.payload, f.payload_len, &arp));
    TEST_ASSERT_EQUAL_UINT16(SID, arp.station_id);
    TEST_ASSERT_EQUAL_INT64(EX, arp.ecef_x_01mm);
    TEST_ASSERT_EQUAL_INT64(EY, arp.ecef_y_01mm);
    TEST_ASSERT_EQUAL_INT64(EZ, arp.ecef_z_01mm);
    TEST_ASSERT_FALSE(arp.has_height);
}

void test_parse_frame_and_1006()
{
    Rtcm3Frame f;
    memset(&f, 0, sizeof(f));
    size_t n = pc_rtcm3_frame_parse(FRAME_1006, sizeof(FRAME_1006), &f);
    TEST_ASSERT_EQUAL_size_t(sizeof(FRAME_1006), n);
    TEST_ASSERT_EQUAL_UINT16(1006, f.msg_type);
    TEST_ASSERT_EQUAL_UINT16(21, f.payload_len);
    TEST_ASSERT_TRUE(f.crc_ok);

    Rtcm3StationArp arp;
    memset(&arp, 0, sizeof(arp));
    TEST_ASSERT_TRUE(pc_rtcm3_parse_1005(f.payload, f.payload_len, &arp));
    TEST_ASSERT_EQUAL_INT64(EZ, arp.ecef_z_01mm);
    TEST_ASSERT_TRUE(arp.has_height);
    TEST_ASSERT_EQUAL_UINT16(AH, arp.antenna_height_01mm);
}

void test_crc24q_matches_frame()
{
    // The 3 trailing CRC bytes are CRC-24Q over the preamble + header + payload (all but the last 3 bytes).
    size_t body = sizeof(FRAME_1005) - RTCM3_CRC_LEN;
    uint32_t crc = pc_rtcm3_crc24q(FRAME_1005, body);
    uint32_t appended =
        ((uint32_t)FRAME_1005[body] << 16) | ((uint32_t)FRAME_1005[body + 1] << 8) | FRAME_1005[body + 2];
    TEST_ASSERT_EQUAL_HEX32(appended, crc);
}

void test_crc_detects_corruption()
{
    uint8_t bad[sizeof(FRAME_1005)];
    memcpy(bad, FRAME_1005, sizeof(bad));
    bad[8] ^= 0x01; // flip a payload bit
    Rtcm3Frame f;
    memset(&f, 0, sizeof(f));
    size_t n = pc_rtcm3_frame_parse(bad, sizeof(bad), &f);
    TEST_ASSERT_EQUAL_size_t(sizeof(bad), n); // still a complete frame...
    TEST_ASSERT_FALSE(f.crc_ok);              // ...but the CRC no longer matches
}

void test_partial_frame_needs_more()
{
    Rtcm3Frame f;
    TEST_ASSERT_EQUAL_size_t(0, pc_rtcm3_frame_parse(FRAME_1005, 2, &f));                      // < header
    TEST_ASSERT_EQUAL_size_t(0, pc_rtcm3_frame_parse(FRAME_1005, sizeof(FRAME_1005) - 1, &f)); // one byte short
}

void test_sync_finds_preamble()
{
    const uint8_t stream[] = {0x11, 0x22, 0x33, 0xD3, 0x00, 0x13};
    TEST_ASSERT_EQUAL_size_t(3, pc_rtcm3_sync(stream, sizeof(stream)));
    const uint8_t none[] = {0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_size_t(sizeof(none), pc_rtcm3_sync(none, sizeof(none)));
}

void test_bit_io_roundtrip()
{
    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    RtcmBitWriter w;
    pc_rtcm_bw_init(&w, buf, sizeof(buf));
    pc_rtcm_bw_u(&w, 1005, 12);
    pc_rtcm_bw_u(&w, 0x2A, 6);
    pc_rtcm_bw_s(&w, -1, 12);             // all-ones two's complement
    pc_rtcm_bw_s(&w, -48500020000LL, 38); // a 38-bit signed like an ECEF coordinate
    TEST_ASSERT_TRUE(w.ok);

    size_t p = 0;
    TEST_ASSERT_EQUAL_UINT64(1005, pc_rtcm_br_u(buf, &p, 12));
    TEST_ASSERT_EQUAL_UINT64(0x2A, pc_rtcm_br_u(buf, &p, 6));
    TEST_ASSERT_EQUAL_INT64(-1, pc_rtcm_br_s(buf, &p, 12));
    TEST_ASSERT_EQUAL_INT64(-48500020000LL, pc_rtcm_br_s(buf, &p, 38));
}

void test_writer_overflow_fails_closed()
{
    uint8_t small[2]; // 16 bits
    memset(small, 0, sizeof(small));
    RtcmBitWriter w;
    pc_rtcm_bw_init(&w, small, sizeof(small));
    pc_rtcm_bw_u(&w, 0, 12);
    pc_rtcm_bw_u(&w, 0, 12); // would need 24 bits > 16 -> overflow
    TEST_ASSERT_FALSE(w.ok);
}

void test_frame_build_roundtrip()
{
    const uint8_t payload[] = {0x3E, 0xD0, 0x00}; // arbitrary 3-byte body (msg number in first 12 bits)
    uint8_t frame[RTCM3_MAX_FRAME];
    size_t n = pc_rtcm3_frame_build(frame, sizeof(frame), payload, sizeof(payload));
    TEST_ASSERT_EQUAL_size_t(RTCM3_HDR_LEN + sizeof(payload) + RTCM3_CRC_LEN, n);

    Rtcm3Frame f;
    memset(&f, 0, sizeof(f));
    TEST_ASSERT_EQUAL_size_t(n, pc_rtcm3_frame_parse(frame, n, &f));
    TEST_ASSERT_TRUE(f.crc_ok);
    TEST_ASSERT_EQUAL_UINT16(sizeof(payload), f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, sizeof(payload));

    // Build fails closed when the output buffer is too small.
    uint8_t tiny[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_rtcm3_frame_build(tiny, sizeof(tiny), payload, sizeof(payload)));
}

// pc_rtcm_bw_u() rejects a zero-width and an over-64-bit field, and once the writer has failed it
// stays failed: a later well-formed write neither advances the position nor touches the buffer.
void test_writer_rejects_bad_widths_and_is_sticky()
{
    uint8_t buf[8];
    RtcmBitWriter w;

    memset(buf, 0, sizeof(buf));
    pc_rtcm_bw_init(&w, buf, sizeof(buf));
    pc_rtcm_bw_u(&w, 1, 0); // zero-width field
    TEST_ASSERT_FALSE(w.ok);
    TEST_ASSERT_EQUAL_size_t(0, w.pos);

    memset(buf, 0, sizeof(buf));
    pc_rtcm_bw_init(&w, buf, sizeof(buf));
    pc_rtcm_bw_u(&w, 1, 65); // wider than the 64-bit value it is given
    TEST_ASSERT_FALSE(w.ok);

    memset(buf, 0, sizeof(buf));
    pc_rtcm_bw_init(&w, buf, sizeof(buf));
    pc_rtcm_bw_u(&w, 0xFF, 8);
    pc_rtcm_bw_u(&w, 0, 0); // fails the writer here
    TEST_ASSERT_FALSE(w.ok);
    pc_rtcm_bw_u(&w, 0xFF, 8); // ignored: the writer is already failed
    TEST_ASSERT_EQUAL_size_t(8, w.pos);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
}

// A full-width 64-bit signed field takes the no-mask / no-sign-extend path at both ends of the bit
// I/O - the stored bits are already the whole word - so the value round-trips unchanged.
void test_signed_bit_io_full_width()
{
    uint8_t buf[8];
    memset(buf, 0, sizeof(buf));
    RtcmBitWriter w;
    pc_rtcm_bw_init(&w, buf, sizeof(buf));
    pc_rtcm_bw_s(&w, -48500020000LL, 64);
    TEST_ASSERT_TRUE(w.ok);
    size_t p = 0;
    TEST_ASSERT_EQUAL_INT64(-48500020000LL, pc_rtcm_br_s(buf, &p, 64));
    TEST_ASSERT_EQUAL_size_t(64, p);
}

// Frame parsing: a buffer long enough but not aligned to the preamble is rejected, a null output
// makes the call a pure length probe, and a body too short to hold the 12-bit DF002 reports 0.
void test_frame_parse_edges()
{
    const uint8_t notpre[8] = {0x00, 0x00, 0x03, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_size_t(0, pc_rtcm3_frame_parse(notpre, sizeof(notpre), NULL));
    TEST_ASSERT_EQUAL_size_t(sizeof(FRAME_1005), pc_rtcm3_frame_parse(FRAME_1005, sizeof(FRAME_1005), NULL));

    const uint8_t body[1] = {0x3E};
    uint8_t frame[RTCM3_MAX_FRAME];
    size_t fn = pc_rtcm3_frame_build(frame, sizeof(frame), body, sizeof(body));
    Rtcm3Frame f;
    memset(&f, 0, sizeof(f));
    TEST_ASSERT_EQUAL_size_t(fn, pc_rtcm3_frame_parse(frame, fn, &f));
    TEST_ASSERT_TRUE(f.crc_ok);
    TEST_ASSERT_EQUAL_UINT16(0, f.msg_type); // one byte cannot carry a 12-bit message number
}

// Frame building: a body over the 10-bit length field and a null output buffer are both refused;
// an empty body still yields a valid header-plus-CRC frame, and a null body pointer leaves the
// reserved body bytes untouched while the CRC still covers them.
void test_frame_build_edges()
{
    uint8_t frame[RTCM3_MAX_FRAME];
    const uint8_t body[3] = {0x3E, 0xD0, 0x00};
    Rtcm3Frame f;

    TEST_ASSERT_EQUAL_size_t(0, pc_rtcm3_frame_build(frame, sizeof(frame), body, RTCM3_MAX_PAYLOAD + 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_rtcm3_frame_build(NULL, sizeof(frame), body, sizeof(body)));

    size_t n = pc_rtcm3_frame_build(frame, sizeof(frame), body, 0);
    TEST_ASSERT_EQUAL_size_t(RTCM3_HDR_LEN + RTCM3_CRC_LEN, n);
    memset(&f, 0, sizeof(f));
    TEST_ASSERT_EQUAL_size_t(n, pc_rtcm3_frame_parse(frame, n, &f));
    TEST_ASSERT_TRUE(f.crc_ok);
    TEST_ASSERT_EQUAL_UINT16(0, f.payload_len);

    memset(frame, 0, sizeof(frame));
    const size_t n3 = RTCM3_HDR_LEN + 3 + RTCM3_CRC_LEN;
    TEST_ASSERT_EQUAL_size_t(n3, pc_rtcm3_frame_build(frame, sizeof(frame), NULL, 3));
    memset(&f, 0, sizeof(f));
    TEST_ASSERT_EQUAL_size_t(n3, pc_rtcm3_frame_parse(frame, sizeof(frame), &f));
    TEST_ASSERT_TRUE(f.crc_ok);
    TEST_ASSERT_EQUAL_UINT16(3, f.payload_len);
}

// pc_rtcm3_parse_1005() fails closed on a null payload, a null output, a body shorter than the
// 19-byte 1005 minimum, a message number that is neither 1005 nor 1006, and a 1006 whose body is
// too short to hold the antenna-height field.
void test_parse_1005_rejects_bad_input()
{
    Rtcm3StationArp arp;
    memset(&arp, 0, sizeof(arp));
    const uint8_t *body = FRAME_1005 + RTCM3_HDR_LEN;

    TEST_ASSERT_FALSE(pc_rtcm3_parse_1005(NULL, 19, &arp));
    TEST_ASSERT_FALSE(pc_rtcm3_parse_1005(body, 19, NULL));
    TEST_ASSERT_FALSE(pc_rtcm3_parse_1005(body, 18, &arp)); // one byte short of a 1005 body

    // Rewrite DF002 to 1004 (0x3EC), a GPS observable message rather than an ARP one.
    uint8_t other[19];
    memcpy(other, body, sizeof(other));
    other[1] = (uint8_t)((other[1] & 0x0F) | 0xC0);
    TEST_ASSERT_FALSE(pc_rtcm3_parse_1005(other, sizeof(other), &arp));

    // A 1006 header in a 19-byte body: the 16-bit antenna height would run off the end.
    uint8_t short1006[19];
    memcpy(short1006, FRAME_1006 + RTCM3_HDR_LEN, sizeof(short1006));
    TEST_ASSERT_FALSE(pc_rtcm3_parse_1005(short1006, sizeof(short1006), &arp));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_writer_rejects_bad_widths_and_is_sticky);
    RUN_TEST(test_signed_bit_io_full_width);
    RUN_TEST(test_frame_parse_edges);
    RUN_TEST(test_frame_build_edges);
    RUN_TEST(test_parse_1005_rejects_bad_input);
    RUN_TEST(test_build_1005_matches_pyrtcm);
    RUN_TEST(test_build_1006_matches_pyrtcm);
    RUN_TEST(test_parse_frame_and_1005);
    RUN_TEST(test_parse_frame_and_1006);
    RUN_TEST(test_crc24q_matches_frame);
    RUN_TEST(test_crc_detects_corruption);
    RUN_TEST(test_partial_frame_needs_more);
    RUN_TEST(test_sync_finds_preamble);
    RUN_TEST(test_bit_io_roundtrip);
    RUN_TEST(test_writer_overflow_fails_closed);
    RUN_TEST(test_frame_build_roundtrip);
    return UNITY_END();
}
