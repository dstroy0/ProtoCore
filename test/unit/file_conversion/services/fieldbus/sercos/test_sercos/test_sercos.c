// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/fieldbus/sercos/sercos.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_idn_bit_structure(void)
{
    struct
    {
        proto_bool product;
        uint8_t set;
        uint16_t block;
        uint16_t idn;
    } static const CASES[] = {
        {PROTO_FALSE, 0, 100, 0x0064},
        {PROTO_FALSE, 0, 1, 0x0001},
        {PROTO_FALSE, 0, 0, 0x0000},
        {PROTO_TRUE, 0, 100, 0x8064},
        {PROTO_FALSE, 1, 100, 0x1064},
        {PROTO_FALSE, 7, 4095, 0x7FFF},
        {PROTO_TRUE, 7, 4095, 0xFFFF},
        {PROTO_TRUE, 0, 0, 0x8000},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint16_t got = protocore_sercos_idn(CASES[i].product, CASES[i].set, CASES[i].block);
        TEST_ASSERT_EQUAL_HEX16(CASES[i].idn, got);

        proto_bool p = PROTO_FALSE;
        uint8_t s = 0xFF;
        uint16_t b = 0xFFFF;
        protocore_sercos_idn_parse(CASES[i].idn, &p, &s, &b);
        TEST_ASSERT_EQUAL_INT(CASES[i].product ? 1 : 0, p ? 1 : 0);
        TEST_ASSERT_EQUAL_UINT8(CASES[i].set, s);
        TEST_ASSERT_EQUAL_UINT16(CASES[i].block, b);
    }
}

void test_idn_fields_do_not_overlap(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x8000, protocore_sercos_idn(PROTO_TRUE, 0, 0));
    TEST_ASSERT_EQUAL_HEX16(0x7000, protocore_sercos_idn(PROTO_FALSE, 7, 0));
    TEST_ASSERT_EQUAL_HEX16(0x0FFF, protocore_sercos_idn(PROTO_FALSE, 0, 4095));

    TEST_ASSERT_EQUAL_HEX16(0x0000, protocore_sercos_idn(PROTO_FALSE, 8, 0));
    TEST_ASSERT_EQUAL_HEX16(0x1000, protocore_sercos_idn(PROTO_FALSE, 9, 0));
    TEST_ASSERT_EQUAL_HEX16(0x0000, protocore_sercos_idn(PROTO_FALSE, 0, 0x1000));
    TEST_ASSERT_EQUAL_HEX16(0x0001, protocore_sercos_idn(PROTO_FALSE, 0, 0x1001));
}

void test_idn_round_trip_over_every_word(void)
{
    for (uint32_t v = 0; v <= 0xFFFFu; v++)
    {
        proto_bool p = PROTO_FALSE;
        uint8_t s = 0;
        uint16_t b = 0;
        protocore_sercos_idn_parse((uint16_t)v, &p, &s, &b);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)v, protocore_sercos_idn(p, s, b));
    }
}

void test_idn_parse_accepts_null_outputs(void)
{
    uint8_t s = 0;
    uint16_t b = 0;
    proto_bool p = PROTO_FALSE;

    protocore_sercos_idn_parse(0x9064, NULL, &s, &b);
    TEST_ASSERT_EQUAL_UINT8(1, s);
    TEST_ASSERT_EQUAL_UINT16(100, b);

    protocore_sercos_idn_parse(0x9064, &p, NULL, &b);
    TEST_ASSERT_TRUE(p);
    TEST_ASSERT_EQUAL_UINT16(100, b);

    protocore_sercos_idn_parse(0x9064, &p, &s, NULL);
    TEST_ASSERT_TRUE(p);
    TEST_ASSERT_EQUAL_UINT8(1, s);

    protocore_sercos_idn_parse(0x9064, NULL, NULL, NULL);
}

void test_telegram_round_trip(void)
{
    TEST_ASSERT_EQUAL_INT(4, SERCOS_HDR_LEN);
    TEST_ASSERT_EQUAL_HEX8(0x00, SERCOS_TEL_MDT);
    TEST_ASSERT_EQUAL_HEX8(0x01, SERCOS_TEL_AT);

    static const uint8_t TYPES[2] = {SERCOS_TEL_MDT, SERCOS_TEL_AT};
    uint8_t pdo[32];
    for (size_t i = 0; i < sizeof(pdo); i++)
    {
        pdo[i] = (uint8_t)(i * 11 + 5);
    }

    for (size_t t = 0; t < 2; t++)
    {
        for (size_t len = 0; len <= sizeof(pdo); len++)
        {
            uint8_t out[64];
            size_t n = protocore_sercos_build(TYPES[t], 0x04, 0xBEEF, len ? pdo : NULL, len, out, sizeof(out));
            TEST_ASSERT_EQUAL_UINT(SERCOS_HDR_LEN + len, n);
            TEST_ASSERT_EQUAL_HEX8(TYPES[t], out[0]);
            TEST_ASSERT_EQUAL_HEX8(0x04, out[1]);
            TEST_ASSERT_EQUAL_HEX8(0xEF, out[2]);
            TEST_ASSERT_EQUAL_HEX8(0xBE, out[3]);

            SercosTelegram s;
            TEST_ASSERT_TRUE(protocore_sercos_parse(out, n, &s));
            TEST_ASSERT_EQUAL_HEX8(TYPES[t], s.type);
            TEST_ASSERT_EQUAL_HEX8(0x04, s.phase);
            TEST_ASSERT_EQUAL_HEX16(0xBEEF, s.cycle);
            TEST_ASSERT_EQUAL_UINT(len, s.data_len);
            if (len)
            {
                TEST_ASSERT_EQUAL_HEX8_ARRAY(pdo, s.data, len);
                TEST_ASSERT_EQUAL_PTR(out + SERCOS_HDR_LEN, s.data);
            }
            else
            {
                TEST_ASSERT_NULL(s.data);
            }
        }
    }
}

void test_cycle_count_is_a_full_16_bit_field(void)
{
    static const uint16_t CYCLES[5] = {0, 1, 0x00FF, 0x0100, 0xFFFF};
    uint8_t out[8];
    SercosTelegram s;
    for (size_t i = 0; i < 5; i++)
    {
        TEST_ASSERT_EQUAL_UINT(SERCOS_HDR_LEN,
                               protocore_sercos_build(SERCOS_TEL_MDT, 0, CYCLES[i], NULL, 0, out, sizeof(out)));
        TEST_ASSERT_TRUE(protocore_sercos_parse(out, SERCOS_HDR_LEN, &s));
        TEST_ASSERT_EQUAL_HEX16(CYCLES[i], s.cycle);
    }
}

void test_phase_octet_is_carried_whole(void)
{
    uint8_t out[8];
    SercosTelegram s;
    for (unsigned p = 0; p < 256; p++)
    {
        TEST_ASSERT_EQUAL_UINT(SERCOS_HDR_LEN,
                               protocore_sercos_build(SERCOS_TEL_AT, (uint8_t)p, 1, NULL, 0, out, sizeof(out)));
        TEST_ASSERT_TRUE(protocore_sercos_parse(out, SERCOS_HDR_LEN, &s));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)p, s.phase);
    }
}

void test_only_mdt_and_at_are_accepted(void)
{
    uint8_t out[8];
    SercosTelegram s;
    for (unsigned t = 2; t < 256; t++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, protocore_sercos_build((uint8_t)t, 0, 0, NULL, 0, out, sizeof(out)));
        uint8_t frame[4] = {(uint8_t)t, 0x00, 0x00, 0x00};
        TEST_ASSERT_FALSE(protocore_sercos_parse(frame, sizeof(frame), &s));
    }
}

void test_bounds_refusals(void)
{
    uint8_t out[16];
    SercosTelegram s;
    static const uint8_t PDO[4] = {1, 2, 3, 4};

    static const uint8_t FRAME[4] = {SERCOS_TEL_MDT, 0x02, 0x34, 0x12};
    for (size_t n = 0; n < SERCOS_HDR_LEN; n++)
    {
        TEST_ASSERT_FALSE(protocore_sercos_parse(FRAME, n, &s));
    }
    TEST_ASSERT_TRUE(protocore_sercos_parse(FRAME, SERCOS_HDR_LEN, &s));
    TEST_ASSERT_EQUAL_HEX16(0x1234, s.cycle);
    TEST_ASSERT_FALSE(protocore_sercos_parse(NULL, SERCOS_HDR_LEN, &s));
    TEST_ASSERT_FALSE(protocore_sercos_parse(FRAME, SERCOS_HDR_LEN, NULL));

    for (size_t cap = 0; cap < SERCOS_HDR_LEN + sizeof(PDO); cap++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, protocore_sercos_build(SERCOS_TEL_MDT, 0, 0, PDO, sizeof(PDO), out, cap));
    }
    TEST_ASSERT_EQUAL_UINT(8u, protocore_sercos_build(SERCOS_TEL_MDT, 0, 0, PDO, sizeof(PDO), out, 8));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_sercos_build(SERCOS_TEL_MDT, 0, 0, NULL, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_sercos_build(SERCOS_TEL_MDT, 0, 0, NULL, 0, NULL, sizeof(out)));
}

void test_mdt_at_exchange(void)
{
    uint8_t buf[32];
    SercosTelegram s;

    uint16_t cmd_idn = protocore_sercos_idn(PROTO_FALSE, 0, 47);
    uint16_t fb_idn = protocore_sercos_idn(PROTO_FALSE, 0, 51);
    TEST_ASSERT_EQUAL_HEX16(0x002F, cmd_idn);
    TEST_ASSERT_EQUAL_HEX16(0x0033, fb_idn);

    static const uint8_t SETPOINT[4] = {0x10, 0x27, 0x00, 0x00};
    size_t n = protocore_sercos_build(SERCOS_TEL_MDT, 4, 1, SETPOINT, sizeof(SETPOINT), buf, sizeof(buf));
    TEST_ASSERT_TRUE(protocore_sercos_parse(buf, n, &s));
    TEST_ASSERT_EQUAL_HEX8(SERCOS_TEL_MDT, s.type);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SETPOINT, s.data, sizeof(SETPOINT));

    static const uint8_t FEEDBACK[4] = {0x0F, 0x27, 0x00, 0x00};
    n = protocore_sercos_build(SERCOS_TEL_AT, 4, 1, FEEDBACK, sizeof(FEEDBACK), buf, sizeof(buf));
    TEST_ASSERT_TRUE(protocore_sercos_parse(buf, n, &s));
    TEST_ASSERT_EQUAL_HEX8(SERCOS_TEL_AT, s.type);
    TEST_ASSERT_EQUAL_HEX16(1, s.cycle);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FEEDBACK, s.data, sizeof(FEEDBACK));
}
