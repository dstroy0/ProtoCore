// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/wave: the IEEE 1609 WSMP + 1609.2 envelope + PSID codec.

#include "services/transportation/wave/wave.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_psid_p_encoding(void)
{
    uint8_t out[4];
    uint32_t psid = 0;
    // 1-octet: 0x20 -> 20.
    TEST_ASSERT_EQUAL_size_t(1, pc_wave_encode_psid(0x20, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x20, out[0]);
    TEST_ASSERT_EQUAL_size_t(1, pc_wave_decode_psid(out, 1, &psid));
    TEST_ASSERT_EQUAL_UINT32(0x20, psid);

    // 2-octet: 0x8002 -> 80 | (0x8002>>8)=0xC0?? no: 0x8002 >= 0x4000 so it's 3-octet.
    TEST_ASSERT_EQUAL_size_t(3, pc_wave_encode_psid(0x8002, out, sizeof(out)));
    // C0 | (0x8002>>16=0) = C0, then 80 02.
    TEST_ASSERT_EQUAL_HEX8(0xC0, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x80, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, out[2]);
    TEST_ASSERT_EQUAL_size_t(3, pc_wave_decode_psid(out, 3, &psid));
    TEST_ASSERT_EQUAL_UINT32(0x8002, psid);

    // 2-octet range: 0x0100 -> 81 00.
    TEST_ASSERT_EQUAL_size_t(2, pc_wave_encode_psid(0x0100, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x81, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[1]);
    pc_wave_decode_psid(out, 2, &psid);
    TEST_ASSERT_EQUAL_UINT32(0x0100, psid);
}

void test_wsmp_roundtrip(void)
{
    uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t out[16];
    size_t n = pc_wsmp_build(WAVE_PSID_BSM, payload, 4, out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8(WSMP_VERSION, out[0]);
    WsmpFrame f;
    TEST_ASSERT_TRUE(pc_wsmp_parse(out, n, &f));
    TEST_ASSERT_EQUAL_UINT32(WAVE_PSID_BSM, f.psid);
    TEST_ASSERT_EQUAL_size_t(4, f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, 4);

    // A multi-octet PSID (SPaT).
    n = pc_wsmp_build(WAVE_PSID_SPAT, payload, 2, out, sizeof(out));
    TEST_ASSERT_TRUE(pc_wsmp_parse(out, n, &f));
    TEST_ASSERT_EQUAL_UINT32(WAVE_PSID_SPAT, f.psid);
}

void test_1609dot2_wrap(void)
{
    uint8_t inner[3] = {0x01, 0x02, 0x03};
    uint8_t out[8];
    size_t n = pc_wave_1609dot2_wrap(WAVE_16092_SIGNED, inner, 3, out, sizeof(out));
    const uint8_t expect[] = {WAVE_16092_VERSION, WAVE_16092_SIGNED, 0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);
}

void test_wsmp_parse_rejects(void)
{
    WsmpFrame f;
    uint8_t bad_ver[3] = {0x00, 0x20, 0x00};
    TEST_ASSERT_FALSE(pc_wsmp_parse(bad_ver, 3, &f));        // wrong version
    uint8_t truncated[4] = {WSMP_VERSION, 0x20, 0x05, 0x11}; // len says 5, only 1 present
    TEST_ASSERT_FALSE(pc_wsmp_parse(truncated, 4, &f));
}

// 4-octet PSID (>= 0x200000) round-trips; every encode length class rejects a too-small buffer.
void test_psid_four_octet_and_caps(void)
{
    uint8_t out[4];
    uint32_t psid = 0;
    TEST_ASSERT_EQUAL_size_t(4, pc_wave_encode_psid(0x00654321, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0xE0, out[0] & 0xF0);
    TEST_ASSERT_EQUAL_size_t(4, pc_wave_decode_psid(out, 4, &psid));
    TEST_ASSERT_EQUAL_UINT32(0x00654321, psid);
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_encode_psid(0x20, out, 0));       // 1-octet, cap 0
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_encode_psid(0x0100, out, 1));     // 2-octet, cap 1
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_encode_psid(0x8002, out, 2));     // 3-octet, cap 2
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_encode_psid(0x00654321, out, 3)); // 4-octet, cap 3
}

// decode_psid guards: null/short inputs, truncated multi-octet forms, an invalid first byte.
void test_psid_decode_guards(void)
{
    uint32_t psid = 0;
    uint8_t x = 0x20;
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_decode_psid(NULL, 4, &psid));
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_decode_psid(&x, 0, &psid));
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_decode_psid(&x, 4, NULL));
    uint8_t two = 0x81, three = 0xC1, four = 0xE1, invalid = 0xF8;
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_decode_psid(&two, 1, &psid));     // 2-octet, len < 2
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_decode_psid(&three, 1, &psid));   // 3-octet, len < 3
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_decode_psid(&four, 1, &psid));    // 4-octet, len < 4
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_decode_psid(&invalid, 4, &psid)); // no valid length prefix
}

// wsmp_build rejects bad args, an over-long payload, and buffers too small for the PSID or payload.
void test_wsmp_build_guards(void)
{
    uint8_t out[16];
    uint8_t pl[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_size_t(0, pc_wsmp_build(0x20, pl, 4, NULL, sizeof(out)));  // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_wsmp_build(0x20, NULL, 4, out, sizeof(out))); // len but null payload
    TEST_ASSERT_EQUAL_size_t(0, pc_wsmp_build(0x20, pl, 256, out, sizeof(out))); // payload > 255
    TEST_ASSERT_EQUAL_size_t(0, pc_wsmp_build(0x20, pl, 4, out, 0));             // cap 0
    TEST_ASSERT_EQUAL_size_t(0, pc_wsmp_build(0x00654321, pl, 0, out, 2));       // PSID encode doesn't fit
    TEST_ASSERT_EQUAL_size_t(0, pc_wsmp_build(0x20, pl, 4, out, 4));             // payload doesn't fit
}

// wsmp_parse rejects short/null frames, an undecodable PSID, and a missing WSM length byte.
void test_wsmp_parse_more_guards(void)
{
    WsmpFrame f;
    uint8_t two[2] = {WSMP_VERSION, 0x20};
    TEST_ASSERT_FALSE(pc_wsmp_parse(two, 2, &f));     // len < 3
    TEST_ASSERT_FALSE(pc_wsmp_parse(NULL, 3, &f));    // null frame
    uint8_t bad_psid[3] = {WSMP_VERSION, 0xC1, 0x00}; // 3-octet PSID prefix, only 2 bytes present
    TEST_ASSERT_FALSE(pc_wsmp_parse(bad_psid, 3, &f));
    uint8_t no_wlen[3] = {WSMP_VERSION, 0x81, 0x00}; // valid 2-octet PSID, no room for the length byte
    TEST_ASSERT_FALSE(pc_wsmp_parse(no_wlen, 3, &f));
}

// 1609.2 wrap rejects a null output, a null payload with a length, and a too-small buffer.
void test_1609dot2_wrap_guards(void)
{
    uint8_t out[8];
    uint8_t pl[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_1609dot2_wrap(WAVE_16092_SIGNED, pl, 3, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_1609dot2_wrap(WAVE_16092_SIGNED, NULL, 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_wave_1609dot2_wrap(WAVE_16092_SIGNED, pl, 3, out, 4)); // needs 5
}

// wsmp_parse rejects a null output pointer even with an otherwise well-formed frame.
void test_wsmp_parse_null_out(void)
{
    uint8_t frame[3] = {WSMP_VERSION, 0x20, 0x00};
    TEST_ASSERT_FALSE(pc_wsmp_parse(frame, 3, NULL));
}

// A zero-length WSM payload builds and parses cleanly: the payload-copy is skipped on build, and
// parse leaves payload null / payload_len 0 rather than pointing past the frame.
void test_wsmp_zero_length_payload(void)
{
    uint8_t out[8];
    size_t n = pc_wsmp_build(WAVE_PSID_BSM, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(3, n); // version byte + 1-octet PSID + WSM length byte
    WsmpFrame f;
    TEST_ASSERT_TRUE(pc_wsmp_parse(out, n, &f));
    TEST_ASSERT_EQUAL_UINT32(WAVE_PSID_BSM, f.psid);
    TEST_ASSERT_EQUAL_size_t(0, f.payload_len);
    TEST_ASSERT_NULL(f.payload);
}

// A zero-length 1609.2 payload wraps cleanly: the memcpy is skipped, only the 2-byte header is written.
void test_1609dot2_wrap_zero_length(void)
{
    uint8_t out[8];
    size_t n = pc_wave_1609dot2_wrap(WAVE_16092_SIGNED, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_HEX8(WAVE_16092_VERSION, out[0]);
    TEST_ASSERT_EQUAL_HEX8(WAVE_16092_SIGNED, out[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_psid_p_encoding);
    RUN_TEST(test_psid_four_octet_and_caps);
    RUN_TEST(test_psid_decode_guards);
    RUN_TEST(test_wsmp_build_guards);
    RUN_TEST(test_wsmp_parse_more_guards);
    RUN_TEST(test_1609dot2_wrap_guards);
    RUN_TEST(test_wsmp_parse_null_out);
    RUN_TEST(test_wsmp_zero_length_payload);
    RUN_TEST(test_1609dot2_wrap_zero_length);
    RUN_TEST(test_wsmp_roundtrip);
    RUN_TEST(test_1609dot2_wrap);
    RUN_TEST(test_wsmp_parse_rejects);
    return UNITY_END();
}
