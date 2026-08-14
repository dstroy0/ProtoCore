// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "server/peripherals/fdc2214/fdc2214.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// SNOSCZ5B Tables 7-12 and 7-13: the result is DATA0[27:16] from bits 11:0 of 0x00 followed by
// DATA0[15:0] from 0x01. The flags in bits 15:12 belong to the status field and never to the data.
void test_snoscz5_data_register_pair_is_a_28_bit_result(void)
{
    // msb bits 11:0 = 0x123, lsb = 0x4567 -> 0x1234567
    TEST_ASSERT_EQUAL_HEX32(0x01234567u, protocore_fdc2214_data(0x0123u, 0x4567u));
    // the same data with every status bit set must give the same result
    TEST_ASSERT_EQUAL_HEX32(0x01234567u, protocore_fdc2214_data(0xF123u, 0x4567u));
    // all-zero and all-ones data
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, protocore_fdc2214_data(0x0000u, 0x0000u));
    TEST_ASSERT_EQUAL_HEX32(0x0FFFFFFFu, protocore_fdc2214_data(0x0FFFu, 0xFFFFu));
    TEST_ASSERT_EQUAL_HEX32(0x0FFFFFFFu, protocore_fdc2214_data(0xFFFFu, 0xFFFFu));
    // the LSB register contributes exactly the low 16 bits
    TEST_ASSERT_EQUAL_HEX32(0x0000FFFFu, protocore_fdc2214_data(0x0000u, 0xFFFFu));
    // and the MSB register exactly bits 27:16
    TEST_ASSERT_EQUAL_HEX32(0x0FFF0000u, protocore_fdc2214_data(0x0FFFu, 0x0000u));
}

// A 28-bit result never sets bits 31:28, whatever the register pair says.
void test_data_never_exceeds_twenty_eight_bits(void)
{
    for (uint32_t m = 0u; m <= 0xFFFFu; m += 337u)
    {
        for (uint32_t l = 0u; l <= 0xFFFFu; l += 4093u)
        {
            TEST_ASSERT_EQUAL_HEX32(0u, protocore_fdc2214_data((uint16_t)m, (uint16_t)l) & 0xF0000000u);
        }
    }
}

// SNOSCZ5B Table 7-12: bit 13 CH0_ERR_WD (conversion watchdog timeout), bit 12 CH0_ERR_AW
// (amplitude warning); the flag field is the top nibble of the MSB register.
void test_snoscz5_status_flags_come_from_the_top_nibble(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x0u, protocore_fdc2214_error(0x0FFFu)); // full-scale data, no flags
    TEST_ASSERT_EQUAL_HEX8(0x2u, protocore_fdc2214_error(0x2000u)); // bit 13, ERR_WD
    TEST_ASSERT_EQUAL_HEX8(0x1u, protocore_fdc2214_error(0x1000u)); // bit 12, ERR_AW
    TEST_ASSERT_EQUAL_HEX8(0x3u, protocore_fdc2214_error(0x3000u)); // both
    TEST_ASSERT_EQUAL_HEX8(0xFu, protocore_fdc2214_error(0xFFFFu)); // every bit above the data
    // The flag field and the data field partition the MSB register: neither ever reads the other.
    for (uint32_t m = 0u; m <= 0xFFFFu; m += 1u)
    {
        uint32_t data = protocore_fdc2214_data((uint16_t)m, 0u) >> 16;
        uint32_t flags = protocore_fdc2214_error((uint16_t)m);
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
    TEST_ASSERT_EQUAL_UINT64(20000000ULL, protocore_fdc2214_sensor_freq_hz(0x08000000u, 40000000u));
    TEST_ASSERT_EQUAL_UINT64(10000000ULL, protocore_fdc2214_sensor_freq_hz(0x04000000u, 40000000u));
    TEST_ASSERT_EQUAL_UINT64(268435455ULL, protocore_fdc2214_sensor_freq_hz(0x0FFFFFFFu, 0x10000000u));
    TEST_ASSERT_EQUAL_UINT64(0ULL, protocore_fdc2214_sensor_freq_hz(0u, 40000000u));
    TEST_ASSERT_EQUAL_UINT64(0ULL, protocore_fdc2214_sensor_freq_hz(0x08000000u, 0u));
    // the product is formed in 64 bits: full-scale data against a 40 MHz reference does not wrap
    TEST_ASSERT_EQUAL_UINT64(39999999ULL, protocore_fdc2214_sensor_freq_hz(0x0FFFFFFFu, 40000000u));
}

// The scale is proportional, so a larger conversion result is never a lower frequency.
void test_sensor_frequency_is_monotone_in_the_data(void)
{
    uint64_t prev = 0u;
    for (uint32_t d = 0u; d <= 0x0FFFFFFFu; d += 0x00100000u)
    {
        uint64_t f = protocore_fdc2214_sensor_freq_hz(d, 40000000u);
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
    size_t n = protocore_fdc2214_build_config(buf, sizeof(buf), 0x0480u, 0x000Au);
    TEST_ASSERT_EQUAL_size_t(21u, n);
    TEST_ASSERT_EQUAL_size_t(7u * 3u, n);

    static const uint8_t WANT_REG[7] = {
        FDC2214_REG_RCOUNT_CH0,      FDC2214_REG_SETTLECOUNT_CH0, FDC2214_REG_CLOCK_DIVIDERS_CH0,
        FDC2214_REG_DRIVE_CURRENT_CH0, FDC2214_REG_ERROR_CONFIG,  FDC2214_REG_MUX_CONFIG,
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
    TEST_ASSERT_EQUAL_size_t(21u, protocore_fdc2214_build_config(other, sizeof(other), 0xBEEFu, 0xCAFEu));
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
    TEST_ASSERT_EQUAL_size_t(0u, protocore_fdc2214_build_config(NULL, sizeof(buf), 0x0480u, 0x000Au));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_fdc2214_build_config(buf, FDC2214_CONFIG_MAX - 1u, 0x0480u, 0x000Au));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_fdc2214_build_config(buf, 0u, 0x0480u, 0x000Au));
    TEST_ASSERT_EQUAL_size_t(FDC2214_CONFIG_MAX, protocore_fdc2214_build_config(buf, FDC2214_CONFIG_MAX, 0u, 0u));
}
