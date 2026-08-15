// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the PMBus numeric encodings (server/peripherals/pmbus.h).
//
// Expected values come from the PMBus Power System Management Protocol Specification, Part II,
// Revision 1.3.1: section 7.3 and Figure 4 for LINEAR11, section 7.4.1 for DIRECT, section 8.3.1
// with Table 2 and Figures 7 to 10 for VOUT_MODE, section 8.4.1.1 for ULINEAR16, Table 15 for the
// STATUS_BYTE bits, and Table 31 (Appendix I) for the command codes and their SMBus transaction
// types.
//
// test_linear11_field_layout is the load-bearing case. Figure 4 splits the word into a 5-bit
// two's complement exponent above an 11-bit two's complement mantissa, and both fields have to
// sign-extend. Get either wrong and a -1 A current reads as +2047 A, or a 12 V rail reads as 768 V,
// with the reading still inside every plausible range a consumer would sanity-check against.

#include "server/peripherals/pmbus.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Section 7.3 / Figure 4: bits [15:11] are N, an 11-bit two's complement Y sits in bits [10:0].
//   0xD300 -> N = 0xD300 >> 11 = 0b11010 = 26, and 26 - 32 = -6
//             Y = 0xD300 & 0x7FF = 0x300 = 768 (bit 10 clear, so positive)
//   0x07FF -> N = 0, Y = 0x7FF = 2047 with bit 10 set, so 2047 - 2048 = -1
//   0x0400 -> Y = 1024 with bit 10 set: 1024 - 2048 = -1024, the most negative mantissa
//   0x03FF -> Y = 1023, the largest positive mantissa
void test_linear11_field_layout(void)
{
    TEST_ASSERT_EQUAL_INT16(768, protocore_pmbus_l11_mantissa(0xD300));
    TEST_ASSERT_EQUAL_INT8(-6, protocore_pmbus_l11_exponent(0xD300));

    TEST_ASSERT_EQUAL_INT16(-1, protocore_pmbus_l11_mantissa(0x07FF));
    TEST_ASSERT_EQUAL_INT8(0, protocore_pmbus_l11_exponent(0x07FF));

    TEST_ASSERT_EQUAL_INT16(-1024, protocore_pmbus_l11_mantissa(0x0400));
    TEST_ASSERT_EQUAL_INT16(1023, protocore_pmbus_l11_mantissa(0x03FF));
    TEST_ASSERT_EQUAL_INT16(0, protocore_pmbus_l11_mantissa(0xF800)); // exponent bits only

    // the exponent's own two's complement range, at both ends and either side of the sign bit
    TEST_ASSERT_EQUAL_INT8(0, protocore_pmbus_l11_exponent(0x0000));
    TEST_ASSERT_EQUAL_INT8(15, protocore_pmbus_l11_exponent((uint16_t)(15u << 11)));  // b01111
    TEST_ASSERT_EQUAL_INT8(-16, protocore_pmbus_l11_exponent((uint16_t)(16u << 11))); // b10000
    TEST_ASSERT_EQUAL_INT8(-1, protocore_pmbus_l11_exponent((uint16_t)(31u << 11)));  // b11111
}

// Section 7.3: X = Y * 2^N, returned in micro-units.
//   0xD300 -> 768 * 2^-6 = 12.0        -> 12 000 000
//   0x07FF ->  -1 * 2^0  = -1.0        -> -1 000 000
//   0x0019 ->  25 * 2^0  = 25.0        -> 25 000 000, a plausible READ_TEMPERATURE_1
//   0x07D8 -> -40 * 2^0  = -40.0       -> -40 000 000 (Y = 2008, 2008 - 2048 = -40)
//   0xE802 ->   2 * 2^-3 = 0.25        ->    250 000  (N = 0b11101 = 29, 29 - 32 = -3)
void test_linear11_decode(void)
{
    TEST_ASSERT_EQUAL_INT32(12000000, protocore_pmbus_linear11_micro(0xD300));
    TEST_ASSERT_EQUAL_INT32(-1000000, protocore_pmbus_linear11_micro(0x07FF));
    TEST_ASSERT_EQUAL_INT32(25000000, protocore_pmbus_linear11_micro(0x0019));
    TEST_ASSERT_EQUAL_INT32(-40000000, protocore_pmbus_linear11_micro(0x07D8));
    TEST_ASSERT_EQUAL_INT32(250000, protocore_pmbus_linear11_micro(0xE802));
    TEST_ASSERT_EQUAL_INT32(0, protocore_pmbus_linear11_micro(0x0000));
}

// Section 7.3 says devices "must accept and be able to process any value of N", so a word whose
// exponent scales the value out of an int32 of micro-units is reported invalid rather than wrapped
// into a small, plausible-looking reading.
void test_linear11_out_of_range_is_refused(void)
{
    // Y = 1023 at N = 15 is 1023 * 32768 = 33 521 664, which past the micro scaling is ~3.35e13.
    TEST_ASSERT_EQUAL_INT32(PROTOCORE_PMBUS_INVALID, protocore_pmbus_linear11_micro((uint16_t)((15u << 11) | 1023u)));
    // and the same at the negative end
    TEST_ASSERT_EQUAL_INT32(PROTOCORE_PMBUS_INVALID, protocore_pmbus_linear11_micro((uint16_t)((15u << 11) | 0x400u)));
    // 2147 A would fit; 2148 A would not, so the boundary is real rather than a blanket refusal
    TEST_ASSERT_EQUAL_INT32(2046000000, protocore_pmbus_linear11_micro((uint16_t)((1u << 11) | 1023u)));
}

// The encoder picks the exponent that keeps the most significant mantissa bits, so a round trip
// returns the value to within the 11 signed bits the format has - not to some coarser step.
void test_linear11_round_trip_keeps_the_mantissa_resolution(void)
{
    static const int32_t VALS[8] = {12000000, 1000000, 3300000, -5000000, 250000, 0, 48000000, -125000};
    for (size_t i = 0; i < 8; i++)
    {
        const uint16_t w = protocore_pmbus_linear11_encode(VALS[i]);
        const int32_t back = protocore_pmbus_linear11_micro(w);
        const int32_t err = back > VALS[i] ? back - VALS[i] : VALS[i] - back;
        const int32_t mag = VALS[i] < 0 ? -VALS[i] : VALS[i];
        TEST_ASSERT_TRUE_MESSAGE(err <= mag / 512 + 1, "round trip lost more than the mantissa resolution");
    }
    // exactly representable values come back exactly: 12 = 768 * 2^-6 is one of them
    TEST_ASSERT_EQUAL_INT32(12000000, protocore_pmbus_linear11_micro(protocore_pmbus_linear11_encode(12000000)));
    TEST_ASSERT_EQUAL_HEX16(0, protocore_pmbus_linear11_encode(0));
}

// Section 8.3.1, Table 2 with Figures 7 to 10: VOUT_MODE is a 3-bit Mode in bits [7:5] over a
// 5-bit Parameter. Mode 000b is ULINEAR16, 001b VID, 010b DIRECT and 011b IEEE half precision.
void test_vout_mode_selector(void)
{
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PMBUS_MODE_LINEAR, protocore_pmbus_vout_mode_kind(0x00));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PMBUS_MODE_LINEAR, protocore_pmbus_vout_mode_kind(0x17));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PMBUS_MODE_VID, protocore_pmbus_vout_mode_kind(0x20));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PMBUS_MODE_DIRECT, protocore_pmbus_vout_mode_kind(0x40));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PMBUS_MODE_IEEE, protocore_pmbus_vout_mode_kind(0x60));
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_PMBUS_MODE_LINEAR);
    TEST_ASSERT_EQUAL_UINT8(1, PROTOCORE_PMBUS_MODE_VID);
    TEST_ASSERT_EQUAL_UINT8(2, PROTOCORE_PMBUS_MODE_DIRECT);
    TEST_ASSERT_EQUAL_UINT8(3, PROTOCORE_PMBUS_MODE_IEEE);
}

// Table 2: the Parameter is the "five bit two's complement exponent for the mantissa". 0x17 is the
// value a part using 1.953125 mV steps reports: bits [4:0] = 0b10111 = 23, and 23 - 32 = -9.
void test_vout_mode_exponent(void)
{
    TEST_ASSERT_EQUAL_INT8(-9, protocore_pmbus_vout_exponent(0x17));
    TEST_ASSERT_EQUAL_INT8(0, protocore_pmbus_vout_exponent(0x00));
    TEST_ASSERT_EQUAL_INT8(15, protocore_pmbus_vout_exponent(0x0F));  // b01111, the largest positive
    TEST_ASSERT_EQUAL_INT8(-16, protocore_pmbus_vout_exponent(0x10)); // b10000, the most negative
    TEST_ASSERT_EQUAL_INT8(-1, protocore_pmbus_vout_exponent(0x1F));  // b11111
    // the Mode bits above it are not part of the exponent
    TEST_ASSERT_EQUAL_INT8(-9, protocore_pmbus_vout_exponent(0xF7));
}

// Section 8.4.1.1: Voltage = V * 2^N, V a 16-bit unsigned integer and N from VOUT_MODE.
//   614 at 2^-9 = 614 / 512 = 1.19921875 V -> 1 199 218 micro-units once truncated
//   512 at 2^-9 = 1.0 V exactly            -> 1 000 000
//     2 at 2^1  = 4.0 V                    -> 4 000 000
void test_ulinear16_decode(void)
{
    TEST_ASSERT_EQUAL_INT32(1199218, protocore_pmbus_linear16_micro(614, -9));
    TEST_ASSERT_EQUAL_INT32(1000000, protocore_pmbus_linear16_micro(512, -9));
    TEST_ASSERT_EQUAL_INT32(4000000, protocore_pmbus_linear16_micro(2, 1));
    TEST_ASSERT_EQUAL_INT32(0, protocore_pmbus_linear16_micro(0, -9));
    // section 8.1.1 restricts ULINEAR16 to positive values, so the mantissa is unsigned throughout.
    // Full scale is one step short of 2^16 * 2^-9 = 128 V: 65535 / 512 = 128 - 1/512 = 127.998046875,
    // which truncates to 127 998 046 micro-volts.
    TEST_ASSERT_EQUAL_INT32(127998046, protocore_pmbus_linear16_micro(65535, -9));
}

// Encode then decode returns the commanded voltage to within one mantissa step at the part's own
// exponent, which at 2^-9 is 1953 micro-volts.
void test_ulinear16_round_trip(void)
{
    static const int32_t VALS[5] = {1000000, 1200000, 3300000, 5000000, 12000000};
    for (size_t i = 0; i < 5; i++)
    {
        const uint16_t w = protocore_pmbus_linear16_encode(VALS[i], -9);
        const int32_t back = protocore_pmbus_linear16_micro(w, -9);
        const int32_t err = back > VALS[i] ? back - VALS[i] : VALS[i] - back;
        TEST_ASSERT_TRUE_MESSAGE(err <= 1953, "round trip lost more than one mantissa step");
    }
    // 1.0 V at 2^-9 is exactly mantissa 512
    TEST_ASSERT_EQUAL_HEX16(512, protocore_pmbus_linear16_encode(1000000, -9));
}

// Section 7.4.1: X = (1/m) * (Y * 10^-R - b), with m, Y and b two's complement integers and R the
// one-byte two's complement exponent.
//   Y=5,  m=1, b=0, R=0 -> (5 - 0) / 1        =  5
//   Y=4,  m=2, b=0, R=0 -> 4 / 2              =  2
//   Y=5,  m=1, b=0, R=1 -> (5 * 10^-1) / 1    =  0.5
//   Y=5,  m=1, b=1, R=0 -> (5 - 1) / 1        =  4
//   Y=50, m=1, b=0, R=-1 -> 50 * 10^1         =  500  (a negative R scales up)
void test_direct_format(void)
{
    TEST_ASSERT_EQUAL_INT32(5000000, protocore_pmbus_direct_micro(5, 1, 0, 0));
    TEST_ASSERT_EQUAL_INT32(2000000, protocore_pmbus_direct_micro(4, 2, 0, 0));
    TEST_ASSERT_EQUAL_INT32(500000, protocore_pmbus_direct_micro(5, 1, 0, 1));
    TEST_ASSERT_EQUAL_INT32(4000000, protocore_pmbus_direct_micro(5, 1, 1, 0));
    TEST_ASSERT_EQUAL_INT32(500000000, protocore_pmbus_direct_micro(50, 1, 0, -1));
    // Y is two's complement, so a word above 0x7FFF is a negative reading
    TEST_ASSERT_EQUAL_INT32(-5000000, protocore_pmbus_direct_micro(0xFFFB, 1, 0, 0));
    // a zero slope has no inverse
    TEST_ASSERT_EQUAL_INT32(PROTOCORE_PMBUS_INVALID, protocore_pmbus_direct_micro(5, 0, 0, 0));
}

// Table 15: STATUS_BYTE bit 7 BUSY, 6 OFF, 5 VOUT_OV_FAULT, 4 IOUT_OC_FAULT, 3 VIN_UV_FAULT,
// 2 TEMPERATURE, 1 CML, 0 NONE_OF_THE_ABOVE. Section 17.2: the STATUS_WORD's low byte is the same
// register, so a host reading the word finds these bits unmoved in its lower half.
void test_status_byte_bits(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, PROTOCORE_PMBUS_ST_NONE_ABOVE);
    TEST_ASSERT_EQUAL_HEX8(0x02, PROTOCORE_PMBUS_ST_CML);
    TEST_ASSERT_EQUAL_HEX8(0x04, PROTOCORE_PMBUS_ST_TEMPERATURE);
    TEST_ASSERT_EQUAL_HEX8(0x08, PROTOCORE_PMBUS_ST_VIN_UV);
    TEST_ASSERT_EQUAL_HEX8(0x10, PROTOCORE_PMBUS_ST_IOUT_OC);
    TEST_ASSERT_EQUAL_HEX8(0x20, PROTOCORE_PMBUS_ST_VOUT_OV);
    TEST_ASSERT_EQUAL_HEX8(0x40, PROTOCORE_PMBUS_ST_OFF);
    TEST_ASSERT_EQUAL_HEX8(0x80, PROTOCORE_PMBUS_ST_BUSY);

    // the eight are distinct single bits covering the byte
    const uint8_t all =
        (uint8_t)(PROTOCORE_PMBUS_ST_NONE_ABOVE | PROTOCORE_PMBUS_ST_CML | PROTOCORE_PMBUS_ST_TEMPERATURE |
                  PROTOCORE_PMBUS_ST_VIN_UV | PROTOCORE_PMBUS_ST_IOUT_OC | PROTOCORE_PMBUS_ST_VOUT_OV |
                  PROTOCORE_PMBUS_ST_OFF | PROTOCORE_PMBUS_ST_BUSY);
    TEST_ASSERT_EQUAL_HEX8(0xFF, all);
}

// Table 31, Appendix I: the command codes. A wrong code reads some other parameter and the value
// still decodes, so nothing downstream can tell.
void test_command_codes(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, PROTOCORE_PMBUS_PAGE);
    TEST_ASSERT_EQUAL_HEX8(0x01, PROTOCORE_PMBUS_OPERATION);
    TEST_ASSERT_EQUAL_HEX8(0x02, PROTOCORE_PMBUS_ON_OFF_CONFIG);
    TEST_ASSERT_EQUAL_HEX8(0x03, PROTOCORE_PMBUS_CLEAR_FAULTS);
    TEST_ASSERT_EQUAL_HEX8(0x19, PROTOCORE_PMBUS_CAPABILITY);
    TEST_ASSERT_EQUAL_HEX8(0x20, PROTOCORE_PMBUS_VOUT_MODE);
    TEST_ASSERT_EQUAL_HEX8(0x21, PROTOCORE_PMBUS_VOUT_COMMAND);
    TEST_ASSERT_EQUAL_HEX8(0x24, PROTOCORE_PMBUS_VOUT_MAX);
    TEST_ASSERT_EQUAL_HEX8(0x40, PROTOCORE_PMBUS_VOUT_OV_FAULT_LIM);
    TEST_ASSERT_EQUAL_HEX8(0x46, PROTOCORE_PMBUS_IOUT_OC_FAULT_LIM);
    TEST_ASSERT_EQUAL_HEX8(0x4F, PROTOCORE_PMBUS_OT_FAULT_LIMIT);
    TEST_ASSERT_EQUAL_HEX8(0x55, PROTOCORE_PMBUS_VIN_OV_FAULT_LIM);
    TEST_ASSERT_EQUAL_HEX8(0x78, PROTOCORE_PMBUS_STATUS_BYTE);
    TEST_ASSERT_EQUAL_HEX8(0x79, PROTOCORE_PMBUS_STATUS_WORD);
    TEST_ASSERT_EQUAL_HEX8(0x7A, PROTOCORE_PMBUS_STATUS_VOUT);
    TEST_ASSERT_EQUAL_HEX8(0x7B, PROTOCORE_PMBUS_STATUS_IOUT);
    TEST_ASSERT_EQUAL_HEX8(0x7C, PROTOCORE_PMBUS_STATUS_INPUT);
    TEST_ASSERT_EQUAL_HEX8(0x7D, PROTOCORE_PMBUS_STATUS_TEMP);
    TEST_ASSERT_EQUAL_HEX8(0x7E, PROTOCORE_PMBUS_STATUS_CML);
    TEST_ASSERT_EQUAL_HEX8(0x88, PROTOCORE_PMBUS_READ_VIN);
    TEST_ASSERT_EQUAL_HEX8(0x89, PROTOCORE_PMBUS_READ_IIN);
    TEST_ASSERT_EQUAL_HEX8(0x8B, PROTOCORE_PMBUS_READ_VOUT);
    TEST_ASSERT_EQUAL_HEX8(0x8C, PROTOCORE_PMBUS_READ_IOUT);
    TEST_ASSERT_EQUAL_HEX8(0x8D, PROTOCORE_PMBUS_READ_TEMP_1);
    TEST_ASSERT_EQUAL_HEX8(0x8E, PROTOCORE_PMBUS_READ_TEMP_2);
    TEST_ASSERT_EQUAL_HEX8(0x90, PROTOCORE_PMBUS_READ_FAN_SPEED_1);
    TEST_ASSERT_EQUAL_HEX8(0x96, PROTOCORE_PMBUS_READ_POUT);
    TEST_ASSERT_EQUAL_HEX8(0x97, PROTOCORE_PMBUS_READ_PIN);
    TEST_ASSERT_EQUAL_HEX8(0x99, PROTOCORE_PMBUS_MFR_ID);
    TEST_ASSERT_EQUAL_HEX8(0x9A, PROTOCORE_PMBUS_MFR_MODEL);
    TEST_ASSERT_EQUAL_HEX8(0x9B, PROTOCORE_PMBUS_MFR_REVISION);
}
