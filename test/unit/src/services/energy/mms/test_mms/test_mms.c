// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IEC 61850 MMS PDU codec (services/energy/mms/mms.h).
//
// ISO 9506 is not obtainable, so the MMS tag assignments and the Read PDU nesting are taken from the
// module's own documented structure rather than from the standard, and this header says so. The
// encoding underneath is BER, and every length and every INTEGER asserted below is derived from
// ITU-T X.690: sec 8.1.3.4 short-form definite length, sec 8.1.3.5 long form for 128 octets and up,
// sec 8.3.2 minimal two's-complement INTEGER contents.
//
// test_read_request_ber_nesting is the load-bearing case. Each of the seven nested lengths is one
// more than the length inside it plus that element's own tag and length octets, so a single wrong
// length octet anywhere makes the server read the wrong number of octets for every enclosing
// element. It asserts the whole PDU rather than sampling it.

#include "services/energy/mms/mms.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// A Read request for the one-character item "A", invokeID 1. Working outward:
//   1A 01 41                         objectName VisibleString "A"          -> 3
//   A0 03 <3>                        objectName [0]                        -> 5
//   30 05 <5>                        SEQUENCE, one VariableSpecification   -> 7
//   A0 07 <7>                        listOfVariable [0]                    -> 9
//   A1 09 <9>                        variableAccessSpecification [1]       -> 11
//   A4 0B <11>                       confirmedServiceRequest read [4]      -> 13
//   02 01 01                         invokeID INTEGER 1                    -> 3
//   A0 10 <3 + 13>                   confirmed-RequestPDU [0]              -> 18
void test_read_request_ber_nesting(void)
{
    static const uint8_t WANT[18] = {
        0xA0, 0x10,             //
        0x02, 0x01, 0x01,       //
        0xA4, 0x0B,             //
        0xA1, 0x09,             //
        0xA0, 0x07,             //
        0x30, 0x05,             //
        0xA0, 0x03,             //
        0x1A, 0x01, 0x41,       //
    };
    uint8_t out[64];
    size_t n = protocore_mms_read_request(1u, "A", out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, out, sizeof(WANT));
}

// A Read response carrying one pre-encoded Data value, invokeID 1:
//   A1 03 85 01 01                   listOfAccessResult wrapping the value -> 5
//   A4 05 <5>                        confirmedServiceResponse read [4]     -> 7
//   02 01 01                         invokeID                              -> 3
//   A1 0A <3 + 7>                    confirmed-ResponsePDU [1]             -> 12
void test_read_response_ber_nesting(void)
{
    static const uint8_t DATA[3] = {0x85, 0x01, 0x01};
    static const uint8_t WANT[12] = {
        0xA1, 0x0A, 0x02, 0x01, 0x01, 0xA4, 0x05, 0xA1, 0x03, 0x85, 0x01, 0x01,
    };
    uint8_t out[64];
    size_t n = protocore_mms_read_response(1u, DATA, sizeof(DATA), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, out, sizeof(WANT));

    // An empty AccessResult still produces a well-formed response, two octets shorter.
    n = protocore_mms_read_response(1u, NULL, 0u, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(9u, n);
    TEST_ASSERT_EQUAL_HEX8(0xA1u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x07u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xA1u, out[7]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[8]);
}

// X.690 sec 8.3.2: an INTEGER takes the fewest octets that keep its sign, so a value whose top octet
// has bit 8 set gets a leading 0x00 to stay positive.
void test_invoke_id_integer_is_minimal_and_positive(void)
{
    struct
    {
        uint32_t id;
        size_t len;
        uint8_t bytes[5];
    } static const CASES[] = {
        {0u, 1, {0x00, 0, 0, 0, 0}},
        {1u, 1, {0x01, 0, 0, 0, 0}},
        {127u, 1, {0x7F, 0, 0, 0, 0}},
        {128u, 2, {0x00, 0x80, 0, 0, 0}},            // 0x80 alone would read as negative
        {0x1234u, 2, {0x12, 0x34, 0, 0, 0}},
        {0xFFFFu, 3, {0x00, 0xFF, 0xFF, 0, 0}},
        {0xFFFFFFFFu, 5, {0x00, 0xFF, 0xFF, 0xFF, 0xFF}}, // the widest Unsigned32
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t out[64];
        size_t n = protocore_mms_read_request(CASES[i].id, "A", out, sizeof(out));
        TEST_ASSERT_EQUAL_UINT(17u + CASES[i].len, n); // the "A" request less its 1-octet invokeID
        TEST_ASSERT_EQUAL_HEX8(0x02u, out[2]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)CASES[i].len, out[3]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(CASES[i].bytes, out + 4, CASES[i].len);

        // The parse reads the same number back out of the wire form.
        MmsPdu p;
        TEST_ASSERT_TRUE(protocore_mms_parse(out, n, &p));
        TEST_ASSERT_EQUAL_UINT32(CASES[i].id, p.invoke_id);
    }
}

// X.690 sec 8.1.3.5: a content length of 128 or more takes the long form - 0x81 then the length
// octet. A 128-character item name pushes every enclosing element past that boundary.
//
//   1A 81 80 <128>   -> 131      A0 81 83 <131> -> 134     30 81 86 <134> -> 137
//   A0 81 89 <137>   -> 140      A1 81 8C <140> -> 143     A4 81 8F <143> -> 146
//   body = 3 + 146   -> 149      A0 81 95 <149> -> 152
void test_long_form_length_boundary(void)
{
    char name[129];
    uint8_t out[256];
    memset(name, 'x', 128);
    name[128] = '\0';

    size_t n = protocore_mms_read_request(1u, name, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(152u, n);
    TEST_ASSERT_EQUAL_HEX8(0xA0u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x81u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(149u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, out[3]); // the invokeID still follows the outer length
    TEST_ASSERT_EQUAL_HEX8(0xA4u, out[6]);
    TEST_ASSERT_EQUAL_HEX8(0x81u, out[7]);
    TEST_ASSERT_EQUAL_HEX8(143u, out[8]);

    MmsPdu p;
    TEST_ASSERT_TRUE(protocore_mms_parse(out, n, &p));
    TEST_ASSERT_EQUAL_HEX8(MMS_SERVICE_READ, p.service_tag);
    TEST_ASSERT_EQUAL_UINT(143u, p.service_len);

    // One character more than the module's item-name ceiling is refused rather than truncated.
    char over[130];
    memset(over, 'x', 129);
    over[129] = '\0';
    TEST_ASSERT_EQUAL_UINT(0u, protocore_mms_read_request(1u, over, out, sizeof(out)));
}

// The parse hands back the PDU tag, the invokeID, and a borrowed slice of the confirmedService body.
void test_parse_reports_the_pdu_header_and_borrows_the_service_body(void)
{
    uint8_t out[64];
    MmsPdu p;

    size_t n = protocore_mms_read_request(7u, "LD0/GGIO1$ST$Ind1$stVal", out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_TRUE(protocore_mms_parse(out, n, &p));
    TEST_ASSERT_EQUAL_HEX8(MMS_PDU_CONFIRMED_REQUEST, p.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(7u, p.invoke_id);
    TEST_ASSERT_EQUAL_HEX8(MMS_SERVICE_READ, p.service_tag);
    TEST_ASSERT_NOT_NULL(p.service_body);
    TEST_ASSERT_TRUE(p.service_body > out && p.service_body < out + n); // borrowed, not copied
    TEST_ASSERT_EQUAL_UINT(n - (size_t)(p.service_body - out), p.service_len);

    static const uint8_t DATA[3] = {0x85, 0x01, 0x01};
    n = protocore_mms_read_response(7u, DATA, sizeof(DATA), out, sizeof(out));
    TEST_ASSERT_TRUE(protocore_mms_parse(out, n, &p));
    TEST_ASSERT_EQUAL_HEX8(MMS_PDU_CONFIRMED_RESPONSE, p.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(7u, p.invoke_id);
    TEST_ASSERT_EQUAL_HEX8(MMS_SERVICE_READ, p.service_tag);
    TEST_ASSERT_EQUAL_UINT(5u, p.service_len);
}

// A PDU carrying only an invokeID has no confirmedService at all, which reads as tag 0 and an empty
// body rather than as whatever octets follow the PDU.
void test_parse_of_a_pdu_with_no_service_element(void)
{
    static const uint8_t BARE[5] = {0xA0, 0x03, 0x02, 0x01, 0x2A};
    MmsPdu p;
    TEST_ASSERT_TRUE(protocore_mms_parse(BARE, sizeof(BARE), &p));
    TEST_ASSERT_EQUAL_HEX8(MMS_PDU_CONFIRMED_REQUEST, p.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(42u, p.invoke_id);
    TEST_ASSERT_EQUAL_HEX8(0u, p.service_tag);
    TEST_ASSERT_NULL(p.service_body);
    TEST_ASSERT_EQUAL_UINT(0u, p.service_len);

    // The confirmed-error PDU is the third top-level tag the parse accepts.
    static const uint8_t ERR[5] = {0xA2, 0x03, 0x02, 0x01, 0x09};
    TEST_ASSERT_TRUE(protocore_mms_parse(ERR, sizeof(ERR), &p));
    TEST_ASSERT_EQUAL_HEX8(MMS_PDU_CONFIRMED_ERROR, p.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(9u, p.invoke_id);
}

// A PDU that is not a confirmed one, whose lengths overrun the buffer, or whose first inner element
// is not the invokeID, is refused rather than read past.
void test_parse_rejects_malformed_pdus(void)
{
    MmsPdu p;
    static const uint8_t NOT_CONFIRMED[5] = {0xA3, 0x03, 0x02, 0x01, 0x01};
    static const uint8_t OUTER_TOO_LONG[5] = {0xA0, 0x7F, 0x02, 0x01, 0x01};
    static const uint8_t NO_INVOKE_ID[5] = {0xA0, 0x03, 0x30, 0x01, 0x01};
    static const uint8_t ID_TOO_WIDE[9] = {0xA0, 0x07, 0x02, 0x06, 0, 0, 0, 0, 0};
    static const uint8_t ID_NOT_UNSIGNED32[9] = {0xA0, 0x07, 0x02, 0x05, 0x01, 0, 0, 0, 0};
    static const uint8_t ID_EMPTY[4] = {0xA0, 0x02, 0x02, 0x00};
    static const uint8_t ID_TRUNCATED[4] = {0xA0, 0x03, 0x02, 0x04};
    static const uint8_t SERVICE_TOO_LONG[7] = {0xA0, 0x05, 0x02, 0x01, 0x01, 0xA4, 0x7F};

    TEST_ASSERT_FALSE(protocore_mms_parse(NOT_CONFIRMED, sizeof(NOT_CONFIRMED), &p));
    TEST_ASSERT_FALSE(protocore_mms_parse(OUTER_TOO_LONG, sizeof(OUTER_TOO_LONG), &p));
    TEST_ASSERT_FALSE(protocore_mms_parse(NO_INVOKE_ID, sizeof(NO_INVOKE_ID), &p));
    TEST_ASSERT_FALSE(protocore_mms_parse(ID_TOO_WIDE, sizeof(ID_TOO_WIDE), &p));
    TEST_ASSERT_FALSE(protocore_mms_parse(ID_NOT_UNSIGNED32, sizeof(ID_NOT_UNSIGNED32), &p));
    TEST_ASSERT_FALSE(protocore_mms_parse(ID_EMPTY, sizeof(ID_EMPTY), &p));
    TEST_ASSERT_FALSE(protocore_mms_parse(ID_TRUNCATED, sizeof(ID_TRUNCATED), &p));
    TEST_ASSERT_FALSE(protocore_mms_parse(SERVICE_TOO_LONG, sizeof(SERVICE_TOO_LONG), &p));
    TEST_ASSERT_FALSE(protocore_mms_parse(NOT_CONFIRMED, 1u, &p));
    TEST_ASSERT_FALSE(protocore_mms_parse(NULL, 5u, &p));
    TEST_ASSERT_FALSE(protocore_mms_parse(NOT_CONFIRMED, sizeof(NOT_CONFIRMED), NULL));
}

// A buffer that cannot hold the PDU writes nothing and reports 0, at both ends.
void test_build_refuses_bad_arguments_and_undersized_buffers(void)
{
    uint8_t out[64];
    static const uint8_t DATA[3] = {0x85, 0x01, 0x01};

    TEST_ASSERT_EQUAL_UINT(18u, protocore_mms_read_request(1u, "A", out, 18u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_mms_read_request(1u, "A", out, 17u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_mms_read_request(1u, "A", NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_mms_read_request(1u, NULL, out, sizeof(out)));

    TEST_ASSERT_EQUAL_UINT(12u, protocore_mms_read_response(1u, DATA, sizeof(DATA), out, 12u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_mms_read_response(1u, DATA, sizeof(DATA), out, 11u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_mms_read_response(1u, DATA, sizeof(DATA), NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_mms_read_response(1u, NULL, 4u, out, sizeof(out)));
}
