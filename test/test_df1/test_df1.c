// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Allen-Bradley DF1 full-duplex frame codec (services/fieldbus/df1): the BCC and
// CRC-16, the frame builder (with DLE byte-stuffing), and the validating, un-stuffing parser.
// Vectors verified against AB pub. 1770-6.5.16. Pure host tests.

#include "services/fieldbus/df1/df1.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// The manual's BCC example: data sum 0x20 -> BCC 0xE0 (2's complement).
void test_bcc_vector()
{
    const uint8_t data[] = {0x07, 0x19}; // 0x07 + 0x19 = 0x20
    TEST_ASSERT_EQUAL_HEX8(0xE0, pc_df1_bcc(data, sizeof(data)));
    TEST_ASSERT_EQUAL_HEX8(0x00, pc_df1_bcc(NULL, 0)); // empty -> 0
}

// CRC-16/ARC (DF1 polynomial, init 0) check value for "123456789" is 0xBB3D.
void test_crc_vector()
{
    TEST_ASSERT_EQUAL_HEX16(0xBB3D, pc_df1_crc((const uint8_t *)"123456789", 9));
}

void test_build_bcc_frame()
{
    const uint8_t data[] = {0x07, 0x19};
    uint8_t buf[16];
    size_t n = pc_df1_build_frame(buf, sizeof(buf), data, sizeof(data), DF1_CHECK_BCC);
    const uint8_t expect[] = {DF1_DLE, DF1_STX, 0x07, 0x19, DF1_DLE, DF1_ETX, 0xE0};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// A DLE (0x10) data byte is doubled on the wire but counted once in the BCC.
void test_build_dle_stuffing()
{
    const uint8_t data[] = {0x10, 0x05}; // sum 0x15 -> BCC 0xEB
    uint8_t buf[16];
    size_t n = pc_df1_build_frame(buf, sizeof(buf), data, sizeof(data), DF1_CHECK_BCC);
    const uint8_t expect[] = {DF1_DLE, DF1_STX, 0x10, 0x10, 0x05, DF1_DLE, DF1_ETX, 0xEB};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_round_trip_bcc()
{
    const uint8_t data[] = {0x10, 0x05, 0x10, 0xAB}; // two embedded DLEs
    uint8_t buf[32];
    size_t n = pc_df1_build_frame(buf, sizeof(buf), data, sizeof(data), DF1_CHECK_BCC);

    uint8_t out[32];
    size_t out_len;
    TEST_ASSERT_TRUE(pc_df1_parse_frame(buf, n, DF1_CHECK_BCC, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(data), out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, out, out_len);
}

void test_round_trip_crc()
{
    const uint8_t data[] = {0x00, 0x05, 0x0F, 0x00, 0x10, 0x42}; // includes a DLE (0x10)
    uint8_t buf[32];
    size_t n = pc_df1_build_frame(buf, sizeof(buf), data, sizeof(data), DF1_CHECK_CRC);

    uint8_t out[32];
    size_t out_len;
    TEST_ASSERT_TRUE(pc_df1_parse_frame(buf, n, DF1_CHECK_CRC, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(data), out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, out, out_len);
}

void test_empty_data_frame()
{
    uint8_t buf[16];
    size_t n = pc_df1_build_frame(buf, sizeof(buf), NULL, 0, DF1_CHECK_BCC);
    const uint8_t expect[] = {DF1_DLE, DF1_STX, DF1_DLE, DF1_ETX, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    uint8_t out[4];
    size_t out_len;
    TEST_ASSERT_TRUE(pc_df1_parse_frame(buf, n, DF1_CHECK_BCC, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(0, out_len);
}

void test_parse_rejects_bad()
{
    const uint8_t data[] = {0x07, 0x19};
    uint8_t buf[16];
    size_t n = pc_df1_build_frame(buf, sizeof(buf), data, sizeof(data), DF1_CHECK_BCC);

    uint8_t out[16];
    size_t out_len;
    uint8_t corrupt[16];

    // Corrupt a data byte -> BCC mismatch.
    memcpy(corrupt, buf, n);
    corrupt[2] ^= 0x01;
    TEST_ASSERT_FALSE(pc_df1_parse_frame(corrupt, n, DF1_CHECK_BCC, out, sizeof(out), &out_len));

    // Missing DLE STX leader.
    memcpy(corrupt, buf, n);
    corrupt[1] = 0x55;
    TEST_ASSERT_FALSE(pc_df1_parse_frame(corrupt, n, DF1_CHECK_BCC, out, sizeof(out), &out_len));

    // Truncated frame.
    TEST_ASSERT_FALSE(pc_df1_parse_frame(buf, n - 1, DF1_CHECK_BCC, out, sizeof(out), &out_len));

    // out buffer too small for the un-stuffed data.
    TEST_ASSERT_FALSE(pc_df1_parse_frame(buf, n, DF1_CHECK_BCC, out, 1, &out_len));
}

void test_build_overflow_fails_closed()
{
    const uint8_t data[] = {0x07, 0x19, 0x2A};
    uint8_t small[6]; // needs 2 + 3 + 2 + 1 = 8
    TEST_ASSERT_EQUAL_size_t(0, pc_df1_build_frame(small, sizeof(small), data, sizeof(data), DF1_CHECK_BCC));
}

// Builder null guards and every parser edge/reject: a lone trailing DLE, a stuffed DLE
// that overruns the output, an unexpected control symbol, a missing terminator, a
// truncated CRC tail, and a CRC mismatch.
void test_parse_edges_and_guards()
{
    uint8_t out[16];
    size_t out_len;
    const uint8_t d[] = {0x07, 0x19};
    uint8_t bbuf[16];

    // build guards
    TEST_ASSERT_EQUAL_size_t(0, pc_df1_build_frame(NULL, sizeof(bbuf), d, 2, DF1_CHECK_BCC));    // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_df1_build_frame(bbuf, sizeof(bbuf), NULL, 2, DF1_CHECK_BCC)); // len but null data

    // parse guards
    const uint8_t ok5[5] = {DF1_DLE, DF1_STX, DF1_DLE, DF1_ETX, 0x00};
    TEST_ASSERT_FALSE(pc_df1_parse_frame(NULL, 8, DF1_CHECK_BCC, out, sizeof(out), &out_len)); // null buf
    TEST_ASSERT_FALSE(pc_df1_parse_frame(ok5, sizeof(ok5), DF1_CHECK_BCC, NULL, 0, &out_len)); // null out
    const uint8_t two[2] = {DF1_DLE, DF1_STX};
    TEST_ASSERT_FALSE(pc_df1_parse_frame(two, sizeof(two), DF1_CHECK_BCC, out, sizeof(out), &out_len)); // too short

    // a DLE as the final octet (no following byte)
    const uint8_t dle_end[5] = {DF1_DLE, DF1_STX, 0x41, 0x42, DF1_DLE};
    TEST_ASSERT_FALSE(pc_df1_parse_frame(dle_end, sizeof(dle_end), DF1_CHECK_BCC, out, sizeof(out), &out_len));

    // a doubled DLE while the output buffer is already full (out_cap 0)
    uint8_t dframe[16];
    const uint8_t one_dle[1] = {DF1_DLE};
    size_t dn = pc_df1_build_frame(dframe, sizeof(dframe), one_dle, 1, DF1_CHECK_BCC);
    TEST_ASSERT_FALSE(pc_df1_parse_frame(dframe, dn, DF1_CHECK_BCC, out, 0, &out_len));

    // a DLE followed by an unexpected control symbol
    const uint8_t bad_ctrl[5] = {DF1_DLE, DF1_STX, DF1_DLE, 0x41, 0x00};
    TEST_ASSERT_FALSE(pc_df1_parse_frame(bad_ctrl, sizeof(bad_ctrl), DF1_CHECK_BCC, out, sizeof(out), &out_len));

    // data that never reaches a DLE ETX terminator
    const uint8_t no_end[5] = {DF1_DLE, DF1_STX, 0x41, 0x42, 0x43};
    TEST_ASSERT_FALSE(pc_df1_parse_frame(no_end, sizeof(no_end), DF1_CHECK_BCC, out, sizeof(out), &out_len));

    // CRC selected but fewer than 2 check octets follow the ETX
    const uint8_t crc_trunc[6] = {DF1_DLE, DF1_STX, 0x41, DF1_DLE, DF1_ETX, 0x00};
    TEST_ASSERT_FALSE(pc_df1_parse_frame(crc_trunc, sizeof(crc_trunc), DF1_CHECK_CRC, out, sizeof(out), &out_len));

    // a valid CRC frame with a corrupted CRC byte
    uint8_t cbuf[16];
    size_t cn = pc_df1_build_frame(cbuf, sizeof(cbuf), d, 2, DF1_CHECK_CRC);
    cbuf[cn - 1] ^= 0xFF;
    TEST_ASSERT_FALSE(pc_df1_parse_frame(cbuf, cn, DF1_CHECK_CRC, out, sizeof(out), &out_len));
}

// The leader check is a short-circuiting OR; a bad first octet (DLE) must reject on its own,
// without ever looking at the second octet. And a successful parse with a null out_len
// pointer (caller doesn't want the length back) must still return true, not dereference it.
void test_parse_bad_leader_first_byte_and_null_outlen()
{
    const uint8_t data[] = {0x07, 0x19};
    uint8_t buf[16];
    size_t n = pc_df1_build_frame(buf, sizeof(buf), data, sizeof(data), DF1_CHECK_BCC);

    uint8_t out[16];
    size_t out_len;

    // First octet is not DLE (second octet left untouched/valid).
    uint8_t corrupt[16];
    memcpy(corrupt, buf, n);
    corrupt[0] = 0x55;
    TEST_ASSERT_FALSE(pc_df1_parse_frame(corrupt, n, DF1_CHECK_BCC, out, sizeof(out), &out_len));

    // A successful parse with a null out_len pointer.
    TEST_ASSERT_TRUE(pc_df1_parse_frame(buf, n, DF1_CHECK_BCC, out, sizeof(out), NULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_bcc_vector);
    RUN_TEST(test_crc_vector);
    RUN_TEST(test_build_bcc_frame);
    RUN_TEST(test_build_dle_stuffing);
    RUN_TEST(test_round_trip_bcc);
    RUN_TEST(test_round_trip_crc);
    RUN_TEST(test_empty_data_frame);
    RUN_TEST(test_parse_rejects_bad);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_parse_edges_and_guards);
    RUN_TEST(test_parse_bad_leader_first_byte_and_null_outlen);
    return UNITY_END();
}
