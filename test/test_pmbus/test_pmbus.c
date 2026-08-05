// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// PMBus 1.3 numeric encodings. Every value a part reports arrives in one of three formats, and all
// three fold a power-of-two exponent into the word, so the decode is a shift and the sign
// extension of a 5-bit and an 11-bit field is where it goes wrong.

#include "services/peripherals/pmbus.h"
#include "services/peripherals/smbus.h" // pc_smbus_set_pec: the wire shape depends on it
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// VOUT_MODE splits into a 3-bit format selector and a 5-bit signed exponent.
static void test_vout_mode(void)
{
    TEST_ASSERT_EQUAL_UINT8(PC_PMBUS_MODE_LINEAR, pc_pmbus_vout_mode_kind(0x17));
    TEST_ASSERT_EQUAL_INT8(-9, pc_pmbus_vout_exponent(0x17)); // 0b10111 sign-extends to -9
    TEST_ASSERT_EQUAL_INT8(0, pc_pmbus_vout_exponent(0x00));
    TEST_ASSERT_EQUAL_INT8(15, pc_pmbus_vout_exponent(0x0F));  // the largest positive
    TEST_ASSERT_EQUAL_INT8(-16, pc_pmbus_vout_exponent(0x10)); // the most negative
    TEST_ASSERT_EQUAL_UINT8(PC_PMBUS_MODE_VID, pc_pmbus_vout_mode_kind(0x20));
    TEST_ASSERT_EQUAL_UINT8(PC_PMBUS_MODE_DIRECT, pc_pmbus_vout_mode_kind(0x40));
}

// A LINEAR11 word carries an 11-bit signed mantissa and a 5-bit signed exponent.
static void test_linear11_fields(void)
{
    TEST_ASSERT_EQUAL_INT16(768, pc_pmbus_l11_mantissa(0xD300));
    TEST_ASSERT_EQUAL_INT8(-6, pc_pmbus_l11_exponent(0xD300));
    TEST_ASSERT_EQUAL_INT16(-1, pc_pmbus_l11_mantissa(0x07FF)); // 0x7FF sign-extends to -1
    TEST_ASSERT_EQUAL_INT8(0, pc_pmbus_l11_exponent(0x07FF));
    TEST_ASSERT_EQUAL_INT16(-1024, pc_pmbus_l11_mantissa(0x0400)); // the most negative mantissa
    TEST_ASSERT_EQUAL_INT16(1023, pc_pmbus_l11_mantissa(0x03FF));  // the largest positive
}

// 768 * 2^-6 is 12.0, which is 12000000 in micro-units.
static void test_linear11_decode(void)
{
    TEST_ASSERT_EQUAL_INT32(12000000, pc_pmbus_linear11_micro(0xD300));
    TEST_ASSERT_EQUAL_INT32(0, pc_pmbus_linear11_micro(0x0000));
    TEST_ASSERT_EQUAL_INT32(-1000000, pc_pmbus_linear11_micro(0x07FF));
}

// Encoding picks the exponent that keeps the most significant bits, so a round trip returns the
// value rather than a coarser one.
static void test_linear11_round_trip(void)
{
    const int32_t vals[] = {12000000, 1000000, 3300000, -5000000, 250000, 0};
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
    {
        uint16_t w = pc_pmbus_linear11_encode(vals[i]);
        int32_t back = pc_pmbus_linear11_micro(w);
        // The mantissa holds 11 signed bits, so a round trip is exact only to that resolution.
        int32_t err = back > vals[i] ? back - vals[i] : vals[i] - back;
        int32_t tol = (vals[i] < 0 ? -vals[i] : vals[i]) / 512 + 1;
        TEST_ASSERT_TRUE_MESSAGE(err <= tol, "round trip lost more than the mantissa's resolution");
    }
}

// LINEAR16 is an unsigned mantissa scaled by an exponent the part reports separately.
static void test_linear16(void)
{
    // 614 * 2^-9 is 1.19921875, which truncates to 1199218 micro-units.
    TEST_ASSERT_EQUAL_INT32(1199218, pc_pmbus_linear16_micro(614, -9));
    TEST_ASSERT_EQUAL_INT32(0, pc_pmbus_linear16_micro(0, -9));
    // A positive exponent scales up rather than down.
    TEST_ASSERT_EQUAL_INT32(4000000, pc_pmbus_linear16_micro(2, 1));
}

static void test_linear16_round_trip(void)
{
    const int32_t vals[] = {1200000, 3300000, 5000000, 1000000};
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
    {
        uint16_t w = pc_pmbus_linear16_encode(vals[i], -9);
        int32_t back = pc_pmbus_linear16_micro(w, -9);
        int32_t err = back > vals[i] ? back - vals[i] : vals[i] - back;
        TEST_ASSERT_TRUE_MESSAGE(err <= 2000, "round trip lost more than one mantissa step");
    }
}

// A value the exponent pushes out of an int32 of micro-units is refused rather than wrapped.
static void test_out_of_range_refused(void)
{
    // mantissa 1023 at exponent 15 is 33521664, which past the micro scaling leaves an int32.
    TEST_ASSERT_EQUAL_INT32(PC_PMBUS_INVALID, pc_pmbus_linear11_micro((uint16_t)((15u << 11) | 1023u)));
}

// DIRECT is (Y / 10^R - b) / m, with the coefficients coming from the part.
static void test_direct(void)
{
    TEST_ASSERT_EQUAL_INT32(5000000, pc_pmbus_direct_micro(5, 1, 0, 0));
    TEST_ASSERT_EQUAL_INT32(2000000, pc_pmbus_direct_micro(4, 2, 0, 0));
    TEST_ASSERT_EQUAL_INT32(500000, pc_pmbus_direct_micro(5, 1, 0, 1)); // R shifts the decimal
    TEST_ASSERT_EQUAL_INT32(4000000, pc_pmbus_direct_micro(5, 1, 1, 0)); // b offsets first
    // A zero slope has no inverse, so it is refused rather than dividing.
    TEST_ASSERT_EQUAL_INT32(PC_PMBUS_INVALID, pc_pmbus_direct_micro(5, 0, 0, 0));
}

// Selecting a page is a write byte of the PAGE command, which is what a multi-rail part needs
// before any reading means anything.
static void test_set_page_wire(void)
{
    pc_smbus_set_pec(PROTO_FALSE);
    pc_bus_host_reset();
    TEST_ASSERT_TRUE(pc_pmbus_set_page(0x40, 1));

    uint32_t n = 0;
    const uint8_t *tx = pc_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(2, n);
    TEST_ASSERT_EQUAL_HEX8(PC_PMBUS_PAGE, tx[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx[1]);
}

// A telemetry read sends the command code and decodes the LINEAR11 word the part answers with.
static void test_read_linear11_wire(void)
{
    pc_smbus_set_pec(PROTO_FALSE);
    pc_bus_host_reset();
    const uint8_t reply[2] = {0x00, 0xD3}; // 0xD300 little-endian: 768 * 2^-6 = 12.0
    pc_bus_host_preload(reply, sizeof(reply));

    int32_t v = 0;
    TEST_ASSERT_TRUE(pc_pmbus_read_linear11(0x40, PC_PMBUS_READ_VIN, &v));
    TEST_ASSERT_EQUAL_INT32(12000000, v);

    uint32_t n = 0;
    const uint8_t *tx = pc_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_HEX8(PC_PMBUS_READ_VIN, tx[0]);
}

// Clearing faults is a send byte: the command code alone, with no data after it.
static void test_clear_faults_wire(void)
{
    pc_smbus_set_pec(PROTO_FALSE);
    pc_bus_host_reset();
    TEST_ASSERT_TRUE(pc_pmbus_clear_faults(0x40));

    uint32_t n = 0;
    const uint8_t *tx = pc_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_HEX8(PC_PMBUS_CLEAR_FAULTS, tx[0]);
}

// Writing an output voltage encodes it against the exponent the part reports, low byte first.
static void test_write_linear16_wire(void)
{
    pc_smbus_set_pec(PROTO_FALSE);
    pc_bus_host_reset();
    TEST_ASSERT_TRUE(pc_pmbus_write_linear16(0x40, PC_PMBUS_VOUT_COMMAND, -9, 1200000));

    uint16_t want = pc_pmbus_linear16_encode(1200000, -9);
    uint32_t n = 0;
    const uint8_t *tx = pc_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(3, n);
    TEST_ASSERT_EQUAL_HEX8(PC_PMBUS_VOUT_COMMAND, tx[0]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(want & 0xFF), tx[1]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(want >> 8), tx[2]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_vout_mode);
    RUN_TEST(test_linear11_fields);
    RUN_TEST(test_linear11_decode);
    RUN_TEST(test_linear11_round_trip);
    RUN_TEST(test_linear16);
    RUN_TEST(test_linear16_round_trip);
    RUN_TEST(test_out_of_range_refused);
    RUN_TEST(test_direct);
    RUN_TEST(test_set_page_wire);
    RUN_TEST(test_read_linear11_wire);
    RUN_TEST(test_clear_faults_wire);
    RUN_TEST(test_write_linear16_wire);
    return UNITY_END();
}
