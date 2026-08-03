// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the CIP message codec (services/fieldbus/cip): the EPATH builder, the request
// builders (Get_Attribute_Single), and the response parser. Service codes + segment
// encoding per the Wireshark CIP dissector. Pure host tests.

#include "services/fieldbus/cip/cip.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// class 1 / instance 1 / attribute 7 -> 8-bit logical segments.
void test_epath_8bit()
{
    uint8_t buf[16];
    size_t n = pc_cip_build_epath(buf, sizeof(buf), 0x01, 0x01, 0x07, PROTO_TRUE);
    const uint8_t expect[] = {0x20, 0x01, 0x24, 0x01, 0x30, 0x07};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// A class id > 255 uses the 16-bit segment form (segment byte + pad + LE value).
void test_epath_16bit()
{
    uint8_t buf[16];
    size_t n = pc_cip_build_epath(buf, sizeof(buf), 0x0100, 0x01, 0, PROTO_FALSE);
    const uint8_t expect[] = {0x21, 0x00, 0x00, 0x01, 0x24, 0x01}; // class 0x0100 (16-bit), instance 1 (8-bit)
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// Get_Attribute_Single of the Identity object's product-name attribute.
void test_get_attr_single()
{
    uint8_t buf[16];
    size_t n = pc_cip_build_get_attr_single(buf, sizeof(buf), 0x01, 0x01, 0x07);
    const uint8_t expect[] = {
        0x0E,                              // Get_Attribute_Single
        0x03,                              // path size = 3 words
        0x20, 0x01, 0x24, 0x01, 0x30, 0x07 // EPATH
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_get_attr_all()
{
    uint8_t buf[16];
    size_t n = pc_cip_build_get_attr_all(buf, sizeof(buf), 0x01, 0x01);
    const uint8_t expect[] = {
        0x01,                  // Get_Attributes_All
        0x02,                  // path size = 2 words
        0x20, 0x01, 0x24, 0x01 // EPATH class 1 / instance 1 (no attribute segment)
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    // A too-small buffer fails closed.
    uint8_t small[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_cip_build_get_attr_all(small, sizeof(small), 1, 1)); // needs 6
}

void test_set_attr_single()
{
    uint8_t buf[16];
    const uint8_t value[] = {0xAB, 0xCD};
    size_t n = pc_cip_build_set_attr_single(buf, sizeof(buf), 0x01, 0x01, 0x07, value, sizeof(value));
    const uint8_t expect[] = {
        0x10,                               // Set_Attribute_Single
        0x03,                               // path size = 3 words
        0x20, 0x01, 0x24, 0x01, 0x30, 0x07, // EPATH class 1 / instance 1 / attribute 7
        0xAB, 0xCD                          // the attribute value written
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    // A too-small buffer and a null value with a nonzero length both fail closed.
    uint8_t small[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_cip_build_set_attr_single(small, sizeof(small), 1, 1, 7, value, sizeof(value)));
    TEST_ASSERT_EQUAL_size_t(0, pc_cip_build_set_attr_single(buf, sizeof(buf), 1, 1, 7, NULL, 2));
}

void test_build_request_with_data()
{
    const uint8_t epath[] = {0x20, 0x01, 0x24, 0x01, 0x30, 0x07};
    const uint8_t data[] = {0xAB, 0xCD};
    uint8_t buf[16];
    size_t n = pc_cip_build_request(buf, sizeof(buf), CIP_SC_SET_ATTR_SINGLE, epath, sizeof(epath), data, sizeof(data));
    const uint8_t expect[] = {0x10, 0x03, 0x20, 0x01, 0x24, 0x01, 0x30, 0x07, 0xAB, 0xCD};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_parse_response_ok()
{
    const uint8_t resp[] = {0x8E, 0x00, 0x00, 0x00, 'A', 'c', 'm', 'e'}; // reply, status OK, no addl, data
    CipResponse r;
    TEST_ASSERT_TRUE(pc_cip_parse_response(resp, sizeof(resp), &r));
    TEST_ASSERT_EQUAL_HEX8(0x8E, r.service); // Get_Attribute_Single reply
    TEST_ASSERT_EQUAL_HEX8(CIP_STATUS_SUCCESS, r.general_status);
    TEST_ASSERT_EQUAL_size_t(4, r.data_len);
    TEST_ASSERT_EQUAL_MEMORY("Acme", r.data, 4);
}

// A response with additional status words before the data.
void test_parse_response_additional_status()
{
    const uint8_t resp[] = {0x8E, 0x00, 0x1F, 0x01, 0xAA, 0xBB, 0x12, 0x34}; // addl size 1 word (AA BB), data 12 34
    CipResponse r;
    TEST_ASSERT_TRUE(pc_cip_parse_response(resp, sizeof(resp), &r));
    TEST_ASSERT_EQUAL_HEX8(0x1F, r.general_status);
    TEST_ASSERT_EQUAL_size_t(2, r.data_len);
    TEST_ASSERT_EQUAL_HEX8(0x12, r.data[0]);
}

void test_parse_response_error()
{
    const uint8_t resp[] = {0x8E, 0x00, 0x05, 0x00}; // status 0x05 = path destination unknown, no data
    CipResponse r;
    TEST_ASSERT_TRUE(pc_cip_parse_response(resp, sizeof(resp), &r));
    TEST_ASSERT_EQUAL_HEX8(0x05, r.general_status);
    TEST_ASSERT_EQUAL_size_t(0, r.data_len);
}

void test_rejects_bad()
{
    CipResponse r;
    const uint8_t short_resp[] = {0x8E, 0x00, 0x00}; // < 4 octets
    TEST_ASSERT_FALSE(pc_cip_parse_response(short_resp, sizeof(short_resp), &r));
    const uint8_t bad_addl[] = {0x8E, 0x00, 0x00, 0x05}; // addl size 5 words overruns
    TEST_ASSERT_FALSE(pc_cip_parse_response(bad_addl, sizeof(bad_addl), &r));

    uint8_t small[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_cip_build_get_attr_single(small, sizeof(small), 1, 1, 7)); // needs 8
}

// EPATH and request builders fail closed on a null buffer, an 8-bit/16-bit segment
// that does not fit, and bad request arguments.
void test_cip_build_guards()
{
    uint8_t buf[16];
    TEST_ASSERT_EQUAL_UINT(0, pc_cip_build_epath(NULL, sizeof(buf), 1, 1, 1, PROTO_TRUE)); // null buffer
    TEST_ASSERT_EQUAL_UINT(0, pc_cip_build_epath(buf, 1, 1, 1, 1, PROTO_FALSE));           // 8-bit segment, cap < 2
    TEST_ASSERT_EQUAL_UINT(0, pc_cip_build_epath(buf, 3, 0x1234, 1, 1, PROTO_FALSE));      // 16-bit segment, cap < 4
    TEST_ASSERT_EQUAL_UINT(0, pc_cip_build_epath(buf, 2, 1, 1, 1, PROTO_FALSE)); // instance segment does not fit
    TEST_ASSERT_EQUAL_UINT(0, pc_cip_build_epath(buf, 4, 1, 1, 1, PROTO_TRUE));  // attribute segment does not fit

    uint8_t ep[4] = {0x20, 1, 0x24, 1};
    TEST_ASSERT_EQUAL_UINT(0, pc_cip_build_request(NULL, sizeof(buf), 0x0E, ep, 4, NULL, 0));  // null buffer
    TEST_ASSERT_EQUAL_UINT(0, pc_cip_build_request(buf, sizeof(buf), 0x0E, NULL, 4, NULL, 0)); // null epath
    TEST_ASSERT_EQUAL_UINT(0, pc_cip_build_request(buf, sizeof(buf), 0x0E, ep, 3, NULL, 0));   // odd epath length
    TEST_ASSERT_EQUAL_UINT(0, pc_cip_build_request(buf, sizeof(buf), 0x0E, ep, 4, NULL, 5));   // data length w/o data
    // epath word count overflows the 1-octet path-size field (512 / 2 = 256 > 0xFF); the guard
    // short-circuits before epath is ever read, so a 4-byte backing array is fine here.
    TEST_ASSERT_EQUAL_UINT(0, pc_cip_build_request(buf, sizeof(buf), 0x0E, ep, 512, NULL, 0));
}

// pc_cip_parse_response fails closed on a null buffer or a null output pointer.
void test_parse_response_null_guards()
{
    CipResponse r;
    const uint8_t resp[] = {0x8E, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(pc_cip_parse_response(NULL, sizeof(resp), &r));   // null buffer
    TEST_ASSERT_FALSE(pc_cip_parse_response(resp, sizeof(resp), NULL)); // null output
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_cip_build_guards);
    RUN_TEST(test_epath_8bit);
    RUN_TEST(test_epath_16bit);
    RUN_TEST(test_get_attr_single);
    RUN_TEST(test_get_attr_all);
    RUN_TEST(test_set_attr_single);
    RUN_TEST(test_build_request_with_data);
    RUN_TEST(test_parse_response_ok);
    RUN_TEST(test_parse_response_additional_status);
    RUN_TEST(test_parse_response_error);
    RUN_TEST(test_parse_response_null_guards);
    RUN_TEST(test_rejects_bad);
    return UNITY_END();
}
