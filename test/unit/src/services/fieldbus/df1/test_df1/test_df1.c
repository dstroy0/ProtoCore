// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Allen-Bradley DF1 full-duplex frame codec (services/fieldbus/df1/df1.h).
//
// Two published anchors carry this module. The block check character is defined in AB publication
// 1770-6.5.16 as the two's complement of the modulo-256 sum of the application data, so
// sum(data) + BCC == 0 (mod 256) is an identity the arithmetic itself gives, and each expected BCC
// below is added up in its comment. The frame's other check is CRC-16/ARC (poly 0x8005 reflected as
// 0xA001, init 0, no final XOR), whose catalogued check value is CRC("123456789") = 0xBB3D - that
// is test_crc_matches_the_published_check_value, the load-bearing case, because it pins reflection,
// initial value and final XOR all at once. Get any of the three wrong and every frame this codec
// emits is rejected by a real PLC.

#include "services/fieldbus/df1/df1.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The DF1 control characters are their ASCII code points.
void test_control_characters(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x10u, DF1_DLE);
    TEST_ASSERT_EQUAL_HEX8(0x02u, DF1_STX);
    TEST_ASSERT_EQUAL_HEX8(0x03u, DF1_ETX);
}

// CRC-16/ARC's catalogued check value: the CRC of the nine ASCII characters "123456789" is 0xBB3D.
void test_crc_matches_the_published_check_value(void)
{
    static const uint8_t CHECK[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0xBB3Du, protocore_df1_crc(CHECK, sizeof(CHECK)));

    // The initial value is zero and there is no final XOR, so an empty message CRCs to zero.
    TEST_ASSERT_EQUAL_HEX16(0x0000u, protocore_df1_crc(CHECK, 0));
    static const uint8_t ZERO[1] = {0x00};
    TEST_ASSERT_EQUAL_HEX16(0x0000u, protocore_df1_crc(ZERO, 1));
}

// The BCC is the two's complement of the modulo-256 sum, so data and check always sum to zero.
void test_bcc_and_data_sum_to_zero(void)
{
    static const uint8_t DATA[3] = {0x11, 0x22, 0x33}; // 0x11 + 0x22 + 0x33 = 0x66
    uint8_t bcc = protocore_df1_bcc(DATA, sizeof(DATA));
    TEST_ASSERT_EQUAL_HEX8(0x9Au, bcc); // 0x100 - 0x66
    TEST_ASSERT_EQUAL_HEX8(0x00u, (uint8_t)(0x66u + bcc));

    // A sum of zero yields a check of zero, not 0x100.
    static const uint8_t ZERO_SUM[2] = {0x80, 0x80};
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_df1_bcc(ZERO_SUM, sizeof(ZERO_SUM)));
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_df1_bcc(ZERO_SUM, 0));

    // The identity holds for any run: adding the BCC to the sum gives zero.
    static const uint8_t LONG[8] = {0xFF, 0x01, 0x7F, 0x80, 0x00, 0xAB, 0xCD, 0xEF};
    uint8_t sum = 0;
    for (size_t i = 0; i < sizeof(LONG); i++)
    {
        sum = (uint8_t)(sum + LONG[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0x00u, (uint8_t)(sum + protocore_df1_bcc(LONG, sizeof(LONG))));
}

// A full-duplex frame is DLE STX, the application data, DLE ETX, then the check octet.
//
// A protected typed logical read (CMD 0x0F, FNC 0xA2) of two octets from file 7, type 0x89:
//   DST 00, SRC 00, CMD 0F, STS 00, TNS 01 00, FNC A2, size 02, file 07, type 89, elem 00, sub 00
//   sum = 0x0F + 0x01 + 0xA2 + 0x02 + 0x07 + 0x89 = 15 + 1 + 162 + 2 + 7 + 137 = 324 = 0x144
//   modulo 256 = 0x44, so BCC = 0x100 - 0x44 = 0xBC
void test_bcc_frame_layout(void)
{
    static const uint8_t DATA[12] = {0x00, 0x00, 0x0F, 0x00, 0x01, 0x00, 0xA2, 0x02, 0x07, 0x89, 0x00, 0x00};
    static const uint8_t WANT[17] = {0x10, 0x02, 0x00, 0x00, 0x0F, 0x00, 0x01, 0x00, 0xA2,
                                     0x02, 0x07, 0x89, 0x00, 0x00, 0x10, 0x03, 0xBC};
    uint8_t buf[32];
    memset(buf, 0xEE, sizeof(buf));
    size_t n = protocore_df1_build_frame(buf, sizeof(buf), DATA, sizeof(DATA), DF1_CHECK_BCC);
    TEST_ASSERT_EQUAL_size_t(2u + 12u + 2u + 1u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 17);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[17]);

    uint8_t out[32];
    size_t out_len = 0;
    TEST_ASSERT_TRUE(protocore_df1_parse_frame(buf, n, DF1_CHECK_BCC, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(DATA), out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, out, sizeof(DATA));
}

// The CRC form appends two octets, low byte first, and the CRC covers the application data plus the
// ETX octet - so it is the CRC of data || 0x03, not of the data alone.
void test_crc_frame_covers_the_data_and_the_etx(void)
{
    static const uint8_t DATA[3] = {0x11, 0x22, 0x33};
    static const uint8_t DATA_PLUS_ETX[4] = {0x11, 0x22, 0x33, 0x03};
    uint8_t buf[32];
    size_t n = protocore_df1_build_frame(buf, sizeof(buf), DATA, sizeof(DATA), DF1_CHECK_CRC);
    TEST_ASSERT_EQUAL_size_t(2u + 3u + 2u + 2u, n);
    TEST_ASSERT_EQUAL_HEX8(0x10u, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x03u, buf[6]);

    uint16_t want = protocore_df1_crc(DATA_PLUS_ETX, sizeof(DATA_PLUS_ETX));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(want & 0xFFu), buf[7]); // low byte first
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(want >> 8), buf[8]);
    // The CRC of the data alone is a different value, so a codec that omitted the ETX would differ.
    TEST_ASSERT_TRUE(want != protocore_df1_crc(DATA, sizeof(DATA)));

    uint8_t out[32];
    size_t out_len = 0;
    TEST_ASSERT_TRUE(protocore_df1_parse_frame(buf, n, DF1_CHECK_CRC, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(3u, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, out, 3);
}

// A data octet equal to DLE is transmitted twice so the receiver cannot mistake it for the start of
// a control sequence, and the doubled pair un-stuffs back to one octet.
void test_dle_bytes_are_doubled_on_the_wire(void)
{
    static const uint8_t DATA[3] = {0x10, 0x41, 0x10};
    // sum = 0x10 + 0x41 + 0x10 = 0x61, so BCC = 0x100 - 0x61 = 0x9F
    static const uint8_t WANT[10] = {0x10, 0x02, 0x10, 0x10, 0x41, 0x10, 0x10, 0x10, 0x03, 0x9F};
    uint8_t buf[32];
    size_t n = protocore_df1_build_frame(buf, sizeof(buf), DATA, sizeof(DATA), DF1_CHECK_BCC);
    TEST_ASSERT_EQUAL_size_t(10u, n); // 2 + (3 data + 2 stuffing) + 2 + 1
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 10);

    uint8_t out[32];
    size_t out_len = 0;
    TEST_ASSERT_TRUE(protocore_df1_parse_frame(buf, n, DF1_CHECK_BCC, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(3u, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, out, 3);

    // A doubled DLE counts once in the check, so the BCC is over the un-stuffed data.
    TEST_ASSERT_EQUAL_HEX8(protocore_df1_bcc(DATA, sizeof(DATA)), buf[n - 1]);

    // An ETX inside the data needs no stuffing: only DLE is a lead-in.
    static const uint8_t WITH_ETX[2] = {0x03, 0x02};
    n = protocore_df1_build_frame(buf, sizeof(buf), WITH_ETX, sizeof(WITH_ETX), DF1_CHECK_BCC);
    TEST_ASSERT_EQUAL_size_t(7u, n);
    TEST_ASSERT_TRUE(protocore_df1_parse_frame(buf, n, DF1_CHECK_BCC, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(2u, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WITH_ETX, out, 2);
}

// Anything the builder emits, the parser must return unchanged, over data that exercises every
// octet value including the control characters.
void test_round_trip_over_every_octet_value(void)
{
    uint8_t data[256];
    for (size_t i = 0; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)i;
    }
    uint8_t buf[600];
    uint8_t out[300];
    size_t out_len = 0;

    size_t n = protocore_df1_build_frame(buf, sizeof(buf), data, sizeof(data), DF1_CHECK_BCC);
    TEST_ASSERT_EQUAL_size_t(2u + 256u + 1u + 2u + 1u, n); // one DLE in the range, so one stuffing octet
    TEST_ASSERT_TRUE(protocore_df1_parse_frame(buf, n, DF1_CHECK_BCC, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(data), out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, out, sizeof(data));

    n = protocore_df1_build_frame(buf, sizeof(buf), data, sizeof(data), DF1_CHECK_CRC);
    TEST_ASSERT_EQUAL_size_t(2u + 256u + 1u + 2u + 2u, n);
    TEST_ASSERT_TRUE(protocore_df1_parse_frame(buf, n, DF1_CHECK_CRC, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(data), out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, out, sizeof(data));
}

// A zero-length message is a legal frame: DLE STX DLE ETX plus the check.
void test_empty_message(void)
{
    uint8_t buf[16];
    size_t n = protocore_df1_build_frame(buf, sizeof(buf), NULL, 0, DF1_CHECK_BCC);
    static const uint8_t WANT[5] = {0x10, 0x02, 0x10, 0x03, 0x00}; // empty sum -> BCC 0
    TEST_ASSERT_EQUAL_size_t(5u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 5);

    uint8_t out[8];
    size_t out_len = 0xFFFF;
    TEST_ASSERT_TRUE(protocore_df1_parse_frame(buf, n, DF1_CHECK_BCC, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(0u, out_len);

    n = protocore_df1_build_frame(buf, sizeof(buf), NULL, 0, DF1_CHECK_CRC);
    TEST_ASSERT_EQUAL_size_t(6u, n);
    TEST_ASSERT_TRUE(protocore_df1_parse_frame(buf, n, DF1_CHECK_CRC, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(0u, out_len);
}

// A corrupted octet must not verify: flipping one bit of any application-data octet changes the
// sum, and flipping the check octet fails just as surely.
void test_a_corrupted_frame_fails_its_check(void)
{
    static const uint8_t DATA[4] = {0x41, 0x42, 0x43, 0x44};
    uint8_t buf[24];
    size_t n = protocore_df1_build_frame(buf, sizeof(buf), DATA, sizeof(DATA), DF1_CHECK_BCC);
    TEST_ASSERT_EQUAL_size_t(9u, n);

    uint8_t out[16];
    size_t out_len = 0;
    for (size_t i = 2; i < n; i++)
    {
        if (i == n - 3 || i == n - 2) // the DLE ETX terminator, not data
        {
            continue;
        }
        uint8_t bad[24];
        memcpy(bad, buf, n);
        bad[i] ^= 0x01;
        TEST_ASSERT_FALSE_MESSAGE(protocore_df1_parse_frame(bad, n, DF1_CHECK_BCC, out, sizeof(out), &out_len),
                                  "corrupted octet accepted");
    }

    // The same for the CRC form.
    n = protocore_df1_build_frame(buf, sizeof(buf), DATA, sizeof(DATA), DF1_CHECK_CRC);
    for (size_t i = 2; i < n; i++)
    {
        if (i == n - 4 || i == n - 3)
        {
            continue;
        }
        uint8_t bad[24];
        memcpy(bad, buf, n);
        bad[i] ^= 0x01;
        TEST_ASSERT_FALSE_MESSAGE(protocore_df1_parse_frame(bad, n, DF1_CHECK_CRC, out, sizeof(out), &out_len),
                                  "corrupted octet accepted");
    }
}

// Framing that is not DLE STX, or that never reaches DLE ETX, is refused.
void test_framing_refusals(void)
{
    uint8_t out[16];
    size_t out_len = 0;

    static const uint8_t NO_STX[6] = {0x10, 0x05, 0x41, 0x10, 0x03, 0xBF};
    TEST_ASSERT_FALSE(protocore_df1_parse_frame(NO_STX, sizeof(NO_STX), DF1_CHECK_BCC, out, sizeof(out), &out_len));

    static const uint8_t NO_DLE[6] = {0x00, 0x02, 0x41, 0x10, 0x03, 0xBF};
    TEST_ASSERT_FALSE(protocore_df1_parse_frame(NO_DLE, sizeof(NO_DLE), DF1_CHECK_BCC, out, sizeof(out), &out_len));

    static const uint8_t NO_ETX[5] = {0x10, 0x02, 0x41, 0x42, 0x43}; // no terminator at all
    TEST_ASSERT_FALSE(protocore_df1_parse_frame(NO_ETX, sizeof(NO_ETX), DF1_CHECK_BCC, out, sizeof(out), &out_len));

    // A DLE followed by something that is neither DLE nor ETX is an unexpected control symbol.
    static const uint8_t BAD_ESCAPE[7] = {0x10, 0x02, 0x41, 0x10, 0x06, 0x10, 0x03};
    TEST_ASSERT_FALSE(
        protocore_df1_parse_frame(BAD_ESCAPE, sizeof(BAD_ESCAPE), DF1_CHECK_BCC, out, sizeof(out), &out_len));

    // A DLE as the very last octet has no partner.
    static const uint8_t TRAILING_DLE[5] = {0x10, 0x02, 0x41, 0x42, 0x10};
    TEST_ASSERT_FALSE(
        protocore_df1_parse_frame(TRAILING_DLE, sizeof(TRAILING_DLE), DF1_CHECK_BCC, out, sizeof(out), &out_len));

    // Shorter than DLE STX DLE ETX plus a check.
    static const uint8_t STUB[4] = {0x10, 0x02, 0x10, 0x03};
    TEST_ASSERT_FALSE(protocore_df1_parse_frame(STUB, sizeof(STUB), DF1_CHECK_BCC, out, sizeof(out), &out_len));
    TEST_ASSERT_FALSE(protocore_df1_parse_frame(STUB, 5, DF1_CHECK_CRC, out, sizeof(out), &out_len));
    TEST_ASSERT_FALSE(protocore_df1_parse_frame(NULL, 8, DF1_CHECK_BCC, out, sizeof(out), &out_len));
    TEST_ASSERT_FALSE(protocore_df1_parse_frame(STUB, sizeof(STUB), DF1_CHECK_BCC, NULL, sizeof(out), &out_len));
}

// A destination too small for the un-stuffed data stops rather than writing past it, and a build
// that does not fit reports 0 rather than a partial frame.
void test_capacity_refusals(void)
{
    static const uint8_t DATA[4] = {0x41, 0x42, 0x43, 0x44};
    uint8_t buf[24];
    size_t n = protocore_df1_build_frame(buf, sizeof(buf), DATA, sizeof(DATA), DF1_CHECK_BCC);
    TEST_ASSERT_EQUAL_size_t(9u, n);

    uint8_t small[3];
    size_t out_len = 0;
    TEST_ASSERT_FALSE(protocore_df1_parse_frame(buf, n, DF1_CHECK_BCC, small, sizeof(small), &out_len));
    uint8_t exact[4];
    TEST_ASSERT_TRUE(protocore_df1_parse_frame(buf, n, DF1_CHECK_BCC, exact, sizeof(exact), &out_len));
    TEST_ASSERT_EQUAL_size_t(4u, out_len);

    // A doubled DLE takes one octet of the destination, not two.
    static const uint8_t DLE_DATA[2] = {0x10, 0x10};
    n = protocore_df1_build_frame(buf, sizeof(buf), DLE_DATA, sizeof(DLE_DATA), DF1_CHECK_BCC);
    TEST_ASSERT_EQUAL_size_t(9u, n); // 2 + 4 + 2 + 1
    uint8_t two[2];
    TEST_ASSERT_TRUE(protocore_df1_parse_frame(buf, n, DF1_CHECK_BCC, two, sizeof(two), &out_len));
    TEST_ASSERT_EQUAL_size_t(2u, out_len);
    uint8_t one[1];
    TEST_ASSERT_FALSE(protocore_df1_parse_frame(buf, n, DF1_CHECK_BCC, one, sizeof(one), &out_len));

    // Build refusals.
    TEST_ASSERT_EQUAL_size_t(0u, protocore_df1_build_frame(buf, 8, DATA, sizeof(DATA), DF1_CHECK_BCC));
    TEST_ASSERT_EQUAL_size_t(9u, protocore_df1_build_frame(buf, 9, DATA, sizeof(DATA), DF1_CHECK_BCC));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_df1_build_frame(buf, 9, DATA, sizeof(DATA), DF1_CHECK_CRC)); // needs 10
    TEST_ASSERT_EQUAL_size_t(0u, protocore_df1_build_frame(NULL, sizeof(buf), DATA, sizeof(DATA), DF1_CHECK_BCC));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_df1_build_frame(buf, sizeof(buf), NULL, 4, DF1_CHECK_BCC));
}

// The out_len pointer is optional: a caller that only wants the check verified may omit it.
void test_out_len_is_optional(void)
{
    static const uint8_t DATA[2] = {0x41, 0x42};
    uint8_t buf[16];
    uint8_t out[8];
    size_t n = protocore_df1_build_frame(buf, sizeof(buf), DATA, sizeof(DATA), DF1_CHECK_CRC);
    TEST_ASSERT_TRUE(protocore_df1_parse_frame(buf, n, DF1_CHECK_CRC, out, sizeof(out), NULL));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, out, 2);
}
