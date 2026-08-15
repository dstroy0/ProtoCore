// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IEC 60870-5-101 / -104 telecontrol codec (services/energy/iec60870/iec60870.h).
//
// IEC 60870-5-104 is not freely distributable, so the -104 APCI expectations here follow the widely
// published control-field layout: an I-format octet 1 carries the low 7 bits of a 15-bit send
// sequence number in bits 8-2 with bit 1 fixed at 0, octet 2 the high 8 bits, and octets 3-4 do the
// same for the receive sequence number; the U-format commands are the one-hot bit pairs whose values
// (STARTDT act 0x07, con 0x0B, STOPDT act 0x13, con 0x23, TESTFR act 0x43, con 0x83) the module's own
// constants already name. Everything else is arithmetic derived here: the FT1.2 checksum is the 8-bit
// sum of the fields it covers, and the information objects are little-endian two's complement.
//
// test_rfc_free_sequence_numbers_span_fifteen_bits is the load-bearing case: the shifted-by-one
// layout is the single detail an implementation gets wrong, and a controlling station that reads
// N(S) one bit off acknowledges the wrong telegram and silently drops or duplicates measurements.

#include "services/energy/iec60870/iec60870.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The FT1.2 check octet, by its definition: the 8-bit sum of the octets it covers.
static uint8_t sum8(const uint8_t *p, size_t n)
{
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++)
    {
        s = (uint8_t)(s + p[i]);
    }
    return s;
}

// --- IEC 60870-5-104 APCI -------------------------------------------------------------------------

// The 6-octet APCI: start 0x68, an APDU length counting the 4 control octets plus the ASDU, then the
// control field. An I-format's octet 1 has bit 1 clear, which is what marks it as I-format.
void test_iec104_i_format_field_layout(void)
{
    static const uint8_t ASDU[3] = {0x01, 0x02, 0x03};
    uint8_t buf[64];
    size_t n = protocore_iec104_build_i(buf, sizeof(buf), 1u, 2u, ASDU, sizeof(ASDU));

    TEST_ASSERT_EQUAL_UINT(IEC104_APCI_LEN + sizeof(ASDU), n);
    TEST_ASSERT_EQUAL_HEX8(IEC_START_104, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(4u + 3u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, buf[2]); // N(S) 1 shifted left one, bit 1 clear
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x04u, buf[4]); // N(R) 2 shifted left one
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[5]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ASDU, buf + 6, sizeof(ASDU));
}

// Both sequence numbers are 15 bits: 0 .. 32767. Octet 1 holds the low 7 in its top 7 bits, octet 2
// the high 8, so 0x1234 becomes octet1 = (0x1234 << 1) & 0xFE = 0x68 and octet2 = 0x1234 >> 7 = 0x24.
void test_iec104_sequence_numbers_span_fifteen_bits(void)
{
    static const uint16_t SEQS[] = {0u, 1u, 127u, 128u, 0x1234u, 32766u, 32767u};
    for (size_t i = 0; i < sizeof(SEQS) / sizeof(SEQS[0]); i++)
    {
        uint8_t buf[16];
        Iec104Apci a;
        size_t consumed = 0;
        uint16_t s = SEQS[i];
        size_t n = protocore_iec104_build_i(buf, sizeof(buf), s, (uint16_t)(32767u - s), NULL, 0);

        TEST_ASSERT_EQUAL_HEX8((uint8_t)((s << 1) & 0xFEu), buf[2]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(s >> 7), buf[3]);
        TEST_ASSERT_EQUAL_UINT(6u, n);
        TEST_ASSERT_TRUE(protocore_iec104_parse(buf, n, &a, &consumed));
        TEST_ASSERT_EQUAL_INT(IEC104_I, a.format);
        TEST_ASSERT_EQUAL_UINT16(s, a.ns);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(32767u - s), a.nr);
        TEST_ASSERT_EQUAL_UINT(6u, consumed);
        TEST_ASSERT_NULL(a.asdu);
    }
    // The specific octet pair spelled out above.
    uint8_t buf[16];
    TEST_ASSERT_EQUAL_UINT(6u, protocore_iec104_build_i(buf, sizeof(buf), 0x1234u, 0u, NULL, 0));
    TEST_ASSERT_EQUAL_HEX8(0x68u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x24u, buf[3]);
}

// The supervisory format acknowledges only: octet 1 is 0x01 (bit 1 set, bit 2 clear) and carries no
// send sequence number at all.
void test_iec104_s_format(void)
{
    uint8_t buf[16];
    Iec104Apci a;
    TEST_ASSERT_EQUAL_UINT(IEC104_APCI_LEN, protocore_iec104_build_s(buf, sizeof(buf), 9u));
    TEST_ASSERT_EQUAL_HEX8(IEC_START_104, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(4u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x12u, buf[4]); // N(R) 9 shifted left one
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[5]);

    TEST_ASSERT_TRUE(protocore_iec104_parse(buf, IEC104_APCI_LEN, &a, NULL));
    TEST_ASSERT_EQUAL_INT(IEC104_S, a.format);
    TEST_ASSERT_EQUAL_UINT16(9u, a.nr);
    TEST_ASSERT_NULL(a.asdu);
}

// Every U-format command has bits 1-2 set, which is what separates it from I and S, and exactly one
// of the six higher bit pairs.
void test_iec104_u_format_commands(void)
{
    static const uint8_t CMDS[] = {IEC104_STARTDT_ACT, IEC104_STARTDT_CON, IEC104_STOPDT_ACT,
                                   IEC104_STOPDT_CON,  IEC104_TESTFR_ACT,  IEC104_TESTFR_CON};
    for (size_t i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++)
    {
        uint8_t buf[16];
        Iec104Apci a;
        TEST_ASSERT_EQUAL_HEX8(0x03u, (uint8_t)(CMDS[i] & 0x03u)); // the U-format marker bits
        TEST_ASSERT_EQUAL_UINT(IEC104_APCI_LEN, protocore_iec104_build_u(buf, sizeof(buf), CMDS[i]));
        TEST_ASSERT_EQUAL_HEX8(4u, buf[1]);
        TEST_ASSERT_EQUAL_HEX8(CMDS[i], buf[2]);
        TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]);
        TEST_ASSERT_EQUAL_HEX8(0x00u, buf[4]);
        TEST_ASSERT_EQUAL_HEX8(0x00u, buf[5]);

        TEST_ASSERT_TRUE(protocore_iec104_parse(buf, IEC104_APCI_LEN, &a, NULL));
        TEST_ASSERT_EQUAL_INT(IEC104_U, a.format);
        TEST_ASSERT_EQUAL_HEX8(CMDS[i], a.u_cmd);
    }
}

// The parse refuses anything that is not a complete APDU: a wrong start octet, an APDU length below
// the four control octets, or a frame whose declared length is not yet buffered.
void test_iec104_parse_rejects_malformed_apdus(void)
{
    static const uint8_t ASDU[3] = {1, 2, 3};
    uint8_t buf[32];
    Iec104Apci a;
    size_t n = protocore_iec104_build_i(buf, sizeof(buf), 1u, 1u, ASDU, sizeof(ASDU));

    TEST_ASSERT_TRUE(protocore_iec104_parse(buf, n, &a, NULL));
    TEST_ASSERT_EQUAL_UINT(sizeof(ASDU), a.asdu_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ASDU, a.asdu, sizeof(ASDU));

    TEST_ASSERT_FALSE(protocore_iec104_parse(buf, n - 1u, &a, NULL));
    TEST_ASSERT_FALSE(protocore_iec104_parse(buf, 1u, &a, NULL));
    TEST_ASSERT_FALSE(protocore_iec104_parse(NULL, n, &a, NULL));
    TEST_ASSERT_FALSE(protocore_iec104_parse(buf, n, NULL, NULL));

    uint8_t saved = buf[0];
    buf[0] = 0x69u;
    TEST_ASSERT_FALSE(protocore_iec104_parse(buf, n, &a, NULL));
    buf[0] = saved;
    buf[1] = 3u; // an APDU length that cannot even cover the control field
    TEST_ASSERT_FALSE(protocore_iec104_parse(buf, n, &a, NULL));
}

// The APDU length octet caps the ASDU at 249 (253 minus the 4 control octets), and a buffer that
// cannot hold the whole APDU writes nothing.
void test_iec104_build_refuses_oversized_or_unbuffered_apdus(void)
{
    uint8_t big[250];
    uint8_t buf[300];
    memset(big, 0x5A, sizeof(big));

    TEST_ASSERT_EQUAL_UINT(255u, protocore_iec104_build_i(buf, sizeof(buf), 0u, 0u, big, 249u));
    TEST_ASSERT_EQUAL_HEX8(253u, buf[1]);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec104_build_i(buf, sizeof(buf), 0u, 0u, big, 250u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec104_build_i(buf, 8u, 0u, 0u, big, 4u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec104_build_i(NULL, sizeof(buf), 0u, 0u, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec104_build_i(buf, sizeof(buf), 0u, 0u, NULL, 4u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec104_build_s(buf, IEC104_APCI_LEN - 1u, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec104_build_u(buf, IEC104_APCI_LEN - 1u, IEC104_STARTDT_ACT));
}

// --- the ASDU header and the Information Object Address -------------------------------------------

// Six octets: type id, the variable structure qualifier (SQ in bit 8, the element count in bits 7-1),
// the cause of transmission (T in bit 8, P/N in bit 7, the cause in bits 6-1), the originator
// address, and the two-octet common address, little-endian.
void test_asdu_header_field_layout(void)
{
    IecAsduHeader h;
    IecAsduHeader got;
    uint8_t buf[16];
    size_t consumed = 0;

    h.type_id = IEC_TYPE_M_ME_NC_1;
    h.sq = PROTO_TRUE;
    h.count = 3u;
    h.test = PROTO_FALSE;
    h.negative = PROTO_TRUE;
    h.cot = IEC_COT_ACT_CON;
    h.orig_addr = 0x11u;
    h.common_addr = 0x1234u;

    TEST_ASSERT_EQUAL_UINT(6u, protocore_iec_asdu_build_header(buf, sizeof(buf), &h));
    TEST_ASSERT_EQUAL_HEX8(13u, buf[0]);   // M_ME_NC_1
    TEST_ASSERT_EQUAL_HEX8(0x83u, buf[1]); // SQ set | count 3
    TEST_ASSERT_EQUAL_HEX8(0x47u, buf[2]); // P/N set | cause 7
    TEST_ASSERT_EQUAL_HEX8(0x11u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x34u, buf[4]); // common address, little-endian
    TEST_ASSERT_EQUAL_HEX8(0x12u, buf[5]);

    TEST_ASSERT_TRUE(protocore_iec_asdu_parse_header(buf, 6u, &got, &consumed));
    TEST_ASSERT_EQUAL_UINT(6u, consumed);
    TEST_ASSERT_EQUAL_UINT8(h.type_id, got.type_id);
    TEST_ASSERT_TRUE(got.sq);
    TEST_ASSERT_EQUAL_UINT8(3u, got.count);
    TEST_ASSERT_FALSE(got.test);
    TEST_ASSERT_TRUE(got.negative);
    TEST_ASSERT_EQUAL_UINT8(IEC_COT_ACT_CON, got.cot);
    TEST_ASSERT_EQUAL_UINT8(0x11u, got.orig_addr);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, got.common_addr);

    // The test bit is the other flag sharing that octet, and the count field is 7 bits wide.
    h.test = PROTO_TRUE;
    h.negative = PROTO_FALSE;
    h.sq = PROTO_FALSE;
    h.count = 127u;
    h.cot = IEC_COT_SPONTANEOUS;
    TEST_ASSERT_EQUAL_UINT(6u, protocore_iec_asdu_build_header(buf, sizeof(buf), &h));
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x83u, buf[2]);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_asdu_build_header(buf, 5u, &h));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_asdu_build_header(NULL, sizeof(buf), &h));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_asdu_build_header(buf, sizeof(buf), NULL));
    TEST_ASSERT_FALSE(protocore_iec_asdu_parse_header(buf, 5u, &got, NULL));
    TEST_ASSERT_FALSE(protocore_iec_asdu_parse_header(NULL, 6u, &got, NULL));
}

// The Information Object Address is three octets, little-endian, so it spans 0 .. 16777215.
void test_information_object_address_is_three_octets_little_endian(void)
{
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_UINT(3u, protocore_iec_put_ioa(buf, sizeof(buf), 0x123456u));
    TEST_ASSERT_EQUAL_HEX8(0x56u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12u, buf[2]);
    TEST_ASSERT_EQUAL_HEX32(0x123456u, protocore_iec_get_ioa(buf));

    TEST_ASSERT_EQUAL_UINT(3u, protocore_iec_put_ioa(buf, sizeof(buf), 0xFFFFFFu));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, protocore_iec_get_ioa(buf));
    // The field is three octets wide, so anything above it is dropped rather than overrunning.
    TEST_ASSERT_EQUAL_UINT(3u, protocore_iec_put_ioa(buf, sizeof(buf), 0xAB123456u));
    TEST_ASSERT_EQUAL_HEX32(0x123456u, protocore_iec_get_ioa(buf));

    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_put_ioa(buf, 2u, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_put_ioa(NULL, sizeof(buf), 1u));
}

// --- typed information objects --------------------------------------------------------------------

// M_SP_NA_1: IOA(3) + SIQ(1), the single-point value in bit 1 and the quality flags in bits 5-8.
void test_single_point_object(void)
{
    uint8_t buf[8];
    uint32_t ioa = 0;
    proto_bool on = PROTO_FALSE;
    uint8_t q = 0;

    TEST_ASSERT_EQUAL_UINT(4u,
                           protocore_iec_io_build_sp(buf, sizeof(buf), 100u, PROTO_TRUE, IEC_QUAL_NT | IEC_QUAL_IV));
    TEST_ASSERT_EQUAL_HEX8(0x64u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xC1u, buf[3]); // NT | IV | SPI
    TEST_ASSERT_TRUE(protocore_iec_io_parse_sp(buf, 4u, &ioa, &on, &q));
    TEST_ASSERT_EQUAL_UINT32(100u, ioa);
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_EQUAL_HEX8(IEC_QUAL_NT | IEC_QUAL_IV, q);

    TEST_ASSERT_EQUAL_UINT(4u, protocore_iec_io_build_sp(buf, sizeof(buf), 1u, PROTO_FALSE, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]);
    TEST_ASSERT_TRUE(protocore_iec_io_parse_sp(buf, 4u, NULL, &on, NULL));
    TEST_ASSERT_FALSE(on);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_io_build_sp(buf, 3u, 1u, PROTO_TRUE, 0u));
    TEST_ASSERT_FALSE(protocore_iec_io_parse_sp(buf, 3u, &ioa, &on, &q));
}

// M_DP_NA_1: IOA(3) + DIQ(1), the double-point value in bits 1-2 and the quality in bits 5-8. The
// two indeterminate encodings are distinct wire values and must stay distinct.
void test_double_point_object(void)
{
    static const uint8_t DPI[] = {IEC_DP_INDETERMINATE, IEC_DP_OFF, IEC_DP_ON, IEC_DP_INDETERMINATE_3};
    for (size_t i = 0; i < sizeof(DPI) / sizeof(DPI[0]); i++)
    {
        uint8_t buf[8];
        uint32_t ioa = 0;
        uint8_t got = 0xFF;
        uint8_t q = 0;
        TEST_ASSERT_EQUAL_UINT(4u, protocore_iec_io_build_dp(buf, sizeof(buf), 7u, DPI[i], IEC_QUAL_BL));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(IEC_QUAL_BL | DPI[i]), buf[3]);
        TEST_ASSERT_TRUE(protocore_iec_io_parse_dp(buf, 4u, &ioa, &got, &q));
        TEST_ASSERT_EQUAL_UINT32(7u, ioa);
        TEST_ASSERT_EQUAL_UINT8(DPI[i], got);
        TEST_ASSERT_EQUAL_HEX8(IEC_QUAL_BL, q);
    }

    uint8_t buf[8];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_io_build_dp(buf, 3u, 1u, IEC_DP_ON, 0u));
    TEST_ASSERT_FALSE(protocore_iec_io_parse_dp(buf, 3u, NULL, NULL, NULL));
}

// M_ME_NC_1: IOA(3) + an IEEE 754 binary32 value, little-endian + QDS(1). 1.0 is sign 0, biased
// exponent 127 (0x7F), zero significand: 0 01111111 0000... = 0x3F800000.
void test_short_float_measured_value(void)
{
    uint8_t buf[16];
    uint32_t ioa = 0;
    float v = 0.0f;
    uint8_t qds = 0;

    TEST_ASSERT_EQUAL_UINT(8u, protocore_iec_io_build_float(buf, sizeof(buf), 0x010203u, 1.0f, IEC_QUAL_OV));
    TEST_ASSERT_EQUAL_HEX8(0x03u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x80u, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(IEC_QUAL_OV, buf[7]);

    TEST_ASSERT_TRUE(protocore_iec_io_parse_float(buf, 8u, &ioa, &v, &qds));
    TEST_ASSERT_EQUAL_UINT32(0x010203u, ioa);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, v);
    TEST_ASSERT_EQUAL_HEX8(IEC_QUAL_OV, qds);

    // -2.0 is sign 1, biased exponent 128 (0x80), zero significand: 1 10000000 0000... = 0xC0000000.
    TEST_ASSERT_EQUAL_UINT(8u, protocore_iec_io_build_float(buf, sizeof(buf), 0u, -2.0f, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0xC0u, buf[6]);
    TEST_ASSERT_TRUE(protocore_iec_io_parse_float(buf, 8u, NULL, &v, NULL));
    TEST_ASSERT_EQUAL_FLOAT(-2.0f, v);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_io_build_float(buf, 7u, 0u, 1.0f, 0u));
    TEST_ASSERT_FALSE(protocore_iec_io_parse_float(buf, 7u, &ioa, &v, &qds));
}

// M_ME_NB_1: IOA(3) + SVA(2, signed, little-endian) + QDS(1), across the signed 16-bit range.
void test_scaled_measured_value(void)
{
    struct
    {
        int16_t value;
        uint8_t lo;
        uint8_t hi;
    } static const CASES[] = {
        {0, 0x00, 0x00},   {1, 0x01, 0x00},     {-1, 0xFF, 0xFF},
        {256, 0x00, 0x01}, {32767, 0xFF, 0x7F}, {-32768, 0x00, 0x80},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t buf[8];
        uint32_t ioa = 0;
        int16_t v = 0;
        uint8_t qds = 0;
        TEST_ASSERT_EQUAL_UINT(6u, protocore_iec_io_build_scaled(buf, sizeof(buf), 5u, CASES[i].value, IEC_QUAL_SB));
        TEST_ASSERT_EQUAL_HEX8(CASES[i].lo, buf[3]);
        TEST_ASSERT_EQUAL_HEX8(CASES[i].hi, buf[4]);
        TEST_ASSERT_EQUAL_HEX8(IEC_QUAL_SB, buf[5]);
        TEST_ASSERT_TRUE(protocore_iec_io_parse_scaled(buf, 6u, &ioa, &v, &qds));
        TEST_ASSERT_EQUAL_UINT32(5u, ioa);
        TEST_ASSERT_EQUAL_INT16(CASES[i].value, v);
        TEST_ASSERT_EQUAL_HEX8(IEC_QUAL_SB, qds);
    }

    uint8_t buf[8];
    int16_t v = 0;
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_io_build_scaled(buf, 5u, 0u, 1, 0u));
    TEST_ASSERT_FALSE(protocore_iec_io_parse_scaled(buf, 5u, NULL, &v, NULL));
}

// M_ME_NA_1: the normalized fraction stored as value * 32768 in a signed 16-bit field, so +1 has no
// exact encoding and saturates at 32767 while -1 lands exactly on -32768.
void test_normalized_measured_value(void)
{
    uint8_t buf[8];
    float v = 0.0f;

    TEST_ASSERT_EQUAL_UINT(6u, protocore_iec_io_build_normalized(buf, sizeof(buf), 1u, 0.5f, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]); // 0.5 * 32768 = 16384 = 0x4000
    TEST_ASSERT_EQUAL_HEX8(0x40u, buf[4]);
    TEST_ASSERT_TRUE(protocore_iec_io_parse_normalized(buf, 6u, NULL, &v, NULL));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, v);

    TEST_ASSERT_EQUAL_UINT(6u, protocore_iec_io_build_normalized(buf, sizeof(buf), 1u, -1.0f, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]); // -1 * 32768 = -32768 = 0x8000
    TEST_ASSERT_EQUAL_HEX8(0x80u, buf[4]);
    TEST_ASSERT_TRUE(protocore_iec_io_parse_normalized(buf, 6u, NULL, &v, NULL));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, v);

    TEST_ASSERT_EQUAL_UINT(6u, protocore_iec_io_build_normalized(buf, sizeof(buf), 1u, 1.0f, 0u));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, buf[3]); // saturated to 32767 = 0x7FFF
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, buf[4]);
    TEST_ASSERT_TRUE(protocore_iec_io_parse_normalized(buf, 6u, NULL, &v, NULL));
    TEST_ASSERT_EQUAL_FLOAT(32767.0f / 32768.0f, v);

    // Values beyond the interval clamp to the field rather than wrapping through it.
    TEST_ASSERT_EQUAL_UINT(6u, protocore_iec_io_build_normalized(buf, sizeof(buf), 1u, -4.0f, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x80u, buf[4]);
    TEST_ASSERT_EQUAL_UINT(6u, protocore_iec_io_build_normalized(buf, sizeof(buf), 1u, 4.0f, 0u));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, buf[4]);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_io_build_normalized(buf, 5u, 0u, 0.0f, 0u));
    TEST_ASSERT_FALSE(protocore_iec_io_parse_normalized(buf, 5u, NULL, &v, NULL));
}

// M_IT_NA_1: IOA(3) + a signed 32-bit counter, little-endian, then the sequence-notation octet with
// the 5-bit sequence number plus the carry / adjusted / invalid flags.
void test_integrated_totals_counter(void)
{
    uint8_t buf[16];
    uint32_t ioa = 0;
    int32_t v = 0;
    uint8_t seq = 0;
    uint8_t notation = (uint8_t)(IEC_BCR_CY | IEC_BCR_IV | 5u);

    TEST_ASSERT_EQUAL_UINT(8u, protocore_iec_io_build_counter(buf, sizeof(buf), 9u, 1000, notation));
    TEST_ASSERT_EQUAL_HEX8(0xE8u, buf[3]); // 1000 = 0x000003E8
    TEST_ASSERT_EQUAL_HEX8(0x03u, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, buf[7]); // CY | IV | sequence 5

    TEST_ASSERT_TRUE(protocore_iec_io_parse_counter(buf, 8u, &ioa, &v, &seq));
    TEST_ASSERT_EQUAL_UINT32(9u, ioa);
    TEST_ASSERT_EQUAL_INT32(1000, v);
    TEST_ASSERT_EQUAL_HEX8(notation, seq);
    TEST_ASSERT_EQUAL_UINT8(5u, (uint8_t)(seq & IEC_BCR_SQ_MASK));
    TEST_ASSERT_TRUE((seq & IEC_BCR_CY) != 0);
    TEST_ASSERT_TRUE((seq & IEC_BCR_IV) != 0);
    TEST_ASSERT_TRUE((seq & IEC_BCR_CA) == 0);

    TEST_ASSERT_EQUAL_UINT(8u, protocore_iec_io_build_counter(buf, sizeof(buf), 0u, -1, 0u));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, buf[3]); // two's complement -1
    TEST_ASSERT_EQUAL_HEX8(0xFFu, buf[6]);
    TEST_ASSERT_TRUE(protocore_iec_io_parse_counter(buf, 8u, NULL, &v, NULL));
    TEST_ASSERT_EQUAL_INT32(-1, v);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_io_build_counter(buf, 7u, 0u, 0, 0u));
    TEST_ASSERT_FALSE(protocore_iec_io_parse_counter(buf, 7u, NULL, &v, NULL));
}

// C_SC_NA_1: IOA(3) + SCO(1), the commanded state in bit 1 and the select/execute flag in bit 8. A
// select and an execute of the same state are different octets, which is what makes select-before-
// operate a real two-step exchange.
void test_single_command_object(void)
{
    uint8_t buf[8];
    uint32_t ioa = 0;
    proto_bool on = PROTO_FALSE;
    proto_bool sel = PROTO_FALSE;

    TEST_ASSERT_EQUAL_UINT(4u, protocore_iec_io_build_sc(buf, sizeof(buf), 3u, PROTO_TRUE, PROTO_TRUE));
    TEST_ASSERT_EQUAL_HEX8(0x81u, buf[3]);
    TEST_ASSERT_TRUE(protocore_iec_io_parse_sc(buf, 4u, &ioa, &on, &sel));
    TEST_ASSERT_EQUAL_UINT32(3u, ioa);
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_TRUE(sel);

    TEST_ASSERT_EQUAL_UINT(4u, protocore_iec_io_build_sc(buf, sizeof(buf), 3u, PROTO_TRUE, PROTO_FALSE));
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[3]);
    TEST_ASSERT_TRUE(protocore_iec_io_parse_sc(buf, 4u, NULL, &on, &sel));
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_FALSE(sel);

    TEST_ASSERT_EQUAL_UINT(4u, protocore_iec_io_build_sc(buf, sizeof(buf), 3u, PROTO_FALSE, PROTO_FALSE));
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_io_build_sc(buf, 3u, 0u, PROTO_TRUE, PROTO_FALSE));
    TEST_ASSERT_FALSE(protocore_iec_io_parse_sc(buf, 3u, NULL, &on, &sel));
}

// C_DC_NA_1: IOA(3) + DCO(1), the 2-bit command state, the 5-bit qualifier of command in bits 3-7,
// and the select/execute flag in bit 8.
void test_double_command_object(void)
{
    uint8_t buf[8];
    uint32_t ioa = 0;
    uint8_t dcs = 0;
    uint8_t qu = 0;
    proto_bool sel = PROTO_FALSE;

    TEST_ASSERT_EQUAL_UINT(4u, protocore_iec_io_build_dc(buf, sizeof(buf), 11u, IEC_DP_ON, 3u, PROTO_TRUE));
    TEST_ASSERT_EQUAL_HEX8(0x8Eu, buf[3]); // S/E | (3 << 2) | 2
    TEST_ASSERT_TRUE(protocore_iec_io_parse_dc(buf, 4u, &ioa, &dcs, &qu, &sel));
    TEST_ASSERT_EQUAL_UINT32(11u, ioa);
    TEST_ASSERT_EQUAL_UINT8(IEC_DP_ON, dcs);
    TEST_ASSERT_EQUAL_UINT8(3u, qu);
    TEST_ASSERT_TRUE(sel);

    // The qualifier is five bits, so its widest value fills bits 3-7 without reaching S/E.
    TEST_ASSERT_EQUAL_UINT(4u, protocore_iec_io_build_dc(buf, sizeof(buf), 0u, IEC_DP_OFF, 31u, PROTO_FALSE));
    TEST_ASSERT_EQUAL_HEX8(0x7Du, buf[3]);
    TEST_ASSERT_TRUE(protocore_iec_io_parse_dc(buf, 4u, NULL, &dcs, &qu, &sel));
    TEST_ASSERT_EQUAL_UINT8(IEC_DP_OFF, dcs);
    TEST_ASSERT_EQUAL_UINT8(31u, qu);
    TEST_ASSERT_FALSE(sel);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec_io_build_dc(buf, 3u, 0u, IEC_DP_ON, 0u, PROTO_FALSE));
    TEST_ASSERT_FALSE(protocore_iec_io_parse_dc(buf, 3u, NULL, &dcs, &qu, &sel));
}

// --- IEC 60870-5-101 FT1.2 link frames ------------------------------------------------------------

// The fixed-length frame is 10 C A CS 16, with the check octet the 8-bit sum of the control and
// address octets.
void test_ft12_fixed_length_frame(void)
{
    uint8_t buf[16];
    Iec101Frame f;
    size_t consumed = 0;

    TEST_ASSERT_EQUAL_UINT(5u, protocore_iec101_build_fixed(buf, sizeof(buf), 0x49u, 0x01u));
    TEST_ASSERT_EQUAL_HEX8(IEC_START_FIXED, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x49u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x4Au, buf[3]); // 0x49 + 0x01
    TEST_ASSERT_EQUAL_HEX8(IEC_STOP, buf[4]);

    TEST_ASSERT_TRUE(protocore_iec101_parse(buf, 5u, &f, &consumed));
    TEST_ASSERT_TRUE(f.fixed);
    TEST_ASSERT_EQUAL_HEX8(0x49u, f.control);
    TEST_ASSERT_EQUAL_HEX8(0x01u, f.addr);
    TEST_ASSERT_NULL(f.asdu);
    TEST_ASSERT_EQUAL_UINT(5u, consumed);

    // The sum wraps at 256, which is the only way the check octet can be reached from large fields.
    TEST_ASSERT_EQUAL_UINT(5u, protocore_iec101_build_fixed(buf, sizeof(buf), 0xFFu, 0x02u));
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[3]);
    TEST_ASSERT_TRUE(protocore_iec101_parse(buf, 5u, &f, NULL));

    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec101_build_fixed(buf, 4u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec101_build_fixed(NULL, sizeof(buf), 0u, 0u));
}

// The variable-length frame is 68 L L 68 C A <ASDU> CS 16, L counting the control, address and ASDU
// octets, and the check octet the 8-bit sum of exactly those L octets.
void test_ft12_variable_length_frame(void)
{
    static const uint8_t ASDU[6] = {0x0D, 0x01, 0x03, 0x00, 0x01, 0x00};
    uint8_t buf[32];
    Iec101Frame f;
    size_t consumed = 0;

    size_t n = protocore_iec101_build_variable(buf, sizeof(buf), 0x73u, 0x01u, ASDU, sizeof(ASDU));
    TEST_ASSERT_EQUAL_UINT(6u + 2u + sizeof(ASDU), n);
    TEST_ASSERT_EQUAL_HEX8(IEC_START_104, buf[0]); // the variable frame shares the 0x68 start octet
    TEST_ASSERT_EQUAL_HEX8(8u, buf[1]);            // L = 2 + 6
    TEST_ASSERT_EQUAL_HEX8(8u, buf[2]);            // repeated, so a corrupted length is detectable
    TEST_ASSERT_EQUAL_HEX8(IEC_START_104, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x73u, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[5]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ASDU, buf + 6, sizeof(ASDU));
    TEST_ASSERT_EQUAL_HEX8(sum8(buf + 4, 8u), buf[12]);
    TEST_ASSERT_EQUAL_HEX8(IEC_STOP, buf[13]);

    TEST_ASSERT_TRUE(protocore_iec101_parse(buf, n, &f, &consumed));
    TEST_ASSERT_FALSE(f.fixed);
    TEST_ASSERT_EQUAL_HEX8(0x73u, f.control);
    TEST_ASSERT_EQUAL_HEX8(0x01u, f.addr);
    TEST_ASSERT_EQUAL_UINT8(sizeof(ASDU), f.asdu_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ASDU, f.asdu, sizeof(ASDU));
    TEST_ASSERT_EQUAL_UINT(n, consumed);

    // An empty ASDU is still a legal variable frame: L is 2 and no ASDU slice is reported.
    n = protocore_iec101_build_variable(buf, sizeof(buf), 0x53u, 0x02u, NULL, 0u);
    TEST_ASSERT_EQUAL_UINT(8u, n);
    TEST_ASSERT_EQUAL_HEX8(2u, buf[1]);
    TEST_ASSERT_TRUE(protocore_iec101_parse(buf, n, &f, NULL));
    TEST_ASSERT_EQUAL_UINT8(0u, f.asdu_len);
    TEST_ASSERT_NULL(f.asdu);
}

// Each of the four things the frame carries to be checked - the repeated length, the start octet, the
// stop octet, and the checksum - rejects the frame on its own.
void test_ft12_parse_rejects_corrupted_frames(void)
{
    static const uint8_t ASDU[4] = {1, 2, 3, 4};
    uint8_t buf[32];
    Iec101Frame f;
    size_t n = protocore_iec101_build_variable(buf, sizeof(buf), 0x73u, 0x01u, ASDU, sizeof(ASDU));
    TEST_ASSERT_TRUE(protocore_iec101_parse(buf, n, &f, NULL));

    uint8_t saved = buf[2];
    buf[2] = 7u; // the repeated length no longer agrees
    TEST_ASSERT_FALSE(protocore_iec101_parse(buf, n, &f, NULL));
    buf[2] = saved;

    saved = buf[3];
    buf[3] = 0x69u;
    TEST_ASSERT_FALSE(protocore_iec101_parse(buf, n, &f, NULL));
    buf[3] = saved;

    saved = buf[n - 1u];
    buf[n - 1u] = 0x17u;
    TEST_ASSERT_FALSE(protocore_iec101_parse(buf, n, &f, NULL));
    buf[n - 1u] = saved;

    saved = buf[6];
    buf[6] = (uint8_t)(saved ^ 0x01u); // an ASDU octet: the checksum no longer matches
    TEST_ASSERT_FALSE(protocore_iec101_parse(buf, n, &f, NULL));
    buf[6] = saved;

    TEST_ASSERT_FALSE(protocore_iec101_parse(buf, n - 1u, &f, NULL)); // not fully buffered
    TEST_ASSERT_FALSE(protocore_iec101_parse(buf, 0u, &f, NULL));
    TEST_ASSERT_FALSE(protocore_iec101_parse(NULL, n, &f, NULL));
    TEST_ASSERT_FALSE(protocore_iec101_parse(buf, n, NULL, NULL));

    // A fixed-length frame with a wrong check octet or stop octet is refused the same way.
    TEST_ASSERT_EQUAL_UINT(5u, protocore_iec101_build_fixed(buf, sizeof(buf), 0x49u, 0x01u));
    buf[3] = 0x00u;
    TEST_ASSERT_FALSE(protocore_iec101_parse(buf, 5u, &f, NULL));
    buf[3] = 0x4Au;
    buf[4] = 0x00u;
    TEST_ASSERT_FALSE(protocore_iec101_parse(buf, 5u, &f, NULL));

    // A start octet that is neither 0x10 nor 0x68 is not an FT1.2 frame at all.
    buf[0] = 0x11u;
    TEST_ASSERT_FALSE(protocore_iec101_parse(buf, 5u, &f, NULL));
}

// L is a single octet counting the control and address octets too, so the ASDU cannot exceed 253.
void test_ft12_build_refuses_oversized_or_unbuffered_frames(void)
{
    uint8_t big[254];
    uint8_t buf[300];
    memset(big, 0x5A, sizeof(big));

    TEST_ASSERT_EQUAL_UINT(261u, protocore_iec101_build_variable(buf, sizeof(buf), 0u, 0u, big, 253u));
    TEST_ASSERT_EQUAL_HEX8(255u, buf[1]);
    TEST_ASSERT_EQUAL_UINT(12u, protocore_iec101_build_variable(buf, 12u, 0u, 0u, big, 4u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec101_build_variable(buf, 11u, 0u, 0u, big, 4u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec101_build_variable(buf, sizeof(buf), 0u, 0u, big, 254u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec101_build_variable(NULL, sizeof(buf), 0u, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_iec101_build_variable(buf, sizeof(buf), 0u, 0u, NULL, 4u));
}
