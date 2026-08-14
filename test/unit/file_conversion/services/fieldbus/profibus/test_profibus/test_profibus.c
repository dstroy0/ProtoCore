// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/fieldbus/profibus/profibus.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_start_delimiters_are_four_bits_apart(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x10, PB_SD1);
    TEST_ASSERT_EQUAL_HEX8(0x68, PB_SD2);
    TEST_ASSERT_EQUAL_HEX8(0xA2, PB_SD3);
    TEST_ASSERT_EQUAL_HEX8(0xDC, PB_SD4);
    TEST_ASSERT_EQUAL_HEX8(0x16, PB_ED);

    static const uint8_t SD[4] = {PB_SD1, PB_SD2, PB_SD3, PB_SD4};
    for (int i = 0; i < 4; i++)
    {
        for (int j = i + 1; j < 4; j++)
        {
            uint8_t diff = (uint8_t)(SD[i] ^ SD[j]);
            int bits = 0;
            for (int b = 0; b < 8; b++)
            {
                bits += (diff >> b) & 1;
            }
            TEST_ASSERT_TRUE_MESSAGE(bits >= 4, "start delimiters closer than a Hamming distance of 4");
        }
    }
}

void test_frame_control_request_bits(void)
{
    static const uint8_t REQUESTS[3] = {PB_FC_REQUEST_FDL_STATUS, PB_FC_SRD_LOW, PB_FC_SRD_HIGH};
    for (size_t i = 0; i < sizeof(REQUESTS); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, (uint8_t)(REQUESTS[i] & 0x80u));
        TEST_ASSERT_EQUAL_HEX8(0x40, (uint8_t)(REQUESTS[i] & 0x40u));
    }
    TEST_ASSERT_TRUE(PB_FC_SRD_LOW != PB_FC_SRD_HIGH);
}

void test_fcs_is_the_low_octet_of_the_sum(void)
{
    static const uint8_t A[3] = {0x03, 0x02, 0x49};
    TEST_ASSERT_EQUAL_HEX8(0x4E, protocore_pb_fcs(A, 3));

    static const uint8_t B[3] = {0xFF, 0xFF, 0x02};
    TEST_ASSERT_EQUAL_HEX8(0x00, protocore_pb_fcs(B, 3));

    static const uint8_t C[4] = {0x80, 0x80, 0x80, 0x81};
    TEST_ASSERT_EQUAL_HEX8(0x01, protocore_pb_fcs(C, 4));

    TEST_ASSERT_EQUAL_HEX8(0x00, protocore_pb_fcs(A, 0));
}

void test_fdl_telegram_formats(void)
{
    uint8_t out[32];
    PbTelegram t;

    TEST_ASSERT_EQUAL_UINT(6u, protocore_pb_build_sd1(0x03, 0x02, PB_FC_REQUEST_FDL_STATUS, out, sizeof(out)));
    static const uint8_t SD1_WANT[6] = {0x10, 0x03, 0x02, 0x49, 0x4E, 0x16};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SD1_WANT, out, 6);
    TEST_ASSERT_TRUE(protocore_pb_parse(SD1_WANT, 6, &t));
    TEST_ASSERT_EQUAL_HEX8(PB_SD1, t.sd);
    TEST_ASSERT_EQUAL_HEX8(0x03, t.da);
    TEST_ASSERT_EQUAL_HEX8(0x02, t.sa);
    TEST_ASSERT_EQUAL_HEX8(0x49, t.fc);
    TEST_ASSERT_EQUAL_UINT(0u, t.data_len);
    TEST_ASSERT_NULL(t.data);

    static const uint8_t PDU[4] = {0x11, 0x22, 0x33, 0x44};
    TEST_ASSERT_EQUAL_UINT(13u, protocore_pb_build_sd2(0x05, 0x02, PB_FC_SRD_LOW, PDU, sizeof(PDU), out, sizeof(out)));
    static const uint8_t SD2_WANT[13] = {0x68, 0x07, 0x07, 0x68, 0x05, 0x02, 0x6C, 0x11, 0x22, 0x33, 0x44, 0x1D, 0x16};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SD2_WANT, out, 13);
    TEST_ASSERT_TRUE(protocore_pb_parse(SD2_WANT, 13, &t));
    TEST_ASSERT_EQUAL_HEX8(PB_SD2, t.sd);
    TEST_ASSERT_EQUAL_HEX8(0x05, t.da);
    TEST_ASSERT_EQUAL_HEX8(0x02, t.sa);
    TEST_ASSERT_EQUAL_HEX8(PB_FC_SRD_LOW, t.fc);
    TEST_ASSERT_EQUAL_UINT(sizeof(PDU), t.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PDU, t.data, sizeof(PDU));

    static const uint8_t EIGHT[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_UINT(14u, protocore_pb_build_sd3(0x05, 0x02, PB_FC_SRD_HIGH, EIGHT, out, sizeof(out)));
    static const uint8_t SD3_WANT[14] = {0xA2, 0x05, 0x02, 0x7C, 1, 2, 3, 4, 5, 6, 7, 8, 0xA7, 0x16};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SD3_WANT, out, 14);
    TEST_ASSERT_TRUE(protocore_pb_parse(SD3_WANT, 14, &t));
    TEST_ASSERT_EQUAL_HEX8(PB_SD3, t.sd);
    TEST_ASSERT_EQUAL_UINT(8u, t.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(EIGHT, t.data, 8);
}

void test_sd2_with_no_data_unit(void)
{
    uint8_t out[16];
    TEST_ASSERT_EQUAL_UINT(9u, protocore_pb_build_sd2(0x7F, 0x02, PB_FC_REQUEST_FDL_STATUS, NULL, 0, out, sizeof(out)));
    static const uint8_t WANT[9] = {0x68, 0x03, 0x03, 0x68, 0x7F, 0x02, 0x49, 0xCA, 0x16};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, 9);

    PbTelegram t;
    TEST_ASSERT_TRUE(protocore_pb_parse(WANT, 9, &t));
    TEST_ASSERT_EQUAL_UINT(0u, t.data_len);
    TEST_ASSERT_NULL(t.data);
}

void test_sd2_length_field_across_the_range(void)
{
    static uint8_t data[246];
    for (size_t i = 0; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)(i * 3 + 1);
    }
    uint8_t out[300];

    for (size_t dl = 0; dl <= sizeof(data); dl++)
    {
        size_t n = protocore_pb_build_sd2(0x03, 0x02, PB_FC_SRD_LOW, dl ? data : NULL, dl, out, sizeof(out));
        TEST_ASSERT_EQUAL_UINT(9u + dl, n);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(3 + dl), out[1]);
        TEST_ASSERT_EQUAL_HEX8(out[1], out[2]);
        TEST_ASSERT_EQUAL_HEX8(PB_SD2, out[3]);

        PbTelegram t;
        TEST_ASSERT_TRUE(protocore_pb_parse(out, n, &t));
        TEST_ASSERT_EQUAL_UINT(dl, t.data_len);
        if (dl)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(data, t.data, dl);
        }
    }

    TEST_ASSERT_EQUAL_UINT(0u, protocore_pb_build_sd2(0x03, 0x02, PB_FC_SRD_LOW, data, 247, out, sizeof(out)));
}

void test_parse_refuses_a_damaged_telegram(void)
{
    PbTelegram t;
    uint8_t f[16];

    static const uint8_t SD1[6] = {0x10, 0x03, 0x02, 0x49, 0x4E, 0x16};
    memcpy(f, SD1, sizeof(SD1));
    f[1] ^= 0x01;
    TEST_ASSERT_FALSE(protocore_pb_parse(f, 6, &t));
    memcpy(f, SD1, sizeof(SD1));
    f[5] = 0x17;
    TEST_ASSERT_FALSE(protocore_pb_parse(f, 6, &t));

    static const uint8_t SD2[13] = {0x68, 0x07, 0x07, 0x68, 0x05, 0x02, 0x6C, 0x11, 0x22, 0x33, 0x44, 0x1D, 0x16};
    memcpy(f, SD2, sizeof(SD2));
    f[2] = 0x08;
    TEST_ASSERT_FALSE(protocore_pb_parse(f, 13, &t));
    memcpy(f, SD2, sizeof(SD2));
    f[3] = 0x10;
    TEST_ASSERT_FALSE(protocore_pb_parse(f, 13, &t));
    memcpy(f, SD2, sizeof(SD2));
    f[1] = 0x02;
    f[2] = 0x02;
    TEST_ASSERT_FALSE(protocore_pb_parse(f, 13, &t));
    memcpy(f, SD2, sizeof(SD2));
    f[9] ^= 0x80;
    TEST_ASSERT_FALSE(protocore_pb_parse(f, 13, &t));

    static const uint8_t SD3[14] = {0xA2, 0x05, 0x02, 0x7C, 1, 2, 3, 4, 5, 6, 7, 8, 0xA7, 0x16};
    memcpy(f, SD3, sizeof(SD3));
    f[12] ^= 0x01;
    TEST_ASSERT_FALSE(protocore_pb_parse(f, 14, &t));

    static const uint8_t SD4[6] = {0xDC, 0x03, 0x02, 0x00, 0x00, 0x16};
    TEST_ASSERT_FALSE(protocore_pb_parse(SD4, 6, &t));
    static const uint8_t UNKNOWN[6] = {0x11, 0x03, 0x02, 0x49, 0x4E, 0x16};
    TEST_ASSERT_FALSE(protocore_pb_parse(UNKNOWN, 6, &t));
}

void test_parse_refuses_a_truncated_telegram(void)
{
    PbTelegram t;
    static const uint8_t SD2[13] = {0x68, 0x07, 0x07, 0x68, 0x05, 0x02, 0x6C, 0x11, 0x22, 0x33, 0x44, 0x1D, 0x16};
    for (size_t n = 0; n < 13; n++)
    {
        TEST_ASSERT_FALSE(protocore_pb_parse(SD2, n, &t));
    }
    static const uint8_t SD3[14] = {0xA2, 0x05, 0x02, 0x7C, 1, 2, 3, 4, 5, 6, 7, 8, 0xA7, 0x16};
    for (size_t n = 6; n < 14; n++)
    {
        TEST_ASSERT_FALSE(protocore_pb_parse(SD3, n, &t));
    }
    TEST_ASSERT_FALSE(protocore_pb_parse(NULL, 6, &t));
    TEST_ASSERT_FALSE(protocore_pb_parse(SD2, 13, NULL));
}

void test_builders_refuse_a_short_buffer(void)
{
    uint8_t out[32];
    static const uint8_t EIGHT[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t PDU[4] = {0x11, 0x22, 0x33, 0x44};

    for (size_t cap = 0; cap < 6; cap++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, protocore_pb_build_sd1(1, 2, 3, out, cap));
    }
    for (size_t cap = 0; cap < 13; cap++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, protocore_pb_build_sd2(1, 2, 3, PDU, sizeof(PDU), out, cap));
    }
    for (size_t cap = 0; cap < 14; cap++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, protocore_pb_build_sd3(1, 2, 3, EIGHT, out, cap));
    }

    TEST_ASSERT_EQUAL_UINT(0u, protocore_pb_build_sd1(1, 2, 3, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_pb_build_sd2(1, 2, 3, NULL, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_pb_build_sd3(1, 2, 3, NULL, out, sizeof(out)));
}

void test_address_octets_round_trip(void)
{
    uint8_t out[16];
    PbTelegram t;
    for (unsigned da = 0; da < 256; da++)
    {
        size_t n = protocore_pb_build_sd1((uint8_t)da, (uint8_t)(255 - da), PB_FC_SRD_LOW, out, sizeof(out));
        TEST_ASSERT_EQUAL_UINT(6u, n);
        TEST_ASSERT_TRUE(protocore_pb_parse(out, n, &t));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)da, t.da);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(255 - da), t.sa);
    }
}
