// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the LonWorks network-variable codec (services/fieldbus/lonworks/lonworks.h).
//
// Two documents govern, both obtained and read for this suite:
//   LonTalk Protocol Specification, Echelon Corp (created 1989-1994), the published text of the
//   protocol standardized as ANSI/CEA-709.1 and ISO/IEC 14908-1. Sec 10.2 (p 69), sec 10.4 (p 70-71,
//   "APDU Types and Formats"), sec 10.7 (p 76). http://www.stitcs.com/en/LonWorks/Lontalk%20Protocol%20Spec.pdf
//   LONMARK International SNVT Master List, Version 15 Revision 00, November 2014. SNVT_switch
//   index 95 (p 365-367), SNVT_temp index 39 (p 372). https://www.lonmark.org/wp-content/uploads/2020/01/snvt.pdf
//
// The load-bearing case is test_lontalk_nv_header_is_two_octets. Sec 10.2: "A special two byte
// header is used to convey the presentation layer information that the APDU is to be interpreted as
// a network variable." Sec 10.4: the APDU header "is a single byte which is followed by a second
// byte only if the header specifies that network variable information is to follow", and
// "1dxxxxxx a network variable message; 'd' indicates direction: 1 for outgoing, 0 for incoming.
// The remaining code bits are combined with the first data byte to form a 14 bit network variable
// selector."
//
// THREE CASES FAIL AGAINST THE SHIPPED CODE, all from one defect: lonworks.c builds and parses a
// three-octet header, [msg-code][sel_hi][sel_lo], where the published APDU has a two-octet header
// that packs the message bit, the direction bit and the top six selector bits into one octet.
//   test_lontalk_nv_header_is_two_octets                lonworks.c:21,26-28 emit 3 + value_len
//   test_lontalk_parse_reads_the_two_octet_header       lonworks.c:43 reads the selector from 1..2
//   test_lontalk_a_poll_response_is_header_only         lonworks.c:38 refuses a 2-octet APDU
//
// The same defect makes LON_MSG_NV_UPDATE (0x80) and LON_MSG_NV_POLL (0x81) unassertable: sec 10.4
// spends the low six bits of that octet on the selector, so 0x81 is not a second message type, and
// the published protocol has no NV poll APDU code at all (sec 10.7 makes a poll a request-response
// on the same selector). Only the one bit sec 10.4 does fix is asserted here.

#include "services/fieldbus/lonworks/lonworks.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Sec 10.4 destin_type table: "1dxxxxxx a network variable message", so bit 7 marks the APDU as a
// network variable message whatever else the octet carries.
//
// The selector is 14 bits, so it counts 0 .. 2^14 - 1 = 16383 = 0x3FFF.
void test_lontalk_nv_message_bit_and_selector_width(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x80u, (uint8_t)(LON_MSG_NV_UPDATE & 0x80u));
    TEST_ASSERT_EQUAL_HEX8(0x80u, (uint8_t)(LON_MSG_NV_POLL & 0x80u));
    TEST_ASSERT_EQUAL_HEX16(0x3FFFu, LON_NV_SELECTOR_MAX);
}

// Sec 10.2 and sec 10.4: the network-variable header is two octets, the second holding the low
// eight bits of the selector and the first holding the message bit, the direction bit and the top
// six. Selector 0x1234 = 01 0010 0011 0100 splits as
//   selector >> 8 = 0b010010 = 0x12   into the low six bits of octet 0
//   selector      = 0b00110100 = 0x34 into octet 1
// so an outgoing update (d = 1) reads 1 1 010010 = 0xD2, 0x34 and an incoming one 1 0 010010 = 0x92,
// 0x34. This API has no direction argument, so only the message bit and the selector are asserted.
void test_lontalk_nv_header_is_two_octets(void)
{
    static const uint8_t VALUE[3] = {0xAA, 0xBB, 0xCC};
    uint8_t out[16];

    TEST_ASSERT_EQUAL_size_t(
        2u + sizeof(VALUE), protocore_lon_build_nv(LON_MSG_NV_UPDATE, 0x1234u, VALUE, sizeof(VALUE), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x80u, (uint8_t)(out[0] & 0x80u));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, (uint16_t)(((uint16_t)(out[0] & 0x3Fu) << 8) | out[1]));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(VALUE, out + 2, sizeof(VALUE));
}

// The same two-octet header, read back: 0xD2 = 1 1 010010 is an outgoing network variable message
// carrying selector bits 13..8, and 0x34 carries bits 7..0, so the selector is 0x1234 and every
// octet after the second is the value.
void test_lontalk_parse_reads_the_two_octet_header(void)
{
    static const uint8_t PDU[4] = {0xD2, 0x34, 0xAA, 0xBB};
    LonNv nv;
    memset(&nv, 0, sizeof(nv));

    TEST_ASSERT_TRUE(protocore_lon_parse_nv(PDU, sizeof(PDU), &nv));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, nv.selector);
    TEST_ASSERT_EQUAL_size_t(2u, nv.value_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PDU + 2, nv.value, 2);
}

// Sec 10.7: "When the response is to a network variable poll the response shall contain the network
// variable selector, but shall not have any associated data." A poll response is therefore the
// two-octet header alone, and a codec that cannot read one cannot read a poll response.
void test_lontalk_a_poll_response_is_header_only(void)
{
    static const uint8_t PDU[2] = {0x92, 0x34};
    LonNv nv;
    memset(&nv, 0, sizeof(nv));

    TEST_ASSERT_TRUE(protocore_lon_parse_nv(PDU, sizeof(PDU), &nv));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, nv.selector);
    TEST_ASSERT_EQUAL_size_t(0u, nv.value_len);
}

// A 14-bit selector cannot carry 0x4000, so the build refuses it, and every value it can carry
// survives a build followed by a parse whatever framing the codec uses.
void test_nv_selector_bounds_and_round_trip(void)
{
    uint8_t out[8];
    LonNv nv;

    TEST_ASSERT_EQUAL_size_t(0u, protocore_lon_build_nv(LON_MSG_NV_UPDATE, 0x4000u, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_lon_build_nv(LON_MSG_NV_UPDATE, 0xFFFFu, NULL, 0, out, sizeof(out)));

    for (uint32_t s = 0; s <= LON_NV_SELECTOR_MAX; s++)
    {
        size_t n = protocore_lon_build_nv(LON_MSG_NV_UPDATE, (uint16_t)s, NULL, 0, out, sizeof(out));
        TEST_ASSERT_TRUE(n != 0u);
        TEST_ASSERT_TRUE(protocore_lon_parse_nv(out, n, &nv));
        TEST_ASSERT_EQUAL_HEX16((uint16_t)s, nv.selector);
    }
}

// The value comes back octet for octet, and the length the build reports is the length the parse
// walks: neither depends on where the header ends.
void test_nv_value_round_trip(void)
{
    static const uint8_t VALUE[5] = {0x00, 0x7F, 0x80, 0xFF, 0x01};
    uint8_t out[16];
    LonNv nv;

    size_t n = protocore_lon_build_nv(LON_MSG_NV_UPDATE, 0x2AAAu, VALUE, sizeof(VALUE), out, sizeof(out));
    TEST_ASSERT_TRUE(n != 0u);
    TEST_ASSERT_TRUE(protocore_lon_parse_nv(out, n, &nv));
    TEST_ASSERT_EQUAL_HEX16(0x2AAAu, nv.selector);
    TEST_ASSERT_EQUAL_size_t(sizeof(VALUE), nv.value_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(VALUE, nv.value, sizeof(VALUE));
}

// SNVT Master List, SNVT_temp (index 39, size 2, Neuron C unsigned long, Minimum 0, Maximum 65535):
//   "Scaling (A,B,C): 1, -1, -2740"   "Scaled value: 1 * 10^-1 * (Raw + -2740)"   "Resolution: 0.1"
// and the overview, "SNVT_temp represents tenths of a degree Celsius above -274 C".
//
// LonTalk sec 10.4: "Any long or quad quantities stored in the APDU are stored with the most
// significant bit on the left", so the two octets are big-endian.
//
//   Raw 0      -> 0.1 * (0     - 2740) = -274.0
//   Raw 2740   -> 0.1 * (2740  - 2740) =    0.0    2740 = 0x0AB4
//   Raw 65535  -> 0.1 * (65535 - 2740) = 6279.5
//   20.0 C     -> Raw = 20.0 / 0.1 + 2740 = 2940 = 0x0B7C
//   20.1 C     -> Raw = 2941 = 0x0B7D, one count of the published 0.1 resolution
void test_snvt_temp_published_scaling(void)
{
    static const uint8_t MIN[2] = {0x00, 0x00};
    static const uint8_t ZERO_C[2] = {0x0A, 0xB4};
    static const uint8_t MAX[2] = {0xFF, 0xFF};

    TEST_ASSERT_EQUAL_DOUBLE(-274.0, protocore_lon_snvt_temp_decode(MIN));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, protocore_lon_snvt_temp_decode(ZERO_C));
    TEST_ASSERT_EQUAL_DOUBLE(6279.5, protocore_lon_snvt_temp_decode(MAX));

    uint8_t enc[2];
    protocore_lon_snvt_temp_encode(-274.0, enc);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MIN, enc, 2);
    protocore_lon_snvt_temp_encode(0.0, enc);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ZERO_C, enc, 2);
    protocore_lon_snvt_temp_encode(6279.5, enc);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MAX, enc, 2);

    protocore_lon_snvt_temp_encode(20.0, enc);
    TEST_ASSERT_EQUAL_HEX8(0x0Bu, enc[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7Cu, enc[1]);
    protocore_lon_snvt_temp_encode(20.1, enc);
    TEST_ASSERT_EQUAL_HEX8(0x7Du, enc[1]);
}

// The published Minimum is 0 and Maximum 65535, which the scaling maps to -274.0 C and 6279.5 C, so
// a temperature outside that band has no raw value and stops at the end of the range. Every raw the
// field can hold decodes and re-encodes to itself.
void test_snvt_temp_saturates_at_the_published_bounds(void)
{
    uint8_t enc[2];
    protocore_lon_snvt_temp_encode(-1000.0, enc);
    TEST_ASSERT_EQUAL_HEX8(0x00u, enc[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, enc[1]);
    TEST_ASSERT_EQUAL_DOUBLE(-274.0, protocore_lon_snvt_temp_decode(enc));

    protocore_lon_snvt_temp_encode(1e9, enc);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, enc[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, enc[1]);
    TEST_ASSERT_EQUAL_DOUBLE(6279.5, protocore_lon_snvt_temp_decode(enc));

    for (uint32_t raw = 0; raw <= 65535u; raw += 271u)
    {
        uint8_t in[2] = {(uint8_t)(raw >> 8), (uint8_t)raw};
        uint8_t back[2];
        protocore_lon_snvt_temp_encode(protocore_lon_snvt_temp_decode(in), back);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(in, back, 2);
    }
}

// SNVT Master List, SNVT_switch (index 95, size 2, a structure of value then state, stored in that
// order by LonTalk sec 10.4, "Structure fields are also stored left to right"):
//   value  unsigned short, Minimum 0, Maximum 200, "Scaling (A,B,C): 5, -1, 0",
//          "Scaled value: 5 * 10^-1 * (Raw + 0)", "Resolution: 0.5"
//   state  signed short, Minimum -1, Maximum 1, "This field can either be -1 (NULL), 0 (OFF), or
//          1 (ON)", and "A state value of 0xFF indicates the switch value is undefined"
// The Output Network Variable table publishes the two discrete points verbatim: "0  0  off" and
// "200 (0xC8)  1  on".
//
//   100.0 % -> Raw = 100.0 / 0.5 = 200 = 0xC8
//    50.0 % -> Raw = 100 = 0x64
//     0.5 % -> Raw = 1, one count of the published 0.5 resolution
void test_snvt_switch_published_states(void)
{
    uint8_t enc[2];

    protocore_lon_snvt_switch_encode(0.0, 0, enc);
    TEST_ASSERT_EQUAL_HEX8(0x00u, enc[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, enc[1]);

    protocore_lon_snvt_switch_encode(100.0, 1, enc);
    TEST_ASSERT_EQUAL_HEX8(0xC8u, enc[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, enc[1]);

    protocore_lon_snvt_switch_encode(50.0, 1, enc);
    TEST_ASSERT_EQUAL_HEX8(0x64u, enc[0]);
    protocore_lon_snvt_switch_encode(0.5, 1, enc);
    TEST_ASSERT_EQUAL_HEX8(0x01u, enc[0]);

    double pct = -1.0;
    uint8_t state = 0xAA;
    protocore_lon_snvt_switch_decode(enc, &pct, &state);
    TEST_ASSERT_EQUAL_DOUBLE(0.5, pct);
    TEST_ASSERT_EQUAL_HEX8(0x01u, state);

    // -1 in a signed 8-bit field is 0xFF, the published NULL state.
    protocore_lon_snvt_switch_encode(100.0, 0xFF, enc);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, enc[1]);
    protocore_lon_snvt_switch_decode(enc, NULL, &state);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, state);
    protocore_lon_snvt_switch_decode(enc, &pct, NULL);
    TEST_ASSERT_EQUAL_DOUBLE(100.0, pct);
}

// The published Maximum for the value field is 200 (100.0 %), so a level past 100 % has no raw
// value and stops there. Every raw in 0..200 decodes and re-encodes to itself.
void test_snvt_switch_clamps_to_the_published_range(void)
{
    uint8_t enc[2];
    protocore_lon_snvt_switch_encode(-5.0, 1, enc);
    TEST_ASSERT_EQUAL_HEX8(0x00u, enc[0]);
    protocore_lon_snvt_switch_encode(200.0, 1, enc);
    TEST_ASSERT_EQUAL_HEX8(0xC8u, enc[0]);
    protocore_lon_snvt_switch_encode(100.5, 1, enc);
    TEST_ASSERT_EQUAL_HEX8(0xC8u, enc[0]);

    for (unsigned raw = 0; raw <= 200u; raw++)
    {
        uint8_t in[2] = {(uint8_t)raw, 1};
        uint8_t back[2];
        double pct = 0;
        protocore_lon_snvt_switch_decode(in, &pct, NULL);
        protocore_lon_snvt_switch_encode(pct, 1, back);
        TEST_ASSERT_EQUAL_HEX8(in[0], back[0]);
    }
}

// A buffer one octet short of the PDU refuses rather than truncates, a null on either side of the
// call is refused, and a PDU too short to hold the two-octet header of sec 10.4 is not a PDU.
void test_guards(void)
{
    uint8_t out[8];
    static const uint8_t VALUE[3] = {1, 2, 3};
    LonNv nv;

    size_t n = protocore_lon_build_nv(LON_MSG_NV_UPDATE, 1, VALUE, sizeof(VALUE), out, sizeof(out));
    TEST_ASSERT_TRUE(n != 0u);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_lon_build_nv(LON_MSG_NV_UPDATE, 1, VALUE, sizeof(VALUE), out, n - 1u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_lon_build_nv(LON_MSG_NV_UPDATE, 1, VALUE, sizeof(VALUE), NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_lon_build_nv(LON_MSG_NV_UPDATE, 1, NULL, sizeof(VALUE), out, sizeof(out)));

    TEST_ASSERT_FALSE(protocore_lon_parse_nv(out, 1, &nv));
    TEST_ASSERT_FALSE(protocore_lon_parse_nv(NULL, n, &nv));
    TEST_ASSERT_FALSE(protocore_lon_parse_nv(out, n, NULL));
}
