// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/hart: the HART command frame + HART-IP header codec.

#include "services/fieldbus/hart/hart.h"
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_checksum(void)
{
    // XOR longitudinal parity.
    const uint8_t b[] = {0x02, 0x80, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0x82, pc_hart_checksum(b, sizeof(b)));
}

void test_build_command0_short(void)
{
    // Command 0 (read unique id), STX, primary-master short address 0, no data.
    uint8_t addr = 0x80;
    uint8_t out[16];
    size_t n = pc_hart_build(HART_DELIM_STX, &addr, 1, 0x00, NULL, 0, out, sizeof(out));
    const uint8_t expect[] = {0x02, 0x80, 0x00, 0x00, 0x82};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, sizeof(expect));
}

void test_build_with_data(void)
{
    uint8_t addr = 0x80;
    uint8_t data[] = {0xAB, 0xCD};
    uint8_t out[16];
    size_t n = pc_hart_build(HART_DELIM_STX, &addr, 1, 0x01, data, 2, out, sizeof(out));
    // [02 80 01 02 AB CD ck], ck = 02^80^01^02^AB^CD = 0xE7.
    const uint8_t expect[] = {0x02, 0x80, 0x01, 0x02, 0xAB, 0xCD, 0xE7};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, sizeof(expect));
}

void test_build_long_address(void)
{
    uint8_t addr[5] = {0x86, 0x01, 0x02, 0x03, 0x04}; // long addr, master bit set
    uint8_t out[24];
    size_t n = pc_hart_build((uint8_t)(HART_DELIM_STX | HART_DELIM_LONG_ADDR), addr, 5, 0x03,
                             NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(1 + 5 + 1 + 1 + 0 + 1, n); // delim+addr+cmd+bc+ck
    HartFrame f;
    TEST_ASSERT_TRUE(pc_hart_parse(out, n, &f));
    TEST_ASSERT_EQUAL_size_t(5, f.addr_len);
    TEST_ASSERT_EQUAL_HEX8(0x03, f.command);
}

void test_parse_roundtrip_and_bad_checksum(void)
{
    uint8_t addr = 0x80;
    uint8_t data[] = {0x11, 0x22, 0x33};
    uint8_t buf[16];
    size_t n = pc_hart_build(HART_DELIM_STX, &addr, 1, 0x2A, data, 3, buf, sizeof(buf));
    HartFrame f;
    TEST_ASSERT_TRUE(pc_hart_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_HEX8(0x2A, f.command);
    TEST_ASSERT_EQUAL_size_t(3, f.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, f.data, 3);
    // Corrupt the checksum -> parse fails.
    buf[n - 1] ^= 0xFF;
    TEST_ASSERT_FALSE(pc_hart_parse(buf, n, &f));
    // Truncated -> fails.
    TEST_ASSERT_FALSE(pc_hart_parse(buf, 3, &f));
}

void test_hartip_header(void)
{
    uint8_t out[8];
    size_t n = pc_hartip_build_header(HARTIP_MSG_REQUEST, HARTIP_ID_TOKEN_PDU, 0, 0x1234, 13, out,
                                      sizeof(out));
    const uint8_t expect[] = {0x01, 0x00, 0x03, 0x00, 0x12, 0x34, 0x00, 0x0D};
    TEST_ASSERT_EQUAL_size_t(8, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 8);
    // Too small a buffer -> 0.
    TEST_ASSERT_EQUAL_size_t(0, pc_hartip_build_header(0, 0, 0, 0, 0, out, 4));
    // Big enough cap but null out pointer -> 0.
    TEST_ASSERT_EQUAL_size_t(0, pc_hartip_build_header(0, 0, 0, 0, 0, NULL, sizeof(out)));
}

void test_hartip_parse(void)
{
    uint8_t msg[32];
    // A HART-IP response carrying a 5-octet token PDU payload; total length = 8 + 5 = 13.
    size_t hn = pc_hartip_build_header(HARTIP_MSG_RESPONSE, HARTIP_ID_TOKEN_PDU, 0x00, 0x0042, 13, msg,
                                       sizeof(msg));
    TEST_ASSERT_EQUAL_size_t(8, hn);
    const uint8_t payload[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    memcpy(msg + 8, payload, sizeof(payload));

    HartIpHeader h;
    TEST_ASSERT_TRUE(pc_hartip_parse_header(msg, 13, &h));
    TEST_ASSERT_EQUAL_UINT8(1, h.version);
    TEST_ASSERT_EQUAL_UINT8(HARTIP_MSG_RESPONSE, h.msg_type);
    TEST_ASSERT_EQUAL_UINT8(HARTIP_ID_TOKEN_PDU, h.msg_id);
    TEST_ASSERT_EQUAL_UINT8(0, h.status);
    TEST_ASSERT_EQUAL_UINT16(0x0042, h.seq);
    TEST_ASSERT_EQUAL_UINT16(13, h.total_len);
    TEST_ASSERT_EQUAL_size_t(5, h.payload_len);
    TEST_ASSERT_NOT_NULL(h.payload);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, h.payload, 5);

    // A header-only message (total_len 8) parses with a null payload.
    pc_hartip_build_header(HARTIP_MSG_REQUEST, HARTIP_ID_KEEPALIVE, 0, 1, 8, msg, sizeof(msg));
    TEST_ASSERT_TRUE(pc_hartip_parse_header(msg, 8, &h));
    TEST_ASSERT_EQUAL_size_t(0, h.payload_len);
    TEST_ASSERT_NULL(h.payload);

    // Truncation (declared 13, only 10 present), a byte count below the header, a short buffer, and nulls.
    pc_hartip_build_header(HARTIP_MSG_RESPONSE, HARTIP_ID_TOKEN_PDU, 0, 1, 13, msg, sizeof(msg));
    TEST_ASSERT_FALSE(pc_hartip_parse_header(msg, 10, &h)); // total_len 13 > len 10
    msg[6] = 0x00;
    msg[7] = 0x05; // total_len 5 < header length 8
    TEST_ASSERT_FALSE(pc_hartip_parse_header(msg, 8, &h));
    TEST_ASSERT_FALSE(pc_hartip_parse_header(msg, 7, &h)); // short buffer
    TEST_ASSERT_FALSE(pc_hartip_parse_header(NULL, 8, &h));
    TEST_ASSERT_FALSE(pc_hartip_parse_header(msg, 8, NULL));
}

void test_build_and_parse_guards()
{
    uint8_t out[32];
    uint8_t addr[5] = {0};
    uint8_t data[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_size_t(0, pc_hart_build(0x82, addr, 2, 0, data, sizeof(data), out, sizeof(out))); // bad addr_len
    TEST_ASSERT_EQUAL_size_t(0, pc_hart_build(0x82, NULL, 5, 0, data, sizeof(data), out, sizeof(out))); // null addr
    TEST_ASSERT_EQUAL_size_t(0, pc_hart_build(0x82, addr, 5, 0, data, sizeof(data), out, 4)); // cap too small
    // Valid addr, but data_len > 0 with a null data pointer -> 0.
    TEST_ASSERT_EQUAL_size_t(0, pc_hart_build(0x82, addr, 1, 0, NULL, 3, out, sizeof(out)));
    // data_len exceeds the 1-byte byte-count field (> 0xFF), even though it would otherwise fit -> 0.
    static uint8_t big_data[300] = {0};
    static uint8_t big_out[400];
    TEST_ASSERT_EQUAL_size_t(0, pc_hart_build(0x82, addr, 1, 0, big_data, sizeof(big_data), big_out, sizeof(big_out)));

    HartFrame hf;
    TEST_ASSERT_FALSE(pc_hart_parse(NULL, 10, &hf)); // null frame
    uint8_t tiny[2] = {0x82, 0x00};
    TEST_ASSERT_FALSE(pc_hart_parse(tiny, sizeof(tiny), &hf)); // len < minimum

    uint8_t addr1 = 0x80;
    uint8_t valid_frame[16];
    size_t vn = pc_hart_build(HART_DELIM_STX, &addr1, 1, 0x00, NULL, 0, valid_frame, sizeof(valid_frame));
    TEST_ASSERT_FALSE(pc_hart_parse(valid_frame, vn, NULL)); // valid frame, null out struct

    // len >= min (header readable) but < the byte-count-derived expected length -> truncated-data rejection.
    uint8_t data3[] = {0x01, 0x02, 0x03};
    uint8_t frame_full[16];
    size_t fn =
        pc_hart_build(HART_DELIM_STX, &addr1, 1, 0x00, data3, sizeof(data3), frame_full, sizeof(frame_full));
    TEST_ASSERT_FALSE(pc_hart_parse(frame_full, fn - 1, &hf));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_checksum);
    RUN_TEST(test_build_command0_short);
    RUN_TEST(test_build_with_data);
    RUN_TEST(test_build_long_address);
    RUN_TEST(test_parse_roundtrip_and_bad_checksum);
    RUN_TEST(test_hartip_header);
    RUN_TEST(test_hartip_parse);
    RUN_TEST(test_build_and_parse_guards);
    return UNITY_END();
}
