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
    Sht3x.crc8_args.data = &reply[0];
    Sht3x.crc8_args.len = 2;
    Sht3x.crc8(protocore_sht3x_span());
    reply[2] = Sht3x.crc;
    reply[3] = 0x80;
    reply[4] = 0x00;
    Sht3x.crc8_args.data = &reply[3];
    Sht3x.crc8_args.len = 2;
    Sht3x.crc8(protocore_sht3x_span());
    reply[5] = Sht3x.crc;
    protocore_bus_host_preload(reply, sizeof(reply));

    int32_t t = 0;
    int32_t rh = 0;
    Sht3x.read_args.temp_mc = &t;
    Sht3x.read_args.rh_mpct = &rh;
    Sht3x.read(protocore_sht3x_span());
    TEST_ASSERT_TRUE(Sht3x.ok);

    const uint8_t want[2] = {(uint8_t)(SHT3X_CMD_SINGLE_HIGH >> 8), (uint8_t)(SHT3X_CMD_SINGLE_HIGH & 0xFF)};
    expect_tx(want, sizeof(want), "sht3x measurement command");

    Sht3x.temp_mc_args.raw = 0x6666;
    Sht3x.temp_mc(protocore_sht3x_span());
    TEST_ASSERT_EQUAL_INT32(Sht3x.milli, t);
    Sht3x.rh_mpct_args.raw = 0x8000;
    Sht3x.rh_mpct(protocore_sht3x_span());
    TEST_ASSERT_EQUAL_INT32(Sht3x.milli, rh);
}

static void test_sht3x_bad_crc_rejected(void)
{
    uint8_t reply[6] = {0x66, 0x66, 0x00, 0x80, 0x00, 0x00};
    protocore_bus_host_preload(reply, sizeof(reply));
    int32_t t = 0;
    int32_t rh = 0;
    Sht3x.read_args.temp_mc = &t;
    Sht3x.read_args.rh_mpct = &rh;
    Sht3x.read(protocore_sht3x_span());
    TEST_ASSERT_FALSE(Sht3x.ok);
}

static void test_pca9685_set_pwm_wire(void)
{
    Pca9685.set_pwm_args.channel = 3;
    Pca9685.set_pwm_args.on = 0;
    Pca9685.set_pwm_args.off = 2048;
    Pca9685.set_pwm(protocore_pca9685_span());
    TEST_ASSERT_TRUE(Pca9685.ok);
    const uint8_t want[5] = {(uint8_t)(PCA9685_REG_LED0_ON_L + 4 * 3), 0x00, 0x00, (uint8_t)(2048 & 0xFF),
                             (uint8_t)((2048 >> 8) & 0x1F)};
    expect_tx(want, sizeof(want), "pca9685 channel 3 write");
}

static void test_pca9685_servo_wire(void)
{
    Pca9685.set_servo_us_args.channel = 0;
    Pca9685.set_servo_us_args.microseconds = 1500;
    Pca9685.set_servo_us(protocore_pca9685_span());
    TEST_ASSERT_TRUE(Pca9685.ok);
    Pca9685.us_to_count_args.microseconds = 1500;
    Pca9685.us_to_count_args.freq_hz = PROTOCORE_PCA9685_FREQ;
    Pca9685.us_to_count(protocore_pca9685_span());
    uint16_t off = Pca9685.count;
    const uint8_t want[5] = {PCA9685_REG_LED0_ON_L, 0x00, 0x00, (uint8_t)(off & 0xFF), (uint8_t)((off >> 8) & 0x1F)};
    expect_tx(want, sizeof(want), "pca9685 servo write");
}

static void test_ina219_wire_is_big_endian(void)
{
    Ina219.begin_args.addr = 0x40;
    Ina219.begin_args.current_lsb_ua = 100;
    Ina219.begin_args.shunt_mohm = 100;
    Ina219.begin(protocore_ina219_span());
    TEST_ASSERT_TRUE(Ina219.ok);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, protocore_bus_host_count());
    uint32_t n = 0;
    const uint8_t *first = protocore_bus_host_txn_bytes(0, &n);
    TEST_ASSERT_EQUAL_UINT32(3, n);
    TEST_ASSERT_EQUAL_HEX8(INA219_REG_CALIBRATION, first[0]);
    uint16_t cal = (uint16_t)(((uint16_t)first[1] << 8) | first[2]);
    Ina219.calibration_args.current_lsb_ua = 100;
    Ina219.calibration_args.shunt_mohm = 100;
    Ina219.calibration(protocore_ina219_span());
    TEST_ASSERT_EQUAL_UINT16(Ina219.cal, cal);

    for (uint32_t i = 0; i < protocore_bus_host_count(); i++)
    {
        TEST_ASSERT_EQUAL_UINT16(0x40, protocore_bus_host_txn_at(i)->target);
    }
}

static void test_rtc_read_wire(void)
{
    const uint8_t regs[7] = {0x05, 0x04, 0x03, 0x02, 0x02, 0x01, 0x24};
    protocore_bus_host_preload(regs, sizeof(regs));

    Rtc.read_epoch(protocore_rtc_span());
    uint32_t epoch = Rtc.epoch;

    const uint8_t want[1] = {0x00};
    expect_tx(want, sizeof(want), "rtc register pointer");

    uint32_t expect = 0;
    Rtc.regs_to_epoch_args.regs = regs;
    Rtc.regs_to_epoch_args.epoch = &expect;
    Rtc.regs_to_epoch(protocore_rtc_span());
    TEST_ASSERT_TRUE(Rtc.ok);
    TEST_ASSERT_EQUAL_UINT32(expect, epoch);
}

static void test_rtc_set_wire(void)
{
    uint32_t epoch = 1700000000u;
    Rtc.set_epoch_args.epoch = epoch;
    Rtc.set_epoch(protocore_rtc_span());
    TEST_ASSERT_TRUE(Rtc.ok);

    uint8_t want[8];
    want[0] = 0x00;
    Rtc.epoch_to_regs_args.epoch = epoch;
    Rtc.epoch_to_regs_args.regs = &want[1];
    Rtc.epoch_to_regs(protocore_rtc_span());
    expect_tx(want, sizeof(want), "rtc set");
}

static void test_smbus_pec_on_the_wire(void)
{
    Smbus.set_pec_args.on = PROTO_TRUE;
    Smbus.set_pec(protocore_smbus_span());
    Smbus.write_byte_args.addr = 0x2A;
    Smbus.write_byte_args.cmd = 0x10;
    Smbus.write_byte_args.value = 0x5A;
    Smbus.write_byte(protocore_smbus_span());
    TEST_ASSERT_TRUE(Smbus.ok);

    const uint8_t payload[2] = {0x10, 0x5A};
    Smbus.pec_write_args.addr = 0x2A;
    Smbus.pec_write_args.payload = payload;
    Smbus.pec_write_args.len = sizeof(payload);
    Smbus.pec_write(protocore_smbus_span());
    const uint8_t want[3] = {0x10, 0x5A, Smbus.value};
    expect_tx(want, sizeof(want), "smbus write byte with pec");
    Smbus.set_pec_args.on = PROTO_FALSE;
    Smbus.set_pec(protocore_smbus_span());
}

static void test_smbus_without_pec(void)
{
    Smbus.set_pec_args.on = PROTO_FALSE;
    Smbus.set_pec(protocore_smbus_span());
    Smbus.write_byte_args.addr = 0x2A;
    Smbus.write_byte_args.cmd = 0x10;
    Smbus.write_byte_args.value = 0x5A;
    Smbus.write_byte(protocore_smbus_span());
    TEST_ASSERT_TRUE(Smbus.ok);
    const uint8_t want[2] = {0x10, 0x5A};
    expect_tx(want, sizeof(want), "smbus write byte without pec");
}

static void test_smbus_word_is_little_endian(void)
{
    Smbus.write_word_args.addr = 0x2A;
    Smbus.write_word_args.cmd = 0x20;
    Smbus.write_word_args.value = 0xBEEF;
    Smbus.write_word(protocore_smbus_span());
    TEST_ASSERT_TRUE(Smbus.ok);
    const uint8_t want[3] = {0x20, 0xEF, 0xBE};
    expect_tx(want, sizeof(want), "smbus write word");
}

static void test_smbus_read_word_wire(void)
{
    const uint8_t reply[2] = {0xEF, 0xBE};
    protocore_bus_host_preload(reply, sizeof(reply));
    uint16_t v = 0;
    Smbus.read_word_args.addr = 0x2A;
    Smbus.read_word_args.cmd = 0x20;
    Smbus.read_word_args.out = &v;
    Smbus.read_word(protocore_smbus_span());
    TEST_ASSERT_TRUE(Smbus.ok);
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
    Pca9685.set_pwm_args.channel = 0;
    Pca9685.set_pwm_args.on = 0;
    Pca9685.set_pwm_args.off = 0;
    Pca9685.set_pwm(protocore_pca9685_span());
    TEST_ASSERT_TRUE(Pca9685.ok);
    Smbus.write_byte_args.addr = 0x2A;
    Smbus.write_byte_args.cmd = 0x10;
    Smbus.write_byte_args.value = 0x5A;
    Smbus.write_byte(protocore_smbus_span());
    TEST_ASSERT_TRUE(Smbus.ok);

    TEST_ASSERT_EQUAL_UINT32(2, protocore_bus_host_count());
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_PCA9685_I2C_ADDR, protocore_bus_host_txn_at(0)->target);
    TEST_ASSERT_EQUAL_UINT16(0x2A, protocore_bus_host_txn_at(1)->target);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BUS_HOST_I2C, protocore_bus_host_txn_at(0)->kind);
}

static void test_rtc_read_is_one_transaction(void)
{
    const uint8_t regs[7] = {0x05, 0x04, 0x03, 0x02, 0x02, 0x01, 0x24};
    protocore_bus_host_preload(regs, sizeof(regs));
    Rtc.read_epoch(protocore_rtc_span());
    (void)Rtc.epoch;

    TEST_ASSERT_EQUAL_UINT32(1, protocore_bus_host_count());
    const protocore_bus_host_rec *t = protocore_bus_host_txn_at(0);
    TEST_ASSERT_EQUAL_UINT32(1, t->wlen);
    TEST_ASSERT_EQUAL_UINT32(7, t->rlen);
}

static void test_pca9685_begin_settles_the_oscillator(void)
{
    Pca9685.begin_args.addr = PROTOCORE_PCA9685_I2C_ADDR;
    Pca9685.begin_args.freq_hz = PROTOCORE_PCA9685_FREQ;
    Pca9685.begin(protocore_pca9685_span());
    TEST_ASSERT_TRUE(Pca9685.ok);

    TEST_ASSERT_EQUAL_UINT32(5, protocore_bus_host_count());
    uint32_t gap = protocore_bus_host_gap_us(2, 3);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(500, gap, "oscillator settle was skipped");
}

static void test_failure_propagates(void)
{
    protocore_bus_host_fail_next(1);
    int32_t mv = 0;
    Ina219.read_bus_mv_args.millivolts = &mv;
    Ina219.read_bus_mv(protocore_ina219_span());
    TEST_ASSERT_FALSE(Ina219.ok);
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
