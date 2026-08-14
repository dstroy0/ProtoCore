// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the ICCP / TASE.2 (IEC 60870-6) indication-point codec (services/energy/iccp/iccp.h).
//
// IEC 60870-6-802 is not obtainable, so the TASE.2 tag assignments and the state / quality wire
// values are taken from the module's own documented Data_Value structure rather than from the
// standard, and this header says so. What IS standard here is the encoding: TASE.2 rides on MMS,
// which is BER, so every length and every INTEGER below is derived from ITU-T X.690 - sec 8.1.3.4
// short-form definite length, sec 8.3.2 minimal two's-complement INTEGER contents.
//
// test_real_q_integer_is_minimal_twos_complement is the load-bearing case: X.690 sec 8.3.2 fixes
// exactly how many octets a signed value takes and where the sign pad goes, and a control center
// reading one octet too few or too many gets a different megawatt figure, not a parse error.

#include "services/energy/iccp/iccp.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t TIME[4] = {0x2E, 0xBC, 0x5C, 0x61}; // a 4-octet TimeStamp, big-endian

// StateQ: the state occupies the two high bits of the stateAndQuality octet and the quality flags
// the two low ones, wrapped as `A2 { 85 <octet> }`. With no timestamp the whole value is 5 octets:
// the A2 tag, its length 3, and the 3-octet 85 TLV.
void test_state_q_ber_layout(void)
{
    uint8_t out[32];
    size_t n = protocore_iccp_state_q(ICCP_STATE_ON, ICCP_QUAL_VALID, NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(5u, n);
    TEST_ASSERT_EQUAL_HEX8(0xA2u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x85u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0x80u, out[4]); // state 2 in bits 7-6, quality 0 in bits 1-0
}

// The optional TimeStamp appends a 4-octet `17` TLV inside the same A2, taking the value from 3 to
// 9 octets and the whole encoding to 11.
void test_state_q_carries_an_optional_timestamp(void)
{
    uint8_t out[32];
    size_t n = protocore_iccp_state_q(ICCP_STATE_OFF, ICCP_QUAL_SUSPECT, TIME, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(11u, n);
    TEST_ASSERT_EQUAL_HEX8(0xA2u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x09u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x85u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0x42u, out[4]); // state 1 -> 0x40, quality 2 -> 0x02
    TEST_ASSERT_EQUAL_HEX8(0x17u, out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x04u, out[6]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TIME, out + 7, 4);
}

// Every state / quality pair lands in its own bit field, and neither can reach into the other.
void test_state_and_quality_occupy_separate_fields(void)
{
    static const uint8_t STATES[] = {ICCP_STATE_BETWEEN, ICCP_STATE_OFF, ICCP_STATE_ON, ICCP_STATE_INVALID};
    static const uint8_t QUALS[] = {ICCP_QUAL_VALID, ICCP_QUAL_HELD, ICCP_QUAL_SUSPECT, ICCP_QUAL_NOTVALID};
    for (size_t s = 0; s < 4; s++)
    {
        for (size_t q = 0; q < 4; q++)
        {
            uint8_t out[16];
            TEST_ASSERT_EQUAL_UINT(5u, protocore_iccp_state_q(STATES[s], QUALS[q], NULL, out, sizeof(out)));
            TEST_ASSERT_EQUAL_HEX8((uint8_t)((STATES[s] << 6) | QUALS[q]), out[4]);
        }
    }

    // Values wider than the fields are masked, never allowed to spill into the neighbouring bits.
    uint8_t out[16];
    TEST_ASSERT_EQUAL_UINT(5u, protocore_iccp_state_q(0xFFu, 0xFFu, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0xC3u, out[4]);
}

// RealQ wraps `A3 { 02 <INTEGER> 85 <quality> }`. 12345 milli-units is 0x3039, whose top octet has
// no sign bit, so X.690 sec 8.3.2 gives it two content octets with no pad.
void test_real_q_ber_layout(void)
{
    uint8_t out[32];
    size_t n = protocore_iccp_real_q(12345, ICCP_QUAL_VALID, NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(9u, n);
    TEST_ASSERT_EQUAL_HEX8(0xA3u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x07u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0x30u, out[4]);
    TEST_ASSERT_EQUAL_HEX8(0x39u, out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x85u, out[6]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[7]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[8]);

    // With a timestamp the 17 TLV follows the quality, inside the same A3.
    n = protocore_iccp_real_q(12345, ICCP_QUAL_HELD, TIME, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(15u, n);
    TEST_ASSERT_EQUAL_HEX8(0x0Du, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[8]); // quality
    TEST_ASSERT_EQUAL_HEX8(0x17u, out[9]);
    TEST_ASSERT_EQUAL_HEX8(0x04u, out[10]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TIME, out + 11, 4);
}

// X.690 sec 8.3.2: "the bits of the first octet and bit 8 of the second octet ... shall not all be
// ones and shall not all be zero" - the minimal two's-complement form. Each expectation below is the
// value written big-endian with redundant sign octets removed, one octet before the sign flips.
void test_real_q_integer_is_minimal_twos_complement(void)
{
    struct
    {
        int32_t milli;
        size_t len;
        uint8_t bytes[4];
    } static const CASES[] = {
        {0, 1, {0x00, 0, 0, 0}},                    // 0 is one zero octet, never none
        {1, 1, {0x01, 0, 0, 0}},                    //
        {127, 1, {0x7F, 0, 0, 0}},                  // the widest positive value in one octet
        {128, 2, {0x00, 0x80, 0, 0}},               // 0x80 alone would read as -128, so a 0x00 pad
        {255, 2, {0x00, 0xFF, 0, 0}},               //
        {-1, 1, {0xFF, 0, 0, 0}},                   // all-ones octets collapse to one
        {-128, 1, {0x80, 0, 0, 0}},                 // the narrowest negative value in one octet
        {-129, 2, {0xFF, 0x7F, 0, 0}},              // 0x7F alone would read as +127, so a 0xFF pad
        {32767, 2, {0x7F, 0xFF, 0, 0}},             //
        {-32768, 2, {0x80, 0x00, 0, 0}},            //
        {8388607, 3, {0x7F, 0xFF, 0xFF, 0}},        // 2^23 - 1
        {2147483647, 4, {0x7F, 0xFF, 0xFF, 0xFF}},  // INT32_MAX
        {-2147483647 - 1, 4, {0x80, 0, 0, 0}},      // INT32_MIN
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t out[32];
        size_t n = protocore_iccp_real_q(CASES[i].milli, ICCP_QUAL_VALID, NULL, out, sizeof(out));
        // A3 + len + (02 + len + content) + (85 01 00)
        TEST_ASSERT_EQUAL_UINT(2u + 2u + CASES[i].len + 3u, n);
        TEST_ASSERT_EQUAL_HEX8(0x02u, out[2]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)CASES[i].len, out[3]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(CASES[i].bytes, out + 4, CASES[i].len);
        TEST_ASSERT_EQUAL_HEX8(0x85u, out[4 + CASES[i].len]);
    }
}

// The quality octet is masked to its two bits in RealQ too, so a caller passing a wider flag word
// cannot corrupt the value that precedes it.
void test_real_q_quality_is_masked(void)
{
    // milli 1 is one content octet, so the value is 02 01 01 + 85 01 <q> and the quality lands last.
    uint8_t out[32];
    TEST_ASSERT_EQUAL_UINT(8u, protocore_iccp_real_q(1, 0xFCu, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x85u, out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[7]);
    TEST_ASSERT_EQUAL_UINT(8u, protocore_iccp_real_q(1, ICCP_QUAL_NOTVALID, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x03u, out[7]);
}

// A buffer that cannot hold the encoding writes nothing and reports 0, so a caller never wraps a
// half-written Data_Value in an MMS Read response.
void test_build_refuses_an_undersized_buffer(void)
{
    uint8_t out[32];
    TEST_ASSERT_EQUAL_UINT(5u, protocore_iccp_state_q(ICCP_STATE_ON, ICCP_QUAL_VALID, NULL, out, 5u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iccp_state_q(ICCP_STATE_ON, ICCP_QUAL_VALID, NULL, out, 4u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iccp_state_q(ICCP_STATE_ON, ICCP_QUAL_VALID, TIME, out, 10u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iccp_state_q(ICCP_STATE_ON, ICCP_QUAL_VALID, NULL, NULL, sizeof(out)));

    TEST_ASSERT_EQUAL_UINT(9u, protocore_iccp_real_q(12345, ICCP_QUAL_VALID, NULL, out, 9u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iccp_real_q(12345, ICCP_QUAL_VALID, NULL, out, 8u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iccp_real_q(12345, ICCP_QUAL_VALID, TIME, out, 14u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iccp_real_q(12345, ICCP_QUAL_VALID, NULL, NULL, sizeof(out)));
}
