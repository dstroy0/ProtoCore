// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the VL53L0X time-of-flight ranging codec (server/peripherals/vl53l0x/vl53l0x.h).
//
// The register semantics are ST's, not this library's. ST's VL53L0X API extracts the device range
// status as `DeviceRangeStatusInternal = ((DeviceRangeStatus & 0x78) >> 3)` and treats the single
// value 11 as a good ranging - every other code is a hardware, phase, signal or min-range failure.
// test_range_status_is_bits_6_to_3 is therefore the load-bearing case: it walks all sixteen codes
// of that field and asserts 11 and only 11 reads as valid, so a mask or shift that is off by one
// bit cannot pass. The 16-bit distance at RESULT_RANGE_STATUS + 10 is read high octet first, which
// ST's own driver spells `readReg16Bit(RESULT_RANGE_STATUS + 10)`.

#include "server/peripherals/vl53l0x/vl53l0x.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The distance is the register pair at RESULT_RANGE_STATUS + 10 / + 11, high octet first.
void test_range_is_the_big_endian_register_pair(void)
{
    TEST_ASSERT_EQUAL_UINT16(0u, protocore_vl53l0x_range_mm(0x00, 0x00));
    // 0x04D2 = 4*256 + 13*16 + 2 = 1024 + 208 + 2 = 1234 mm
    TEST_ASSERT_EQUAL_UINT16(1234u, protocore_vl53l0x_range_mm(0x04, 0xD2));
    // the two octets are not interchangeable: swapping them names a different distance
    TEST_ASSERT_EQUAL_UINT16(0xD204u, protocore_vl53l0x_range_mm(0xD2, 0x04));
    // 8190 mm is the out-of-range value the part reports when nothing is in front of it
    TEST_ASSERT_EQUAL_UINT16(8190u, protocore_vl53l0x_range_mm(0x1F, 0xFE));
    TEST_ASSERT_EQUAL_UINT16(65535u, protocore_vl53l0x_range_mm(0xFF, 0xFF));
}

// Every high octet lands in bits 15:8 and every low octet in bits 7:0, over the whole range.
void test_range_octets_never_overlap(void)
{
    for (unsigned hi = 0; hi < 256u; hi += 17u)
    {
        for (unsigned lo = 0; lo < 256u; lo += 19u)
        {
            uint16_t mm = protocore_vl53l0x_range_mm((uint8_t)hi, (uint8_t)lo);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)hi, (uint8_t)(mm >> 8));
            TEST_ASSERT_EQUAL_UINT8((uint8_t)lo, (uint8_t)(mm & 0xFFu));
        }
    }
}

// ST's driver polls RESULT_INTERRUPT_STATUS until any of its low three bits is set; the bits above
// them are the interrupt configuration, not a data-ready report.
void test_data_ready_is_the_low_three_interrupt_bits(void)
{
    TEST_ASSERT_FALSE(protocore_vl53l0x_data_ready(0x00));
    TEST_ASSERT_TRUE(protocore_vl53l0x_data_ready(0x01));
    TEST_ASSERT_TRUE(protocore_vl53l0x_data_ready(0x02));
    TEST_ASSERT_TRUE(protocore_vl53l0x_data_ready(0x04));
    TEST_ASSERT_TRUE(protocore_vl53l0x_data_ready(0x07));
    // bits 3 and up on their own are not a data-ready report
    TEST_ASSERT_FALSE(protocore_vl53l0x_data_ready(0x08));
    TEST_ASSERT_FALSE(protocore_vl53l0x_data_ready(0x10));
    TEST_ASSERT_FALSE(protocore_vl53l0x_data_ready(0xF8));
    // a set low bit is still a report when the bits above it are set too
    TEST_ASSERT_TRUE(protocore_vl53l0x_data_ready(0xFF));
}

// ST: DeviceRangeStatusInternal = (RESULT_RANGE_STATUS & 0x78) >> 3, and 11 is the good ranging.
void test_range_status_is_bits_6_to_3(void)
{
    TEST_ASSERT_EQUAL_UINT8(11u, (uint8_t)VL53L0X_RANGE_VALID);

    for (unsigned code = 0; code < 16u; code++)
    {
        // the field sits in bits 6:3; bits 7, 2, 1 and 0 belong to other fields and must not leak in
        uint8_t reg = (uint8_t)((code << 3) | 0x87u);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)code, protocore_vl53l0x_range_status(reg));
        if (code == VL53L0X_RANGE_VALID)
        {
            TEST_ASSERT_TRUE(protocore_vl53l0x_range_valid(reg));
        }
        else
        {
            TEST_ASSERT_FALSE(protocore_vl53l0x_range_valid(reg));
        }
    }
}

// The named ST failure codes, spelled as the register octet a part would report.
void test_named_status_codes(void)
{
    TEST_ASSERT_EQUAL_UINT8(11u, protocore_vl53l0x_range_status(11u << 3)); // good ranging
    TEST_ASSERT_TRUE(protocore_vl53l0x_range_valid(11u << 3));
    TEST_ASSERT_EQUAL_UINT8(1u, protocore_vl53l0x_range_status(1u << 3)); // VCSEL continuity fail
    TEST_ASSERT_FALSE(protocore_vl53l0x_range_valid(1u << 3));
    TEST_ASSERT_EQUAL_UINT8(4u, protocore_vl53l0x_range_status(4u << 3)); // signal fail
    TEST_ASSERT_FALSE(protocore_vl53l0x_range_valid(4u << 3));
    TEST_ASSERT_EQUAL_UINT8(6u, protocore_vl53l0x_range_status(6u << 3)); // phase fail
    TEST_ASSERT_FALSE(protocore_vl53l0x_range_valid(6u << 3));
    TEST_ASSERT_EQUAL_UINT8(8u, protocore_vl53l0x_range_status(8u << 3)); // min range fail
    TEST_ASSERT_FALSE(protocore_vl53l0x_range_valid(8u << 3));
    // a zero register is not a valid measurement, which is the fail-closed case that matters: a
    // part that never answered leaves the buffer at zero
    TEST_ASSERT_FALSE(protocore_vl53l0x_range_valid(0x00));
}

// The documented register addresses and the identification value, from the part's register map.
void test_register_map(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, VL53L0X_REG_SYSRANGE_START);
    TEST_ASSERT_EQUAL_HEX8(0x0B, VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR);
    TEST_ASSERT_EQUAL_HEX8(0x13, VL53L0X_REG_RESULT_INTERRUPT_STATUS);
    TEST_ASSERT_EQUAL_HEX8(0x14, VL53L0X_REG_RESULT_RANGE_STATUS);
    TEST_ASSERT_EQUAL_HEX8(0xC0, VL53L0X_REG_IDENTIFICATION_MODEL_ID);
    TEST_ASSERT_EQUAL_HEX8(0xEE, VL53L0X_MODEL_ID);
}

// begin() reads IDENTIFICATION_MODEL_ID first and refuses anything that is not 0xEE, so a wrong
// part on the bus does not get ranging commands written into it.
void test_begin_refuses_a_wrong_model_id(void)
{
    protocore_bus_host_reset();
    static const uint8_t wrong[1] = {0xAA};
    protocore_bus_host_preload(wrong, sizeof(wrong));
    TEST_ASSERT_FALSE(protocore_vl53l0x_begin(0x29));

    // the only octet that went out is the model-id register address, no SYSRANGE_START behind it
    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(1u, n);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_IDENTIFICATION_MODEL_ID, tx[0]);
}

// With the right model id, begin arms continuous back-to-back ranging: SYSRANGE_START = 0x02.
void test_begin_arms_continuous_ranging(void)
{
    protocore_bus_host_reset();
    static const uint8_t right[1] = {VL53L0X_MODEL_ID};
    protocore_bus_host_preload(right, sizeof(right));
    TEST_ASSERT_TRUE(protocore_vl53l0x_begin(0x29));

    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(3u, n);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_IDENTIFICATION_MODEL_ID, tx[0]);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_SYSRANGE_START, tx[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, tx[2]);
}

// A read with no measurement pending answers false and reads no result registers.
void test_read_refuses_when_no_measurement_is_ready(void)
{
    protocore_bus_host_reset();
    static const uint8_t not_ready[1] = {0x00};
    protocore_bus_host_preload(not_ready, sizeof(not_ready));
    uint16_t mm = 0xFFFF;
    TEST_ASSERT_FALSE(protocore_vl53l0x_read_mm(&mm));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, mm); // untouched

    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(1u, n);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_RESULT_INTERRUPT_STATUS, tx[0]);
}

// A ready, valid measurement: the twelve result registers from RESULT_RANGE_STATUS, status 11 in
// bits 6:3 of the first, distance in the eleventh and twelfth. The interrupt is cleared after.
void test_read_takes_the_distance_from_offset_ten(void)
{
    protocore_bus_host_reset();
    // interrupt status, then RESULT_RANGE_STATUS[0..11]: 0x0000 = 1234 mm at +10 / +11
    static const uint8_t reply[13] = {
        0x04,            // RESULT_INTERRUPT_STATUS: a data-ready bit
        11u << 3,        // RESULT_RANGE_STATUS + 0: DeviceRangeStatus 11
        0, 0, 0, 0, 0, 0, 0, 0, 0, // + 1 .. + 9
        0x04, 0xD2,      // + 10 / + 11: 1234 mm
    };
    protocore_bus_host_preload(reply, sizeof(reply));

    uint16_t mm = 0;
    TEST_ASSERT_TRUE(protocore_vl53l0x_read_mm(&mm));
    TEST_ASSERT_EQUAL_UINT16(1234u, mm);

    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(4u, n);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_RESULT_INTERRUPT_STATUS, tx[0]);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_RESULT_RANGE_STATUS, tx[1]);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR, tx[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx[3]);
}

// A ready measurement whose status is not 11 answers false: the reading exists but the part says it
// is not trustworthy, and a caller that ignores that publishes a wrong distance.
void test_read_refuses_an_invalid_status(void)
{
    protocore_bus_host_reset();
    static const uint8_t reply[13] = {
        0x04,      // ready
        4u << 3,   // DeviceRangeStatus 4: signal fail
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0xD2,
    };
    protocore_bus_host_preload(reply, sizeof(reply));

    uint16_t mm = 0;
    TEST_ASSERT_FALSE(protocore_vl53l0x_read_mm(&mm));
}

// A null destination is refused rather than written through.
void test_read_refuses_a_null_destination(void)
{
    TEST_ASSERT_FALSE(protocore_vl53l0x_read_mm(NULL));
}
