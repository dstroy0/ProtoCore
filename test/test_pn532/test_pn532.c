// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the PN532 NFC frame codec (services/peripherals/pn532): the normal-information-frame
// build/parse against the documented GetFirmwareVersion command + response frames (LCS +
// DCS checksums), a round trip, malformed framing (bad preamble / start / LCS / DCS),
// incomplete frames, over-length rejection, and the ACK frame. Pure host tests.
//
// The env sizes PC_PN532_MAX_DATA = 8.

#include "services/peripherals/pn532/pn532.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_build_getfirmwareversion_kat()
{
    // Host -> PN532 GetFirmwareVersion (command 0x02): the documented frame is
    // 00 00 FF 02 FE D4 02 2A 00.
    const uint8_t expect[9] = {0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00};
    const uint8_t cmd[1] = {0x02};
    uint8_t out[16];
    uint16_t n = pc_pn532_build_frame(PN532_TFI_HOST, cmd, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(9, n);
    TEST_ASSERT_EQUAL_MEMORY(expect, out, 9);
}

void test_parse_getfirmwareversion_response_kat()
{
    // PN532 -> host response: 00 00 FF 06 FA D5 03 32 01 06 07 E8 00.
    const uint8_t frame[13] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03, 0x32, 0x01, 0x06, 0x07, 0xE8, 0x00};
    uint8_t tfi = 0;
    const uint8_t *pd = NULL;
    uint8_t pdlen = 0;
    int c = pc_pn532_parse_frame(frame, sizeof(frame), &tfi, &pd, &pdlen);
    TEST_ASSERT_EQUAL_INT(13, c);
    TEST_ASSERT_EQUAL_HEX8(PN532_TFI_PN532, tfi);
    TEST_ASSERT_EQUAL_UINT8(5, pdlen);
    const uint8_t expect_pd[5] = {0x03, 0x32, 0x01, 0x06, 0x07};
    TEST_ASSERT_EQUAL_MEMORY(expect_pd, pd, 5);
}

void test_build_then_parse_round_trip()
{
    const uint8_t data[4] = {0x4A, 0x01, 0x00, 0xAB}; // e.g. InListPassiveTarget-ish
    uint8_t buf[24];
    uint16_t n = pc_pn532_build_frame(PN532_TFI_HOST, data, 4, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT16(12, n); // 8 + 4
    uint8_t tfi = 0;
    const uint8_t *pd = NULL;
    uint8_t pdlen = 0;
    TEST_ASSERT_EQUAL_INT(12, pc_pn532_parse_frame(buf, n, &tfi, &pd, &pdlen));
    TEST_ASSERT_EQUAL_HEX8(PN532_TFI_HOST, tfi);
    TEST_ASSERT_EQUAL_UINT8(4, pdlen);
    TEST_ASSERT_EQUAL_MEMORY(data, pd, 4);
}

static uint16_t sample(uint8_t *buf, uint16_t cap)
{
    const uint8_t d[2] = {0x02, 0x00};
    return pc_pn532_build_frame(PN532_TFI_HOST, d, 2, buf, cap);
}

void test_parse_rejects_bad_preamble_and_start()
{
    uint8_t buf[16];
    uint16_t n = sample(buf, sizeof(buf));
    buf[0] = 0x11;
    TEST_ASSERT_EQUAL_INT(-1, pc_pn532_parse_frame(buf, n, NULL, NULL, NULL));
    n = sample(buf, sizeof(buf));
    buf[2] = 0x00; // start should be 00 FF
    TEST_ASSERT_EQUAL_INT(-1, pc_pn532_parse_frame(buf, n, NULL, NULL, NULL));
    n = sample(buf, sizeof(buf));
    buf[1] = 0x22; // start's first byte should be 0x00 (preamble at buf[0] is untouched)
    TEST_ASSERT_EQUAL_INT(-1, pc_pn532_parse_frame(buf, n, NULL, NULL, NULL));
}

void test_parse_rejects_bad_lcs()
{
    uint8_t buf[16];
    uint16_t n = sample(buf, sizeof(buf));
    buf[4] ^= 0xFF; // corrupt LCS
    TEST_ASSERT_EQUAL_INT(-1, pc_pn532_parse_frame(buf, n, NULL, NULL, NULL));
}

void test_parse_rejects_bad_dcs()
{
    uint8_t buf[16];
    uint16_t n = sample(buf, sizeof(buf));
    buf[n - 2] ^= 0xFF; // corrupt DCS (byte before the postamble)
    TEST_ASSERT_EQUAL_INT(-1, pc_pn532_parse_frame(buf, n, NULL, NULL, NULL));
}

void test_parse_needs_more_bytes()
{
    uint8_t buf[16];
    uint16_t n = sample(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, pc_pn532_parse_frame(buf, 0, NULL, NULL, NULL));     // len == 0, raw non-null
    TEST_ASSERT_EQUAL_INT(0, pc_pn532_parse_frame(buf, 4, NULL, NULL, NULL));     // header incomplete
    TEST_ASSERT_EQUAL_INT(0, pc_pn532_parse_frame(buf, n - 1, NULL, NULL, NULL)); // body incomplete
}

void test_parse_rejects_over_length()
{
    // frame_len 20 (> PC_PN532_MAX_DATA + 1 = 9) is rejected early.
    uint8_t buf[8] = {0x00, 0x00, 0xFF, 0x14, 0xEC, 0xD5, 0x00, 0x00};
    TEST_ASSERT_EQUAL_INT(-1, pc_pn532_parse_frame(buf, sizeof(buf), NULL, NULL, NULL));
}

void test_parse_rejects_zero_length()
{
    // frame_len == 0 (no TFI at all) with a matching LCS is rejected explicitly, distinct
    // from the over-length rejection above.
    uint8_t buf[5] = {0x00, 0x00, 0xFF, 0x00, 0x00};
    TEST_ASSERT_EQUAL_INT(-1, pc_pn532_parse_frame(buf, sizeof(buf), NULL, NULL, NULL));
}

void test_parse_success_with_null_outputs()
{
    // A fully valid, complete frame with every output pointer null must not dereference
    // them and must still report the number of bytes consumed.
    uint8_t buf[16];
    uint16_t n = sample(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT((int)n, pc_pn532_parse_frame(buf, n, NULL, NULL, NULL));
}

void test_ack_frame()
{
    uint8_t ack[6];
    TEST_ASSERT_EQUAL_UINT16(6, pc_pn532_build_ack(ack, sizeof(ack)));
    const uint8_t expect[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    TEST_ASSERT_EQUAL_MEMORY(expect, ack, 6);
    TEST_ASSERT_TRUE(pc_pn532_is_ack(ack, 6));

    uint8_t buf[16];
    uint16_t n = sample(buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_pn532_is_ack(buf, n)); // a normal frame is not an ACK
    uint8_t small[3];
    TEST_ASSERT_EQUAL_UINT16(0, pc_pn532_build_ack(small, sizeof(small))); // too small
    TEST_ASSERT_EQUAL_UINT16(0, pc_pn532_build_ack(NULL, 6));              // out == NULL
}

void test_build_frame_null_data_and_out_guards()
{
    uint8_t out[16];
    const uint8_t cmd[1] = {0x02};
    // out == NULL is rejected regardless of other args.
    TEST_ASSERT_EQUAL_UINT16(0, pc_pn532_build_frame(PN532_TFI_HOST, cmd, 1, NULL, 16));

    // data == NULL with len == 0 is a legal "no PData" frame (TFI only).
    uint16_t n = pc_pn532_build_frame(PN532_TFI_HOST, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(8, n); // 8 + 0
    const uint8_t expect[8] = {0x00, 0x00, 0xFF, 0x01, 0xFF, 0xD4, 0x2C, 0x00};
    TEST_ASSERT_EQUAL_MEMORY(expect, out, 8);

    // data == NULL with len > 0 is rejected (nothing to copy).
    TEST_ASSERT_EQUAL_UINT16(0, pc_pn532_build_frame(PN532_TFI_HOST, NULL, 2, out, sizeof(out)));
}

void test_build_bounds()
{
    uint8_t data[8] = {0};
    uint8_t small[8];
    TEST_ASSERT_EQUAL_UINT16(0, pc_pn532_build_frame(PN532_TFI_HOST, data, 4, small, sizeof(small))); // 12 > 8
    uint8_t big[64];
    TEST_ASSERT_EQUAL_UINT16(0, pc_pn532_build_frame(PN532_TFI_HOST, big, 9, big, sizeof(big))); // 9 > MAX_DATA 8
}

void test_frame_parse_and_ack_guards()
{
    uint8_t tfi = 0;
    const uint8_t *pdata = NULL;
    uint8_t pdata_len = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_pn532_parse_frame(NULL, 10, &tfi, &pdata, &pdata_len)); // null raw
    uint8_t tiny[1] = {0};
    TEST_ASSERT_FALSE(pc_pn532_is_ack(tiny, sizeof(tiny))); // too short
    TEST_ASSERT_FALSE(pc_pn532_is_ack(NULL, 6));            // null
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_getfirmwareversion_kat);
    RUN_TEST(test_parse_getfirmwareversion_response_kat);
    RUN_TEST(test_build_then_parse_round_trip);
    RUN_TEST(test_parse_rejects_bad_preamble_and_start);
    RUN_TEST(test_parse_rejects_bad_lcs);
    RUN_TEST(test_parse_rejects_bad_dcs);
    RUN_TEST(test_parse_needs_more_bytes);
    RUN_TEST(test_parse_rejects_over_length);
    RUN_TEST(test_parse_rejects_zero_length);
    RUN_TEST(test_parse_success_with_null_outputs);
    RUN_TEST(test_ack_frame);
    RUN_TEST(test_build_bounds);
    RUN_TEST(test_build_frame_null_data_and_out_guards);
    RUN_TEST(test_frame_parse_and_ack_guards);
    return UNITY_END();
}
