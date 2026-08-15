// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the TI LDC1614 inductance-to-digital codec (server/peripherals/ldc1614/ldc1614.h).
//
// Every expected value is read out of TI datasheet SNOSCY9A (LDC1612/LDC1614, rev March 2018):
// Figure 14 for the register list and the two ID reset values, Figures 15/16 and Tables 1/2 for the
// DATA0_MSB / DATA0_LSB bit layout, equation 5 for the 28-bit combine and equation 4 for the sensor
// frequency, Tables 27/28 for the reserved fields CONFIG and MUX_CONFIG must be written with.
//
// test_datasheet_register_ids_and_data_layout is the load-bearing case: it pins the split at bit 12
// that separates the four error flags from DATA0[27:16]. A codec that takes the whole MSB register
// as data reports a wildly wrong coil frequency the moment any error flag latches, and nothing else
// in the system can tell.

#include "server/peripherals/ldc1614/ldc1614.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Figure 14 register list, and the two ID registers with their published reset values. 0x5449 is
// also "TI" in US-ASCII: 0x54 'T', 0x49 'I'.
void test_datasheet_register_map(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, LDC1614_REG_DATA_CH0_MSB);
    TEST_ASSERT_EQUAL_HEX8(0x01, LDC1614_REG_DATA_CH0_LSB);
    TEST_ASSERT_EQUAL_HEX8(0x08, LDC1614_REG_RCOUNT_CH0);
    TEST_ASSERT_EQUAL_HEX8(0x10, LDC1614_REG_SETTLECOUNT_CH0);
    TEST_ASSERT_EQUAL_HEX8(0x14, LDC1614_REG_CLOCK_DIVIDERS_CH0);
    TEST_ASSERT_EQUAL_HEX8(0x18, LDC1614_REG_STATUS);
    TEST_ASSERT_EQUAL_HEX8(0x19, LDC1614_REG_ERROR_CONFIG);
    TEST_ASSERT_EQUAL_HEX8(0x1A, LDC1614_REG_CONFIG);
    TEST_ASSERT_EQUAL_HEX8(0x1B, LDC1614_REG_MUX_CONFIG);
    TEST_ASSERT_EQUAL_HEX8(0x1E, LDC1614_REG_DRIVE_CURRENT_CH0);
    TEST_ASSERT_EQUAL_HEX8(0x7E, LDC1614_REG_MANUFACTURER_ID);
    TEST_ASSERT_EQUAL_HEX8(0x7F, LDC1614_REG_DEVICE_ID);

    TEST_ASSERT_EQUAL_HEX16(0x5449, LDC1614_MANUFACTURER_ID);
    TEST_ASSERT_EQUAL_HEX16(((uint16_t)'T' << 8) | (uint16_t)'I', LDC1614_MANUFACTURER_ID);
    TEST_ASSERT_EQUAL_HEX16(0x3055, LDC1614_DEVICE_ID);
}

// Table 1: DATA0_MSB bit 15 ERR_UR0, 14 ERR_OR0, 13 ERR_WD0, 12 ERR_AE0, 11:0 DATA0[27:16].
// Table 2: DATA0_LSB 15:0 DATA0[15:0]. Equation 5: DATAx = DATAx_MSB * 65536 + DATAx_LSB, where
// DATAx_MSB names the 12-bit field, not the register - so the flags never reach the result.
void test_datasheet_register_ids_and_data_layout(void)
{
    // every flag set over a 12-bit field of 0x123: the data is the field alone.
    TEST_ASSERT_EQUAL_HEX32(0x01234567u, protocore_ldc1614_data(0xF123, 0x4567));
    TEST_ASSERT_EQUAL_HEX8(0x0F, protocore_ldc1614_error(0xF123));

    // no flag set: the same data, and no flags.
    TEST_ASSERT_EQUAL_HEX32(0x01234567u, protocore_ldc1614_data(0x0123, 0x4567));
    TEST_ASSERT_EQUAL_HEX8(0x00, protocore_ldc1614_error(0x0123));

    // each flag alone, at the bit Table 1 names it at.
    TEST_ASSERT_EQUAL_HEX8(0x08, protocore_ldc1614_error(0x8000)); // ERR_UR0, bit 15
    TEST_ASSERT_EQUAL_HEX8(0x04, protocore_ldc1614_error(0x4000)); // ERR_OR0, bit 14
    TEST_ASSERT_EQUAL_HEX8(0x02, protocore_ldc1614_error(0x2000)); // ERR_WD0, bit 13
    TEST_ASSERT_EQUAL_HEX8(0x01, protocore_ldc1614_error(0x1000)); // ERR_AE0, bit 12
    // bit 11 is the top data bit, not a flag.
    TEST_ASSERT_EQUAL_HEX8(0x00, protocore_ldc1614_error(0x0800));
}

// Table 37 notes: DATAx 0x000'0000 is the under-range condition and 0xFFF'FFFF the over-range one,
// so both sentinels must survive the combine exactly.
void test_range_sentinels_round_trip(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x0000000u, protocore_ldc1614_data(0x0000, 0x0000));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFu, protocore_ldc1614_data(0x0FFF, 0xFFFF));
    // the flags above a full-scale field do not push the result past 28 bits.
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFu, protocore_ldc1614_data(0xFFFF, 0xFFFF));
}

// Equation 4 with FREF_DIVIDER and FIN_DIVIDER at 1: f_SENSOR = f_REF * DATA / 2^28.
//   DATA = 2^27      -> f_REF / 2       = 20 000 000 Hz at f_REF = 40 MHz
//   DATA = 2^26      -> f_REF / 4       = 10 000 000 Hz
//   DATA = 2^28 - 1  -> just under f_REF: (40e6 * (2^28-1)) >> 28 = 39 999 999 Hz
//     since 40e6 * (2^28 - 1) = 40e6 * 2^28 - 40e6, and 40e6 / 2^28 < 1 so the shift takes one off.
void test_equation4_sensor_frequency(void)
{
    TEST_ASSERT_EQUAL_UINT64(20000000ULL, protocore_ldc1614_sensor_freq_hz(1u << 27, 40000000u));
    TEST_ASSERT_EQUAL_UINT64(10000000ULL, protocore_ldc1614_sensor_freq_hz(1u << 26, 40000000u));
    TEST_ASSERT_EQUAL_UINT64(39999999ULL, protocore_ldc1614_sensor_freq_hz(0x0FFFFFFFu, 40000000u));
    // under-range data is zero frequency, whatever the reference.
    TEST_ASSERT_EQUAL_UINT64(0ULL, protocore_ldc1614_sensor_freq_hz(0u, 40000000u));
    // the product exceeds 32 bits long before the shift, so a 32-bit intermediate would wrap here.
    TEST_ASSERT_EQUAL_UINT64(17500000ULL, protocore_ldc1614_sensor_freq_hz(1u << 27, 35000000u));
}

// The bring-up writes each register Figure 14 names, at its own address, as (reg, msb, lsb) triples,
// with CONFIG last because CONFIG.SLEEP_MODE_EN going to b0 is what starts converting.
void test_build_config_writes_the_datasheet_registers_in_order(void)
{
    uint8_t buf[LDC1614_CONFIG_MAX];
    TEST_ASSERT_EQUAL_size_t(21, protocore_ldc1614_build_config(buf, sizeof(buf), 0xFFFF, 0x0400));

    static const uint8_t ORDER[7] = {
        LDC1614_REG_RCOUNT_CH0,
        LDC1614_REG_SETTLECOUNT_CH0,
        LDC1614_REG_CLOCK_DIVIDERS_CH0,
        LDC1614_REG_DRIVE_CURRENT_CH0,
        LDC1614_REG_ERROR_CONFIG,
        LDC1614_REG_MUX_CONFIG,
        LDC1614_REG_CONFIG,
    };
    for (size_t i = 0; i < 7; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(ORDER[i], buf[i * 3]);
    }

    // the two caller-supplied counts land big-endian, the way a 16-bit register write is framed.
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x04, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[5]);
}

// Tables 27 and 28 make the reserved fields mandatory values rather than don't-cares: CONFIG[5:0]
// must be b00'0001 and MUX_CONFIG[12:3] must be b00'0100'0001. A write that gets them wrong is
// "improper device operation" per section 7.6.1, and nothing reports it.
void test_build_config_honors_the_reserved_fields(void)
{
    uint8_t buf[LDC1614_CONFIG_MAX];
    TEST_ASSERT_EQUAL_size_t(21, protocore_ldc1614_build_config(buf, sizeof(buf), 0xFFFF, 0x0400));

    const uint16_t clock_dividers = (uint16_t)(((uint16_t)buf[7] << 8) | buf[8]);
    const uint16_t mux = (uint16_t)(((uint16_t)buf[16] << 8) | buf[17]);
    const uint16_t config = (uint16_t)(((uint16_t)buf[19] << 8) | buf[20]);

    // equation 4 only holds at divider 1, so both dividers are 1: FIN_DIVIDER0 [15:12], FREF [9:0].
    TEST_ASSERT_EQUAL_UINT16(1, (clock_dividers >> 12) & 0x000F);
    TEST_ASSERT_EQUAL_UINT16(1, clock_dividers & 0x03FF);

    // MUX_CONFIG: AUTOSCAN_EN b0 (single channel), reserved b00'0100'0001, DEGLITCH b101 = 10 MHz.
    TEST_ASSERT_EQUAL_UINT16(0, (mux >> 15) & 1u);
    TEST_ASSERT_EQUAL_HEX16(0x041, (mux >> 3) & 0x03FFu);
    TEST_ASSERT_EQUAL_UINT16(5, mux & 0x0007u);

    // CONFIG: ACTIVE_CHAN b00 (channel 0), SLEEP_MODE_EN b0 (active), reserved b00'0001.
    TEST_ASSERT_EQUAL_UINT16(0, (config >> 14) & 0x0003u);
    TEST_ASSERT_EQUAL_UINT16(0, (config >> 13) & 1u);
    TEST_ASSERT_EQUAL_HEX16(0x01, config & 0x003Fu);
}

// A sequence is emitted whole or not at all: a partial bring-up would leave the part converting on
// a half-written configuration.
void test_build_config_refuses_a_short_buffer(void)
{
    uint8_t small[LDC1614_CONFIG_MAX - 1];
    small[0] = 0xAA;
    TEST_ASSERT_EQUAL_size_t(0, protocore_ldc1614_build_config(small, sizeof(small), 0xFFFF, 0x0400));
    TEST_ASSERT_EQUAL_HEX8(0xAA, small[0]); // untouched
    TEST_ASSERT_EQUAL_size_t(0, protocore_ldc1614_build_config(small, 0, 0xFFFF, 0x0400));
}

void test_build_config_refuses_a_null_buffer(void)
{
    TEST_ASSERT_EQUAL_size_t(0, protocore_ldc1614_build_config(NULL, LDC1614_CONFIG_MAX, 0xFFFF, 0x0400));
}
