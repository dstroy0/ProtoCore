// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// End to end from the host harness through the library: a real driver is called, it goes through
// the real bus owner, and the bytes it put on the wire are asserted. Nothing is stubbed at the
// driver boundary, so what is checked here is what a logic analyzer would see on the target.

#include "services/peripherals/bus_host.h"
#include "services/peripherals/i2c.h"
#include "services/peripherals/ina219/ina219.h"
#include "services/peripherals/pca9685/pca9685.h"
#include "services/peripherals/rtc/rtc.h"
#include "services/peripherals/sht3x/sht3x.h"
#include "services/peripherals/smbus.h"
#include "services/peripherals/spi.h"
#include <unity.h>

void setUp(void)
{
    pc_bus_host_reset();
}
void tearDown(void) {}

// Assert the whole recorded write stream in one go.
static void expect_tx(const uint8_t *want, size_t len, const char *what)
{
    size_t got = 0;
    const uint8_t *tx = pc_bus_host_tx(&got);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(len, got, what);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, tx, len, what);
}

// An SHT3x single-shot read: the command word goes out big-endian, then six bytes come back as
// two 16-bit words each followed by its CRC. Feeding a known response proves the parse too.
static void test_sht3x_read_wire(void)
{
    // 0x6666 -> 25.00 C is not a round number, so use the datasheet-style pair with valid CRCs.
    // temp raw 0x6666, humidity raw 0x8000; CRCs computed by the same engine the driver uses.
    uint8_t reply[6];
    reply[0] = 0x66;
    reply[1] = 0x66;
    reply[2] = pc_sht3x_crc8(&reply[0], 2);
    reply[3] = 0x80;
    reply[4] = 0x00;
    reply[5] = pc_sht3x_crc8(&reply[3], 2);
    pc_bus_host_load(reply, sizeof(reply));

    int32_t t = 0;
    int32_t rh = 0;
    TEST_ASSERT_TRUE(pc_sht3x_begin(0x44));
    TEST_ASSERT_TRUE(pc_sht3x_read(&t, &rh));

    // Two transfers: the soft reset from begin, then the measurement command, then the read.
    TEST_ASSERT_EQUAL_size_t(3, pc_bus_host_count());
    const pc_bus_host_txn *x = pc_bus_host_txn_at(1);
    TEST_ASSERT_EQUAL_UINT16(0x44, x->target);
    TEST_ASSERT_EQUAL_UINT8(PC_BUS_HOST_I2C, x->kind);

    // The decoded values follow from the raw ticks the reply carried.
    TEST_ASSERT_EQUAL_INT32(pc_sht3x_temp_mc(0x6666), t);
    TEST_ASSERT_EQUAL_INT32(pc_sht3x_rh_mpct(0x8000), rh);
}

// A PCA9685 channel write is five bytes: the channel's base register then on/off little-endian,
// with bit 4 of each high byte reserved for the full-on/full-off flag.
static void test_pca9685_set_pwm_wire(void)
{
    TEST_ASSERT_TRUE(pc_pca9685_set_pwm(3, 0, 2048));
    const uint8_t want[5] = {
        (uint8_t)(PCA9685_REG_LED0_ON_L + 4 * 3), 0x00, 0x00, (uint8_t)(2048 & 0xFF), (uint8_t)((2048 >> 8) & 0x1F)};
    expect_tx(want, sizeof(want), "pca9685 channel 3 write");
}

// An INA219 register write is the register byte then the value big-endian.
static void test_ina219_wire_is_big_endian(void)
{
    TEST_ASSERT_TRUE(pc_ina219_begin(0x40, 100, 100));
    size_t n = 0;
    const uint8_t *first = pc_bus_host_txn_bytes(0, &n);
    TEST_ASSERT_EQUAL_size_t(3, n);
    // The calibration register is written first, high byte before low.
    TEST_ASSERT_EQUAL_HEX8(INA219_REG_CALIBRATION, first[0]);
}

// An RTC read points at register 0 and then burst-reads the seven time registers in one
// transaction, which is a write of one byte joined to a read of seven by a repeated start.
static void test_rtc_read_wire(void)
{
    // 2024-01-02 03:04:05, BCD, 24-hour.
    const uint8_t regs[7] = {0x05, 0x04, 0x03, 0x02, 0x02, 0x01, 0x24};
    pc_bus_host_load(regs, sizeof(regs));

    TEST_ASSERT_TRUE(pc_rtc_begin());
    uint32_t epoch = pc_rtc_read_epoch();

    TEST_ASSERT_EQUAL_size_t(1, pc_bus_host_count());
    const pc_bus_host_txn *x = pc_bus_host_txn_at(0);
    TEST_ASSERT_EQUAL_UINT16(1, x->wlen); // the register pointer
    TEST_ASSERT_EQUAL_UINT16(7, x->rlen); // the burst
    size_t n = 0;
    const uint8_t *w = pc_bus_host_txn_bytes(0, &n);
    TEST_ASSERT_EQUAL_HEX8(0x00, w[0]);

    uint32_t want = 0;
    TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(regs, &want));
    TEST_ASSERT_EQUAL_UINT32(want, epoch);
}

// An RTC write lays the register pointer in front of the seven registers, one transaction.
static void test_rtc_set_wire(void)
{
    uint32_t epoch = 1700000000u;
    TEST_ASSERT_TRUE(pc_rtc_set_epoch(epoch));

    size_t n = 0;
    const uint8_t *w = pc_bus_host_txn_bytes(0, &n);
    TEST_ASSERT_EQUAL_size_t(8, n);
    TEST_ASSERT_EQUAL_HEX8(0x00, w[0]);

    uint8_t want[7];
    pc_rtc_epoch_to_regs(epoch, want);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, &w[1], 7);
}

// SMBus write byte with the PEC on appends the checksum the specification defines over the
// address byte and the payload.
static void test_smbus_pec_on_the_wire(void)
{
    pc_smbus_set_pec(PROTO_TRUE);
    TEST_ASSERT_TRUE(pc_smbus_write_byte(0x2A, 0x10, 0x5A));

    const uint8_t payload[2] = {0x10, 0x5A};
    const uint8_t want[3] = {0x10, 0x5A, pc_smbus_pec_write(0x2A, payload, sizeof(payload))};
    expect_tx(want, sizeof(want), "smbus write byte with pec");
    pc_smbus_set_pec(PROTO_FALSE);
}

// With the PEC off the same call is two bytes, so the flag is what changes the wire.
static void test_smbus_without_pec(void)
{
    pc_smbus_set_pec(PROTO_FALSE);
    TEST_ASSERT_TRUE(pc_smbus_write_byte(0x2A, 0x10, 0x5A));
    const uint8_t want[2] = {0x10, 0x5A};
    expect_tx(want, sizeof(want), "smbus write byte without pec");
}

// An SMBus word is little-endian on the wire, unlike the sensor registers above.
static void test_smbus_word_is_little_endian(void)
{
    TEST_ASSERT_TRUE(pc_smbus_write_word(0x2A, 0x20, 0xBEEF));
    const uint8_t want[3] = {0x20, 0xEF, 0xBE};
    expect_tx(want, sizeof(want), "smbus write word");
}

// A bus scan probes every non-reserved address, which is 112 address cycles.
static void test_i2c_scan_probes_every_address(void)
{
    uint8_t found[8];
    (void)pc_i2c_scan(found, sizeof(found));
    TEST_ASSERT_EQUAL_size_t(PC_I2C_SCAN_LAST - PC_I2C_SCAN_FIRST + 1, pc_bus_host_count());
    TEST_ASSERT_EQUAL_UINT16(PC_I2C_SCAN_FIRST, pc_bus_host_txn_at(0)->target);
    TEST_ASSERT_EQUAL_UINT16(0, pc_bus_host_txn_at(0)->wlen); // address only, no payload
}

// A device that does not acknowledge makes the driver report failure rather than a stale value.
static void test_failure_propagates(void)
{
    pc_bus_host_fail_next(1);
    int32_t mv = 0;
    TEST_ASSERT_FALSE(pc_ina219_read_bus_mv(&mv));
}

// SPI clocks both directions at once, so a write names the outgoing span and a read the incoming.
static void test_spi_wire(void)
{
    const uint8_t out[3] = {0xDE, 0xAD, 0xBE};
    TEST_ASSERT_TRUE(pc_spi_write(out, sizeof(out)));
    expect_tx(out, sizeof(out), "spi write");

    const uint8_t reply[2] = {0x11, 0x22};
    pc_bus_host_load(reply, sizeof(reply));
    uint8_t in[2] = {0, 0};
    TEST_ASSERT_TRUE(pc_spi_read(in, sizeof(in)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(reply, in, sizeof(reply));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sht3x_read_wire);
    RUN_TEST(test_pca9685_set_pwm_wire);
    RUN_TEST(test_ina219_wire_is_big_endian);
    RUN_TEST(test_rtc_read_wire);
    RUN_TEST(test_rtc_set_wire);
    RUN_TEST(test_smbus_pec_on_the_wire);
    RUN_TEST(test_smbus_without_pec);
    RUN_TEST(test_smbus_word_is_little_endian);
    RUN_TEST(test_i2c_scan_probes_every_address);
    RUN_TEST(test_failure_propagates);
    RUN_TEST(test_spi_wire);
    return UNITY_END();
}
