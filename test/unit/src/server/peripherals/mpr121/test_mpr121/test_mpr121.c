// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NXP MPR121 capacitive-touch codec (server/peripherals/mpr121/mpr121.h).
//
// Expected values come from the MPR121 datasheet (rev 4): Table 2 for the register map, section 5.2
// for the two touch-status registers, section 5.11 and Table 9 for the Electrode Configuration
// Register's CL / ELEPROX_EN / ELE_EN fields, section 5.13 for the soft reset, and section 5.1 for
// the rule that fixes the write order.
//
// test_datasheet_status_register_bit_positions is the load-bearing case. Section 5.2 puts OVCF at
// D7 and ELEPROX at D4 of register 0x01, with D6 and D5 unused, so a decoder that treats the high
// register as four more electrodes reports electrode 12 or 15 as touched on a wiring fault.

#include "server/peripherals/mpr121/mpr121.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Section 5.2: register 0x00 is D7..D0 = ELE7..ELE0 and register 0x01 is
// D7 OVCF | D6 - | D5 - | D4 ELEPROX | D3..D0 = ELE11..ELE8.
void test_datasheet_status_register_bit_positions(void)
{
    // every electrode, and nothing else in the two registers.
    TEST_ASSERT_EQUAL_HEX16(0x0FFF, protocore_mpr121_touched(0xFF, 0x0F));
    TEST_ASSERT_EQUAL_HEX16(0x0000, protocore_mpr121_touched(0x00, 0x00));

    // one electrode at a time, low register.
    TEST_ASSERT_EQUAL_HEX16(0x0001, protocore_mpr121_touched(0x01, 0x00)); // ELE0
    TEST_ASSERT_EQUAL_HEX16(0x0080, protocore_mpr121_touched(0x80, 0x00)); // ELE7
    // and high register: ELE8 is D0 of 0x01, so bit 8 of the mask.
    TEST_ASSERT_EQUAL_HEX16(0x0100, protocore_mpr121_touched(0x00, 0x01)); // ELE8
    TEST_ASSERT_EQUAL_HEX16(0x0800, protocore_mpr121_touched(0x00, 0x08)); // ELE11

    // ELEPROX (D4) and OVCF (D7) are not electrodes and never appear in the mask.
    TEST_ASSERT_EQUAL_HEX16(0x0000, protocore_mpr121_touched(0x00, 0x10));
    TEST_ASSERT_EQUAL_HEX16(0x0000, protocore_mpr121_touched(0x00, 0x80));
    TEST_ASSERT_EQUAL_HEX16(0x0000, protocore_mpr121_touched(0x00, 0x90));
    // the unused D6 / D5 do not leak in either.
    TEST_ASSERT_EQUAL_HEX16(0x0000, protocore_mpr121_touched(0x00, 0x60));
}

// The same two bits, read as their own flags.
void test_proximity_and_overcurrent_flags(void)
{
    TEST_ASSERT_TRUE(protocore_mpr121_proximity(0x10));
    TEST_ASSERT_FALSE(protocore_mpr121_proximity(0x0F)); // the four electrode bits are not proximity
    TEST_ASSERT_FALSE(protocore_mpr121_proximity(0x80)); // nor is the over-current flag

    TEST_ASSERT_TRUE(protocore_mpr121_overcurrent(0x80));
    TEST_ASSERT_FALSE(protocore_mpr121_overcurrent(0x7F));
    // section 5.2: an over-current also clears the electrode bits, so both can be read from one byte
    TEST_ASSERT_TRUE(protocore_mpr121_overcurrent(0x90));
    TEST_ASSERT_TRUE(protocore_mpr121_proximity(0x90));
}

// Bit i of a mask from protocore_mpr121_touched is electrode i, for the twelve the part has.
void test_is_touched_is_bounded_to_twelve_electrodes(void)
{
    const uint16_t all = protocore_mpr121_touched(0xFF, 0x0F);
    for (uint8_t e = 0; e < MPR121_ELECTRODES; e++)
    {
        TEST_ASSERT_TRUE(protocore_mpr121_is_touched(all, e));
        TEST_ASSERT_FALSE(protocore_mpr121_is_touched(0x0000, e));
    }
    // 12 and 15 are the proximity and over-current bit positions of the raw status word: not
    // electrodes, and refused rather than answered from whatever the mask happens to hold.
    TEST_ASSERT_FALSE(protocore_mpr121_is_touched(0xFFFF, 12));
    TEST_ASSERT_FALSE(protocore_mpr121_is_touched(0xFFFF, 15));
    TEST_ASSERT_FALSE(protocore_mpr121_is_touched(0xFFFF, 255));

    TEST_ASSERT_EQUAL_INT(12, MPR121_ELECTRODES);
}

// Table 2: electrode filtered data is a 10-bit value split LSB (0x04) then MSB (0x05), so the
// two bits above it in the MSB register are not part of the reading.
void test_filtered_data_is_ten_bits(void)
{
    TEST_ASSERT_EQUAL_UINT16(0x03FF, protocore_mpr121_word10(0xFF, 0x03));
    TEST_ASSERT_EQUAL_UINT16(0x0000, protocore_mpr121_word10(0x00, 0x00));
    TEST_ASSERT_EQUAL_UINT16(0x0100, protocore_mpr121_word10(0x00, 0x01));
    TEST_ASSERT_EQUAL_UINT16(0x00AB, protocore_mpr121_word10(0xAB, 0x00));
    // anything above bit 9 belongs to no field and is dropped.
    TEST_ASSERT_EQUAL_UINT16(0x03FF, protocore_mpr121_word10(0xFF, 0xFF));
}

// The bring-up writes the registers Table 2 names, at their own addresses. Section 5.13 fixes the
// soft reset (0x80 <- 0x63) and section 5.1 fixes the order: register writes are only accepted in
// Stop Mode, and the ECR is what leaves Stop Mode, so the ECR-run pair must be written last.
void test_build_init_writes_the_datasheet_registers(void)
{
    uint8_t buf[MPR121_INIT_MAX];
    const size_t n = protocore_mpr121_build_init(buf, sizeof(buf), MPR121_ELECTRODES, 12, 6);
    TEST_ASSERT_EQUAL_size_t(MPR121_INIT_MAX, n);
    TEST_ASSERT_EQUAL_size_t(0, n % 2); // (register, value) pairs

    // section 5.13 soft reset, then the ECR written to zero to guarantee Stop Mode.
    TEST_ASSERT_EQUAL_HEX8(0x80, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x63, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x5E, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]);

    // Table 2, the eleven baseline-filter registers, in address order 0x2B..0x35.
    static const uint8_t FILTER[11] = {0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35};
    for (size_t i = 0; i < 11; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(FILTER[i], buf[4 + i * 2]);
    }

    // Table 2: ELE0 touch threshold 0x41, ELE0 release 0x42, and a pair every two addresses after.
    const size_t thr = 4 + 11 * 2;
    for (uint8_t e = 0; e < MPR121_ELECTRODES; e++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x41 + 2 * e, buf[thr + e * 4]);
        TEST_ASSERT_EQUAL_HEX8(12, buf[thr + e * 4 + 1]);
        TEST_ASSERT_EQUAL_HEX8(0x42 + 2 * e, buf[thr + e * 4 + 2]);
        TEST_ASSERT_EQUAL_HEX8(6, buf[thr + e * 4 + 3]);
    }

    // debounce 0x5B, then the two filter/global configuration registers 0x5C and 0x5D.
    const size_t tail = thr + MPR121_ELECTRODES * 4;
    TEST_ASSERT_EQUAL_HEX8(0x5B, buf[tail]);
    TEST_ASSERT_EQUAL_HEX8(0x5C, buf[tail + 2]);
    TEST_ASSERT_EQUAL_HEX8(0x5D, buf[tail + 4]);

    // and the ECR last, which is the write that enters Run Mode.
    TEST_ASSERT_EQUAL_HEX8(0x5E, buf[n - 2]);
}

// Table 9: ECR is CL[7:6] | ELEPROX_EN[5:4] | ELE_EN[3:0]. The emitted value asks for baseline
// tracking with the initial baseline loaded from the first measurement (CL = b10), proximity
// detection off (b00), and exactly the electrodes the caller asked for.
void test_ecr_encodes_the_datasheet_fields(void)
{
    static const uint8_t COUNTS[3] = {1, 4, MPR121_ELECTRODES};
    for (size_t i = 0; i < 3; i++)
    {
        uint8_t buf[MPR121_INIT_MAX];
        const size_t n = protocore_mpr121_build_init(buf, sizeof(buf), COUNTS[i], 12, 6);
        TEST_ASSERT_TRUE(n > 0);
        const uint8_t ecr = buf[n - 1];
        TEST_ASSERT_EQUAL_UINT8(0x02, (ecr >> 6) & 0x03u); // CL = b10
        TEST_ASSERT_EQUAL_UINT8(0x00, (ecr >> 4) & 0x03u); // ELEPROX_EN = b00
        TEST_ASSERT_EQUAL_UINT8(COUNTS[i], ecr & 0x0Fu);   // ELE_EN
        TEST_ASSERT_TRUE((ecr & 0x3Fu) != 0);              // non-zero, so it is Run Mode
    }
}

// Fewer electrodes is a shorter sequence, by exactly the two threshold pairs each one costs.
void test_build_init_length_tracks_the_electrode_count(void)
{
    uint8_t full[MPR121_INIT_MAX];
    uint8_t four[MPR121_INIT_MAX];
    const size_t n12 = protocore_mpr121_build_init(full, sizeof(full), 12, 12, 6);
    const size_t n4 = protocore_mpr121_build_init(four, sizeof(four), 4, 12, 6);
    TEST_ASSERT_EQUAL_size_t(n12 - 8 * 4, n4);
}

// A half-written bring-up would leave the part in Stop Mode with a partial configuration, so a
// sequence is emitted whole or not at all.
void test_build_init_refuses_bad_arguments(void)
{
    uint8_t buf[MPR121_INIT_MAX];
    TEST_ASSERT_EQUAL_size_t(0, protocore_mpr121_build_init(NULL, MPR121_INIT_MAX, 12, 12, 6));
    TEST_ASSERT_EQUAL_size_t(0, protocore_mpr121_build_init(buf, sizeof(buf), 0, 12, 6));
    TEST_ASSERT_EQUAL_size_t(0, protocore_mpr121_build_init(buf, sizeof(buf), MPR121_ELECTRODES + 1, 12, 6));
    TEST_ASSERT_EQUAL_size_t(0, protocore_mpr121_build_init(buf, MPR121_INIT_MAX - 1, 12, 12, 6));
    TEST_ASSERT_EQUAL_size_t(0, protocore_mpr121_build_init(buf, 0, 12, 12, 6));
}
