// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the TI FDC2114/2214 codec (server/peripherals/fdc2214/fdc2214.h).
//
// The governing document is the FDC2212/2214/2112/2114 data sheet SNOSCZ5B. Its Figure 7-9 register
// list fixes every address used here, Table 7-12 puts DATA0[27:16] in bits 11:0 of DATA_CH0 (0x00)
// with the error/status flags above them, Table 7-13 puts DATA0[15:0] in DATA_LSB_CH0 (0x01), and
// equation (9) gives fSENSORx = CHx_FIN_SEL * fREFx * DATAx / 2^28 for the 28-bit parts.
//
// test_snoscz5_data_register_pair_is_a_28_bit_result is the load-bearing case: the top nibble of
// the MSB register is status, not data, so a combine that forgot to mask it would read a valid
// conversion as a wildly out-of-range one exactly when an error flag is set.

// The bus cases at the bottom drive a datasheet model of the part (test/core_setup/hal/host/devices/
// fdc2214_device.h) rather than a primed byte queue: a suite puts a conversion result on channel 0
// and the driver's own two reads bring it back, in the order section 7.6.3 requires.

#include "server/peripherals/fdc2214/fdc2214.h"

#include "devices/fdc2214_device.h"

#include <unity.h>

static protocore_fdc2214_dev s_part;

void setUp(void)
{
    protocore_bus_host_reset();
    protocore_bus_host_detach_all();
    protocore_fdc2214_dev_place(&s_part, (uint16_t)PROTOCORE_FDC2214_I2C_ADDR);
}
void tearDown(void)
{
    protocore_bus_host_detach_all();
}

// SNOSCZ5B Tables 7-12 and 7-13: the result is DATA0[27:16] from bits 11:0 of 0x00 followed by
// DATA0[15:0] from 0x01. The flags in bits 15:12 belong to the status field and never to the data.
void test_snoscz5_data_register_pair_is_a_28_bit_result(void)
{
    // msb bits 11:0 = 0x123, lsb = 0x4567 -> 0x1234567
    Fdc2214V.data_args.msb_reg = 0x0123u;
    Fdc2214V.data_args.lsb_reg = 0x4567u;
    Fdc2214.data(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX32(0x01234567u, Fdc2214V.value);
    // the same data with every status bit set must give the same result
    Fdc2214V.data_args.msb_reg = 0xF123u;
    Fdc2214V.data_args.lsb_reg = 0x4567u;
    Fdc2214.data(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX32(0x01234567u, Fdc2214V.value);
    // all-zero and all-ones data
    Fdc2214V.data_args.msb_reg = 0x0000u;
    Fdc2214V.data_args.lsb_reg = 0x0000u;
    Fdc2214.data(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, Fdc2214V.value);
    Fdc2214V.data_args.msb_reg = 0x0FFFu;
    Fdc2214V.data_args.lsb_reg = 0xFFFFu;
    Fdc2214.data(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX32(0x0FFFFFFFu, Fdc2214V.value);
    Fdc2214V.data_args.msb_reg = 0xFFFFu;
    Fdc2214V.data_args.lsb_reg = 0xFFFFu;
    Fdc2214.data(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX32(0x0FFFFFFFu, Fdc2214V.value);
    // the LSB register contributes exactly the low 16 bits
    Fdc2214V.data_args.msb_reg = 0x0000u;
    Fdc2214V.data_args.lsb_reg = 0xFFFFu;
    Fdc2214.data(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX32(0x0000FFFFu, Fdc2214V.value);
    // and the MSB register exactly bits 27:16
    Fdc2214V.data_args.msb_reg = 0x0FFFu;
    Fdc2214V.data_args.lsb_reg = 0x0000u;
    Fdc2214.data(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX32(0x0FFF0000u, Fdc2214V.value);
}

// A 28-bit result never sets bits 31:28, whatever the register pair says.
void test_data_never_exceeds_twenty_eight_bits(void)
{
    for (uint32_t m = 0u; m <= 0xFFFFu; m += 337u)
    {
        for (uint32_t l = 0u; l <= 0xFFFFu; l += 4093u)
        {
            Fdc2214V.data_args.msb_reg = (uint16_t)m;
            Fdc2214V.data_args.lsb_reg = (uint16_t)l;
            Fdc2214.data(protocore_fdc2214_span());
            TEST_ASSERT_EQUAL_HEX32(0u, Fdc2214V.value & 0xF0000000u);
        }
    }
}

// SNOSCZ5B Table 7-12: bit 13 CH0_ERR_WD (conversion watchdog timeout), bit 12 CH0_ERR_AW
// (amplitude warning); the flag field is the top nibble of the MSB register.
void test_snoscz5_status_flags_come_from_the_top_nibble(void)
{
    Fdc2214V.error_args.msb_reg = 0x0FFFu;
    Fdc2214.error(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX8(0x0u, Fdc2214V.flags); // full-scale data, no flags
    Fdc2214V.error_args.msb_reg = 0x2000u;
    Fdc2214.error(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX8(0x2u, Fdc2214V.flags); // bit 13, ERR_WD
    Fdc2214V.error_args.msb_reg = 0x1000u;
    Fdc2214.error(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX8(0x1u, Fdc2214V.flags); // bit 12, ERR_AW
    Fdc2214V.error_args.msb_reg = 0x3000u;
    Fdc2214.error(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX8(0x3u, Fdc2214V.flags); // both
    Fdc2214V.error_args.msb_reg = 0xFFFFu;
    Fdc2214.error(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX8(0xFu, Fdc2214V.flags); // every bit above the data
    // The flag field and the data field partition the MSB register: neither ever reads the other.
    for (uint32_t m = 0u; m <= 0xFFFFu; m += 1u)
    {
        Fdc2214V.data_args.msb_reg = (uint16_t)m;
        Fdc2214V.data_args.lsb_reg = 0u;
        Fdc2214.data(protocore_fdc2214_span());
        uint32_t data = Fdc2214V.value >> 16;
        Fdc2214V.error_args.msb_reg = (uint16_t)m;
        Fdc2214.error(protocore_fdc2214_span());
        uint32_t flags = Fdc2214V.flags;
        TEST_ASSERT_EQUAL_HEX32(m, (uint32_t)((flags << 12) | data));
    }
}

// SNOSCZ5B equation (9), fSENSOR = FIN_SEL * fREF * DATA / 2^28, at FIN_SEL = 1. Each expectation
// is that division worked out:
//   DATA = 2^27 (half scale), fREF = 40 MHz -> 40e6 / 2 = 20 MHz
//   DATA = 2^26 (quarter),    fREF = 40 MHz -> 10 MHz
//   DATA = 2^28 - 1 (full),   fREF = 2^28   -> 2^28 - 1 = 268435455 Hz
void test_snoscz5_sensor_frequency_scales_data_over_two_to_the_28(void)
{
    Fdc2214V.sensor_freq_hz_args.data28 = 0x08000000u;
    Fdc2214V.sensor_freq_hz_args.fref_hz = 40000000u;
    Fdc2214.sensor_freq_hz(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_UINT64(20000000ULL, Fdc2214V.hz);
    Fdc2214V.sensor_freq_hz_args.data28 = 0x04000000u;
    Fdc2214V.sensor_freq_hz_args.fref_hz = 40000000u;
    Fdc2214.sensor_freq_hz(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_UINT64(10000000ULL, Fdc2214V.hz);
    Fdc2214V.sensor_freq_hz_args.data28 = 0x0FFFFFFFu;
    Fdc2214V.sensor_freq_hz_args.fref_hz = 0x10000000u;
    Fdc2214.sensor_freq_hz(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_UINT64(268435455ULL, Fdc2214V.hz);
    Fdc2214V.sensor_freq_hz_args.data28 = 0u;
    Fdc2214V.sensor_freq_hz_args.fref_hz = 40000000u;
    Fdc2214.sensor_freq_hz(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_UINT64(0ULL, Fdc2214V.hz);
    Fdc2214V.sensor_freq_hz_args.data28 = 0x08000000u;
    Fdc2214V.sensor_freq_hz_args.fref_hz = 0u;
    Fdc2214.sensor_freq_hz(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_UINT64(0ULL, Fdc2214V.hz);
    // the product is formed in 64 bits: full-scale data against a 40 MHz reference does not wrap
    Fdc2214V.sensor_freq_hz_args.data28 = 0x0FFFFFFFu;
    Fdc2214V.sensor_freq_hz_args.fref_hz = 40000000u;
    Fdc2214.sensor_freq_hz(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_UINT64(39999999ULL, Fdc2214V.hz);
}

// The scale is proportional, so a larger conversion result is never a lower frequency.
void test_sensor_frequency_is_monotone_in_the_data(void)
{
    uint64_t prev = 0u;
    for (uint32_t d = 0u; d <= 0x0FFFFFFFu; d += 0x00100000u)
    {
        Fdc2214V.sensor_freq_hz_args.data28 = d;
        Fdc2214V.sensor_freq_hz_args.fref_hz = 40000000u;
        Fdc2214.sensor_freq_hz(protocore_fdc2214_span());
        uint64_t f = Fdc2214V.hz;
        TEST_ASSERT_TRUE(f >= prev);
        prev = f;
    }
}

// The bring-up sequence is seven (register, value MSB, value LSB) triples. Every register address
// is the one SNOSCZ5B Figure 7-9 assigns, the caller's RCOUNT and SETTLECOUNT ride through
// big-endian, and CONFIG (0x1A) is written last because it starts the conversion.
void test_config_sequence_register_order_and_addresses(void)
{
    uint8_t buf[FDC2214_CONFIG_MAX];
    Fdc2214V.build_config_args.buf = buf;
    Fdc2214V.build_config_args.cap = sizeof(buf);
    Fdc2214V.build_config_args.rcount = 0x0480u;
    Fdc2214V.build_config_args.settlecount = 0x000Au;
    Fdc2214.build_config(protocore_fdc2214_span());
    size_t n = Fdc2214V.n;
    TEST_ASSERT_EQUAL_size_t(21u, n);
    TEST_ASSERT_EQUAL_size_t(7u * 3u, n);

    static const uint8_t WANT_REG[7] = {
        FDC2214_REG_RCOUNT_CH0,
        FDC2214_REG_SETTLECOUNT_CH0,
        FDC2214_REG_CLOCK_DIVIDERS_CH0,
        FDC2214_REG_DRIVE_CURRENT_CH0,
        FDC2214_REG_ERROR_CONFIG,
        FDC2214_REG_MUX_CONFIG,
        FDC2214_REG_CONFIG,
    };
    for (size_t i = 0; i < 7u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(WANT_REG[i], buf[i * 3u]);
    }
    // CONFIG is the last write of the sequence.
    TEST_ASSERT_EQUAL_HEX8(FDC2214_REG_CONFIG, buf[18]);

    // The two caller-supplied counts appear big-endian at their triples.
    TEST_ASSERT_EQUAL_HEX8(0x04u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x80u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x0Au, buf[5]);

    // A different pair moves only those four octets.
    uint8_t other[FDC2214_CONFIG_MAX];
    Fdc2214V.build_config_args.buf = other;
    Fdc2214V.build_config_args.cap = sizeof(other);
    Fdc2214V.build_config_args.rcount = 0xBEEFu;
    Fdc2214V.build_config_args.settlecount = 0xCAFEu;
    Fdc2214.build_config(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_size_t(21u, Fdc2214V.n);
    TEST_ASSERT_EQUAL_HEX8(0xBEu, other[1]);
    TEST_ASSERT_EQUAL_HEX8(0xEFu, other[2]);
    TEST_ASSERT_EQUAL_HEX8(0xCAu, other[4]);
    TEST_ASSERT_EQUAL_HEX8(0xFEu, other[5]);
    for (size_t i = 6u; i < 21u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(buf[i], other[i]);
    }
}

// SNOSCZ5B Figure 7-9, "Register List": the addresses this codec names.
void test_snoscz5_register_addresses(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00u, FDC2214_REG_DATA_CH0_MSB);
    TEST_ASSERT_EQUAL_HEX8(0x01u, FDC2214_REG_DATA_CH0_LSB);
    TEST_ASSERT_EQUAL_HEX8(0x08u, FDC2214_REG_RCOUNT_CH0);
    TEST_ASSERT_EQUAL_HEX8(0x10u, FDC2214_REG_SETTLECOUNT_CH0);
    TEST_ASSERT_EQUAL_HEX8(0x14u, FDC2214_REG_CLOCK_DIVIDERS_CH0);
    TEST_ASSERT_EQUAL_HEX8(0x18u, FDC2214_REG_STATUS);
    TEST_ASSERT_EQUAL_HEX8(0x19u, FDC2214_REG_ERROR_CONFIG);
    TEST_ASSERT_EQUAL_HEX8(0x1Au, FDC2214_REG_CONFIG);
    TEST_ASSERT_EQUAL_HEX8(0x1Bu, FDC2214_REG_MUX_CONFIG);
    TEST_ASSERT_EQUAL_HEX8(0x1Eu, FDC2214_REG_DRIVE_CURRENT_CH0);
    TEST_ASSERT_EQUAL_HEX8(0x7Eu, FDC2214_REG_MANUFACTURER_ID);
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, FDC2214_REG_DEVICE_ID);
}

// SNOSCZ5B Tables 7-45 and 7-46: MANUFACTURER_ID reads 0x5449 and DEVICE_ID reads 0x3055 on the
// 28-bit parts (0x3054 on the 12-bit FDC2112/2114).
void test_snoscz5_identity_registers(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x5449u, FDC2214_MANUFACTURER_ID);
    TEST_ASSERT_EQUAL_HEX16(0x3055u, FDC2214_DEVICE_ID);
    TEST_ASSERT_NOT_EQUAL(0x3054u, FDC2214_DEVICE_ID);
}

// A buffer that cannot hold the whole sequence gets nothing: a half-written bring-up would start a
// conversion on a partly configured channel.
void test_config_builder_fails_closed(void)
{
    uint8_t buf[FDC2214_CONFIG_MAX];
    Fdc2214V.build_config_args.buf = NULL;
    Fdc2214V.build_config_args.cap = sizeof(buf);
    Fdc2214V.build_config_args.rcount = 0x0480u;
    Fdc2214V.build_config_args.settlecount = 0x000Au;
    Fdc2214.build_config(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_size_t(0u, Fdc2214V.n);
    Fdc2214V.build_config_args.buf = buf;
    Fdc2214V.build_config_args.cap = FDC2214_CONFIG_MAX - 1u;
    Fdc2214V.build_config_args.rcount = 0x0480u;
    Fdc2214V.build_config_args.settlecount = 0x000Au;
    Fdc2214.build_config(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_size_t(0u, Fdc2214V.n);
    Fdc2214V.build_config_args.buf = buf;
    Fdc2214V.build_config_args.cap = 0u;
    Fdc2214V.build_config_args.rcount = 0x0480u;
    Fdc2214V.build_config_args.settlecount = 0x000Au;
    Fdc2214.build_config(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_size_t(0u, Fdc2214V.n);
    Fdc2214V.build_config_args.buf = buf;
    Fdc2214V.build_config_args.cap = FDC2214_CONFIG_MAX;
    Fdc2214V.build_config_args.rcount = 0u;
    Fdc2214V.build_config_args.settlecount = 0u;
    Fdc2214.build_config(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_size_t(FDC2214_CONFIG_MAX, Fdc2214V.n);
}

// ---------------------------------------------------------------------------
// Over the bus, against the device model
// ---------------------------------------------------------------------------

// SNOSCZ5B 7.6.36 gives DEVICE_ID 3055h, 7.6.35 gives MANUFACTURER_ID 5449h, 7.6.28 Table 7-38
// gives CONFIG 2801h and 7.6.10 gives RCOUNT 0080h. Asserted through the platform seam, because
// every case below reads a register back and a model that started elsewhere would agree with a
// wrong driver.
void test_snoscz5_model_reset_values(void)
{
    uint8_t reg = 0x7Fu;
    uint8_t r[2] = {0u, 0u};
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_FDC2214_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x3055u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    reg = 0x7Eu;
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_FDC2214_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x5449u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    reg = 0x1Au;
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_FDC2214_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x2801u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    reg = 0x08u;
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_FDC2214_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x0080u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    // 7.4.1: it powers up in Sleep Mode, waiting to be configured
    TEST_ASSERT_FALSE(protocore_fdc2214_dev_awake(&s_part));
}

// begin() reads DEVICE_ID first and refuses anything that is not one of the two parts this codec
// decodes - 3055h for the 28-bit FDC2214, 3054h for the 12-bit FDC2114. A part that answers with
// anything else is not configured at all.
void test_snoscz5_begin_refuses_a_part_that_is_not_an_fdc(void)
{
    s_part.reg[0x7Fu] = 0x1234u;
    Fdc2214V.begin_args.addr = (uint8_t)PROTOCORE_FDC2214_I2C_ADDR;
    Fdc2214V.begin_args.rcount = 0xFFFFu;
    Fdc2214V.begin_args.settlecount = 0x0400u;
    Fdc2214.begin(protocore_fdc2214_span());
    TEST_ASSERT_FALSE(Fdc2214V.ok);
    TEST_ASSERT_EQUAL_HEX16(0x0080u, s_part.reg[0x08u]); // RCOUNT untouched: it never configured
    TEST_ASSERT_FALSE(protocore_fdc2214_dev_awake(&s_part));

    // the 12-bit sibling is accepted
    s_part.reg[0x7Fu] = 0x3054u;
    Fdc2214V.begin_args.addr = (uint8_t)PROTOCORE_FDC2214_I2C_ADDR;
    Fdc2214V.begin_args.rcount = 0xFFFFu;
    Fdc2214V.begin_args.settlecount = 0x0400u;
    Fdc2214.begin(protocore_fdc2214_span());
    TEST_ASSERT_TRUE(Fdc2214V.ok);
}

// begin() replays the bring-up sequence, so every register in it holds what build_config laid down
// and 7.4.1's Sleep Mode is left behind - CONFIG is written last because it starts the conversion.
void test_snoscz5_begin_leaves_the_part_configured_and_converting(void)
{
    Fdc2214V.begin_args.addr = (uint8_t)PROTOCORE_FDC2214_I2C_ADDR;
    Fdc2214V.begin_args.rcount = 0xFFFFu;
    Fdc2214V.begin_args.settlecount = 0x0400u;
    Fdc2214.begin(protocore_fdc2214_span());
    TEST_ASSERT_TRUE(Fdc2214V.ok);

    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, s_part.reg[0x08u]); // RCOUNT_CH0
    TEST_ASSERT_EQUAL_HEX16(0x0400u, s_part.reg[0x10u]); // SETTLECOUNT_CH0
    TEST_ASSERT_EQUAL_HEX16(0x1001u, s_part.reg[0x14u]); // CLOCK_DIVIDERS_CH0
    TEST_ASSERT_EQUAL_HEX16(0x8C40u, s_part.reg[0x1Eu]); // DRIVE_CURRENT_CH0
    TEST_ASSERT_EQUAL_HEX16(0x0000u, s_part.reg[0x19u]); // ERROR_CONFIG
    TEST_ASSERT_EQUAL_HEX16(0x020Du, s_part.reg[0x1Bu]); // MUX_CONFIG
    TEST_ASSERT_EQUAL_HEX16(0x1E01u, s_part.reg[0x1Au]); // CONFIG, written last
    TEST_ASSERT_TRUE(protocore_fdc2214_dev_awake(&s_part));
    // 7.6.28 Table 7-38 requires bits 12 and 10 set and bit 8 clear; the written word obeys it
    TEST_ASSERT_EQUAL_HEX16(0x1400u, (uint16_t)(s_part.reg[0x1Au] & 0x1500u));
}

// SNOSCZ5B 7.6.2 / 7.6.3: the result is 28 bits, its high twelve in DATA_CH0's bits 11:0 and its
// low sixteen in DATA_LSB_CH0. A conversion put on the channel comes back through the driver's two
// reads as the same number.
void test_snoscz5_a_conversion_reads_back_whole(void)
{
    Fdc2214V.begin_args.addr = (uint8_t)PROTOCORE_FDC2214_I2C_ADDR;
    Fdc2214V.begin_args.rcount = 0xFFFFu;
    Fdc2214V.begin_args.settlecount = 0x0400u;
    Fdc2214.begin(protocore_fdc2214_span());

    static const uint32_t APPLIED[5] = {0u, 1u, 0x0ABC1234u, 0x08000000u, 0x0FFFFFFFu};
    for (uint32_t i = 0; i < 5u; i++)
    {
        s_part.ch0 = APPLIED[i];
        uint32_t out = 0xFFFFFFFFu;
        Fdc2214V.read_ch0_args.out = &out;
        Fdc2214.read_ch0(protocore_fdc2214_span());
        TEST_ASSERT_TRUE(Fdc2214V.ok);
        TEST_ASSERT_EQUAL_HEX32(APPLIED[i], out);
    }
}

// SNOSCZ5B 7.6.2 puts CH0_ERR_WD at bit 13 and CH0_ERR_AW at bit 12, above the data. A conversion
// carrying both flags still reads back as its own 28-bit value, because the decode masks them off.
void test_snoscz5_the_error_flags_do_not_leak_into_the_result(void)
{
    Fdc2214V.begin_args.addr = (uint8_t)PROTOCORE_FDC2214_I2C_ADDR;
    Fdc2214V.begin_args.rcount = 0xFFFFu;
    Fdc2214V.begin_args.settlecount = 0x0400u;
    Fdc2214.begin(protocore_fdc2214_span());

    s_part.ch0 = 0x0ABC1234u;
    s_part.err = 0x3u; // ERR_WD and ERR_AW both raised
    uint32_t out = 0u;
    Fdc2214V.read_ch0_args.out = &out;
    Fdc2214.read_ch0(protocore_fdc2214_span());
    TEST_ASSERT_TRUE(Fdc2214V.ok);
    TEST_ASSERT_EQUAL_HEX32(0x0ABC1234u, out);
    // and the flags are readable where the datasheet puts them
    Fdc2214V.error_args.msb_reg = s_part.reg[0x00u];
    Fdc2214.error(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX8(0x3u, Fdc2214V.flags);
}

// SNOSCZ5B 7.6.3: DATA_LSB_CH0 "must be read after Register address 0x00", so the low half is
// latched by the read of the high half. read_ch0 does them in that order, which is why a result
// that changes between the two still reads back as one coherent moment rather than a mix.
void test_snoscz5_the_low_half_is_latched_by_reading_the_high_half(void)
{
    Fdc2214V.begin_args.addr = (uint8_t)PROTOCORE_FDC2214_I2C_ADDR;
    Fdc2214V.begin_args.rcount = 0xFFFFu;
    Fdc2214V.begin_args.settlecount = 0x0400u;
    Fdc2214.begin(protocore_fdc2214_span());

    // read the low half on its own: it holds whatever the last high read latched, which is nothing
    uint8_t reg = 0x01u;
    uint8_t r[2] = {0xAAu, 0xAAu};
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_FDC2214_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));

    // now a conversion, and the pair read in the documented order
    s_part.ch0 = 0x0ABC1234u;
    reg = 0x00u;
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_FDC2214_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x0ABCu, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
    reg = 0x01u;
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_FDC2214_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));

    // the channel moves, but nothing has read the high half since, so the low half still holds the
    // moment that was latched
    s_part.ch0 = 0x01115678u;
    reg = 0x01u;
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_FDC2214_I2C_ADDR, &reg, 1u, r, 2u, 10u));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, (uint16_t)(((uint16_t)r[0] << 8) | r[1]));
}

// SNOSCZ5B 7.4.1: the part powers up in Sleep Mode and waits for configuration, so a channel read
// before begin() reports nothing rather than a stale or invented conversion.
void test_snoscz5_nothing_converts_while_the_part_is_asleep(void)
{
    s_part.ch0 = 0x0ABC1234u;
    uint32_t out = 0xFFFFFFFFu;
    Fdc2214V.read_ch0_args.out = &out;
    Fdc2214.read_ch0(protocore_fdc2214_span());
    TEST_ASSERT_TRUE(Fdc2214V.ok);
    TEST_ASSERT_EQUAL_HEX32(0u, out);
}

// begin() sends later transfers to the address it was given, and back to the ADDR-pin default when
// handed zero - so the address is state and not a constant. 0x2B is ADDR tied high.
void test_begin_sends_later_transfers_to_the_address_it_was_given(void)
{
    protocore_bus_host_detach_all();
    protocore_fdc2214_dev_place(&s_part, 0x2Bu);
    Fdc2214V.begin_args.addr = 0x2Bu;
    Fdc2214V.begin_args.rcount = 0xFFFFu;
    Fdc2214V.begin_args.settlecount = 0x0400u;
    Fdc2214.begin(protocore_fdc2214_span());
    TEST_ASSERT_TRUE(Fdc2214V.ok);
    TEST_ASSERT_EQUAL_HEX16(0x2Bu, protocore_bus_host_log[protocore_bus_host_log_len - 1u].target);
    Fdc2214V.begin_args.addr = 0u;
    Fdc2214V.begin_args.rcount = 0xFFFFu;
    Fdc2214V.begin_args.settlecount = 0x0400u;
    Fdc2214.begin(protocore_fdc2214_span());
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_FDC2214_I2C_ADDR, protocore_bus_host_log[protocore_bus_host_log_len - 1u].target);
}

// A refused transfer is reported rather than passed off as a configured part.
void test_a_refused_transfer_fails_begin(void)
{
    protocore_bus_host_fail = 1u;
    Fdc2214V.begin_args.addr = (uint8_t)PROTOCORE_FDC2214_I2C_ADDR;
    Fdc2214V.begin_args.rcount = 0xFFFFu;
    Fdc2214V.begin_args.settlecount = 0x0400u;
    Fdc2214.begin(protocore_fdc2214_span());
    TEST_ASSERT_FALSE(Fdc2214V.ok);
    TEST_ASSERT_FALSE(protocore_fdc2214_dev_awake(&s_part));
}

// A null destination is refused before anything reaches the bus.
void test_read_ch0_refuses_a_null_destination(void)
{
    Fdc2214V.read_ch0_args.out = 0;
    Fdc2214.read_ch0(protocore_fdc2214_span());
    TEST_ASSERT_FALSE(Fdc2214V.ok);
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_bus_host_log_len);
}
