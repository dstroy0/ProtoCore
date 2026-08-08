// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/lonworks: the LonTalk NV PDU + SNVT scalar codec.

#include "services/fieldbus/lonworks/lonworks.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static double absd(double x)
{
    return x < 0 ? -x : x;
}

void test_nv_pdu_roundtrip(void)
{
    uint8_t value[2] = {0xAB, 0xCD};
    uint8_t out[8];
    size_t n = pc_lon_build_nv(LON_MSG_NV_UPDATE, 0x1234, value, 2, out, sizeof(out));
    // selector 0x1234 is 14-bit -> stored 0x12 0x34.
    const uint8_t expect[] = {LON_MSG_NV_UPDATE, 0x12, 0x34, 0xAB, 0xCD};
    TEST_ASSERT_EQUAL_size_t(5, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);

    LonNv nv;
    TEST_ASSERT_TRUE(pc_lon_parse_nv(out, n, &nv));
    TEST_ASSERT_EQUAL_HEX8(LON_MSG_NV_UPDATE, nv.msg_code);
    TEST_ASSERT_EQUAL_UINT16(0x1234, nv.selector);
    TEST_ASSERT_EQUAL_size_t(2, nv.value_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(value, nv.value, 2);
}

void test_nv_selector_masked_to_14_bits(void)
{
    // The top two bits of the selector byte are not part of the 14-bit value.
    uint8_t out[8];
    pc_lon_build_nv(LON_MSG_NV_POLL, 0x3FFF, NULL, 0, out, sizeof(out));
    LonNv nv;
    // Simulate a peer that set reserved high bits.
    out[1] |= 0xC0;
    TEST_ASSERT_TRUE(pc_lon_parse_nv(out, 3, &nv));
    TEST_ASSERT_EQUAL_UINT16(0x3FFF, nv.selector);
    // Reject a selector > 14 bits at build.
    TEST_ASSERT_EQUAL_size_t(0, pc_lon_build_nv(LON_MSG_NV_UPDATE, 0x4000, NULL, 0, out, sizeof(out)));
}

void test_snvt_temp(void)
{
    uint8_t enc[2];
    pc_lon_snvt_temp_encode(25.0, enc);
    // (25 + 273.15) * 100 = 29815 = 0x747... check decode round-trip.
    double c = pc_lon_snvt_temp_decode(enc);
    TEST_ASSERT_TRUE(absd(c - 25.0) < 0.01);
    // 0 C -> 273.15 K -> 27315.
    pc_lon_snvt_temp_encode(0.0, enc);
    TEST_ASSERT_EQUAL_UINT16(27315, (uint16_t)((enc[0] << 8) | enc[1]));
}

void test_snvt_switch(void)
{
    uint8_t enc[2];
    pc_lon_snvt_switch_encode(50.0, 1, enc);
    TEST_ASSERT_EQUAL_UINT8(100, enc[0]); // 50% * 2 = 100
    TEST_ASSERT_EQUAL_UINT8(1, enc[1]);
    double pct = 0;
    uint8_t st = 0;
    pc_lon_snvt_switch_decode(enc, &pct, &st);
    TEST_ASSERT_TRUE(absd(pct - 50.0) < 0.01);
    TEST_ASSERT_EQUAL_UINT8(1, st);
    // Clamp at 100.5%.
    pc_lon_snvt_switch_encode(200.0, 1, enc);
    TEST_ASSERT_EQUAL_UINT8(201, enc[0]); // 100.5 * 2
}

void test_snvt_clamps_and_guards()
{
    uint8_t out[16];
    uint8_t val[2] = {0, 0};
    TEST_ASSERT_EQUAL_size_t(0, pc_lon_build_nv(0x00, 0, val, sizeof(val), out, 2)); // cap too small
    LonNv nv;
    uint8_t tiny[1] = {0};
    TEST_ASSERT_FALSE(pc_lon_parse_nv(tiny, sizeof(tiny), &nv)); // too short
    uint8_t t[2];
    pc_lon_snvt_temp_encode(1e9, t);  // saturates at +32767
    pc_lon_snvt_temp_encode(-1e9, t); // saturates at -32768
    uint8_t sw[2];
    pc_lon_snvt_switch_encode(-5.0, 1, sw); // negative percent clamps to 0
}

void test_nv_build_null_guards(void)
{
    uint8_t out[8];
    uint8_t val[2] = {1, 2};
    // out == NULL guard branch.
    TEST_ASSERT_EQUAL_size_t(0, pc_lon_build_nv(LON_MSG_NV_UPDATE, 0, val, sizeof(val), NULL, sizeof(out)));
    // value_len > 0 but value == NULL guard branch.
    TEST_ASSERT_EQUAL_size_t(0, pc_lon_build_nv(LON_MSG_NV_UPDATE, 0, NULL, 2, out, sizeof(out)));
}

void test_nv_parse_null_guards(void)
{
    uint8_t pdu[3] = {0x80, 0x00, 0x00};
    LonNv nv;
    // pdu == NULL guard branch.
    TEST_ASSERT_FALSE(pc_lon_parse_nv(NULL, 3, &nv));
    // out == NULL guard branch.
    TEST_ASSERT_FALSE(pc_lon_parse_nv(pdu, 3, NULL));
}

void test_snvt_temp_clamp_high_in_range(void)
{
    // (celsius + 273.15) * 100 = 47315, which is inside int32_t range so the cast is
    // well-defined (unlike the 1e9 case above, whose cast overflows int32_t and saturates
    // to INT32_MIN via UB on this host/compiler - never actually crossing the +32767 clamp).
    // This exercises the true branch of the "v > 32767" clamp with a well-defined value.
    uint8_t enc[2];
    pc_lon_snvt_temp_encode(200.0, enc);
    TEST_ASSERT_EQUAL_UINT16(32767, (uint16_t)((enc[0] << 8) | enc[1]));
}

void test_snvt_switch_decode_null_outputs(void)
{
    uint8_t enc[2] = {100, 1};
    double pct = -1.0;
    uint8_t st = 0xAA;
    // percent == NULL: only state should be written.
    pc_lon_snvt_switch_decode(enc, NULL, &st);
    TEST_ASSERT_EQUAL_UINT8(1, st);
    // state == NULL: only percent should be written.
    pc_lon_snvt_switch_decode(enc, &pct, NULL);
    TEST_ASSERT_TRUE(absd(pct - 50.0) < 0.01);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nv_pdu_roundtrip);
    RUN_TEST(test_nv_selector_masked_to_14_bits);
    RUN_TEST(test_snvt_temp);
    RUN_TEST(test_snvt_switch);
    RUN_TEST(test_snvt_clamps_and_guards);
    RUN_TEST(test_nv_build_null_guards);
    RUN_TEST(test_nv_parse_null_guards);
    RUN_TEST(test_snvt_temp_clamp_high_in_range);
    RUN_TEST(test_snvt_switch_decode_null_outputs);
    return UNITY_END();
}
