// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SMBus 3.1 Packet Error Code. The PEC covers every byte of a transaction including the address
// bytes and their direction bits, which is the part a driver gets wrong, so the vectors here are
// built the way the specification lays a transaction out rather than from the payload alone.

#include "services/peripherals/smbus.h"
#include "shared_primitives/crc.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// The address byte is the 7-bit address shifted up with the direction in bit 0.
static void test_addr_byte(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x50, pc_smbus_addr_byte(0x28, PC_SMBUS_WRITE));
    TEST_ASSERT_EQUAL_HEX8(0x51, pc_smbus_addr_byte(0x28, PC_SMBUS_READ));
    TEST_ASSERT_EQUAL_HEX8(0x00, pc_smbus_addr_byte(0x00, PC_SMBUS_WRITE));
    TEST_ASSERT_EQUAL_HEX8(0xFF, pc_smbus_addr_byte(0x7F, PC_SMBUS_READ));
    // Bits above the 7-bit address are dropped rather than shifted into the direction bit.
    TEST_ASSERT_EQUAL_HEX8(0x50, pc_smbus_addr_byte(0xA8, PC_SMBUS_WRITE));
}

// A write PEC is the CRC over the write address byte followed by the payload. Computing the same
// sequence through the shared engine directly is what pins the helper to the catalogue entry.
static void test_pec_write_matches_engine(void)
{
    const uint8_t payload[] = {0x04, 0x12, 0x34};
    uint8_t seq[4] = {pc_smbus_addr_byte(0x2A, PC_SMBUS_WRITE), 0x04, 0x12, 0x34};
    TEST_ASSERT_EQUAL_HEX8((uint8_t)pc_crc(&PC_CRC8_SMBUS, seq, sizeof(seq)),
                           pc_smbus_pec_write(0x2A, payload, sizeof(payload)));
}

// A read PEC spans both halves: the write address, the command, the read address, then the data.
static void test_pec_read_matches_engine(void)
{
    const uint8_t sent[] = {0x08};
    const uint8_t got[] = {0xAB, 0xCD};
    uint8_t seq[5] = {pc_smbus_addr_byte(0x2A, PC_SMBUS_WRITE), 0x08,
                      pc_smbus_addr_byte(0x2A, PC_SMBUS_READ), 0xAB, 0xCD};
    TEST_ASSERT_EQUAL_HEX8((uint8_t)pc_crc(&PC_CRC8_SMBUS, seq, sizeof(seq)),
                           pc_smbus_pec_read(0x2A, sent, sizeof(sent), got, sizeof(got)));
}

// A receive byte has no command going out, so the PEC covers the read address and the data only.
static void test_pec_read_no_command(void)
{
    const uint8_t got[] = {0x5A};
    uint8_t seq[3] = {pc_smbus_addr_byte(0x2A, PC_SMBUS_WRITE), pc_smbus_addr_byte(0x2A, PC_SMBUS_READ), 0x5A};
    TEST_ASSERT_EQUAL_HEX8((uint8_t)pc_crc(&PC_CRC8_SMBUS, seq, sizeof(seq)),
                           pc_smbus_pec_read(0x2A, NULL, 0, got, sizeof(got)));
}

// The address is part of the checksum, so the same payload to a different device differs.
static void test_pec_depends_on_address(void)
{
    const uint8_t payload[] = {0x01, 0x02};
    TEST_ASSERT_NOT_EQUAL(pc_smbus_pec_write(0x2A, payload, sizeof(payload)),
                          pc_smbus_pec_write(0x2B, payload, sizeof(payload)));
}

// The direction bit is part of the checksum, so a read and a write over the same bytes differ.
static void test_pec_depends_on_direction(void)
{
    const uint8_t one[] = {0x77};
    TEST_ASSERT_NOT_EQUAL(pc_smbus_pec_write(0x2A, one, 1), pc_smbus_pec_read(0x2A, NULL, 0, one, 1));
}

// A payload of no bytes still checksums the address byte.
static void test_pec_empty_payload(void)
{
    uint8_t a = pc_smbus_addr_byte(0x2A, PC_SMBUS_WRITE);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)pc_crc(&PC_CRC8_SMBUS, &a, 1), pc_smbus_pec_write(0x2A, NULL, 0));
}

// The flag is off until it is asked for, and it survives being set both ways.
static void test_pec_flag(void)
{
    pc_smbus_set_pec(PROTO_FALSE);
    TEST_ASSERT_FALSE(pc_smbus_pec_enabled());
    pc_smbus_set_pec(PROTO_TRUE);
    TEST_ASSERT_TRUE(pc_smbus_pec_enabled());
    pc_smbus_set_pec(PROTO_FALSE);
    TEST_ASSERT_FALSE(pc_smbus_pec_enabled());
}

// A host build has no bus, so every transfer refuses rather than pretending to have run.
static void test_host_transfers_refuse(void)
{
    uint8_t b = 0;
    uint16_t w = 0;
    size_t n = 1;
    uint8_t blk[4] = {0};
    TEST_ASSERT_FALSE(pc_smbus_begin());
    TEST_ASSERT_FALSE(pc_smbus_quick(0x2A, PC_SMBUS_WRITE));
    TEST_ASSERT_FALSE(pc_smbus_send_byte(0x2A, 0x01));
    TEST_ASSERT_FALSE(pc_smbus_receive_byte(0x2A, &b));
    TEST_ASSERT_FALSE(pc_smbus_write_byte(0x2A, 0x00, 0x01));
    TEST_ASSERT_FALSE(pc_smbus_read_byte(0x2A, 0x00, &b));
    TEST_ASSERT_FALSE(pc_smbus_write_word(0x2A, 0x00, 0x1234));
    TEST_ASSERT_FALSE(pc_smbus_read_word(0x2A, 0x00, &w));
    TEST_ASSERT_FALSE(pc_smbus_write_block(0x2A, 0x00, blk, sizeof(blk)));
    TEST_ASSERT_FALSE(pc_smbus_read_block(0x2A, 0x00, blk, sizeof(blk), &n));
    TEST_ASSERT_FALSE(pc_smbus_process_call(0x2A, 0x00, 0x1234, &w));
    TEST_ASSERT_FALSE(pc_smbus_block_process_call(0x2A, 0x00, blk, 2, blk, sizeof(blk), &n));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_addr_byte);
    RUN_TEST(test_pec_write_matches_engine);
    RUN_TEST(test_pec_read_matches_engine);
    RUN_TEST(test_pec_read_no_command);
    RUN_TEST(test_pec_depends_on_address);
    RUN_TEST(test_pec_depends_on_direction);
    RUN_TEST(test_pec_empty_payload);
    RUN_TEST(test_pec_flag);
    RUN_TEST(test_host_transfers_refuse);
    return UNITY_END();
}
