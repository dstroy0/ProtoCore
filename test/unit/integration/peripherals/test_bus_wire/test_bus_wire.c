// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/peripherals/i2c.h"
#include "server/peripherals/ina219/ina219.h"
#include "server/peripherals/pca9685/pca9685.h"
#include "server/peripherals/rtc/rtc.h"
#include "server/peripherals/sht3x/sht3x.h"
#include "server/peripherals/smbus.h"
#include "server/peripherals/spi.h"
#include <unity.h>

void setUp(void)
{
    protocore_bus_host_reset();
}
void tearDown(void)
{
}

static void expect_tx(const uint8_t *want, size_t len, const char *what)
{
    uint32_t got = 0;
    const uint8_t *tx = protocore_bus_host_written(&got);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)len, got, what);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, tx, len, what);
}

static void test_sht3x_read_wire(void)
{
    uint8_t reply[6];
    reply[0] = 0x66;
    reply[1] = 0x66;
    reply[2] = protocore_sht3x_crc8(&reply[0], 2);
    reply[3] = 0x80;
    reply[4] = 0x00;
    reply[5] = protocore_sht3x_crc8(&reply[3], 2);
    protocore_bus_host_preload(reply, sizeof(reply));

    int32_t t = 0;
    int32_t rh = 0;
    TEST_ASSERT_TRUE(protocore_sht3x_read(&t, &rh));

    const uint8_t want[2] = {(uint8_t)(SHT3X_CMD_SINGLE_HIGH >> 8), (uint8_t)(SHT3X_CMD_SINGLE_HIGH & 0xFF)};
    expect_tx(want, sizeof(want), "sht3x measurement command");

    TEST_ASSERT_EQUAL_INT32(protocore_sht3x_temp_mc(0x6666), t);
    TEST_ASSERT_EQUAL_INT32(protocore_sht3x_rh_mpct(0x8000), rh);
}

static void test_sht3x_bad_crc_rejected(void)
{
    uint8_t reply[6] = {0x66, 0x66, 0x00, 0x80, 0x00, 0x00};
    protocore_bus_host_preload(reply, sizeof(reply));
    int32_t t = 0;
    int32_t rh = 0;
    TEST_ASSERT_FALSE(protocore_sht3x_read(&t, &rh));
}

static void test_pca9685_set_pwm_wire(void)
{
    TEST_ASSERT_TRUE(protocore_pca9685_set_pwm(3, 0, 2048));
    const uint8_t want[5] = {(uint8_t)(PCA9685_REG_LED0_ON_L + 4 * 3), 0x00, 0x00, (uint8_t)(2048 & 0xFF),
                             (uint8_t)((2048 >> 8) & 0x1F)};
    expect_tx(want, sizeof(want), "pca9685 channel 3 write");
}

static void test_pca9685_servo_wire(void)
{
    TEST_ASSERT_TRUE(protocore_pca9685_set_servo_us(0, 1500));
    uint16_t off = protocore_pca9685_us_to_count(1500, PROTOCORE_PCA9685_FREQ);
    const uint8_t want[5] = {PCA9685_REG_LED0_ON_L, 0x00, 0x00, (uint8_t)(off & 0xFF), (uint8_t)((off >> 8) & 0x1F)};
    expect_tx(want, sizeof(want), "pca9685 servo write");
}

static void test_ina219_wire_is_big_endian(void)
{
    TEST_ASSERT_TRUE(protocore_ina219_begin(0x40, 100, 100));

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, protocore_bus_host_count());
    uint32_t n = 0;
    const uint8_t *first = protocore_bus_host_txn_bytes(0, &n);
    TEST_ASSERT_EQUAL_UINT32(3, n);
    TEST_ASSERT_EQUAL_HEX8(INA219_REG_CALIBRATION, first[0]);
    uint16_t cal = (uint16_t)(((uint16_t)first[1] << 8) | first[2]);
    TEST_ASSERT_EQUAL_UINT16(protocore_ina219_calibration(100, 100), cal);

    for (uint32_t i = 0; i < protocore_bus_host_count(); i++)
    {
        TEST_ASSERT_EQUAL_UINT16(0x40, protocore_bus_host_txn_at(i)->target);
    }
}

static void test_rtc_read_wire(void)
{
    const uint8_t regs[7] = {0x05, 0x04, 0x03, 0x02, 0x02, 0x01, 0x24};
    protocore_bus_host_preload(regs, sizeof(regs));

    uint32_t epoch = protocore_rtc_read_epoch();

    const uint8_t want[1] = {0x00};
    expect_tx(want, sizeof(want), "rtc register pointer");

    uint32_t expect = 0;
    TEST_ASSERT_TRUE(protocore_rtc_regs_to_epoch(regs, &expect));
    TEST_ASSERT_EQUAL_UINT32(expect, epoch);
}

static void test_rtc_set_wire(void)
{
    uint32_t epoch = 1700000000u;
    TEST_ASSERT_TRUE(protocore_rtc_set_epoch(epoch));

    uint8_t want[8];
    want[0] = 0x00;
    protocore_rtc_epoch_to_regs(epoch, &want[1]);
    expect_tx(want, sizeof(want), "rtc set");
}

static void test_smbus_pec_on_the_wire(void)
{
    protocore_smbus_set_pec(PROTO_TRUE);
    TEST_ASSERT_TRUE(protocore_smbus_write_byte(0x2A, 0x10, 0x5A));

    const uint8_t payload[2] = {0x10, 0x5A};
    const uint8_t want[3] = {0x10, 0x5A, protocore_smbus_pec_write(0x2A, payload, sizeof(payload))};
    expect_tx(want, sizeof(want), "smbus write byte with pec");
    protocore_smbus_set_pec(PROTO_FALSE);
}

static void test_smbus_without_pec(void)
{
    protocore_smbus_set_pec(PROTO_FALSE);
    TEST_ASSERT_TRUE(protocore_smbus_write_byte(0x2A, 0x10, 0x5A));
    const uint8_t want[2] = {0x10, 0x5A};
    expect_tx(want, sizeof(want), "smbus write byte without pec");
}

static void test_smbus_word_is_little_endian(void)
{
    TEST_ASSERT_TRUE(protocore_smbus_write_word(0x2A, 0x20, 0xBEEF));
    const uint8_t want[3] = {0x20, 0xEF, 0xBE};
    expect_tx(want, sizeof(want), "smbus write word");
}

static void test_smbus_read_word_wire(void)
{
    const uint8_t reply[2] = {0xEF, 0xBE};
    protocore_bus_host_preload(reply, sizeof(reply));
    uint16_t v = 0;
    TEST_ASSERT_TRUE(protocore_smbus_read_word(0x2A, 0x20, &v));
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, v);
    const uint8_t want[1] = {0x20};
    expect_tx(want, sizeof(want), "smbus read word command");
}

static void test_i2c_scan_probes_every_address(void)
{
    uint8_t found[8];
    TEST_ASSERT_EQUAL_size_t(0, protocore_i2c_scan(found, sizeof(found)));

    uint32_t want = PROTOCORE_I2C_SCAN_LAST - PROTOCORE_I2C_SCAN_FIRST + 1;
    TEST_ASSERT_EQUAL_UINT32(want, protocore_bus_host_count());
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_I2C_SCAN_FIRST, protocore_bus_host_txn_at(0)->target);
    TEST_ASSERT_EQUAL_UINT32(0, protocore_bus_host_txn_at(0)->wlen);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_I2C_SCAN_LAST, protocore_bus_host_txn_at(want - 1)->target);
}

static void test_transfers_carry_their_address(void)
{
    TEST_ASSERT_TRUE(protocore_pca9685_set_pwm(0, 0, 0));
    TEST_ASSERT_TRUE(protocore_smbus_write_byte(0x2A, 0x10, 0x5A));

    TEST_ASSERT_EQUAL_UINT32(2, protocore_bus_host_count());
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_PCA9685_I2C_ADDR, protocore_bus_host_txn_at(0)->target);
    TEST_ASSERT_EQUAL_UINT16(0x2A, protocore_bus_host_txn_at(1)->target);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BUS_HOST_I2C, protocore_bus_host_txn_at(0)->kind);
}

static void test_rtc_read_is_one_transaction(void)
{
    const uint8_t regs[7] = {0x05, 0x04, 0x03, 0x02, 0x02, 0x01, 0x24};
    protocore_bus_host_preload(regs, sizeof(regs));
    (void)protocore_rtc_read_epoch();

    TEST_ASSERT_EQUAL_UINT32(1, protocore_bus_host_count());
    const protocore_bus_host_rec *t = protocore_bus_host_txn_at(0);
    TEST_ASSERT_EQUAL_UINT32(1, t->wlen);
    TEST_ASSERT_EQUAL_UINT32(7, t->rlen);
}

static void test_pca9685_begin_settles_the_oscillator(void)
{
    TEST_ASSERT_TRUE(protocore_pca9685_begin(PROTOCORE_PCA9685_I2C_ADDR, PROTOCORE_PCA9685_FREQ));

    TEST_ASSERT_EQUAL_UINT32(5, protocore_bus_host_count());
    uint32_t gap = protocore_bus_host_gap_us(2, 3);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(500, gap, "oscillator settle was skipped");
}

static void test_failure_propagates(void)
{
    protocore_bus_host_fail_next(1);
    int32_t mv = 0;
    TEST_ASSERT_FALSE(protocore_ina219_read_bus_mv(&mv));
}

static void test_spi_wire(void)
{
    TEST_ASSERT_TRUE(protocore_spi_begin());
    const uint8_t out[3] = {0xDE, 0xAD, 0xBE};
    TEST_ASSERT_TRUE(protocore_spi_write(out, sizeof(out)));
    expect_tx(out, sizeof(out), "spi write");

    const uint8_t reply[2] = {0x11, 0x22};
    protocore_bus_host_preload(reply, sizeof(reply));
    uint8_t in[3] = {0xFF, 0xFF, 0xFF};
    TEST_ASSERT_TRUE(protocore_spi_read(in, sizeof(in)));
    TEST_ASSERT_EQUAL_HEX8(0x11, in[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22, in[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, in[2]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sht3x_read_wire);
    RUN_TEST(test_sht3x_bad_crc_rejected);
    RUN_TEST(test_pca9685_set_pwm_wire);
    RUN_TEST(test_pca9685_servo_wire);
    RUN_TEST(test_ina219_wire_is_big_endian);
    RUN_TEST(test_rtc_read_wire);
    RUN_TEST(test_rtc_set_wire);
    RUN_TEST(test_smbus_pec_on_the_wire);
    RUN_TEST(test_smbus_without_pec);
    RUN_TEST(test_smbus_word_is_little_endian);
    RUN_TEST(test_smbus_read_word_wire);
    RUN_TEST(test_i2c_scan_probes_every_address);
    RUN_TEST(test_transfers_carry_their_address);
    RUN_TEST(test_rtc_read_is_one_transaction);
    RUN_TEST(test_pca9685_begin_settles_the_oscillator);
    RUN_TEST(test_failure_propagates);
    RUN_TEST(test_spi_wire);
    return UNITY_END();
}
