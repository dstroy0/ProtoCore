// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/ble_gatt: the ATT PDU codec + GATT characteristic JSON.

#include "services/radio/ble_gatt/ble_gatt.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_build_pdus(void)
{
    uint8_t buf[16];
    // Read Request handle 0x0025.
    TEST_ASSERT_EQUAL_size_t(3, att_read_req(0x0025, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_READ_REQ, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x25, buf[1]); // LE low
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]); // LE high
    // Write Request.
    const uint8_t val[3] = {0xDE, 0xAD, 0xBE};
    size_t n = att_write_req(0x0031, val, 3, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_WRITE_REQ, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x31, buf[1]);
    TEST_ASSERT_EQUAL_MEMORY(val, buf + 3, 3);
    // Notification.
    n = att_notify(0x0031, val, 3, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_HANDLE_VALUE_NTF, buf[0]);
    // Error Response.
    n = att_error_rsp(ATT_OP_READ_REQ, 0x0025, 0x0A, buf, sizeof(buf)); // 0x0A = Attribute Not Found
    TEST_ASSERT_EQUAL_size_t(5, n);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_ERROR_RSP, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_READ_REQ, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, buf[4]);
}

void test_build_overflow(void)
{
    uint8_t tiny[2];
    TEST_ASSERT_EQUAL_size_t(0, att_read_req(0x0025, tiny, sizeof(tiny)));
}

void test_parse(void)
{
    AttPdu p;
    // Write Request with value.
    const uint8_t w[] = {ATT_OP_WRITE_REQ, 0x31, 0x00, 0x01, 0x02};
    TEST_ASSERT_TRUE(att_parse(w, sizeof(w), &p));
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_WRITE_REQ, p.opcode);
    TEST_ASSERT_EQUAL_HEX16(0x0031, p.handle);
    TEST_ASSERT_EQUAL_size_t(2, p.value_len);
    TEST_ASSERT_EQUAL_HEX8(0x01, p.value[0]);
    // Error Response.
    const uint8_t e[] = {ATT_OP_ERROR_RSP, ATT_OP_READ_REQ, 0x25, 0x00, 0x0A};
    TEST_ASSERT_TRUE(att_parse(e, sizeof(e), &p));
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_READ_REQ, p.req_op);
    TEST_ASSERT_EQUAL_HEX16(0x0025, p.handle);
    TEST_ASSERT_EQUAL_HEX8(0x0A, p.error);
    // Read Response value.
    const uint8_t r[] = {ATT_OP_READ_RSP, 0xAA, 0xBB};
    TEST_ASSERT_TRUE(att_parse(r, sizeof(r), &p));
    TEST_ASSERT_EQUAL_size_t(2, p.value_len);
    // Truncated write req -> false.
    const uint8_t bad[] = {ATT_OP_WRITE_REQ, 0x31};
    TEST_ASSERT_FALSE(att_parse(bad, sizeof(bad), &p));
}

void test_char_json(void)
{
    GattChar chars[2] = {{0x0025, 0x2A37, GATT_PROP_READ | GATT_PROP_NOTIFY}, // Heart Rate Measurement
                         {0x0031, 0x2A6E, GATT_PROP_READ}};                   // Temperature
    char buf[160];
    size_t n = pc_gatt_char_json(chars, 2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_EQUAL_STRING(
        "[{\"handle\":37,\"uuid\":\"0x2a37\",\"props\":18},{\"handle\":49,\"uuid\":\"0x2a6e\",\"props\":2}]", buf);
    // Empty.
    pc_gatt_char_json(NULL, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("[]", buf);
    // Overflow: a buffer too small returns 0.
    char tiny[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_gatt_char_json(chars, 2, tiny, sizeof(tiny)));
}

// att_read_rsp build + the build-time guards (null out / null value / tiny caps).
void test_read_rsp_and_build_guards(void)
{
    uint8_t out[16];
    const uint8_t val[3] = {1, 2, 3};
    size_t n = att_read_rsp(val, 3, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_READ_RSP, out[0]);
    TEST_ASSERT_EQUAL_MEMORY(val, out + 1, 3);
    TEST_ASSERT_EQUAL_size_t(0, att_read_rsp(val, 3, NULL, sizeof(out)));            // null out
    TEST_ASSERT_EQUAL_size_t(0, att_read_rsp(NULL, 3, out, sizeof(out)));            // len but null value
    TEST_ASSERT_EQUAL_size_t(0, att_read_rsp(val, 3, out, 2));                       // cap < 1 + vlen
    TEST_ASSERT_EQUAL_size_t(0, att_write_req(0x10, NULL, 3, out, sizeof(out)));     // handle_value: null value
    TEST_ASSERT_EQUAL_size_t(0, att_notify(0x10, val, 3, out, 5));                   // handle_value: cap < 3 + vlen
    TEST_ASSERT_EQUAL_size_t(0, att_error_rsp(ATT_OP_READ_REQ, 0x10, 0x0A, out, 4)); // error_rsp: cap < 5

    // Null-`out` branches on the leading guards (never hit above: those calls all pass a real buffer).
    TEST_ASSERT_EQUAL_size_t(0, att_read_req(0x0025, NULL, sizeof(out)));              // null out
    TEST_ASSERT_EQUAL_size_t(0, att_write_req(0x10, val, 3, NULL, sizeof(out)));       // handle_value: null out
    TEST_ASSERT_EQUAL_size_t(0, att_error_rsp(ATT_OP_READ_REQ, 0x10, 0x0A, NULL, 16)); // error_rsp: null out

    // vlen == 0 paths: `vlen && !val` short-circuits false and the value copy is skipped.
    n = att_read_rsp(NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_READ_RSP, out[0]);
    n = att_write_req(0x10, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_WRITE_REQ, out[0]);
}

// att_parse guards (null / empty / truncated) and the remaining opcodes.
void test_parse_guards_and_opcodes(void)
{
    AttPdu p;
    TEST_ASSERT_FALSE(att_parse(NULL, 5, &p)); // null pdu
    uint8_t z = 0;
    TEST_ASSERT_FALSE(att_parse(&z, 0, &p)); // len 0
    const uint8_t err_trunc[3] = {ATT_OP_ERROR_RSP, ATT_OP_READ_REQ, 0x25};
    TEST_ASSERT_FALSE(att_parse(err_trunc, sizeof(err_trunc), &p)); // ERROR_RSP len < 5
    const uint8_t rr[3] = {ATT_OP_READ_REQ, 0x25, 0x00};
    TEST_ASSERT_TRUE(att_parse(rr, sizeof(rr), &p)); // READ_REQ
    TEST_ASSERT_EQUAL_HEX16(0x0025, p.handle);
    const uint8_t rr_trunc[2] = {ATT_OP_READ_REQ, 0x25};
    TEST_ASSERT_FALSE(att_parse(rr_trunc, sizeof(rr_trunc), &p)); // READ_REQ len < 3
    const uint8_t wrsp[1] = {ATT_OP_WRITE_RSP};
    TEST_ASSERT_TRUE(att_parse(wrsp, sizeof(wrsp), &p)); // WRITE_RSP: no fields
    const uint8_t unk[2] = {0xFF, 0x01};
    TEST_ASSERT_TRUE(att_parse(unk, sizeof(unk), &p)); // unknown opcode: reported, no fields
    TEST_ASSERT_EQUAL_HEX8(0xFF, p.opcode);

    TEST_ASSERT_FALSE(att_parse(rr, sizeof(rr), NULL)); // null out

    // READ_RSP with no value bytes: `len > 1` false branch, value stays null/0-length.
    const uint8_t rrsp_empty[1] = {ATT_OP_READ_RSP};
    TEST_ASSERT_TRUE(att_parse(rrsp_empty, sizeof(rrsp_empty), &p));
    TEST_ASSERT_NULL(p.value);
    TEST_ASSERT_EQUAL_size_t(0, p.value_len);

    // WRITE_REQ with no value bytes: `len > 3` false branch, value stays null/0-length.
    const uint8_t wr_novalue[3] = {ATT_OP_WRITE_REQ, 0x31, 0x00};
    TEST_ASSERT_TRUE(att_parse(wr_novalue, sizeof(wr_novalue), &p));
    TEST_ASSERT_EQUAL_HEX16(0x0031, p.handle);
    TEST_ASSERT_NULL(p.value);
    TEST_ASSERT_EQUAL_size_t(0, p.value_len);
}

// pc_gatt_char_json rejects a null output, a zero cap, and a null array with a nonzero count.
void test_char_json_guards(void)
{
    GattChar c = {0x10, 0x2A00, GATT_PROP_READ};
    char out[64];
    TEST_ASSERT_EQUAL_size_t(0, pc_gatt_char_json(&c, 1, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_gatt_char_json(&c, 1, out, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_gatt_char_json(NULL, 1, out, sizeof(out)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_pdus);
    RUN_TEST(test_read_rsp_and_build_guards);
    RUN_TEST(test_parse_guards_and_opcodes);
    RUN_TEST(test_char_json_guards);
    RUN_TEST(test_build_overflow);
    RUN_TEST(test_parse);
    RUN_TEST(test_char_json);
    return UNITY_END();
}
