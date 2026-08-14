// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/peripherals/ad9238/ad9238.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_an877_instruction_phase_bit_fields(void)
{
    struct
    {
        proto_bool read;
        uint16_t addr;
        uint8_t nbytes;
        uint16_t want;
    } static const CASES[] = {

        {PROTO_FALSE, 0x0000u, 1u, 0x0000u},

        {PROTO_TRUE, 0x0000u, 1u, 0x8000u},

        {PROTO_FALSE, 0x0001u, 1u, 0x0001u},

        {PROTO_TRUE, 0x00FFu, 1u, 0x80FFu},

        {PROTO_FALSE, 0x0000u, 2u, 0x2000u},

        {PROTO_FALSE, 0x0000u, 3u, 0x4000u},

        {PROTO_FALSE, 0x0000u, 4u, 0x6000u},

        {PROTO_TRUE, 0x1FFFu, 4u, 0xFFFFu},

        {PROTO_FALSE, 0x1FFFu, 1u, 0x1FFFu},
        {PROTO_FALSE, 0x1000u, 1u, 0x1000u},
    };

    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t w[2] = {0xA5u, 0x5Au};
        TEST_ASSERT_TRUE(protocore_ad9238_build_instruction(CASES[i].read, CASES[i].addr, CASES[i].nbytes, w));

        TEST_ASSERT_EQUAL_HEX8((uint8_t)(CASES[i].want >> 8), w[0]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(CASES[i].want & 0xFFu), w[1]);
    }
}

void test_an877_word_length_field_is_nbytes_minus_one(void)
{
    for (uint8_t n = 1u; n <= 4u; n++)
    {
        uint8_t w[2];
        TEST_ASSERT_TRUE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x0123u, n, w));
        uint16_t word = (uint16_t)(((uint16_t)w[0] << 8) | w[1]);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)(n - 1u), (uint16_t)((word >> 13) & 0x3u));
        TEST_ASSERT_EQUAL_HEX16(0x0123u, (uint16_t)(word & 0x1FFFu));
    }
}

void test_write_is_instruction_plus_one_data_byte(void)
{
    uint8_t out[4] = {0u, 0u, 0u, 0xEEu};
    TEST_ASSERT_EQUAL_size_t(3u, protocore_ad9238_build_write((uint16_t)AD9238_REG_OUTPUT_MODE,
                                                              (uint8_t)AD9238_FORMAT_TWOS_COMPLEMENT, out,
                                                              sizeof(out)));

    TEST_ASSERT_EQUAL_HEX8(0x00u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0Au, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, out[3]);
}

void test_read_is_the_two_octet_instruction_alone(void)
{
    uint8_t out[3] = {0u, 0u, 0xEEu};

    TEST_ASSERT_EQUAL_size_t(2u, protocore_ad9238_build_read((uint16_t)AD9238_REG_CHIP_ID, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x80u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, out[2]);
}

void test_device_update_is_a_write_of_bit0_to_0x0ff(void)
{
    uint8_t out[3];
    TEST_ASSERT_EQUAL_size_t(3u, protocore_ad9238_build_transfer(out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[2]);
    TEST_ASSERT_EQUAL_HEX16(0x00FFu, (uint16_t)AD9238_REG_DEVICE_UPDATE);
}

void test_addresses_wider_than_thirteen_bits_are_refused(void)
{
    uint8_t w[2];
    TEST_ASSERT_TRUE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x1FFFu, 1u, w));
    TEST_ASSERT_FALSE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x2000u, 1u, w));
    TEST_ASSERT_FALSE(protocore_ad9238_build_instruction(PROTO_TRUE, 0xFFFFu, 1u, w));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_write(0x2000u, 0x00u, w, 3u));
}

void test_byte_counts_outside_one_to_four_are_refused(void)
{
    uint8_t w[2];
    TEST_ASSERT_FALSE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x0000u, 0u, w));
    TEST_ASSERT_FALSE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x0000u, 5u, w));
    TEST_ASSERT_FALSE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x0000u, 255u, w));
}

void test_null_and_undersized_buffers_fail_closed(void)
{
    uint8_t out[3];
    TEST_ASSERT_FALSE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x0000u, 1u, NULL));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_write(0x0009u, 0x01u, NULL, 3u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_write(0x0009u, 0x01u, out, 2u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_read(0x0009u, NULL, 2u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_read(0x0009u, out, 1u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_transfer(NULL, 3u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_transfer(out, 2u));
}

void test_read_and_write_of_a_register_differ_only_in_the_rw_bit(void)
{
    for (uint16_t addr = 0u; addr <= 0x1FFFu; addr = (uint16_t)(addr + 0x0111u))
    {
        uint8_t r[2];
        uint8_t w[2];
        TEST_ASSERT_TRUE(protocore_ad9238_build_instruction(PROTO_TRUE, addr, 1u, r));
        TEST_ASSERT_TRUE(protocore_ad9238_build_instruction(PROTO_FALSE, addr, 1u, w));
        uint16_t rw = (uint16_t)(((uint16_t)r[0] << 8) | r[1]);
        uint16_t ww = (uint16_t)(((uint16_t)w[0] << 8) | w[1]);
        TEST_ASSERT_EQUAL_HEX16(0x8000u, (uint16_t)(rw ^ ww));
    }
}

void test_named_registers_all_encode(void)
{
    static const uint16_t REG[] = {
        (uint16_t)AD9238_REG_CHIP_PORT_CONFIG, (uint16_t)AD9238_REG_CHIP_ID,     (uint16_t)AD9238_REG_CHIP_GRADE,
        (uint16_t)AD9238_REG_CHANNEL_INDEX,    (uint16_t)AD9238_REG_POWER_DOWN,  (uint16_t)AD9238_REG_OUTPUT_MODE,
        (uint16_t)AD9238_REG_OUTPUT_PHASE,     (uint16_t)AD9238_REG_OUTPUT_DELAY, (uint16_t)AD9238_REG_VREF,
        (uint16_t)AD9238_REG_ANALOG_INPUT,     (uint16_t)AD9238_REG_TEST_IO,     (uint16_t)AD9238_REG_OFFSET_ADJUST,
        (uint16_t)AD9238_REG_DEVICE_UPDATE,
    };
    for (size_t i = 0; i < sizeof(REG) / sizeof(REG[0]); i++)
    {
        uint8_t out[3];
        TEST_ASSERT_TRUE(REG[i] <= 0x1FFFu);
        TEST_ASSERT_EQUAL_size_t(3u, protocore_ad9238_build_write(REG[i], 0x00u, out, sizeof(out)));

        uint16_t word = (uint16_t)(((uint16_t)out[0] << 8) | out[1]);
        TEST_ASSERT_EQUAL_HEX16(REG[i], (uint16_t)(word & 0x1FFFu));
    }
}
