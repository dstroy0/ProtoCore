// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/profibus: the PROFIBUS-DP FDL telegram codec.

#include "services/fieldbus/profibus/profibus.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_fcs(void)
{
    const uint8_t b[] = {0x03, 0x02, 0x49};
    TEST_ASSERT_EQUAL_HEX8(0x4E, pc_pb_fcs(b, sizeof(b))); // 0x03+0x02+0x49
}

void test_sd1(void)
{
    uint8_t out[8];
    size_t n = pc_pb_build_sd1(0x03, 0x02, PB_FC_REQUEST_FDL_STATUS, out, sizeof(out));
    // SD1 DA SA FC FCS ED : 10 03 02 49 4E 16
    const uint8_t expect[] = {PB_SD1, 0x03, 0x02, 0x49, 0x4E, PB_ED};
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);

    PbTelegram t;
    TEST_ASSERT_TRUE(pc_pb_parse(out, n, &t));
    TEST_ASSERT_EQUAL_HEX8(PB_SD1, t.sd);
    TEST_ASSERT_EQUAL_HEX8(0x03, t.da);
    TEST_ASSERT_EQUAL_HEX8(0x02, t.sa);
    TEST_ASSERT_EQUAL_HEX8(0x49, t.fc);
    TEST_ASSERT_EQUAL_size_t(0, t.data_len);
}

void test_sd2_roundtrip(void)
{
    uint8_t data[3] = {0xAA, 0xBB, 0xCC};
    uint8_t out[16];
    size_t n = pc_pb_build_sd2(0x05, 0x02, PB_FC_SRD_HIGH, data, 3, out, sizeof(out));
    // le = 3 + 3 = 6; total = 4 + 6 + 2 = 12.
    TEST_ASSERT_EQUAL_size_t(12, n);
    TEST_ASSERT_EQUAL_HEX8(PB_SD2, out[0]);
    TEST_ASSERT_EQUAL_HEX8(6, out[1]);
    TEST_ASSERT_EQUAL_HEX8(6, out[2]); // LEr
    TEST_ASSERT_EQUAL_HEX8(PB_SD2, out[3]);

    PbTelegram t;
    TEST_ASSERT_TRUE(pc_pb_parse(out, n, &t));
    TEST_ASSERT_EQUAL_HEX8(PB_SD2, t.sd);
    TEST_ASSERT_EQUAL_HEX8(0x05, t.da);
    TEST_ASSERT_EQUAL_HEX8(PB_FC_SRD_HIGH, t.fc);
    TEST_ASSERT_EQUAL_size_t(3, t.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, t.data, 3);
}

void test_sd3_roundtrip(void)
{
    const uint8_t data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint8_t out[16];
    size_t n = pc_pb_build_sd3(0x05, 0x02, 0x7C, data, out, sizeof(out));
    // FCS = (0x05 + 0x02 + 0x7C + sum(data)) mod 256 = 0xE7.
    const uint8_t expect[14] = {0xA2, 0x05, 0x02, 0x7C, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0xE7, 0x16};
    TEST_ASSERT_EQUAL_size_t(14, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 14);

    PbTelegram t;
    TEST_ASSERT_TRUE(pc_pb_parse(out, n, &t));
    TEST_ASSERT_EQUAL_HEX8(PB_SD3, t.sd);
    TEST_ASSERT_EQUAL_HEX8(0x05, t.da);
    TEST_ASSERT_EQUAL_HEX8(0x02, t.sa);
    TEST_ASSERT_EQUAL_HEX8(0x7C, t.fc);
    TEST_ASSERT_EQUAL_size_t(8, t.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, t.data, 8);

    // Guards: a null buffer, a null data, and a too-small buffer.
    TEST_ASSERT_EQUAL_size_t(0, pc_pb_build_sd3(0x05, 0x02, 0x7C, data, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_pb_build_sd3(0x05, 0x02, 0x7C, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_pb_build_sd3(0x05, 0x02, 0x7C, data, out, 13)); // needs 14

    // A corrupt FCS is rejected; a truncated SD3 (< 14) is rejected.
    out[12] ^= 0xFF;
    TEST_ASSERT_FALSE(pc_pb_parse(out, n, &t));
    out[12] ^= 0xFF;                             // restore
    TEST_ASSERT_FALSE(pc_pb_parse(out, 13, &t)); // truncated
}

void test_parse_rejects(void)
{
    uint8_t data[2] = {0x11, 0x22};
    uint8_t out[16];
    size_t n = pc_pb_build_sd2(0x05, 0x02, PB_FC_SRD_LOW, data, 2, out, sizeof(out));
    PbTelegram t;
    out[n - 2] ^= 0xFF; // corrupt FCS
    TEST_ASSERT_FALSE(pc_pb_parse(out, n, &t));
    // Mismatched LE/LEr.
    n = pc_pb_build_sd2(0x05, 0x02, PB_FC_SRD_LOW, data, 2, out, sizeof(out));
    out[2] = 0x99;
    TEST_ASSERT_FALSE(pc_pb_parse(out, n, &t));
    // data_len > 246 rejected at build.
    TEST_ASSERT_EQUAL_size_t(0, pc_pb_build_sd2(0x05, 0x02, PB_FC_SRD_LOW, data, 300, out, sizeof(out)));
}

void test_build_and_parse_guard_subconditions(void)
{
    uint8_t out[16];
    uint8_t data[3] = {0xAA, 0xBB, 0xCC};
    // Build guards: null out and a capacity below the frame size fail closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_pb_build_sd1(3, 2, 0x6C, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_pb_build_sd1(3, 2, 0x6C, out, 5));
    TEST_ASSERT_EQUAL_size_t(0, pc_pb_build_sd2(3, 2, 0x6C, data, sizeof(data), out, 4));
    // Parse guards: null frame, null out, short frame, and an SD2 with LE < 3.
    PbTelegram tg;
    TEST_ASSERT_FALSE(pc_pb_parse(NULL, 6, &tg));
    TEST_ASSERT_FALSE(pc_pb_parse(out, 6, NULL));
    TEST_ASSERT_FALSE(pc_pb_parse(out, 3, &tg));
    const uint8_t bad_sd2[9] = {PB_SD2, 0x02, 0x02, PB_SD2, 1, 2, 3, 4, 5}; // LE=2 (< 3)
    TEST_ASSERT_FALSE(pc_pb_parse(bad_sd2, sizeof(bad_sd2), &tg));
}

void test_sd2_build_more_guards(void)
{
    uint8_t out[16];
    uint8_t data[3] = {0xAA, 0xBB, 0xCC};
    // Null out pointer fails closed before any other subcondition is checked.
    TEST_ASSERT_EQUAL_size_t(0, pc_pb_build_sd2(0x05, 0x02, PB_FC_SRD_LOW, data, 3, NULL, sizeof(out)));
    // Non-zero data_len with a null data pointer fails closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_pb_build_sd2(0x05, 0x02, PB_FC_SRD_LOW, NULL, 3, out, sizeof(out)));
}

void test_sd2_zero_length_data(void)
{
    // data_len == 0 (data may be null): the memcpy is skipped and the parsed data pointer stays null.
    uint8_t out[16];
    size_t n = pc_pb_build_sd2(0x05, 0x02, PB_FC_SRD_LOW, NULL, 0, out, sizeof(out));
    // le = 3 + 0 = 3; total = 4 + 3 + 2 = 9.
    TEST_ASSERT_EQUAL_size_t(9, n);

    PbTelegram t;
    TEST_ASSERT_TRUE(pc_pb_parse(out, n, &t));
    TEST_ASSERT_EQUAL_size_t(0, t.data_len);
    TEST_ASSERT_NULL(t.data);
}

void test_sd1_parse_corruption(void)
{
    uint8_t out[8];
    size_t n = pc_pb_build_sd1(0x03, 0x02, PB_FC_REQUEST_FDL_STATUS, out, sizeof(out));
    PbTelegram t;

    // FCS mismatch (ED still correct) fails closed.
    uint8_t bad_fcs[6];
    memcpy(bad_fcs, out, n);
    bad_fcs[4] ^= 0xFF;
    TEST_ASSERT_FALSE(pc_pb_parse(bad_fcs, n, &t));

    // FCS correct, but ED mismatch fails closed.
    uint8_t bad_ed[6];
    memcpy(bad_ed, out, n);
    bad_ed[5] = 0x00;
    TEST_ASSERT_FALSE(pc_pb_parse(bad_ed, n, &t));
}

void test_parse_unknown_sd(void)
{
    // Neither SD1 nor SD2: falls through both checks and fails closed at the end.
    const uint8_t frame[6] = {0x00, 0x03, 0x02, 0x49, 0x4E, PB_ED};
    PbTelegram t;
    TEST_ASSERT_FALSE(pc_pb_parse(frame, sizeof(frame), &t));
}

void test_sd2_parse_length_guards(void)
{
    uint8_t data[2] = {0x11, 0x22};
    uint8_t out[16];
    size_t n = pc_pb_build_sd2(0x05, 0x02, PB_FC_SRD_LOW, data, 2, out, sizeof(out));
    PbTelegram t;

    // len >= 6 (passes the top-level guard) but < 9 fails closed before the LE/LEr checks.
    TEST_ASSERT_FALSE(pc_pb_parse(out, 8, &t));

    // LE == LEr (matches), but the repeated SD2 marker byte is wrong.
    uint8_t bad_marker[16];
    memcpy(bad_marker, out, n);
    bad_marker[3] = 0x00;
    TEST_ASSERT_FALSE(pc_pb_parse(bad_marker, n, &t));

    // len >= 9, but LE declares more body than the supplied buffer actually holds.
    const uint8_t short_body[9] = {PB_SD2, 10, 10, PB_SD2, 1, 2, 3, 4, 5};
    TEST_ASSERT_FALSE(pc_pb_parse(short_body, sizeof(short_body), &t));

    // FCS correct, but the ED byte is corrupted.
    uint8_t bad_ed[16];
    memcpy(bad_ed, out, n);
    bad_ed[n - 1] = 0x00;
    TEST_ASSERT_FALSE(pc_pb_parse(bad_ed, n, &t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fcs);
    RUN_TEST(test_sd1);
    RUN_TEST(test_sd3_roundtrip);
    RUN_TEST(test_sd2_roundtrip);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_build_and_parse_guard_subconditions);
    RUN_TEST(test_sd2_build_more_guards);
    RUN_TEST(test_sd2_zero_length_data);
    RUN_TEST(test_sd1_parse_corruption);
    RUN_TEST(test_parse_unknown_sd);
    RUN_TEST(test_sd2_parse_length_guards);
    return UNITY_END();
}
