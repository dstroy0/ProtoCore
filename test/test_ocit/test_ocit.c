// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/ocit: the OCIT-Outstations message codec.

#include "services/transportation/ocit/ocit.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_build_and_parse(void)
{
    uint8_t value[4] = {0x00, 0x00, 0x12, 0x34};
    uint8_t out[16];
    size_t n = pc_ocit_build(OCIT_MSG_SET, 0x0102, 0x0003, OCIT_TYPE_UINT32, value, 4, out, sizeof(out));
    // [02][01 02][00 03][04][00 00 12 34] = 10 bytes.
    const uint8_t expect[] = {0x02, 0x01, 0x02, 0x00, 0x03, 0x04, 0x00, 0x00, 0x12, 0x34};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);

    OcitMsg m;
    TEST_ASSERT_TRUE(pc_ocit_parse(out, n, &m));
    TEST_ASSERT_EQUAL_HEX8(OCIT_MSG_SET, m.msg_type);
    TEST_ASSERT_EQUAL_HEX16(0x0102, m.object_type);
    TEST_ASSERT_EQUAL_HEX16(0x0003, m.instance);
    TEST_ASSERT_EQUAL_HEX8(OCIT_TYPE_UINT32, m.data_type);
    TEST_ASSERT_EQUAL_size_t(4, m.value_len);
}

void test_set_u16_helper(void)
{
    uint8_t out[16];
    size_t n = pc_ocit_set_u16(0x00A0, 0x0005, 0xBEEF, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(8, n);
    OcitMsg m;
    TEST_ASSERT_TRUE(pc_ocit_parse(out, n, &m));
    TEST_ASSERT_EQUAL_HEX8(OCIT_MSG_SET, m.msg_type);
    TEST_ASSERT_EQUAL_HEX8(OCIT_TYPE_UINT16, m.data_type);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, pc_ocit_value_u16(&m));
}

void test_get_no_value(void)
{
    uint8_t out[8];
    size_t n = pc_ocit_build(OCIT_MSG_GET, 0x0102, 0x0003, OCIT_TYPE_UINT16, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(6, n);
    OcitMsg m;
    TEST_ASSERT_TRUE(pc_ocit_parse(out, n, &m));
    TEST_ASSERT_EQUAL_size_t(0, m.value_len);
    TEST_ASSERT_NULL(m.value);
    // value_u16 on a mismatched value -> 0.
    TEST_ASSERT_EQUAL_HEX16(0, pc_ocit_value_u16(&m));
}

void test_parse_rejects_short(void)
{
    OcitMsg m;
    uint8_t tooshort[5] = {0x01, 0x02, 0x03, 0x04, 0x05};
    TEST_ASSERT_FALSE(pc_ocit_parse(tooshort, 5, &m));
}

void test_build_rejects_null_out(void)
{
    uint8_t value[2] = {0x00, 0x01};
    size_t n = pc_ocit_build(OCIT_MSG_SET, 0x0001, 0x0002, OCIT_TYPE_UINT16, value, 2, NULL, 16);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

void test_build_rejects_null_value_with_len(void)
{
    uint8_t out[16];
    size_t n = pc_ocit_build(OCIT_MSG_SET, 0x0001, 0x0002, OCIT_TYPE_UINT16, NULL, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(0, n);
}

void test_build_rejects_overflow(void)
{
    uint8_t value[4] = {0x00, 0x00, 0x00, 0x01};
    uint8_t out[8]; // cap (8) < required 6 + value_len(4) = 10.
    size_t n = pc_ocit_build(OCIT_MSG_SET, 0x0001, 0x0002, OCIT_TYPE_UINT32, value, 4, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(0, n);
}

void test_parse_rejects_null_msg(void)
{
    OcitMsg m;
    TEST_ASSERT_FALSE(pc_ocit_parse(NULL, 6, &m));
}

void test_parse_rejects_null_out(void)
{
    uint8_t msg[6] = {0x01, 0x00, 0x01, 0x00, 0x02, 0x03};
    TEST_ASSERT_FALSE(pc_ocit_parse(msg, sizeof(msg), NULL));
}

void test_value_u16_rejects_null_msg(void)
{
    TEST_ASSERT_EQUAL_HEX16(0, pc_ocit_value_u16(NULL));
}

void test_value_u16_rejects_wrong_type(void)
{
    uint8_t out[16];
    uint8_t value[2] = {0x00, 0x01};
    size_t n = pc_ocit_build(OCIT_MSG_SET, 0x0001, 0x0002, OCIT_TYPE_BYTE, value, 2, out, sizeof(out));
    OcitMsg m;
    TEST_ASSERT_TRUE(pc_ocit_parse(out, n, &m));
    TEST_ASSERT_EQUAL_HEX16(0, pc_ocit_value_u16(&m));
}

void test_value_u16_rejects_null_value_ptr(void)
{
    // Hand-built OcitMsg (not reachable via pc_ocit_parse) exercising the !m->value guard
    // with a data_type/value_len that would otherwise pass.
    OcitMsg m;
    m.msg_type = OCIT_MSG_SET;
    m.object_type = 0x0001;
    m.instance = 0x0002;
    m.data_type = OCIT_TYPE_UINT16;
    m.value = NULL;
    m.value_len = 2;
    TEST_ASSERT_EQUAL_HEX16(0, pc_ocit_value_u16(&m));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_and_parse);
    RUN_TEST(test_set_u16_helper);
    RUN_TEST(test_get_no_value);
    RUN_TEST(test_parse_rejects_short);
    RUN_TEST(test_build_rejects_null_out);
    RUN_TEST(test_build_rejects_null_value_with_len);
    RUN_TEST(test_build_rejects_overflow);
    RUN_TEST(test_parse_rejects_null_msg);
    RUN_TEST(test_parse_rejects_null_out);
    RUN_TEST(test_value_u16_rejects_null_msg);
    RUN_TEST(test_value_u16_rejects_wrong_type);
    RUN_TEST(test_value_u16_rejects_null_value_ptr);
    return UNITY_END();
}
