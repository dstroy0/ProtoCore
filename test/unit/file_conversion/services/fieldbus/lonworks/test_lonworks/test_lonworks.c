// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/fieldbus/lonworks/lonworks.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_nv_message_codes(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x80u, LON_MSG_NV_UPDATE);
    TEST_ASSERT_EQUAL_HEX8(0x81u, LON_MSG_NV_POLL);
    TEST_ASSERT_EQUAL_HEX16(0x3FFFu, LON_NV_SELECTOR_MAX);

    TEST_ASSERT_EQUAL_HEX16(0x4000u, (uint16_t)(LON_NV_SELECTOR_MAX + 1u));
}

void test_nv_pdu_layout(void)
{
    static const uint8_t VALUE[3] = {0xAA, 0xBB, 0xCC};
    static const uint8_t WANT[6] = {0x80, 0x12, 0x34, 0xAA, 0xBB, 0xCC};
    uint8_t out[16];
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT),
                             protocore_lon_build_nv(LON_MSG_NV_UPDATE, 0x1234, VALUE, sizeof(VALUE), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    LonNv nv;
    memset(&nv, 0, sizeof(nv));
    TEST_ASSERT_TRUE(protocore_lon_parse_nv(out, sizeof(WANT), &nv));
    TEST_ASSERT_EQUAL_HEX8(LON_MSG_NV_UPDATE, nv.msg_code);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, nv.selector);
    TEST_ASSERT_EQUAL_size_t(sizeof(VALUE), nv.value_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(VALUE, nv.value, sizeof(VALUE));

    TEST_ASSERT_EQUAL_size_t(3u, protocore_lon_build_nv(LON_MSG_NV_POLL, 0x0001, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_TRUE(protocore_lon_parse_nv(out, 3, &nv));
    TEST_ASSERT_EQUAL_HEX8(LON_MSG_NV_POLL, nv.msg_code);
    TEST_ASSERT_EQUAL_HEX16(0x0001u, nv.selector);
    TEST_ASSERT_EQUAL_size_t(0u, nv.value_len);
    TEST_ASSERT_NULL(nv.value);
}

void test_selector_is_fourteen_bits(void)
{
    uint8_t out[8];
    TEST_ASSERT_EQUAL_size_t(3u, protocore_lon_build_nv(LON_MSG_NV_UPDATE, LON_NV_SELECTOR_MAX, NULL, 0, out,
                                                        sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, out[2]);

    TEST_ASSERT_EQUAL_size_t(0u, protocore_lon_build_nv(LON_MSG_NV_UPDATE, 0x4000, NULL, 0, out, sizeof(out)));

    LonNv nv;
    static const uint8_t HIGH_BITS[3] = {0x80, 0xFF, 0xFF};
    TEST_ASSERT_TRUE(protocore_lon_parse_nv(HIGH_BITS, sizeof(HIGH_BITS), &nv));
    TEST_ASSERT_EQUAL_HEX16(0x3FFFu, nv.selector);

    for (uint32_t s = 0; s <= LON_NV_SELECTOR_MAX; s++)
    {
        TEST_ASSERT_EQUAL_size_t(3u,
                                 protocore_lon_build_nv(LON_MSG_NV_UPDATE, (uint16_t)s, NULL, 0, out, sizeof(out)));
        TEST_ASSERT_TRUE(protocore_lon_parse_nv(out, 3, &nv));
        TEST_ASSERT_EQUAL_HEX16((uint16_t)s, nv.selector);
    }
}

void test_snvt_temp_published_scaling(void)
{
    static const uint8_t MIN[2] = {0x00, 0x00};
    static const uint8_t ZERO_C[2] = {0x0A, 0xB4};
    static const uint8_t MAX[2] = {0xFF, 0xFF};

    TEST_ASSERT_EQUAL_DOUBLE(-274.0, protocore_lon_snvt_temp_decode(MIN));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, protocore_lon_snvt_temp_decode(ZERO_C));
    TEST_ASSERT_EQUAL_DOUBLE(6279.5, protocore_lon_snvt_temp_decode(MAX));

    uint8_t enc[2];
    protocore_lon_snvt_temp_encode(-274.0, enc);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MIN, enc, 2);
    protocore_lon_snvt_temp_encode(0.0, enc);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ZERO_C, enc, 2);
    protocore_lon_snvt_temp_encode(6279.5, enc);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MAX, enc, 2);

    protocore_lon_snvt_temp_encode(20.0, enc);
    TEST_ASSERT_EQUAL_HEX8(0x0Bu, enc[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7Cu, enc[1]);
    protocore_lon_snvt_temp_encode(20.1, enc);
    TEST_ASSERT_EQUAL_HEX8(0x7Du, enc[1]);
}

void test_snvt_temp_saturates_at_the_published_bounds(void)
{
    uint8_t enc[2];
    protocore_lon_snvt_temp_encode(-1000.0, enc);
    TEST_ASSERT_EQUAL_HEX8(0x00u, enc[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, enc[1]);
    TEST_ASSERT_EQUAL_DOUBLE(-274.0, protocore_lon_snvt_temp_decode(enc));

    protocore_lon_snvt_temp_encode(1e9, enc);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, enc[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, enc[1]);
    TEST_ASSERT_EQUAL_DOUBLE(6279.5, protocore_lon_snvt_temp_decode(enc));

    for (uint32_t raw = 0; raw <= 65535u; raw += 271u)
    {
        uint8_t in[2] = {(uint8_t)(raw >> 8), (uint8_t)raw};
        uint8_t back[2];
        protocore_lon_snvt_temp_encode(protocore_lon_snvt_temp_decode(in), back);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(in, back, 2);
    }
}

void test_snvt_switch_published_states(void)
{
    uint8_t enc[2];

    protocore_lon_snvt_switch_encode(0.0, 0, enc);
    TEST_ASSERT_EQUAL_HEX8(0x00u, enc[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, enc[1]);

    protocore_lon_snvt_switch_encode(100.0, 1, enc);
    TEST_ASSERT_EQUAL_HEX8(0xC8u, enc[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, enc[1]);

    protocore_lon_snvt_switch_encode(50.0, 1, enc);
    TEST_ASSERT_EQUAL_HEX8(0x64u, enc[0]);
    protocore_lon_snvt_switch_encode(0.5, 1, enc);
    TEST_ASSERT_EQUAL_HEX8(0x01u, enc[0]);

    double pct = -1.0;
    uint8_t state = 0xAA;
    protocore_lon_snvt_switch_decode(enc, &pct, &state);
    TEST_ASSERT_EQUAL_DOUBLE(0.5, pct);
    TEST_ASSERT_EQUAL_HEX8(0x01u, state);

    protocore_lon_snvt_switch_encode(100.0, 0xFF, enc);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, enc[1]);
    protocore_lon_snvt_switch_decode(enc, NULL, &state);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, state);
    protocore_lon_snvt_switch_decode(enc, &pct, NULL);
    TEST_ASSERT_EQUAL_DOUBLE(100.0, pct);
}

void test_snvt_switch_clamps_to_the_published_range(void)
{
    uint8_t enc[2];
    protocore_lon_snvt_switch_encode(-5.0, 1, enc);
    TEST_ASSERT_EQUAL_HEX8(0x00u, enc[0]);
    protocore_lon_snvt_switch_encode(200.0, 1, enc);
    TEST_ASSERT_EQUAL_HEX8(0xC8u, enc[0]);
    protocore_lon_snvt_switch_encode(100.5, 1, enc);
    TEST_ASSERT_EQUAL_HEX8(0xC8u, enc[0]);

    for (unsigned raw = 0; raw <= 200u; raw++)
    {
        uint8_t in[2] = {(uint8_t)raw, 1};
        uint8_t back[2];
        double pct = 0;
        protocore_lon_snvt_switch_decode(in, &pct, NULL);
        protocore_lon_snvt_switch_encode(pct, 1, back);
        TEST_ASSERT_EQUAL_HEX8(in[0], back[0]);
    }
}

void test_guards(void)
{
    uint8_t out[8];
    static const uint8_t VALUE[3] = {1, 2, 3};

    TEST_ASSERT_EQUAL_size_t(0u, protocore_lon_build_nv(LON_MSG_NV_UPDATE, 1, VALUE, 3, out, 5));
    TEST_ASSERT_EQUAL_size_t(6u, protocore_lon_build_nv(LON_MSG_NV_UPDATE, 1, VALUE, 3, out, 6));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_lon_build_nv(LON_MSG_NV_UPDATE, 1, VALUE, 3, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_lon_build_nv(LON_MSG_NV_UPDATE, 1, NULL, 3, out, sizeof(out)));

    LonNv nv;
    TEST_ASSERT_FALSE(protocore_lon_parse_nv(out, 2, &nv));
    TEST_ASSERT_TRUE(protocore_lon_parse_nv(out, 3, &nv));
    TEST_ASSERT_FALSE(protocore_lon_parse_nv(NULL, 3, &nv));
    TEST_ASSERT_FALSE(protocore_lon_parse_nv(out, 3, NULL));
}
