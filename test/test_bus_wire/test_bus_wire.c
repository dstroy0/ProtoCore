// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// End to end from the host harness through the library: a real driver is called, it runs through
// the real bus owner and the real platform seam, and the bytes it drove onto the wire are
// asserted. Nothing is stubbed at the driver boundary, so what is checked here is the byte stream
// a logic analyzer would show on the target.
//
// The capture lives in test/mocks/pc_net_host.h, which backs the platform seam on a host build.

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
    uint32_t got = 0;
    const uint8_t *tx = pc_bus_host_written(&got);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)len, got, what);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, tx, len, what);
}

// An SHT3x single-shot read sends the measurement command big-endian and reads six bytes back:
// two 16-bit words each followed by its CRC. Feeding a known reply exercises the parse as well.
static void test_sht3x_read_wire(void)
{
    uint8_t reply[6];
    reply[0] = 0x66;
    reply[1] = 0x66;
    reply[2] = pc_sht3x_crc8(&reply[0], 2);
    reply[3] = 0x80;
    reply[4] = 0x00;
    reply[5] = pc_sht3x_crc8(&reply[3], 2);
    pc_bus_host_preload(reply, sizeof(reply));

    int32_t t = 0;
    int32_t rh = 0;
    TEST_ASSERT_TRUE(pc_sht3x_read(&t, &rh));

    // Only the measurement command goes out; the six bytes come back on the read.
    const uint8_t want[2] = {(uint8_t)(SHT3X_CMD_SINGLE_HIGH >> 8), (uint8_t)(SHT3X_CMD_SINGLE_HIGH & 0xFF)};
    expect_tx(want, sizeof(want), "sht3x measurement command");

    TEST_ASSERT_EQUAL_INT32(pc_sht3x_temp_mc(0x6666), t);
    TEST_ASSERT_EQUAL_INT32(pc_sht3x_rh_mpct(0x8000), rh);
}

// A reply whose CRC does not match is rejected rather than decoded.
static void test_sht3x_bad_crc_rejected(void)
{
    uint8_t reply[6] = {0x66, 0x66, 0x00, 0x80, 0x00, 0x00}; // both CRCs deliberately wrong
    pc_bus_host_preload(reply, sizeof(reply));
    int32_t t = 0;
    int32_t rh = 0;
    TEST_ASSERT_FALSE(pc_sht3x_read(&t, &rh));
}

// A PCA9685 channel write is five bytes: the channel's base register then on/off little-endian,
// with bit 4 of each high byte reserved for the full-on / full-off flag.
static void test_pca9685_set_pwm_wire(void)
{
    TEST_ASSERT_TRUE(pc_pca9685_set_pwm(3, 0, 2048));
    const uint8_t want[5] = {
        (uint8_t)(PCA9685_REG_LED0_ON_L + 4 * 3), 0x00, 0x00, (uint8_t)(2048 & 0xFF), (uint8_t)((2048 >> 8) & 0x1F)};
    expect_tx(want, sizeof(want), "pca9685 channel 3 write");
}

// A servo pulse width goes out as the count the conversion produces, on the same five bytes.
static void test_pca9685_servo_wire(void)
{
    TEST_ASSERT_TRUE(pc_pca9685_set_servo_us(0, 1500));
    uint16_t off = pc_pca9685_us_to_count(1500, PC_PCA9685_FREQ);
    const uint8_t want[5] = {PCA9685_REG_LED0_ON_L, 0x00, 0x00, (uint8_t)(off & 0xFF), (uint8_t)((off >> 8) & 0x1F)};
    expect_tx(want, sizeof(want), "pca9685 servo write");
}

// INA219 registers are big-endian: the register byte, then the value high byte first.
static void test_ina219_wire_is_big_endian(void)
{
    TEST_ASSERT_TRUE(pc_ina219_begin(0x40, 100, 100));
    uint32_t got = 0;
    const uint8_t *tx = pc_bus_host_written(&got);
    TEST_ASSERT_EQUAL_UINT32(3, got); // begin writes the calibration register and nothing else
    TEST_ASSERT_EQUAL_HEX8(INA219_REG_CALIBRATION, tx[0]);
    uint16_t cal = (uint16_t)(((uint16_t)tx[1] << 8) | tx[2]);
    TEST_ASSERT_EQUAL_UINT16(pc_ina219_calibration(100, 100), cal);
}

// An RTC read points at register 0 and burst-reads the seven time registers, so only the register
// pointer goes out.
static void test_rtc_read_wire(void)
{
    const uint8_t regs[7] = {0x05, 0x04, 0x03, 0x02, 0x02, 0x01, 0x24}; // 2024-01-02 03:04:05 BCD
    pc_bus_host_preload(regs, sizeof(regs));

    uint32_t epoch = pc_rtc_read_epoch();

    const uint8_t want[1] = {0x00};
    expect_tx(want, sizeof(want), "rtc register pointer");

    uint32_t expect = 0;
    TEST_ASSERT_TRUE(pc_rtc_regs_to_epoch(regs, &expect));
    TEST_ASSERT_EQUAL_UINT32(expect, epoch);
}

// An RTC write lays the register pointer in front of the seven registers, one transaction.
static void test_rtc_set_wire(void)
{
    uint32_t epoch = 1700000000u;
    TEST_ASSERT_TRUE(pc_rtc_set_epoch(epoch));

    uint8_t want[8];
    want[0] = 0x00;
    pc_rtc_epoch_to_regs(epoch, &want[1]);
    expect_tx(want, sizeof(want), "rtc set");
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

// An SMBus word is little-endian on the wire, unlike the big-endian sensor registers above.
static void test_smbus_word_is_little_endian(void)
{
    TEST_ASSERT_TRUE(pc_smbus_write_word(0x2A, 0x20, 0xBEEF));
    const uint8_t want[3] = {0x20, 0xEF, 0xBE};
    expect_tx(want, sizeof(want), "smbus write word");
}

// A read word sends the command and takes two bytes back, low byte first.
static void test_smbus_read_word_wire(void)
{
    const uint8_t reply[2] = {0xEF, 0xBE};
    pc_bus_host_preload(reply, sizeof(reply));
    uint16_t v = 0;
    TEST_ASSERT_TRUE(pc_smbus_read_word(0x2A, 0x20, &v));
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, v);
    const uint8_t want[1] = {0x20};
    expect_tx(want, sizeof(want), "smbus read word command");
}

// Nothing acknowledges on a host bus, so a scan reports an empty bus rather than inventing one,
// and it addressed every non-reserved address exactly once on the way there.
static void test_i2c_scan_probes_every_address(void)
{
    uint8_t found[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_i2c_scan(found, sizeof(found)));

    uint32_t want = PC_I2C_SCAN_LAST - PC_I2C_SCAN_FIRST + 1;
    TEST_ASSERT_EQUAL_UINT32(want, pc_bus_host_count());
    TEST_ASSERT_EQUAL_UINT16(PC_I2C_SCAN_FIRST, pc_bus_host_txn_at(0)->target);
    TEST_ASSERT_EQUAL_UINT32(0, pc_bus_host_txn_at(0)->wlen); // address cycle, no payload
    TEST_ASSERT_EQUAL_UINT16(PC_I2C_SCAN_LAST, pc_bus_host_txn_at(want - 1)->target);
}

// Each driver addresses its own device, which the per-transfer log is what shows: the byte stream
// alone cannot tell one part's traffic from another's on a shared bus.
static void test_transfers_carry_their_address(void)
{
    TEST_ASSERT_TRUE(pc_pca9685_set_pwm(0, 0, 0));
    TEST_ASSERT_TRUE(pc_smbus_write_byte(0x2A, 0x10, 0x5A));

    TEST_ASSERT_EQUAL_UINT32(2, pc_bus_host_count());
    TEST_ASSERT_EQUAL_UINT16(PC_PCA9685_I2C_ADDR, pc_bus_host_txn_at(0)->target);
    TEST_ASSERT_EQUAL_UINT16(0x2A, pc_bus_host_txn_at(1)->target);
    TEST_ASSERT_EQUAL_UINT8(PC_BUS_HOST_I2C, pc_bus_host_txn_at(0)->kind);
}

// An RTC read is one transaction, a write of the register pointer joined to a read of the seven
// registers. Two separate transfers would let another master interleave between them.
static void test_rtc_read_is_one_transaction(void)
{
    const uint8_t regs[7] = {0x05, 0x04, 0x03, 0x02, 0x02, 0x01, 0x24};
    pc_bus_host_preload(regs, sizeof(regs));
    (void)pc_rtc_read_epoch();

    TEST_ASSERT_EQUAL_UINT32(1, pc_bus_host_count());
    const pc_bus_host_rec *t = pc_bus_host_txn_at(0);
    TEST_ASSERT_EQUAL_UINT32(1, t->wlen);
    TEST_ASSERT_EQUAL_UINT32(7, t->rlen);
}

// The PCA9685 needs its oscillator to settle between the wake and the RESTART, which the driver
// spends real time on. The gap between those two writes is what proves the wait happened.
static void test_pca9685_begin_settles_the_oscillator(void)
{
    TEST_ASSERT_TRUE(pc_pca9685_begin(PC_PCA9685_I2C_ADDR, PC_PCA9685_FREQ));

    // sleep, prescale, wake, then RESTART and MODE2 after the settle.
    TEST_ASSERT_EQUAL_UINT32(5, pc_bus_host_count());
    uint32_t gap = pc_bus_host_gap_us(2, 3);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(500, gap, "oscillator settle was skipped");
}

// A device that does not acknowledge makes the driver report failure rather than a stale value.
static void test_failure_propagates(void)
{
    pc_bus_host_fail_next(1);
    int32_t mv = 0;
    TEST_ASSERT_FALSE(pc_ina219_read_bus_mv(&mv));
}

// SPI clocks both directions at once: a write names the outgoing span, a read takes what was
// queued and zero-fills past it.
static void test_spi_wire(void)
{
    TEST_ASSERT_TRUE(pc_spi_begin());
    const uint8_t out[3] = {0xDE, 0xAD, 0xBE};
    TEST_ASSERT_TRUE(pc_spi_write(out, sizeof(out)));
    expect_tx(out, sizeof(out), "spi write");

    const uint8_t reply[2] = {0x11, 0x22};
    pc_bus_host_preload(reply, sizeof(reply));
    uint8_t in[3] = {0xFF, 0xFF, 0xFF};
    TEST_ASSERT_TRUE(pc_spi_read(in, sizeof(in)));
    TEST_ASSERT_EQUAL_HEX8(0x11, in[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22, in[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, in[2]); // past what was queued, zero-filled
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
