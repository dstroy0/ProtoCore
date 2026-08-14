// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the OCIT-Outstations message codec (services/transportation/ocit/ocit.h).
//
// The OCIT-O specification is published by the OCA and is not obtainable here, so nothing below is
// asserted against a spec number. These are PROPERTIES of the message the module documents:
// [message-type:1][object-type:2][instance:2][data-type:1][value...], with the two 16-bit address
// fields big-endian.
//
// test_message_layout_is_big_endian is the load-bearing one. An object address split across two
// octets is the only place a byte order can be silently wrong, and 0x1234 / 0x00FF are chosen so a
// swapped pair reads as a different, valid-looking object rather than as a parse error - which is
// exactly the failure a round-trip test alone would not catch.

#include "services/transportation/ocit/ocit.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// object-type 0x1234 and instance 0x00FF: neither is a palindrome, so a swapped octet pair shows.
void test_message_layout_is_big_endian(void)
{
    static const uint8_t VAL[2] = {0xAB, 0xCD};
    uint8_t out[16];

    size_t n = protocore_ocit_build(OCIT_MSG_SET, 0x1234, 0x00FF, OCIT_TYPE_UINT16, VAL, sizeof(VAL), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(8u, n); // 6 header octets + 2 value octets

    TEST_ASSERT_EQUAL_HEX8(OCIT_MSG_SET, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x12, out[1]); // object type, high octet first
    TEST_ASSERT_EQUAL_HEX8(0x34, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[3]); // instance, high octet first
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[4]);
    TEST_ASSERT_EQUAL_HEX8(OCIT_TYPE_UINT16, out[5]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, out[6]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, out[7]);
}

// The SET-a-uint16 convenience builds the same message, with the value big-endian too.
void test_set_u16_builds_a_set_message(void)
{
    static const uint8_t WANT[8] = {OCIT_MSG_SET, 0x00, 0x0A, 0x00, 0x03, OCIT_TYPE_UINT16, 0x12, 0x34};
    uint8_t out[16];

    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), protocore_ocit_set_u16(0x000A, 0x0003, 0x1234, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));
}

// Every header field and the value survive build then parse.
void test_build_parse_round_trip(void)
{
    static const uint8_t VAL[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t out[16];
    OcitMsg m;

    size_t n = protocore_ocit_build(OCIT_MSG_REPORT, 0xFFFF, 0x8001, OCIT_TYPE_UINT32, VAL, sizeof(VAL), out,
                                    sizeof(out));
    TEST_ASSERT_EQUAL_UINT(10u, n);
    TEST_ASSERT_TRUE(protocore_ocit_parse(out, n, &m));
    TEST_ASSERT_EQUAL_HEX8(OCIT_MSG_REPORT, m.msg_type);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, m.object_type);
    TEST_ASSERT_EQUAL_HEX16(0x8001, m.instance);
    TEST_ASSERT_EQUAL_HEX8(OCIT_TYPE_UINT32, m.data_type);
    TEST_ASSERT_EQUAL_UINT(sizeof(VAL), m.value_len);
    TEST_ASSERT_EQUAL_PTR(out + 6, m.value); // aliases the input, never copied
    TEST_ASSERT_EQUAL_HEX8_ARRAY(VAL, m.value, sizeof(VAL));
}

// A GET carries no value, so the message is the bare six-octet header and there is no value pointer.
void test_get_carries_no_value(void)
{
    uint8_t out[16];
    OcitMsg m;

    size_t n = protocore_ocit_build(OCIT_MSG_GET, 0x0102, 0x0304, OCIT_TYPE_UINT16, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(6u, n);
    TEST_ASSERT_TRUE(protocore_ocit_parse(out, n, &m));
    TEST_ASSERT_EQUAL_HEX8(OCIT_MSG_GET, m.msg_type);
    TEST_ASSERT_EQUAL_UINT(0u, m.value_len);
    TEST_ASSERT_NULL(m.value);
}

// Fewer than the six header octets is not a message.
void test_parse_refuses_a_short_message(void)
{
    uint8_t out[16];
    OcitMsg m;
    size_t n = protocore_ocit_build(OCIT_MSG_GET, 1, 2, OCIT_TYPE_BOOL, NULL, 0, out, sizeof(out));

    for (size_t shorter = 0; shorter < 6; shorter++)
    {
        TEST_ASSERT_FALSE(protocore_ocit_parse(out, shorter, &m));
    }
    TEST_ASSERT_TRUE(protocore_ocit_parse(out, n, &m));
    TEST_ASSERT_FALSE(protocore_ocit_parse(NULL, n, &m));
    TEST_ASSERT_FALSE(protocore_ocit_parse(out, n, NULL));
}

// The typed accessor reads a big-endian uint16, and refuses a value whose tag or length says it is
// not one - a byte read as a uint16 would be a different signal-group value entirely.
void test_value_u16_is_gated_on_the_data_type(void)
{
    static const uint8_t TWO[2] = {0x7F, 0xFF};
    static const uint8_t ONE[1] = {0x7F};
    uint8_t out[16];
    OcitMsg m;

    TEST_ASSERT_TRUE(protocore_ocit_parse(
        out, protocore_ocit_build(OCIT_MSG_SET, 1, 1, OCIT_TYPE_UINT16, TWO, sizeof(TWO), out, sizeof(out)), &m));
    TEST_ASSERT_EQUAL_HEX16(0x7FFFu, protocore_ocit_value_u16(&m));

    // right length, wrong tag
    TEST_ASSERT_TRUE(protocore_ocit_parse(
        out, protocore_ocit_build(OCIT_MSG_SET, 1, 1, OCIT_TYPE_OCTETS, TWO, sizeof(TWO), out, sizeof(out)), &m));
    TEST_ASSERT_EQUAL_HEX16(0u, protocore_ocit_value_u16(&m));

    // right tag, one octet short
    TEST_ASSERT_TRUE(protocore_ocit_parse(
        out, protocore_ocit_build(OCIT_MSG_SET, 1, 1, OCIT_TYPE_UINT16, ONE, sizeof(ONE), out, sizeof(out)), &m));
    TEST_ASSERT_EQUAL_HEX16(0u, protocore_ocit_value_u16(&m));

    // no value at all, and a null message
    TEST_ASSERT_TRUE(protocore_ocit_parse(
        out, protocore_ocit_build(OCIT_MSG_GET, 1, 1, OCIT_TYPE_UINT16, NULL, 0, out, sizeof(out)), &m));
    TEST_ASSERT_EQUAL_HEX16(0u, protocore_ocit_value_u16(&m));
    TEST_ASSERT_EQUAL_HEX16(0u, protocore_ocit_value_u16(NULL));
}

// An octet-string value is whatever remains after the header, however long that is.
void test_octet_string_value_takes_the_remainder(void)
{
    uint8_t val[64];
    uint8_t out[128];
    OcitMsg m;
    for (size_t i = 0; i < sizeof(val); i++)
    {
        val[i] = (uint8_t)i;
    }

    for (size_t len = 0; len <= sizeof(val); len++)
    {
        size_t n = protocore_ocit_build(OCIT_MSG_REPORT, 5, 6, OCIT_TYPE_OCTETS, len ? val : NULL, len, out,
                                        sizeof(out));
        TEST_ASSERT_EQUAL_UINT(6u + len, n);
        TEST_ASSERT_TRUE(protocore_ocit_parse(out, n, &m));
        TEST_ASSERT_EQUAL_UINT(len, m.value_len);
        if (len)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(val, m.value, len);
        }
    }
}

// A buffer that cannot hold the whole message produces nothing, and a null value pointer is legal
// only at length zero.
void test_build_bounds(void)
{
    static const uint8_t VAL[4] = {1, 2, 3, 4};
    uint8_t out[16];

    TEST_ASSERT_EQUAL_UINT(0u, protocore_ocit_build(OCIT_MSG_SET, 1, 1, OCIT_TYPE_UINT32, VAL, 4, out, 9)); // needs 10
    TEST_ASSERT_EQUAL_UINT(10u, protocore_ocit_build(OCIT_MSG_SET, 1, 1, OCIT_TYPE_UINT32, VAL, 4, out, 10));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ocit_build(OCIT_MSG_SET, 1, 1, OCIT_TYPE_UINT32, VAL, 4, NULL, 16));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ocit_build(OCIT_MSG_SET, 1, 1, OCIT_TYPE_UINT32, NULL, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ocit_set_u16(1, 1, 0, out, 7)); // needs 8
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ocit_build(OCIT_MSG_SET, 1, 1, OCIT_TYPE_UINT32, VAL, 4, out, 0));
}

// The message-type and data-type codes have to be distinct, or a peer cannot tell a get from a set
// or a bool from a byte.
void test_codes_are_distinct(void)
{
    const uint8_t MSG[4] = {OCIT_MSG_GET, OCIT_MSG_SET, OCIT_MSG_REPORT, OCIT_MSG_ERROR};
    const uint8_t TYPE[5] = {OCIT_TYPE_BOOL, OCIT_TYPE_BYTE, OCIT_TYPE_UINT16, OCIT_TYPE_UINT32, OCIT_TYPE_OCTETS};

    for (size_t a = 0; a < 4; a++)
    {
        for (size_t b = a + 1; b < 4; b++)
        {
            TEST_ASSERT_NOT_EQUAL(MSG[a], MSG[b]);
        }
    }
    for (size_t a = 0; a < 5; a++)
    {
        for (size_t b = a + 1; b < 5; b++)
        {
            TEST_ASSERT_NOT_EQUAL(TYPE[a], TYPE[b]);
        }
    }
}
