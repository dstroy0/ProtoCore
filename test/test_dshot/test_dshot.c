// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/dshot: the DShot ESC throttle frame codec (hand-computed vectors).

#include "services/peripherals/dshot/dshot.h"
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// value 1046, no telemetry: v12 = 1046<<1 = 0x82C, nibbles 8^2^C = 6, frame = (0x82C<<4)|6 = 0x82C6.
void test_encode_known_vector(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x82C6, pc_dshot_encode(1046, false, false));
    // motor stop (value 0) -> all zero.
    TEST_ASSERT_EQUAL_HEX16(0x0000, pc_dshot_encode(DSHOT_CMD_MOTOR_STOP, false, false));
    // value 1: v12 = 2 -> crc 2 -> 0x0022.
    TEST_ASSERT_EQUAL_HEX16(0x0022, pc_dshot_encode(1, false, false));
}

void test_encode_telemetry_bit(void)
{
    // value 1046, telemetry set: v12 = 0x82D, nibbles 8^2^D = 7, frame = 0x82D7.
    TEST_ASSERT_EQUAL_HEX16(0x82D7, pc_dshot_encode(1046, true, false));
}

void test_encode_bidirectional_inverts_crc(void)
{
    // Same value, bidirectional: crc = ~6 & 0xF = 9, frame = 0x82C9.
    TEST_ASSERT_EQUAL_HEX16(0x82C9, pc_dshot_encode(1046, false, true));
}

void test_value_masked_to_11_bits(void)
{
    // 0xF000 | 1046: the high bits are dropped to the 11-bit field -> same as 1046.
    TEST_ASSERT_EQUAL_HEX16(0x82C6, pc_dshot_encode((uint16_t)(1046 | 0xF800), false, false));
}

void test_decode_roundtrip_and_crc(void)
{
    uint16_t val = 0;
    bool tel = true;
    TEST_ASSERT_TRUE(pc_dshot_decode(0x82C6, &val, &tel, false));
    TEST_ASSERT_EQUAL_UINT16(1046, val);
    TEST_ASSERT_FALSE(tel);

    TEST_ASSERT_TRUE(pc_dshot_decode(0x82D7, &val, &tel, false));
    TEST_ASSERT_EQUAL_UINT16(1046, val);
    TEST_ASSERT_TRUE(tel);

    // A corrupted CRC nibble fails.
    TEST_ASSERT_FALSE(pc_dshot_decode(0x82C5, NULL, NULL, false));
    // Standard CRC is not valid when decoded as bidirectional.
    TEST_ASSERT_FALSE(pc_dshot_decode(0x82C6, NULL, NULL, true));
    TEST_ASSERT_TRUE(pc_dshot_decode(0x82C9, &val, NULL, true));
    TEST_ASSERT_EQUAL_UINT16(1046, val);
}

void test_decode_null_out_params(void)
{
    // A valid frame decodes successfully even when the caller doesn't want the value or telemetry bit
    // back (both out-parameters null), exercising the false side of the "if (value11)" / "if (telemetry)"
    // null-checks on a *successful* decode (the existing null-pointer cases all hit CRC failure first).
    TEST_ASSERT_TRUE(pc_dshot_decode(0x82C6, NULL, NULL, false));
}

void test_bit_timing(void)
{
    // 600 kbit: period 1667 ns; "1" ~3/4, "0" ~3/8.
    TEST_ASSERT_EQUAL_UINT32(1250, pc_dshot_bit_ns(600, true));
    TEST_ASSERT_EQUAL_UINT32(625, pc_dshot_bit_ns(600, false));
    TEST_ASSERT_EQUAL_UINT32(0, pc_dshot_bit_ns(999, true)); // unknown rate
}

void test_esc_pwm_mapping(void)
{
    // OneShot125: 125..250 us.
    TEST_ASSERT_EQUAL_UINT32(125000, pc_esc_pwm_ns(0, PC_ESC_ONESHOT125));
    TEST_ASSERT_EQUAL_UINT32(250000, pc_esc_pwm_ns(1000, PC_ESC_ONESHOT125));
    TEST_ASSERT_EQUAL_UINT32(187500, pc_esc_pwm_ns(500, PC_ESC_ONESHOT125));
    // Multishot: 5..25 us.
    TEST_ASSERT_EQUAL_UINT32(5000, pc_esc_pwm_ns(0, PC_ESC_MULTISHOT));
    TEST_ASSERT_EQUAL_UINT32(25000, pc_esc_pwm_ns(1000, PC_ESC_MULTISHOT));
    TEST_ASSERT_EQUAL_UINT32(15000, pc_esc_pwm_ns(500, PC_ESC_MULTISHOT));
    // Standard PWM 1..2 ms, and OneShot42 42..84 us.
    TEST_ASSERT_EQUAL_UINT32(1500000, pc_esc_pwm_ns(500, PC_ESC_PWM));
    TEST_ASSERT_EQUAL_UINT32(84000, pc_esc_pwm_ns(1000, PC_ESC_ONESHOT42));
    // Throttle clamps at 1000.
    TEST_ASSERT_EQUAL_UINT32(250000, pc_esc_pwm_ns(5000, PC_ESC_ONESHOT125));
}

void test_bit_ns_all_rates()
{
    // Each supported line rate maps to a non-zero bit period; an unknown rate is rejected.
    TEST_ASSERT_TRUE(pc_dshot_bit_ns(150, true) > 0);
    TEST_ASSERT_TRUE(pc_dshot_bit_ns(300, false) > 0);
    TEST_ASSERT_TRUE(pc_dshot_bit_ns(1200, true) > 0);
    TEST_ASSERT_EQUAL_UINT32(0, pc_dshot_bit_ns(999, true));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_known_vector);
    RUN_TEST(test_encode_telemetry_bit);
    RUN_TEST(test_encode_bidirectional_inverts_crc);
    RUN_TEST(test_value_masked_to_11_bits);
    RUN_TEST(test_decode_roundtrip_and_crc);
    RUN_TEST(test_decode_null_out_params);
    RUN_TEST(test_bit_timing);
    RUN_TEST(test_esc_pwm_mapping);
    RUN_TEST(test_bit_ns_all_rates);
    return UNITY_END();
}
