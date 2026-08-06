// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/iccp: the ICCP / TASE.2 Data_Value codec.

#include "services/energy/iccp/iccp.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static int find(const uint8_t *hay, size_t hlen, const uint8_t *needle, size_t nlen)
{
    if (nlen > hlen)
    {
        return -1;
    }
    for (size_t i = 0; i + nlen <= hlen; i++)
    {
        if (memcmp(hay + i, needle, nlen) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

void test_state_q_no_time(void)
{
    uint8_t out[16];
    size_t n = pc_iccp_state_q(ICCP_STATE_ON, ICCP_QUAL_VALID, NULL, out, sizeof(out));
    // A2 { 85 01 <sq> } ; sq = (ON=2)<<6 | valid(0) = 0x80. -> A2 03 85 01 80
    const uint8_t expect[] = {0xA2, 0x03, 0x85, 0x01, 0x80};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);
}

void test_state_q_with_time(void)
{
    uint8_t time[4] = {0x66, 0x00, 0x00, 0x00};
    uint8_t out[24];
    size_t n = pc_iccp_state_q(ICCP_STATE_OFF, ICCP_QUAL_SUSPECT, time, out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8(0xA2, out[0]);
    // The time TLV (17 04 66 00 00 00) is present.
    const uint8_t t[] = {0x17, 0x04, 0x66, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(find(out, n, t, sizeof(t)) >= 0);
    // stateAndQuality: OFF(1)<<6 | suspect(2) = 0x42.
    const uint8_t sq[] = {0x85, 0x01, 0x42};
    TEST_ASSERT_TRUE(find(out, n, sq, 3) >= 0);
}

void test_real_q(void)
{
    uint8_t out[24];
    size_t n = pc_iccp_real_q(12345, ICCP_QUAL_VALID, NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8(0xA3, out[0]);
    // 12345 = 0x3039, INTEGER 02 02 30 39.
    const uint8_t iv[] = {0x02, 0x02, 0x30, 0x39};
    TEST_ASSERT_TRUE(find(out, n, iv, 4) >= 0);
    // quality 85 01 00.
    const uint8_t q[] = {0x85, 0x01, 0x00};
    TEST_ASSERT_TRUE(find(out, n, q, 3) >= 0);
}

void test_real_q_negative(void)
{
    uint8_t out[24];
    size_t n = pc_iccp_real_q(-1, ICCP_QUAL_VALID, NULL, out, sizeof(out));
    // -1 -> minimal two's complement INTEGER 02 01 FF.
    const uint8_t iv[] = {0x02, 0x01, 0xFF};
    TEST_ASSERT_TRUE(find(out, n, iv, 3) >= 0);
    // -256 -> 02 02 FF 00.
    n = pc_iccp_real_q(-256, ICCP_QUAL_VALID, NULL, out, sizeof(out));
    const uint8_t iv2[] = {0x02, 0x02, 0xFF, 0x00};
    TEST_ASSERT_TRUE(find(out, n, iv2, 4) >= 0);
}

void test_state_and_real_q_guards()
{
    uint8_t out[64];
    uint8_t t[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL_size_t(0, pc_iccp_state_q(1, 0, t, NULL, sizeof(out)));  // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_iccp_state_q(1, 0, t, out, 2));             // overflow
    TEST_ASSERT_EQUAL_size_t(0, pc_iccp_real_q(100, 0, t, out, 2));            // overflow
    TEST_ASSERT_EQUAL_size_t(0, pc_iccp_real_q(100, 0, t, NULL, sizeof(out))); // null out
    TEST_ASSERT_TRUE(pc_iccp_state_q(1, 0x40, t, out, sizeof(out)) > 0);       // valid (time-field tlv)
}

void test_real_q_positive_needs_pad_byte(void)
{
    // 128 = 0x80: its low byte alone has the sign bit set, so the minimal two's-complement
    // encoding must keep a leading 0x00 pad byte to stay positive -> INTEGER 02 02 00 80.
    // This exercises int_content()'s trim loop where tmp[start]==0x00 but the next byte's
    // top bit is set, so trimming must stop one byte earlier than the all-zero case.
    uint8_t out[24];
    size_t n = pc_iccp_real_q(128, ICCP_QUAL_VALID, NULL, out, sizeof(out));
    const uint8_t iv[] = {0x02, 0x02, 0x00, 0x80};
    TEST_ASSERT_TRUE(find(out, n, iv, sizeof(iv)) >= 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_state_q_no_time);
    RUN_TEST(test_state_q_with_time);
    RUN_TEST(test_real_q);
    RUN_TEST(test_real_q_negative);
    RUN_TEST(test_state_and_real_q_guards);
    RUN_TEST(test_real_q_positive_needs_pad_byte);
    return UNITY_END();
}
