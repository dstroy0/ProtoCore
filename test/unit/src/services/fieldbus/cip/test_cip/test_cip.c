// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the CIP message codec (services/fieldbus/cip/cip.h).
//
// The load-bearing case is test_identity_vendor_id_request. Reading attribute 1 (Vendor ID) of
// instance 1 of the Identity Object (class 0x01) is the CIP message every EtherNet/IP and DeviceNet
// device answers, and its eight octets are fully determined by CIP Volume 1: service 0x0E, a path
// size counted in 16-bit words, then three logical segments. A logical segment octet is
//   segment type (bits 7..5) = 001 -> 0x20
//   logical type (bits 4..2) = class 000 / instance 001 / attribute 100 -> 0x00 / 0x04 / 0x10
//   logical format (bits 1..0) = 8-bit 00 / 16-bit 01
// so class-8-bit is 0x20, instance-8-bit is 0x24 and attribute-8-bit is 0x30. Each expected octet
// below is assembled from those bit fields rather than copied out of the encoder.

#include "services/fieldbus/cip/cip.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The segment-encoding constants combine into the three 8-bit logical segment octets.
void test_logical_segment_constants(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x20u, CIP_SEG_LOGICAL);
    TEST_ASSERT_EQUAL_HEX8(0x00u, CIP_SEG_CLASS);
    TEST_ASSERT_EQUAL_HEX8(0x04u, CIP_SEG_INSTANCE);
    TEST_ASSERT_EQUAL_HEX8(0x10u, CIP_SEG_ATTRIBUTE);
    TEST_ASSERT_EQUAL_HEX8(0x00u, CIP_SEG_8BIT);
    TEST_ASSERT_EQUAL_HEX8(0x01u, CIP_SEG_16BIT);

    TEST_ASSERT_EQUAL_HEX8(0x20u, CIP_SEG_LOGICAL | CIP_SEG_CLASS | CIP_SEG_8BIT);
    TEST_ASSERT_EQUAL_HEX8(0x24u, CIP_SEG_LOGICAL | CIP_SEG_INSTANCE | CIP_SEG_8BIT);
    TEST_ASSERT_EQUAL_HEX8(0x30u, CIP_SEG_LOGICAL | CIP_SEG_ATTRIBUTE | CIP_SEG_8BIT);
    TEST_ASSERT_EQUAL_HEX8(0x21u, CIP_SEG_LOGICAL | CIP_SEG_CLASS | CIP_SEG_16BIT);
    TEST_ASSERT_EQUAL_HEX8(0x25u, CIP_SEG_LOGICAL | CIP_SEG_INSTANCE | CIP_SEG_16BIT);
    TEST_ASSERT_EQUAL_HEX8(0x31u, CIP_SEG_LOGICAL | CIP_SEG_ATTRIBUTE | CIP_SEG_16BIT);

    // Service codes, and the reply bit that turns a service into its response.
    TEST_ASSERT_EQUAL_HEX8(0x01u, CIP_SC_GET_ATTR_ALL);
    TEST_ASSERT_EQUAL_HEX8(0x03u, CIP_SC_GET_ATTR_LIST);
    TEST_ASSERT_EQUAL_HEX8(0x04u, CIP_SC_SET_ATTR_LIST);
    TEST_ASSERT_EQUAL_HEX8(0x0Eu, CIP_SC_GET_ATTR_SINGLE);
    TEST_ASSERT_EQUAL_HEX8(0x10u, CIP_SC_SET_ATTR_SINGLE);
    TEST_ASSERT_EQUAL_HEX8(0x80u, CIP_REPLY_FLAG);
    TEST_ASSERT_EQUAL_HEX8(0x00u, CIP_STATUS_SUCCESS);
}

// Get_Attribute_Single, Identity Object (class 1), instance 1, attribute 1 (Vendor ID):
//   0E    service = Get_Attribute_Single
//   03    request path size = 3 words = 6 octets
//   20 01 logical class,     8-bit format, class 0x01
//   24 01 logical instance,  8-bit format, instance 1
//   30 01 logical attribute, 8-bit format, attribute 1
void test_identity_vendor_id_request(void)
{
    static const uint8_t WANT[8] = {0x0E, 0x03, 0x20, 0x01, 0x24, 0x01, 0x30, 0x01};
    uint8_t buf[16];
    memset(buf, 0xEE, sizeof(buf));
    size_t n = protocore_cip_build_get_attr_single(buf, sizeof(buf), 0x0001u, 0x0001u, 0x0001u);
    TEST_ASSERT_EQUAL_size_t(8u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 8);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[8]);
}

// Get_Attributes_All over the same object drops the attribute segment, so the path is 2 words.
void test_get_attributes_all_has_no_attribute_segment(void)
{
    static const uint8_t WANT[6] = {0x01, 0x02, 0x20, 0x01, 0x24, 0x01};
    uint8_t buf[16];
    size_t n = protocore_cip_build_get_attr_all(buf, sizeof(buf), 0x0001u, 0x0001u);
    TEST_ASSERT_EQUAL_size_t(6u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 6);
}

// An id above 0xFF cannot fit the 8-bit format, so the segment switches to the 16-bit form: the
// format bits become 01, a pad octet keeps the value 16-bit aligned, and the value is
// little-endian. Class 0x0100 therefore encodes as 21 00 00 01.
void test_wide_ids_use_the_sixteen_bit_segment_form(void)
{
    uint8_t epath[16];
    size_t n = protocore_cip_build_epath(epath, sizeof(epath), 0x0100u, 0x0001u, 0x0001u, PROTO_TRUE);
    static const uint8_t WIDE_CLASS[8] = {0x21, 0x00, 0x00, 0x01, 0x24, 0x01, 0x30, 0x01};
    TEST_ASSERT_EQUAL_size_t(8u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WIDE_CLASS, epath, 8);

    // All three wide: 4 octets each.
    n = protocore_cip_build_epath(epath, sizeof(epath), 0x1234u, 0xABCDu, 0x0102u, PROTO_TRUE);
    static const uint8_t ALL_WIDE[12] = {0x21, 0x00, 0x34, 0x12, 0x25, 0x00, 0xCD, 0xAB, 0x31, 0x00, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(12u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ALL_WIDE, epath, 12);

    // 0xFF is the widest 8-bit id; 0x0100 is the narrowest 16-bit one.
    n = protocore_cip_build_epath(epath, sizeof(epath), 0x00FFu, 0x0100u, 0, PROTO_FALSE);
    static const uint8_t BOUNDARY[6] = {0x20, 0xFF, 0x25, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_size_t(6u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BOUNDARY, epath, 6);
}

// The EPATH is always an even number of octets, because the request path size counts 16-bit words.
void test_epath_is_always_word_aligned(void)
{
    uint8_t epath[16];
    for (uint32_t class_id = 0x00FEu; class_id <= 0x0101u; class_id++)
    {
        for (uint32_t instance = 0x00FEu; instance <= 0x0101u; instance++)
        {
            size_t n = protocore_cip_build_epath(epath, sizeof(epath), (uint16_t)class_id, (uint16_t)instance, 5,
                                                 PROTO_TRUE);
            TEST_ASSERT_TRUE(n != 0u);
            TEST_ASSERT_EQUAL_size_t(0u, n % 2u);

            uint8_t req[24];
            size_t r = protocore_cip_build_request(req, sizeof(req), CIP_SC_GET_ATTR_SINGLE, epath, n, NULL, 0);
            TEST_ASSERT_EQUAL_size_t(2u + n, r);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)(n / 2u), req[1]); // path size is in words, not octets
        }
    }
}

// Set_Attribute_Single carries the value after the path. Attribute 7 of the Identity Object is the
// Product Name, a SHORT_STRING: a one-octet length then that many characters.
void test_set_attribute_single_appends_the_value(void)
{
    static const uint8_t VALUE[5] = {0x04, 'A', 'C', 'M', 'E'}; // SHORT_STRING "ACME"
    uint8_t buf[24];
    size_t n = protocore_cip_build_set_attr_single(buf, sizeof(buf), 0x0001u, 0x0001u, 0x0007u, VALUE, sizeof(VALUE));
    static const uint8_t WANT[13] = {0x10, 0x03, 0x20, 0x01, 0x24, 0x01, 0x30, 0x07, 0x04, 'A', 'C', 'M', 'E'};
    TEST_ASSERT_EQUAL_size_t(13u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 13);

    // A zero-length value is legal and yields the bare request.
    n = protocore_cip_build_set_attr_single(buf, sizeof(buf), 0x0001u, 0x0001u, 0x0007u, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(8u, n);

    // A length without a value is refused.
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cip_build_set_attr_single(buf, sizeof(buf), 1, 1, 7, NULL, 4));
}

// A response is `Service|0x80  reserved(0)  GeneralStatus  AdditionalStatusSize(words)` then the
// service data, so the reply to Get_Attribute_Single (0x0E) is 0x8E.
void test_parse_successful_response(void)
{
    // Vendor ID 0x004D returned as a UINT, little-endian.
    static const uint8_t RESP[6] = {0x8E, 0x00, 0x00, 0x00, 0x4D, 0x00};
    CipResponse r;
    TEST_ASSERT_TRUE(protocore_cip_parse_response(RESP, sizeof(RESP), &r));
    TEST_ASSERT_EQUAL_HEX8(0x8Eu, r.service);
    TEST_ASSERT_EQUAL_HEX8(CIP_SC_GET_ATTR_SINGLE | CIP_REPLY_FLAG, r.service);
    TEST_ASSERT_EQUAL_HEX8(CIP_STATUS_SUCCESS, r.general_status);
    TEST_ASSERT_EQUAL_size_t(2u, r.data_len);
    TEST_ASSERT_EQUAL_PTR(RESP + 4, r.data);
    TEST_ASSERT_EQUAL_HEX16(0x004Du, (uint16_t)(r.data[0] | ((uint16_t)r.data[1] << 8)));

    // No service data at all: a Set reply.
    static const uint8_t ACK[4] = {0x90, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(protocore_cip_parse_response(ACK, sizeof(ACK), &r));
    TEST_ASSERT_EQUAL_HEX8(CIP_SC_SET_ATTR_SINGLE | CIP_REPLY_FLAG, r.service);
    TEST_ASSERT_EQUAL_size_t(0u, r.data_len);
}

// The additional-status field is counted in 16-bit words and sits between the header and the
// service data, so a parser that reads it as octets slices the data at the wrong offset.
void test_additional_status_is_counted_in_words(void)
{
    // General status 0x1F (vendor specific error) with two words of additional status.
    static const uint8_t RESP[10] = {0x8E, 0x00, 0x1F, 0x02, 0x11, 0x22, 0x33, 0x44, 0xAA, 0xBB};
    CipResponse r;
    TEST_ASSERT_TRUE(protocore_cip_parse_response(RESP, sizeof(RESP), &r));
    TEST_ASSERT_EQUAL_HEX8(0x1Fu, r.general_status);
    TEST_ASSERT_EQUAL_size_t(2u, r.data_len); // 10 - (4 + 2*2)
    TEST_ASSERT_EQUAL_HEX8(0xAAu, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBBu, r.data[1]);

    // One word of additional status, no data left over.
    static const uint8_t ONE[6] = {0x8E, 0x00, 0x05, 0x01, 0x00, 0x01}; // 0x05 = path destination unknown
    TEST_ASSERT_TRUE(protocore_cip_parse_response(ONE, sizeof(ONE), &r));
    TEST_ASSERT_EQUAL_HEX8(0x05u, r.general_status);
    TEST_ASSERT_EQUAL_size_t(0u, r.data_len);

    // An additional-status size that runs past the buffer is refused.
    static const uint8_t LIES[6] = {0x8E, 0x00, 0x05, 0x08, 0x00, 0x01};
    TEST_ASSERT_FALSE(protocore_cip_parse_response(LIES, sizeof(LIES), &r));
}

// A response shorter than its own fixed header is not a response.
void test_response_refusals(void)
{
    static const uint8_t RESP[4] = {0x8E, 0x00, 0x00, 0x00};
    CipResponse r;
    TEST_ASSERT_TRUE(protocore_cip_parse_response(RESP, 4, &r));
    TEST_ASSERT_FALSE(protocore_cip_parse_response(RESP, 3, &r));
    TEST_ASSERT_FALSE(protocore_cip_parse_response(RESP, 0, &r));
    TEST_ASSERT_FALSE(protocore_cip_parse_response(NULL, 4, &r));
    TEST_ASSERT_FALSE(protocore_cip_parse_response(RESP, 4, NULL));
}

// A path that is not a whole number of 16-bit words, or one too long for the word count field, is
// refused rather than emitted with a rounded size.
void test_request_refuses_a_misaligned_or_oversized_path(void)
{
    static const uint8_t EPATH[6] = {0x20, 0x01, 0x24, 0x01, 0x30, 0x01};
    uint8_t buf[32];

    TEST_ASSERT_EQUAL_size_t(8u, protocore_cip_build_request(buf, sizeof(buf), CIP_SC_GET_ATTR_SINGLE, EPATH, 6, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cip_build_request(buf, sizeof(buf), CIP_SC_GET_ATTR_SINGLE, EPATH, 5, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cip_build_request(buf, sizeof(buf), CIP_SC_GET_ATTR_SINGLE, NULL, 6, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u,
                             protocore_cip_build_request(NULL, sizeof(buf), CIP_SC_GET_ATTR_SINGLE, EPATH, 6, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cip_build_request(buf, sizeof(buf), CIP_SC_GET_ATTR_SINGLE, EPATH, 6, NULL, 4));
    // 7 octets of capacity cannot hold the 8-octet request.
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cip_build_request(buf, 7, CIP_SC_GET_ATTR_SINGLE, EPATH, 6, NULL, 0));
}

// A buffer too small for the segments being written yields 0 rather than a half-formed path.
void test_epath_refuses_a_short_buffer(void)
{
    uint8_t epath[16];
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cip_build_epath(epath, 1, 1, 1, 1, PROTO_FALSE));  // class needs 2
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cip_build_epath(epath, 3, 1, 1, 1, PROTO_TRUE));   // instance needs 2 more
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cip_build_epath(epath, 5, 1, 1, 1, PROTO_TRUE));   // attribute needs 2 more
    TEST_ASSERT_EQUAL_size_t(6u, protocore_cip_build_epath(epath, 6, 1, 1, 1, PROTO_TRUE));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cip_build_epath(epath, 3, 0x0100u, 1, 1, PROTO_FALSE)); // wide needs 4
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cip_build_epath(NULL, sizeof(epath), 1, 1, 1, PROTO_TRUE));

    uint8_t out[8];
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cip_build_get_attr_single(out, 7, 1, 1, 1));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cip_build_get_attr_all(out, 5, 1, 1));
}

// A request built and its reply parsed carry the same service code, with the reply bit the only
// difference: 0x0E out, 0x8E back.
void test_service_and_reply_service_differ_only_by_the_reply_bit(void)
{
    uint8_t req[16];
    size_t n = protocore_cip_build_get_attr_single(req, sizeof(req), 0x0001u, 0x0001u, 0x0001u);
    TEST_ASSERT_EQUAL_size_t(8u, n);

    uint8_t resp[8];
    resp[0] = (uint8_t)(req[0] | CIP_REPLY_FLAG);
    resp[1] = 0x00;
    resp[2] = CIP_STATUS_SUCCESS;
    resp[3] = 0x00;
    resp[4] = 0x4D;
    resp[5] = 0x00;

    CipResponse r;
    TEST_ASSERT_TRUE(protocore_cip_parse_response(resp, 6, &r));
    TEST_ASSERT_EQUAL_HEX8(req[0], (uint8_t)(r.service & (uint8_t)~CIP_REPLY_FLAG));
    TEST_ASSERT_EQUAL_HEX8(CIP_REPLY_FLAG, (uint8_t)(r.service & CIP_REPLY_FLAG));
}
