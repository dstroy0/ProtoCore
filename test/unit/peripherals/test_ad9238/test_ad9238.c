// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the AD9238 SPI configuration-port codec (services/peripherals/ad9238): the 16-bit
// instruction word (R/W + byte-count + 13-bit address, MSB first), single-register
// write/read transaction framing, the device-update transfer, and the fail-closed
// input-validation paths. Pure host tests - no SPI transport, no real silicon.

#include "services/peripherals/ad9238/ad9238.h"
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_instruction_word_write_single_byte()
{
    uint8_t hdr[2];
    TEST_ASSERT_TRUE(pc_ad9238_build_instruction(PROTO_FALSE, 0x0009, 1, hdr));
    TEST_ASSERT_EQUAL_HEX8(0x00, hdr[0]); // R/W=0, W1:W0=00 (1 byte), addr hi
    TEST_ASSERT_EQUAL_HEX8(0x09, hdr[1]);
}

void test_instruction_word_read_sets_msb()
{
    uint8_t hdr[2];
    TEST_ASSERT_TRUE(pc_ad9238_build_instruction(PROTO_TRUE, 0x0001, 1, hdr));
    TEST_ASSERT_EQUAL_HEX8(0x80, hdr[0]); // R/W=1
    TEST_ASSERT_EQUAL_HEX8(0x01, hdr[1]);
}

void test_instruction_word_byte_count_field()
{
    uint8_t hdr[2];
    // streaming (W1:W0=11): word = R/W(0) | W1:W0(11) << 13 | addr(0x100) = 0x6000 | 0x0100 = 0x6100.
    TEST_ASSERT_TRUE(pc_ad9238_build_instruction(PROTO_FALSE, 0x0100, 4, hdr));
    TEST_ASSERT_EQUAL_HEX8(0x61, hdr[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, hdr[1]);
}

void test_instruction_word_rejects_bad_input()
{
    uint8_t hdr[2];
    TEST_ASSERT_FALSE(pc_ad9238_build_instruction(PROTO_FALSE, 0x00, 1, NULL));  // null out
    TEST_ASSERT_FALSE(pc_ad9238_build_instruction(PROTO_FALSE, 0x00, 0, hdr));   // nbytes 0
    TEST_ASSERT_FALSE(pc_ad9238_build_instruction(PROTO_FALSE, 0x00, 5, hdr));   // nbytes > 4
    TEST_ASSERT_FALSE(pc_ad9238_build_instruction(PROTO_FALSE, 0x2000, 1, hdr)); // addr > 13 bits
    TEST_ASSERT_TRUE(pc_ad9238_build_instruction(PROTO_FALSE, 0x1FFF, 1, hdr));  // max valid addr
}

void test_build_write_transaction()
{
    uint8_t out[3] = {0};
    TEST_ASSERT_EQUAL_size_t(3, pc_ad9238_build_write((uint16_t)AD9238_REG_POWER_DOWN, 0x01, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x09, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[2]);

    TEST_ASSERT_EQUAL_size_t(0, pc_ad9238_build_write(0x09, 0x01, NULL, 3));  // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_ad9238_build_write(0x09, 0x01, out, 2));   // undersized cap
    TEST_ASSERT_EQUAL_size_t(0, pc_ad9238_build_write(0x2000, 0x01, out, 3)); // reg_addr past 13-bit field
}

void test_build_read_transaction()
{
    uint8_t out[2] = {0};
    TEST_ASSERT_EQUAL_size_t(2, pc_ad9238_build_read((uint16_t)AD9238_REG_CHIP_ID, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x80, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[1]);

    TEST_ASSERT_EQUAL_size_t(0, pc_ad9238_build_read(0x01, NULL, 2));  // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_ad9238_build_read(0x01, out, 1));   // undersized cap
    TEST_ASSERT_EQUAL_size_t(0, pc_ad9238_build_read(0x2000, out, 2)); // reg_addr past 13-bit field
}

void test_build_transfer_writes_device_update()
{
    uint8_t out[3] = {0};
    TEST_ASSERT_EQUAL_size_t(3, pc_ad9238_build_transfer(out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[1]); // AD9238_REG_DEVICE_UPDATE
    TEST_ASSERT_EQUAL_HEX8(0x01, out[2]); // SW transfer bit
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_instruction_word_write_single_byte);
    RUN_TEST(test_instruction_word_read_sets_msb);
    RUN_TEST(test_instruction_word_byte_count_field);
    RUN_TEST(test_instruction_word_rejects_bad_input);
    RUN_TEST(test_build_write_transaction);
    RUN_TEST(test_build_read_transaction);
    RUN_TEST(test_build_transfer_writes_device_update);
    return UNITY_END();
}
