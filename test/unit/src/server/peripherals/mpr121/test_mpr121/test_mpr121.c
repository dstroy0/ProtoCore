// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

// The bus cases at the bottom drive a datasheet model of the part (test/core_setup/hal/host/devices/
// mpr121_device.h) rather than asserting the bytes that went out: section 5.1 discards a register
// write unless the part is in Stop Mode, so a bring-up that emits the right bytes in the wrong
// order leaves the part unconfigured, and only reading the registers back shows it.

#include "server/peripherals/mpr121/mpr121.h"

#include "devices/mpr121_device.h"

#include <unity.h>

static protocore_mpr121_dev s_part;

void setUp(void)
{
    protocore_bus_host_reset();
    protocore_bus_host_detach_all();
    protocore_mpr121_dev_place(&s_part, (uint16_t)PROTOCORE_MPR121_I2C_ADDR);
}
void tearDown(void)
{
    protocore_bus_host_detach_all();
}

// Section 5.2: register 0x00 is D7..D0 = ELE7..ELE0 and register 0x01 is
// D7 OVCF | D6 - | D5 - | D4 ELEPROX | D3..D0 = ELE11..ELE8.
void test_datasheet_status_register_bit_positions(void)
{
    // every electrode, and nothing else in the two registers.
    Mpr121.touched_args.status_lo = 0xFF;
    Mpr121.touched_args.status_hi = 0x0F;
    Mpr121.touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0FFF, Mpr121.value);
    Mpr121.touched_args.status_lo = 0x00;
    Mpr121.touched_args.status_hi = 0x00;
    Mpr121.touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0000, Mpr121.value);

    // one electrode at a time, low register.
    Mpr121.touched_args.status_lo = 0x01;
    Mpr121.touched_args.status_hi = 0x00;
    Mpr121.touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0001, Mpr121.value); // ELE0
    Mpr121.touched_args.status_lo = 0x80;
    Mpr121.touched_args.status_hi = 0x00;
    Mpr121.touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0080, Mpr121.value); // ELE7
    // and high register: ELE8 is D0 of 0x01, so bit 8 of the mask.
    Mpr121.touched_args.status_lo = 0x00;
    Mpr121.touched_args.status_hi = 0x01;
    Mpr121.touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0100, Mpr121.value); // ELE8
    Mpr121.touched_args.status_lo = 0x00;
    Mpr121.touched_args.status_hi = 0x08;
    Mpr121.touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0800, Mpr121.value); // ELE11

    // ELEPROX (D4) and OVCF (D7) are not electrodes and never appear in the mask.
    Mpr121.touched_args.status_lo = 0x00;
    Mpr121.touched_args.status_hi = 0x10;
    Mpr121.touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0000, Mpr121.value);
    Mpr121.touched_args.status_lo = 0x00;
    Mpr121.touched_args.status_hi = 0x80;
    Mpr121.touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0000, Mpr121.value);
    Mpr121.touched_args.status_lo = 0x00;
    Mpr121.touched_args.status_hi = 0x90;
    Mpr121.touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0000, Mpr121.value);
    // the unused D6 / D5 do not leak in either.
    Mpr121.touched_args.status_lo = 0x00;
    Mpr121.touched_args.status_hi = 0x60;
    Mpr121.touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0000, Mpr121.value);
}

// The same two bits, read as their own flags.
void test_proximity_and_overcurrent_flags(void)
{
    Mpr121.proximity_args.status_hi = 0x10;
    Mpr121.proximity(protocore_mpr121_span());
    TEST_ASSERT_TRUE(Mpr121.ok);
    Mpr121.proximity_args.status_hi = 0x0F;
    Mpr121.proximity(protocore_mpr121_span());
    TEST_ASSERT_FALSE(Mpr121.ok); // the four electrode bits are not proximity
    Mpr121.proximity_args.status_hi = 0x80;
    Mpr121.proximity(protocore_mpr121_span());
    TEST_ASSERT_FALSE(Mpr121.ok); // nor is the over-current flag

    Mpr121.overcurrent_args.status_hi = 0x80;
    Mpr121.overcurrent(protocore_mpr121_span());
    TEST_ASSERT_TRUE(Mpr121.ok);
    Mpr121.overcurrent_args.status_hi = 0x7F;
    Mpr121.overcurrent(protocore_mpr121_span());
    TEST_ASSERT_FALSE(Mpr121.ok);
    // section 5.2: an over-current also clears the electrode bits, so both can be read from one byte
    Mpr121.overcurrent_args.status_hi = 0x90;
    Mpr121.overcurrent(protocore_mpr121_span());
    TEST_ASSERT_TRUE(Mpr121.ok);
    Mpr121.proximity_args.status_hi = 0x90;
    Mpr121.proximity(protocore_mpr121_span());
    TEST_ASSERT_TRUE(Mpr121.ok);
}

// Bit i of a mask from protocore_mpr121_touched is electrode i, for the twelve the part has.
void test_is_touched_is_bounded_to_twelve_electrodes(void)
{
    Mpr121.touched_args.status_lo = 0xFF;
    Mpr121.touched_args.status_hi = 0x0F;
    Mpr121.touched(protocore_mpr121_span());
    const uint16_t all = Mpr121.value;
    for (uint8_t e = 0; e < MPR121_ELECTRODES; e++)
    {
        Mpr121.is_touched_args.mask = all;
        Mpr121.is_touched_args.e = e;
        Mpr121.is_touched(protocore_mpr121_span());
        TEST_ASSERT_TRUE(Mpr121.ok);
        Mpr121.is_touched_args.mask = 0x0000;
        Mpr121.is_touched_args.e = e;
        Mpr121.is_touched(protocore_mpr121_span());
        TEST_ASSERT_FALSE(Mpr121.ok);
    }
    // 12 and 15 are the proximity and over-current bit positions of the raw status word: not
    // electrodes, and refused rather than answered from whatever the mask happens to hold.
    Mpr121.is_touched_args.mask = 0xFFFF;
    Mpr121.is_touched_args.e = 12;
    Mpr121.is_touched(protocore_mpr121_span());
    TEST_ASSERT_FALSE(Mpr121.ok);
    Mpr121.is_touched_args.mask = 0xFFFF;
    Mpr121.is_touched_args.e = 15;
    Mpr121.is_touched(protocore_mpr121_span());
    TEST_ASSERT_FALSE(Mpr121.ok);
    Mpr121.is_touched_args.mask = 0xFFFF;
    Mpr121.is_touched_args.e = 255;
    Mpr121.is_touched(protocore_mpr121_span());
    TEST_ASSERT_FALSE(Mpr121.ok);

    TEST_ASSERT_EQUAL_INT(12, MPR121_ELECTRODES);
}

// Table 2: electrode filtered data is a 10-bit value split LSB (0x04) then MSB (0x05), so the
// two bits above it in the MSB register are not part of the reading.
void test_filtered_data_is_ten_bits(void)
{
    Mpr121.word10_args.lsb = 0xFF;
    Mpr121.word10_args.msb = 0x03;
    Mpr121.word10(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_UINT16(0x03FF, Mpr121.value);
    Mpr121.word10_args.lsb = 0x00;
    Mpr121.word10_args.msb = 0x00;
    Mpr121.word10(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_UINT16(0x0000, Mpr121.value);
    Mpr121.word10_args.lsb = 0x00;
    Mpr121.word10_args.msb = 0x01;
    Mpr121.word10(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_UINT16(0x0100, Mpr121.value);
    Mpr121.word10_args.lsb = 0xAB;
    Mpr121.word10_args.msb = 0x00;
    Mpr121.word10(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_UINT16(0x00AB, Mpr121.value);
    // anything above bit 9 belongs to no field and is dropped.
    Mpr121.word10_args.lsb = 0xFF;
    Mpr121.word10_args.msb = 0xFF;
    Mpr121.word10(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_UINT16(0x03FF, Mpr121.value);
}

// The bring-up writes the registers Table 2 names, at their own addresses. Section 5.13 fixes the
// soft reset (0x80 <- 0x63) and section 5.1 fixes the order: register writes are only accepted in
// Stop Mode, and the ECR is what leaves Stop Mode, so the ECR-run pair must be written last.
void test_build_init_writes_the_datasheet_registers(void)
{
    uint8_t buf[MPR121_INIT_MAX];
    Mpr121.build_init_args.buf = buf;
    Mpr121.build_init_args.cap = sizeof(buf);
    Mpr121.build_init_args.n_electrodes = MPR121_ELECTRODES;
    Mpr121.build_init_args.touch_thr = 12;
    Mpr121.build_init_args.release_thr = 6;
    Mpr121.build_init(protocore_mpr121_span());
    const size_t n = Mpr121.n;
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
        Mpr121.build_init_args.buf = buf;
        Mpr121.build_init_args.cap = sizeof(buf);
        Mpr121.build_init_args.n_electrodes = COUNTS[i];
        Mpr121.build_init_args.touch_thr = 12;
        Mpr121.build_init_args.release_thr = 6;
        Mpr121.build_init(protocore_mpr121_span());
        const size_t n = Mpr121.n;
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
    Mpr121.build_init_args.buf = full;
    Mpr121.build_init_args.cap = sizeof(full);
    Mpr121.build_init_args.n_electrodes = 12;
    Mpr121.build_init_args.touch_thr = 12;
    Mpr121.build_init_args.release_thr = 6;
    Mpr121.build_init(protocore_mpr121_span());
    const size_t n12 = Mpr121.n;
    Mpr121.build_init_args.buf = four;
    Mpr121.build_init_args.cap = sizeof(four);
    Mpr121.build_init_args.n_electrodes = 4;
    Mpr121.build_init_args.touch_thr = 12;
    Mpr121.build_init_args.release_thr = 6;
    Mpr121.build_init(protocore_mpr121_span());
    const size_t n4 = Mpr121.n;
    TEST_ASSERT_EQUAL_size_t(n12 - 8 * 4, n4);
}

// A half-written bring-up would leave the part in Stop Mode with a partial configuration, so a
// sequence is emitted whole or not at all.
void test_build_init_refuses_bad_arguments(void)
{
    uint8_t buf[MPR121_INIT_MAX];
    Mpr121.build_init_args.buf = NULL;
    Mpr121.build_init_args.cap = MPR121_INIT_MAX;
    Mpr121.build_init_args.n_electrodes = 12;
    Mpr121.build_init_args.touch_thr = 12;
    Mpr121.build_init_args.release_thr = 6;
    Mpr121.build_init(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_size_t(0, Mpr121.n);
    Mpr121.build_init_args.buf = buf;
    Mpr121.build_init_args.cap = sizeof(buf);
    Mpr121.build_init_args.n_electrodes = 0;
    Mpr121.build_init_args.touch_thr = 12;
    Mpr121.build_init_args.release_thr = 6;
    Mpr121.build_init(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_size_t(0, Mpr121.n);
    Mpr121.build_init_args.buf = buf;
    Mpr121.build_init_args.cap = sizeof(buf);
    Mpr121.build_init_args.n_electrodes = MPR121_ELECTRODES + 1;
    Mpr121.build_init_args.touch_thr = 12;
    Mpr121.build_init_args.release_thr = 6;
    Mpr121.build_init(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_size_t(0, Mpr121.n);
    Mpr121.build_init_args.buf = buf;
    Mpr121.build_init_args.cap = MPR121_INIT_MAX - 1;
    Mpr121.build_init_args.n_electrodes = 12;
    Mpr121.build_init_args.touch_thr = 12;
    Mpr121.build_init_args.release_thr = 6;
    Mpr121.build_init(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_size_t(0, Mpr121.n);
    Mpr121.build_init_args.buf = buf;
    Mpr121.build_init_args.cap = 0;
    Mpr121.build_init_args.n_electrodes = 12;
    Mpr121.build_init_args.touch_thr = 12;
    Mpr121.build_init_args.release_thr = 6;
    Mpr121.build_init(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_size_t(0, Mpr121.n);
}

// ---------------------------------------------------------------------------
// Over the bus, against the device model
// ---------------------------------------------------------------------------

// Section 5.11 puts the ECR's reset at 00h, which is Stop Mode, and Table 2 gives every
// configuration register an initial value of 0. Asserted through the platform seam, because every
// case below reads a register back and a model that started elsewhere would agree with a wrong
// driver.
void test_datasheet_model_starts_stopped_and_unconfigured(void)
{
    uint8_t reg = 0x5Eu;
    uint8_t r[2] = {0xFFu, 0xFFu};
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_MPR121_I2C_ADDR, &reg, 1u, r, 1u, 10u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, r[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, s_part.reg[0x5Cu]); // CONFIG1
    TEST_ASSERT_EQUAL_HEX8(0x00u, s_part.reg[0x2Bu]); // MHDR
}

// Section 5.1: "the register write operation can only be done in Stop Mode." Put the part in Run
// Mode and a filter constant simply does not take; the ECR itself is writable at any time, so
// stopping it again lets the same write through.
void test_datasheet_a_config_write_in_run_mode_is_discarded(void)
{
    uint8_t run[2] = {0x5Eu, 0x8Cu}; // CL = 10, ELE_EN = 12: Run Mode
    TEST_ASSERT_TRUE(protocore_platform_i2c_write(0u, (uint16_t)PROTOCORE_MPR121_I2C_ADDR, run, 2u, 10u));
    uint8_t mhdr[2] = {0x2Bu, 0x01u};
    TEST_ASSERT_TRUE(protocore_platform_i2c_write(0u, (uint16_t)PROTOCORE_MPR121_I2C_ADDR, mhdr, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, s_part.reg[0x2Bu]); // discarded
    uint8_t stop[2] = {0x5Eu, 0x00u};
    TEST_ASSERT_TRUE(protocore_platform_i2c_write(0u, (uint16_t)PROTOCORE_MPR121_I2C_ADDR, stop, 2u, 10u));
    TEST_ASSERT_TRUE(protocore_platform_i2c_write(0u, (uint16_t)PROTOCORE_MPR121_I2C_ADDR, mhdr, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX8(0x01u, s_part.reg[0x2Bu]); // took, in Stop Mode
}

// begin() runs the bring-up in the order section 5.1 requires: the soft reset and the ECR stop go
// out before any configuration, so every filter constant, threshold and config byte lands. This is
// the case a byte-capture assertion cannot make - it would pass on a driver that never stopped.
void test_datasheet_begin_leaves_every_configuration_register_written(void)
{
    Mpr121.begin_args.addr = (uint8_t)PROTOCORE_MPR121_I2C_ADDR;
    Mpr121.begin(protocore_mpr121_span());
    TEST_ASSERT_TRUE(Mpr121.ok);

    // the rising / falling / touched baseline filter defaults (AN3944)
    TEST_ASSERT_EQUAL_HEX8(0x01u, s_part.reg[0x2Bu]); // MHDR
    TEST_ASSERT_EQUAL_HEX8(0x01u, s_part.reg[0x2Cu]); // NHDR
    TEST_ASSERT_EQUAL_HEX8(0x0Eu, s_part.reg[0x2Du]); // NCLR
    TEST_ASSERT_EQUAL_HEX8(0x00u, s_part.reg[0x2Eu]); // FDLR
    TEST_ASSERT_EQUAL_HEX8(0x01u, s_part.reg[0x2Fu]); // MHDF
    TEST_ASSERT_EQUAL_HEX8(0x05u, s_part.reg[0x30u]); // NHDF
    TEST_ASSERT_EQUAL_HEX8(0x00u, s_part.reg[0x35u]); // FDLT
    // debounce, CONFIG1, CONFIG2
    TEST_ASSERT_EQUAL_HEX8(0x00u, s_part.reg[0x5Bu]);
    TEST_ASSERT_EQUAL_HEX8(0x10u, s_part.reg[0x5Cu]);
    TEST_ASSERT_EQUAL_HEX8(0x20u, s_part.reg[0x5Du]);
    // Table 2: the per-electrode touch threshold is at 41h + 2e and its release threshold above it
    for (uint8_t e = 0u; e < MPR121_ELECTRODES; e++)
    {
        TEST_ASSERT_EQUAL_HEX8(PROTOCORE_MPR121_TOUCH_THRESHOLD, s_part.reg[0x41u + 2u * e]);
        TEST_ASSERT_EQUAL_HEX8(PROTOCORE_MPR121_RELEASE_THRESHOLD, s_part.reg[0x42u + 2u * e]);
    }
    // 5.11: the ECR goes out last, and it is what puts the part into Run Mode
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x80u | MPR121_ELECTRODES), s_part.reg[0x5Eu]);
    TEST_ASSERT_TRUE(protocore_mpr121_dev_running(&s_part));
    TEST_ASSERT_EQUAL_UINT8(12u, protocore_mpr121_dev_enabled(&s_part));
}

// Section 5.2: the touch status is the electrodes currently deemed touched, register 00h holding
// ELE0..ELE7 and register 01h holding ELE8..ELE11. A touch put on the pads reads back through the
// driver's own two-byte read and decode.
void test_datasheet_a_touch_on_the_pads_reads_back(void)
{
    Mpr121.begin_args.addr = (uint8_t)PROTOCORE_MPR121_I2C_ADDR;
    Mpr121.begin(protocore_mpr121_span());

    s_part.touch = 0x0000u;
    Mpr121.read_touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0000u, Mpr121.value);

    s_part.touch = (uint16_t)(1u << 0);
    Mpr121.read_touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0001u, Mpr121.value);

    // one in each status byte at once, so a decoder that drops the high byte fails here
    s_part.touch = (uint16_t)((1u << 3) | (1u << 11));
    Mpr121.read_touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0808u, Mpr121.value);

    // every electrode
    s_part.touch = 0x0FFFu;
    Mpr121.read_touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0FFFu, Mpr121.value);
}

// Section 5.2 puts ELEPROX at D4 and OVCF at D7 of register 01h, above the twelve electrodes, and
// the decode masks to 0x0FFF - so neither is ever reported as a touched electrode.
void test_datasheet_proximity_and_overcurrent_are_not_electrodes(void)
{
    Mpr121.begin_args.addr = (uint8_t)PROTOCORE_MPR121_I2C_ADDR;
    Mpr121.begin(protocore_mpr121_span());

    s_part.touch = PROTOCORE_MPR121_DEV_ELEPROX; // the 13th channel only
    Mpr121.read_touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0000u, Mpr121.value);
    // and it is visible where the datasheet puts it
    TEST_ASSERT_EQUAL_HEX8(0x10u, (uint8_t)(s_part.reg[0x01u] & 0x10u));

    s_part.reg[0x01u] |= 0x80u; // OVCF, as an over-current fault would set it
    Mpr121.read_touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0000u, Mpr121.value);
}

// Section 5.1: in Stop Mode there is no measurement on any channel, so a touch present on the pads
// before begin() reads as nothing at all.
void test_datasheet_nothing_is_measured_in_stop_mode(void)
{
    s_part.touch = 0x0FFFu;
    Mpr121.read_touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0000u, Mpr121.value);
    Mpr121.read_filtered_args.e = 0u;
    Mpr121.read_filtered(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0000u, Mpr121.value);
}

// Section 5.3: the filtered output is 10 bits, its low eight in the register at 04h + 2e and bits
// 9:8 in the byte above it, so a reading past 255 needs both bytes to come back correctly.
void test_datasheet_filtered_data_is_ten_bits_across_two_registers(void)
{
    Mpr121.begin_args.addr = (uint8_t)PROTOCORE_MPR121_I2C_ADDR;
    Mpr121.begin(protocore_mpr121_span());

    static const uint16_t APPLIED[4] = {0u, 255u, 256u, 1023u};
    for (uint32_t i = 0; i < 4u; i++)
    {
        s_part.filtered[0] = APPLIED[i];
        Mpr121.read_filtered_args.e = 0u;
        Mpr121.read_filtered(protocore_mpr121_span());
        TEST_ASSERT_EQUAL_HEX16(APPLIED[i], Mpr121.value);
    }
}

// Table 2: each electrode's filtered pair is two registers on from the one before it, so a channel
// reads its own measurement and not its neighbour's.
void test_datasheet_each_electrode_reads_its_own_filtered_data(void)
{
    Mpr121.begin_args.addr = (uint8_t)PROTOCORE_MPR121_I2C_ADDR;
    Mpr121.begin(protocore_mpr121_span());

    for (uint8_t e = 0u; e < MPR121_ELECTRODES; e++)
    {
        s_part.filtered[e] = (uint16_t)(300u + 17u * e);
    }
    for (uint8_t e = 0u; e < MPR121_ELECTRODES; e++)
    {
        Mpr121.read_filtered_args.e = e;
        Mpr121.read_filtered(protocore_mpr121_span());
        TEST_ASSERT_EQUAL_HEX16((uint16_t)(300u + 17u * e), Mpr121.value);
    }
}

// Section 5.13: writing 63h to register 80h resets everything but the I2C module, so a part that
// was running comes back stopped and unconfigured - which is what begin() does first.
void test_datasheet_the_soft_reset_returns_the_part_to_its_power_up_state(void)
{
    Mpr121.begin_args.addr = (uint8_t)PROTOCORE_MPR121_I2C_ADDR;
    Mpr121.begin(protocore_mpr121_span());
    TEST_ASSERT_TRUE(protocore_mpr121_dev_running(&s_part));

    uint8_t rst[2] = {0x80u, 0x63u};
    TEST_ASSERT_TRUE(protocore_platform_i2c_write(0u, (uint16_t)PROTOCORE_MPR121_I2C_ADDR, rst, 2u, 10u));
    TEST_ASSERT_FALSE(protocore_mpr121_dev_running(&s_part));
    TEST_ASSERT_EQUAL_HEX8(0x00u, s_part.reg[0x5Cu]); // CONFIG1 back to its initial value
    TEST_ASSERT_EQUAL_HEX8(0x00u, s_part.reg[0x41u]); // and the thresholds with it

    // and begin() brings it all back
    Mpr121.begin_args.addr = (uint8_t)PROTOCORE_MPR121_I2C_ADDR;
    Mpr121.begin(protocore_mpr121_span());
    TEST_ASSERT_TRUE(Mpr121.ok);
    TEST_ASSERT_TRUE(protocore_mpr121_dev_running(&s_part));
    TEST_ASSERT_EQUAL_HEX8(0x10u, s_part.reg[0x5Cu]);
}

// An out-of-range electrode is refused before anything reaches the bus.
void test_read_filtered_refuses_an_out_of_range_electrode(void)
{
    Mpr121.begin_args.addr = (uint8_t)PROTOCORE_MPR121_I2C_ADDR;
    Mpr121.begin(protocore_mpr121_span());
    const uint32_t before = protocore_bus_host_log_len;
    Mpr121.read_filtered_args.e = MPR121_ELECTRODES;
    Mpr121.read_filtered(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0000u, Mpr121.value);
    TEST_ASSERT_EQUAL_UINT32(before, protocore_bus_host_log_len);
}

// begin() sends later transfers to the address it was given, and back to the strapped default when
// handed zero - so the address is state and not a constant.
void test_begin_sends_later_transfers_to_the_address_it_was_given(void)
{
    protocore_bus_host_detach_all();
    protocore_mpr121_dev_place(&s_part, 0x5Bu);
    Mpr121.begin_args.addr = 0x5Bu;
    Mpr121.begin(protocore_mpr121_span());
    TEST_ASSERT_TRUE(Mpr121.ok);
    TEST_ASSERT_TRUE(protocore_mpr121_dev_running(&s_part));
    TEST_ASSERT_EQUAL_HEX16(0x5Bu, protocore_bus_host_log[protocore_bus_host_log_len - 1u].target);
    Mpr121.begin_args.addr = 0u;
    Mpr121.begin(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_MPR121_I2C_ADDR,
                            protocore_bus_host_log[protocore_bus_host_log_len - 1u].target);
}

// A refused transfer is reported rather than passed off as a configured part.
void test_a_refused_transfer_fails_begin(void)
{
    protocore_bus_host_fail = 1u;
    Mpr121.begin_args.addr = (uint8_t)PROTOCORE_MPR121_I2C_ADDR;
    Mpr121.begin(protocore_mpr121_span());
    TEST_ASSERT_FALSE(Mpr121.ok);
    TEST_ASSERT_FALSE(protocore_mpr121_dev_running(&s_part));
}

// A read that fails reports nothing touched rather than a stale mask.
void test_a_refused_read_reports_nothing_touched(void)
{
    Mpr121.begin_args.addr = (uint8_t)PROTOCORE_MPR121_I2C_ADDR;
    Mpr121.begin(protocore_mpr121_span());
    s_part.touch = 0x0FFFu;
    protocore_bus_host_fail = 1u;
    Mpr121.read_touched(protocore_mpr121_span());
    TEST_ASSERT_EQUAL_HEX16(0x0000u, Mpr121.value);
}
