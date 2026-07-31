// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the EnOcean ESP3 codec (services/radio/enocean): the CRC-8 (poly 0x07) against
// known-answer values, a build -> parse round trip, malformed framing (bad sync / header
// CRC / data CRC), incomplete telegrams, over-length rejection, and resynchronisation.
// Pure host tests.
//
// The env sizes PC_ENOCEAN_MAX_DATA = 16.

#include "services/radio/enocean/enocean.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_crc8_known_answers()
{
    const uint8_t one[1] = {0x01};
    TEST_ASSERT_EQUAL_HEX8(0x07, pc_esp3_crc8(one, 1)); // hand-derived for poly 0x07
    // CRC-8 (poly 0x07, init 0, no reflection) check value for "123456789" is 0xF4.
    const uint8_t check[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX8(0xF4, pc_esp3_crc8(check, 9));
}

void test_build_then_parse_round_trip()
{
    const uint8_t data[7] = {0xF6, 0x50, 0x01, 0x02, 0x03, 0x04, 0x30}; // RORG + payload + sender + status
    const uint8_t opt[3] = {0x03, 0x00, 0x00};
    uint8_t buf[64];
    uint16_t n = pc_esp3_build(pc_esp3_type::ESP3_RADIO_ERP1, data, 7, opt, 3, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT16(17, n); // 6 header/crc + 7 data + 3 opt + 1 crc
    TEST_ASSERT_EQUAL_HEX8(ESP3_SYNC, buf[0]);

    pc_esp3_packet p = {};
    int c = pc_esp3_parse(buf, n, &p);
    TEST_ASSERT_EQUAL_INT(17, c);
    TEST_ASSERT_EQUAL_UINT8(pc_esp3_type::ESP3_RADIO_ERP1, p.type);
    TEST_ASSERT_EQUAL_UINT16(7, p.data_len);
    TEST_ASSERT_EQUAL_UINT8(3, p.opt_len);
    TEST_ASSERT_EQUAL_MEMORY(data, p.data, 7);
    TEST_ASSERT_EQUAL_MEMORY(opt, p.opt, 3);
}

static uint16_t sample(uint8_t *buf, uint16_t cap)
{
    const uint8_t data[4] = {0xD5, 0x08, 0x11, 0x22};
    return pc_esp3_build(pc_esp3_type::ESP3_RADIO_ERP1, data, 4, nullptr, 0, buf, cap);
}

void test_parse_rejects_bad_sync()
{
    uint8_t buf[32];
    uint16_t n = sample(buf, sizeof(buf));
    buf[0] = 0x00;
    TEST_ASSERT_EQUAL_INT(-1, pc_esp3_parse(buf, n, nullptr));
}

void test_parse_rejects_bad_header_crc()
{
    uint8_t buf[32];
    uint16_t n = sample(buf, sizeof(buf));
    buf[5] ^= 0xFF; // corrupt CRC8H
    TEST_ASSERT_EQUAL_INT(-1, pc_esp3_parse(buf, n, nullptr));
}

void test_parse_rejects_bad_data_crc()
{
    uint8_t buf[32];
    uint16_t n = sample(buf, sizeof(buf));
    buf[n - 1] ^= 0xFF; // corrupt CRC8D
    TEST_ASSERT_EQUAL_INT(-1, pc_esp3_parse(buf, n, nullptr));
}

void test_parse_needs_more_bytes()
{
    uint8_t buf[32];
    uint16_t n = sample(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, pc_esp3_parse(buf, 3, nullptr));     // header not complete
    TEST_ASSERT_EQUAL_INT(0, pc_esp3_parse(buf, n - 1, nullptr)); // data not complete
}

void test_parse_rejects_over_length()
{
    // A header claiming data_len 100 (> PC_ENOCEAN_MAX_DATA = 16) is rejected early.
    uint8_t buf[8] = {ESP3_SYNC, 0x00, 100, 0x00, (uint8_t)pc_esp3_type::ESP3_RADIO_ERP1, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_INT(-1, pc_esp3_parse(buf, sizeof(buf), nullptr));
}

void test_parse_resynchronises_after_junk()
{
    uint8_t tg[32];
    uint16_t n = sample(tg, sizeof(tg));
    uint8_t buf[40];
    buf[0] = 0x00; // a stray byte before the telegram
    memcpy(buf + 1, tg, n);
    TEST_ASSERT_EQUAL_INT(-1, pc_esp3_parse(buf, (uint16_t)(n + 1), nullptr)); // junk at [0]
    pc_esp3_packet p = {};
    TEST_ASSERT_EQUAL_INT((int)n, pc_esp3_parse(buf + 1, n, &p)); // resynced at the sync byte
    TEST_ASSERT_EQUAL_UINT16(4, p.data_len);
}

void test_build_bounds()
{
    uint8_t data[8] = {0};
    uint8_t small[10];
    TEST_ASSERT_EQUAL_UINT16(
        0, pc_esp3_build(pc_esp3_type::ESP3_RADIO_ERP1, data, 8, nullptr, 0, small, sizeof(small))); // 15 > 10
    uint8_t big[64];
    TEST_ASSERT_EQUAL_UINT16(
        0, pc_esp3_build(pc_esp3_type::ESP3_RADIO_ERP1, big, 17, nullptr, 0, big, sizeof(big))); // 17 > MAX_DATA 16
}

void test_esp3_parse_null_guard()
{
    pc_esp3_packet pkt;
    TEST_ASSERT_EQUAL_INT(0, pc_esp3_parse(nullptr, 10, &pkt)); // null raw
    uint8_t tiny[1] = {0};
    TEST_ASSERT_EQUAL_INT(0, pc_esp3_parse(tiny, 0, &pkt)); // zero length
}

void test_parse_succeeds_with_null_out()
{
    // A fully valid telegram is still framed correctly when the caller doesn't want the
    // parsed fields (out == nullptr) - exercises the false side of `if (out)`.
    uint8_t buf[32];
    uint16_t n = sample(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT((int)n, pc_esp3_parse(buf, n, nullptr));
}

void test_build_rejects_null_out()
{
    const uint8_t data[4] = {0xD5, 0x08, 0x11, 0x22};
    TEST_ASSERT_EQUAL_UINT16(0, pc_esp3_build(pc_esp3_type::ESP3_RADIO_ERP1, data, 4, nullptr, 0, nullptr, 32));
}

void test_erp1_parse()
{
    // A RPS (rocker switch) telegram: RORG 0xF6, 1 payload octet, sender 0x008B1234, status 0x30.
    const uint8_t rps[7] = {0xF6, 0x50, 0x00, 0x8B, 0x12, 0x34, 0x30};
    pc_erp1 t;
    TEST_ASSERT_TRUE(pc_erp1_parse(rps, sizeof(rps), &t));
    TEST_ASSERT_EQUAL_HEX8(PC_ERP_RORG_RPS, t.rorg);
    TEST_ASSERT_EQUAL_UINT8(1, t.payload_len);
    TEST_ASSERT_EQUAL_HEX8(0x50, t.payload[0]);
    TEST_ASSERT_EQUAL_HEX32(0x008B1234, t.sender_id);
    TEST_ASSERT_EQUAL_HEX8(0x30, t.status);

    // A 4BS sensor telegram: RORG 0xA5, 4 payload octets, sender 0xDEADBEEF.
    const uint8_t fbs[10] = {0xA5, 0x01, 0x02, 0x03, 0x04, 0xDE, 0xAD, 0xBE, 0xEF, 0x00};
    TEST_ASSERT_TRUE(pc_erp1_parse(fbs, sizeof(fbs), &t));
    TEST_ASSERT_EQUAL_HEX8(PC_ERP_RORG_4BS, t.rorg);
    TEST_ASSERT_EQUAL_UINT8(4, t.payload_len);
    TEST_ASSERT_EQUAL_HEX8(0x04, t.payload[3]);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, t.sender_id);

    // A minimal 6-octet telegram (zero payload) parses with a null payload.
    const uint8_t minimal[6] = {0xD5, 0x11, 0x22, 0x33, 0x44, 0x55};
    TEST_ASSERT_TRUE(pc_erp1_parse(minimal, sizeof(minimal), &t));
    TEST_ASSERT_EQUAL_UINT8(0, t.payload_len);
    TEST_ASSERT_NULL(t.payload);
    TEST_ASSERT_EQUAL_HEX32(0x11223344, t.sender_id);
    TEST_ASSERT_EQUAL_HEX8(0x55, t.status);

    // Integration: the ERP1 telegram is the data field of a RADIO_ERP1 ESP3 packet.
    uint8_t buf[64];
    uint16_t n = pc_esp3_build(pc_esp3_type::ESP3_RADIO_ERP1, rps, sizeof(rps), nullptr, 0, buf, sizeof(buf));
    pc_esp3_packet p = {};
    TEST_ASSERT_GREATER_THAN(0, pc_esp3_parse(buf, n, &p));
    TEST_ASSERT_TRUE(pc_erp1_parse(p.data, p.data_len, &t));
    TEST_ASSERT_EQUAL_HEX32(0x008B1234, t.sender_id);

    // Too short (< 6) and null guards.
    TEST_ASSERT_FALSE(pc_erp1_parse(rps, 5, &t));
    TEST_ASSERT_FALSE(pc_erp1_parse(nullptr, 7, &t));
    TEST_ASSERT_FALSE(pc_erp1_parse(rps, 7, nullptr));
}

void test_erp1_build()
{
    uint8_t buf[16];

    // Build the RPS telegram from test_erp1_parse and check it byte-for-byte.
    const uint8_t rps_payload[1] = {0x50};
    uint16_t n = pc_erp1_build(buf, sizeof(buf), PC_ERP_RORG_RPS, rps_payload, 1, 0x008B1234, 0x30);
    const uint8_t rps[7] = {0xF6, 0x50, 0x00, 0x8B, 0x12, 0x34, 0x30};
    TEST_ASSERT_EQUAL_UINT16(sizeof(rps), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(rps, buf, n);

    // It round-trips through the parser.
    pc_erp1 t;
    TEST_ASSERT_TRUE(pc_erp1_parse(buf, n, &t));
    TEST_ASSERT_EQUAL_HEX8(PC_ERP_RORG_RPS, t.rorg);
    TEST_ASSERT_EQUAL_HEX32(0x008B1234, t.sender_id);
    TEST_ASSERT_EQUAL_HEX8(0x30, t.status);

    // A 4BS telegram (4 payload octets).
    const uint8_t fbs_payload[4] = {0x01, 0x02, 0x03, 0x04};
    n = pc_erp1_build(buf, sizeof(buf), PC_ERP_RORG_4BS, fbs_payload, 4, 0xDEADBEEF, 0x00);
    const uint8_t fbs[10] = {0xA5, 0x01, 0x02, 0x03, 0x04, 0xDE, 0xAD, 0xBE, 0xEF, 0x00};
    TEST_ASSERT_EQUAL_UINT16(sizeof(fbs), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fbs, buf, n);

    // A zero-payload telegram is 6 octets (RORG + sender id + status).
    n = pc_erp1_build(buf, sizeof(buf), PC_ERP_RORG_1BS, nullptr, 0, 0x11223344, 0x55);
    const uint8_t minimal[6] = {0xD5, 0x11, 0x22, 0x33, 0x44, 0x55};
    TEST_ASSERT_EQUAL_UINT16(sizeof(minimal), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(minimal, buf, n);

    // Guards: a null buffer, a null payload with a nonzero length, and a too-small buffer.
    TEST_ASSERT_EQUAL_UINT16(0, pc_erp1_build(nullptr, sizeof(buf), PC_ERP_RORG_RPS, rps_payload, 1, 0x1, 0x0));
    TEST_ASSERT_EQUAL_UINT16(0, pc_erp1_build(buf, sizeof(buf), PC_ERP_RORG_RPS, nullptr, 1, 0x1, 0x0));
    TEST_ASSERT_EQUAL_UINT16(0, pc_erp1_build(buf, 5, PC_ERP_RORG_RPS, rps_payload, 1, 0x1, 0x0)); // needs 7
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_crc8_known_answers);
    RUN_TEST(test_build_then_parse_round_trip);
    RUN_TEST(test_parse_rejects_bad_sync);
    RUN_TEST(test_parse_rejects_bad_header_crc);
    RUN_TEST(test_parse_rejects_bad_data_crc);
    RUN_TEST(test_parse_needs_more_bytes);
    RUN_TEST(test_parse_rejects_over_length);
    RUN_TEST(test_parse_resynchronises_after_junk);
    RUN_TEST(test_build_bounds);
    RUN_TEST(test_esp3_parse_null_guard);
    RUN_TEST(test_parse_succeeds_with_null_out);
    RUN_TEST(test_build_rejects_null_out);
    RUN_TEST(test_erp1_parse);
    RUN_TEST(test_erp1_build);
    return UNITY_END();
}
