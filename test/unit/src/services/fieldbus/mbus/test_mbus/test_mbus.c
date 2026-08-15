// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the wired M-Bus frame + record codec (services/fieldbus/mbus/mbus.h).
//
// The load-bearing case is test_mbdoc_rsp_ud_example: the M-Bus Usergroup documentation rev 4.8
// (the published text of EN 13757-2/-3) prints one complete RSP_UD telegram with its variable data
// structure decoded field by field in chapter 6.3.3. Every octet of that telegram is fed in here
// and every published reading - id 12345678, manufacturer PAD, medium water, 12565 l, 113 l/h,
// 218.37 kWh - is asserted back out. A frame walker, a BCD decoder, a DIFE/VIFE skip and the VIF
// unit table all have to be right simultaneously for it to pass.

#include "services/fieldbus/mbus/mbus.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// MBDOC48 chapter 6.3.3, "Example for a RSP_UD with variable data structure answer (mode 1)".
static const uint8_t RSP_UD[] = {
    0x68, 0x1F, 0x1F, 0x68,             // start, L, L, start
    0x08, 0x02, 0x72,                   // C = RSP_UD, address 2, CI = 72h (variable data)
    0x78, 0x56, 0x34, 0x12,             // identification number 12345678
    0x24, 0x40, 0x01, 0x07,             // manufacturer 4024h (PAD), generation 1, water
    0x55, 0x00, 0x00, 0x00,             // access no 55h, status 00h, signature 0000h
    0x03, 0x13, 0x15, 0x31, 0x00,       // record 1: volume, 12565 l (24-bit integer)
    0xDA, 0x02, 0x3B, 0x13, 0x01,       // record 2: max volume flow, 113 l/h (4-digit BCD)
    0x8B, 0x60, 0x04, 0x37, 0x18, 0x02, // record 3: energy, 218.37 kWh (6-digit BCD)
    0x18, 0x16,                         // checksum, stop
};

// The whole published telegram: framing, fixed header, and all three records.
void test_mbdoc_rsp_ud_example(void)
{
    MbusFrame f;
    size_t used = 0;
    TEST_ASSERT_TRUE(protocore_mbus_parse(RSP_UD, sizeof(RSP_UD), &f, &used));
    TEST_ASSERT_EQUAL_UINT(sizeof(RSP_UD), used);
    TEST_ASSERT_EQUAL_INT(MBUS_FRAME_LONG, f.type);
    TEST_ASSERT_EQUAL_HEX8(0x08, f.c);
    TEST_ASSERT_EQUAL_HEX8(0x02, f.a);
    TEST_ASSERT_EQUAL_HEX8(MBUS_CI_RSP_VARIABLE, f.ci);
    TEST_ASSERT_EQUAL_UINT(0x1F - 3, f.data_len); // L counts C + A + CI + user data

    MbusVarHeader h;
    TEST_ASSERT_TRUE(protocore_mbus_parse_var_header(f.data, f.data_len, &h));
    TEST_ASSERT_EQUAL_UINT32(12345678u, h.id);
    TEST_ASSERT_EQUAL_HEX16(0x4024, h.manufacturer_raw);
    TEST_ASSERT_EQUAL_STRING("PAD", h.manufacturer);
    TEST_ASSERT_EQUAL_UINT8(1, h.version);
    TEST_ASSERT_EQUAL_UINT8(MBUS_MEDIUM_WATER, h.medium);
    TEST_ASSERT_EQUAL_UINT8(0x55, h.access_no);
    TEST_ASSERT_EQUAL_UINT8(0x00, h.status);
    TEST_ASSERT_EQUAL_UINT16(0x0000, h.signature);

    const uint8_t *body = f.data + MBUS_VAR_HEADER_LEN;
    size_t body_len = (size_t)f.data_len - MBUS_VAR_HEADER_LEN;
    size_t pos = 0;
    MbusRecord r;
    int64_t v = 0;
    MbusUnit unit = MBUS_UNIT_UNKNOWN;
    int8_t exp10 = 0;

    // Record 1: DIF 03 = 24-bit integer, VIF 13 = volume 10^(3-6) m3, i.e. litres.
    TEST_ASSERT_TRUE(protocore_mbus_record_next(body, body_len, &pos, &r));
    TEST_ASSERT_EQUAL_HEX8(0x03, r.dif);
    TEST_ASSERT_EQUAL_HEX8(MBUS_DIF_INT24, r.coding);
    TEST_ASSERT_EQUAL_HEX8(0x13, r.vif);
    TEST_ASSERT_EQUAL_UINT8(3, r.data_len);
    TEST_ASSERT_TRUE(protocore_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(12565, v);
    TEST_ASSERT_TRUE(protocore_mbus_vif_decode(r.vif, &unit, &exp10));
    TEST_ASSERT_EQUAL_INT(MBUS_UNIT_M3, unit);
    TEST_ASSERT_EQUAL_INT8(-3, exp10);

    // Record 2: DIF DA carries a DIFE (02); coding A = 4-digit BCD. VIF 3B = volume flow 10^-3 m3/h.
    TEST_ASSERT_TRUE(protocore_mbus_record_next(body, body_len, &pos, &r));
    TEST_ASSERT_EQUAL_HEX8(0xDA, r.dif);
    TEST_ASSERT_EQUAL_HEX8(MBUS_DIF_BCD4, r.coding);
    TEST_ASSERT_EQUAL_HEX8(0x3B, r.vif);
    TEST_ASSERT_EQUAL_UINT8(2, r.data_len);
    TEST_ASSERT_TRUE(protocore_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(113, v);
    TEST_ASSERT_TRUE(protocore_mbus_vif_decode(r.vif, &unit, &exp10));
    TEST_ASSERT_EQUAL_INT(MBUS_UNIT_M3_PER_H, unit);
    TEST_ASSERT_EQUAL_INT8(-3, exp10);

    // Record 3: DIF 8B carries a DIFE (60); coding B = 6-digit BCD. VIF 04 = energy 10^(4-3) Wh,
    // so the published 218.37 kWh is the raw 21837 scaled by 10^1 = 218370 Wh.
    TEST_ASSERT_TRUE(protocore_mbus_record_next(body, body_len, &pos, &r));
    TEST_ASSERT_EQUAL_HEX8(0x8B, r.dif);
    TEST_ASSERT_EQUAL_HEX8(MBUS_DIF_BCD6, r.coding);
    TEST_ASSERT_EQUAL_HEX8(0x04, r.vif);
    TEST_ASSERT_EQUAL_UINT8(3, r.data_len);
    TEST_ASSERT_TRUE(protocore_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(21837, v);
    TEST_ASSERT_TRUE(protocore_mbus_vif_decode(r.vif, &unit, &exp10));
    TEST_ASSERT_EQUAL_INT(MBUS_UNIT_WH, unit);
    TEST_ASSERT_EQUAL_INT8(1, exp10);

    TEST_ASSERT_EQUAL_UINT(body_len, pos); // three records exactly fill the body
    TEST_ASSERT_FALSE(protocore_mbus_record_next(body, body_len, &pos, &r));
}

// MBDOC48 fig. 13: the single character format is one octet, E5h (decimal 229).
void test_single_character_ack(void)
{
    uint8_t buf[4];
    TEST_ASSERT_EQUAL_UINT(1u, protocore_mbus_build_ack(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0xE5, buf[0]);

    MbusFrame f;
    size_t used = 0;
    TEST_ASSERT_TRUE(protocore_mbus_parse(buf, 1, &f, &used));
    TEST_ASSERT_EQUAL_INT(MBUS_FRAME_ACK, f.type);
    TEST_ASSERT_EQUAL_UINT(1u, used);
}

// MBDOC48 fig. 13 + table 1: SND_NKE is a short frame 10 C A CS 16 with C = 40h; the check sum is
// the arithmetic sum of C and A without carry, so 40h + 05h = 45h.
void test_snd_nke_short_frame(void)
{
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_UINT(5u, protocore_mbus_build_snd_nke(buf, sizeof(buf), 0x05));
    static const uint8_t WANT[5] = {0x10, 0x40, 0x05, 0x45, 0x16};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 5);

    MbusFrame f;
    TEST_ASSERT_TRUE(protocore_mbus_parse(buf, 5, &f, NULL));
    TEST_ASSERT_EQUAL_INT(MBUS_FRAME_SHORT, f.type);
    TEST_ASSERT_EQUAL_HEX8(MBUS_C_SND_NKE, f.c);
    TEST_ASSERT_EQUAL_HEX8(0x05, f.a);
}

// MBDOC48 table 1: REQ_UD2 is 5Bh with FCB = 0 and 7Bh with FCB = 1; REQ_UD1 is 5Ah / 7Ah. The FCB
// is bit 5 of the C field, so the two spellings differ by exactly 20h.
void test_req_ud_fcb_toggles_bit5(void)
{
    uint8_t a[8], b[8];
    TEST_ASSERT_EQUAL_UINT(5u, protocore_mbus_build_req_ud2(a, sizeof(a), 0x01, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT(5u, protocore_mbus_build_req_ud2(b, sizeof(b), 0x01, PROTO_TRUE));
    TEST_ASSERT_EQUAL_HEX8(0x5B, a[1]);
    TEST_ASSERT_EQUAL_HEX8(0x7B, b[1]);
    TEST_ASSERT_EQUAL_HEX8(0x20, (uint8_t)(a[1] ^ b[1]));

    TEST_ASSERT_EQUAL_UINT(5u, protocore_mbus_build_req_ud1(a, sizeof(a), 0x01, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT(5u, protocore_mbus_build_req_ud1(b, sizeof(b), 0x01, PROTO_TRUE));
    TEST_ASSERT_EQUAL_HEX8(0x5A, a[1]);
    TEST_ASSERT_EQUAL_HEX8(0x7A, b[1]);
}

// MBDOC48 5.2: the control frame is a long frame with no user data and an L field of 3, and its
// check sum covers C, A and CI. 53h + FDh + 52h = 1A2h, so the low octet is A2h.
void test_control_frame_is_a_long_frame_with_l_three(void)
{
    uint8_t buf[16];
    size_t n = protocore_mbus_build_long(buf, sizeof(buf), MBUS_C_SND_UD, 0xFD, MBUS_CI_SELECT, NULL, 0);
    TEST_ASSERT_EQUAL_UINT(9u, n); // 68 L L 68 C A CI CS 16
    static const uint8_t WANT[9] = {0x68, 0x03, 0x03, 0x68, 0x53, 0xFD, 0x52, 0xA2, 0x16};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 9);

    MbusFrame f;
    TEST_ASSERT_TRUE(protocore_mbus_parse(buf, n, &f, NULL));
    TEST_ASSERT_EQUAL_INT(MBUS_FRAME_LONG, f.type);
    TEST_ASSERT_EQUAL_UINT8(0, f.data_len);
    TEST_ASSERT_NULL(f.data);
}

// MBDOC48 5.2: the L field gives the user data count plus 3, and the built frame is 6 + L octets.
// Rebuilding the published telegram's own header from its own fields must reproduce it octet for
// octet, checksum included.
void test_build_long_reproduces_the_published_telegram(void)
{
    uint8_t buf[64];
    const uint8_t *user = RSP_UD + 7;
    uint8_t user_len = 0x1F - 3;
    size_t n = protocore_mbus_build_long(buf, sizeof(buf), 0x08, 0x02, MBUS_CI_RSP_VARIABLE, user, user_len);
    TEST_ASSERT_EQUAL_UINT(sizeof(RSP_UD), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RSP_UD, buf, sizeof(RSP_UD));
}

// A corrupted octet anywhere under the check sum changes the sum, so the frame is refused. Both the
// short and the long forms are covered, plus a broken stop octet and a mismatched doubled length.
void test_parse_refuses_a_damaged_frame(void)
{
    uint8_t f[sizeof(RSP_UD)];
    MbusFrame out;

    memcpy(f, RSP_UD, sizeof(RSP_UD));
    f[10] ^= 0x01; // an id octet, covered by the check sum
    TEST_ASSERT_FALSE(protocore_mbus_parse(f, sizeof(RSP_UD), &out, NULL));

    memcpy(f, RSP_UD, sizeof(RSP_UD));
    f[2] = 0x1E; // the doubled L field disagrees
    TEST_ASSERT_FALSE(protocore_mbus_parse(f, sizeof(RSP_UD), &out, NULL));

    memcpy(f, RSP_UD, sizeof(RSP_UD));
    f[sizeof(RSP_UD) - 1] = 0x17; // the stop octet is not 16h
    TEST_ASSERT_FALSE(protocore_mbus_parse(f, sizeof(RSP_UD), &out, NULL));

    static const uint8_t SHORT_BAD[5] = {0x10, 0x40, 0x05, 0x46, 0x16}; // check sum is 45h, not 46h
    TEST_ASSERT_FALSE(protocore_mbus_parse(SHORT_BAD, 5, &out, NULL));

    static const uint8_t UNKNOWN_START[5] = {0x11, 0x40, 0x05, 0x45, 0x16};
    TEST_ASSERT_FALSE(protocore_mbus_parse(UNKNOWN_START, 5, &out, NULL));
}

// A frame cut short of its own declared length is not a frame yet.
void test_parse_refuses_a_truncated_frame(void)
{
    MbusFrame out;
    for (size_t n = 1; n < sizeof(RSP_UD); n++)
    {
        TEST_ASSERT_FALSE(protocore_mbus_parse(RSP_UD, n, &out, NULL));
    }
}

// MBDOC48 8.4.2 "Data Field Codes": the length each DIF low nibble names, in octets. 0101b is a
// 32-bit real and 1101b is variable (an LVAR octet carries the length), so both report 0 here.
void test_dif_data_field_lengths(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, protocore_mbus_dif_data_len(MBUS_DIF_NONE));
    TEST_ASSERT_EQUAL_UINT8(1, protocore_mbus_dif_data_len(MBUS_DIF_INT8));
    TEST_ASSERT_EQUAL_UINT8(2, protocore_mbus_dif_data_len(MBUS_DIF_INT16));
    TEST_ASSERT_EQUAL_UINT8(3, protocore_mbus_dif_data_len(MBUS_DIF_INT24));
    TEST_ASSERT_EQUAL_UINT8(4, protocore_mbus_dif_data_len(MBUS_DIF_INT32));
    TEST_ASSERT_EQUAL_UINT8(4, protocore_mbus_dif_data_len(MBUS_DIF_REAL32));
    TEST_ASSERT_EQUAL_UINT8(6, protocore_mbus_dif_data_len(MBUS_DIF_INT48));
    TEST_ASSERT_EQUAL_UINT8(8, protocore_mbus_dif_data_len(MBUS_DIF_INT64));
    TEST_ASSERT_EQUAL_UINT8(0, protocore_mbus_dif_data_len(MBUS_DIF_READOUT));
    TEST_ASSERT_EQUAL_UINT8(1, protocore_mbus_dif_data_len(MBUS_DIF_BCD2));
    TEST_ASSERT_EQUAL_UINT8(2, protocore_mbus_dif_data_len(MBUS_DIF_BCD4));
    TEST_ASSERT_EQUAL_UINT8(3, protocore_mbus_dif_data_len(MBUS_DIF_BCD6));
    TEST_ASSERT_EQUAL_UINT8(4, protocore_mbus_dif_data_len(MBUS_DIF_BCD8));
    TEST_ASSERT_EQUAL_UINT8(0, protocore_mbus_dif_data_len(MBUS_DIF_VARIABLE));
    TEST_ASSERT_EQUAL_UINT8(6, protocore_mbus_dif_data_len(MBUS_DIF_BCD12));
    TEST_ASSERT_EQUAL_UINT8(0, protocore_mbus_dif_data_len(MBUS_DIF_SPECIAL));
    // only the low nibble selects the coding; the function / storage bits above it do not
    TEST_ASSERT_EQUAL_UINT8(2, protocore_mbus_dif_data_len(0x42));
}

// MBDOC48 8.4.3: each VIF range names a unit and an exponent formula. The boundary code of every
// range implemented is checked against the published formula, evaluated here.
void test_vif_unit_table(void)
{
    struct
    {
        uint8_t vif;
        MbusUnit unit;
        int8_t exp10;
    } static const CASES[] = {
        {0x00, MBUS_UNIT_WH, -3},                                      // E000 0nnn energy 10^(nnn-3) Wh, nnn = 0
        {0x07, MBUS_UNIT_WH, 4},                                       // nnn = 7
        {0x08, MBUS_UNIT_J, 0},                                        // E000 1nnn energy 10^(nnn) J
        {0x0F, MBUS_UNIT_J, 7},        {0x10, MBUS_UNIT_M3, -6},       // E001 0nnn volume 10^(nnn-6) m3
        {0x17, MBUS_UNIT_M3, 1},       {0x18, MBUS_UNIT_KG, -3},       // E001 1nnn mass 10^(nnn-3) kg
        {0x1F, MBUS_UNIT_KG, 4},       {0x28, MBUS_UNIT_W, -3},        // E010 1nnn power 10^(nnn-3) W
        {0x2F, MBUS_UNIT_W, 4},        {0x30, MBUS_UNIT_J_PER_H, 0},   // E011 0nnn power 10^(nnn) J/h
        {0x37, MBUS_UNIT_J_PER_H, 7},  {0x38, MBUS_UNIT_M3_PER_H, -6}, // E011 1nnn volume flow 10^(nnn-6) m3/h
        {0x3F, MBUS_UNIT_M3_PER_H, 1}, {0x58, MBUS_UNIT_CELSIUS, -3},  // E101 10nn flow temperature 10^(nn-3) C
        {0x5B, MBUS_UNIT_CELSIUS, 0},  {0x5C, MBUS_UNIT_CELSIUS, -3},  // E101 11nn return temperature, same scale
        {0x60, MBUS_UNIT_K, -3},                                       // E110 00nn temperature difference 10^(nn-3) K
        {0x63, MBUS_UNIT_K, 0},        {0x64, MBUS_UNIT_CELSIUS, -3},  // E110 01nn external temperature 10^(nn-3) C
        {0x68, MBUS_UNIT_BAR, -3},                                     // E110 10nn pressure 10^(nn-3) bar
        {0x6B, MBUS_UNIT_BAR, 0},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        MbusUnit u = MBUS_UNIT_UNKNOWN;
        int8_t e = 127;
        TEST_ASSERT_TRUE(protocore_mbus_vif_decode(CASES[i].vif, &u, &e));
        TEST_ASSERT_EQUAL_INT(CASES[i].unit, u);
        TEST_ASSERT_EQUAL_INT8(CASES[i].exp10, e);

        // the extension bit is bit 7 and carries no unit information, so setting it changes nothing
        MbusUnit u2 = MBUS_UNIT_UNKNOWN;
        int8_t e2 = 127;
        TEST_ASSERT_TRUE(protocore_mbus_vif_decode((uint8_t)(CASES[i].vif | 0x80u), &u2, &e2));
        TEST_ASSERT_EQUAL_INT(CASES[i].unit, u2);
        TEST_ASSERT_EQUAL_INT8(CASES[i].exp10, e2);
    }

    // 6Ch is a Time Point code, outside every measurement range this decoder covers
    MbusUnit u = MBUS_UNIT_WH;
    TEST_ASSERT_FALSE(protocore_mbus_vif_decode(0x6C, &u, NULL));
    TEST_ASSERT_EQUAL_INT(MBUS_UNIT_UNKNOWN, u);
}

// MBDOC48 6.7.4: a value with an F in the most significant BCD nibble is negative, the remaining
// digits carrying its magnitude. 12 34 56 as 6-digit BCD reads 563412 little-endian octet order.
void test_bcd_decoding_and_sign(void)
{
    static const uint8_t POS[3] = {0x12, 0x34, 0x56};
    MbusRecord r = {0x0B, MBUS_DIF_BCD6, 0x00, POS, 3};
    int64_t v = 0;
    TEST_ASSERT_TRUE(protocore_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(563412, v);

    static const uint8_t NEG[3] = {0x12, 0x34, 0xF6}; // F in the top nibble, magnitude 63412
    r.data = NEG;
    TEST_ASSERT_TRUE(protocore_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(-63412, v);

    static const uint8_t BAD[3] = {0x12, 0x3A, 0x56}; // A is not a decimal digit
    r.data = BAD;
    TEST_ASSERT_FALSE(protocore_mbus_record_value_int(&r, &v));
}

// MBDOC48 8.4.2: integer codings are little-endian and signed. -1 in every width is all-ones, and
// the most negative 16-bit value is 8000h.
void test_integer_decoding_is_little_endian_and_signed(void)
{
    static const uint8_t LE16[2] = {0x34, 0x12};
    MbusRecord r = {0x02, MBUS_DIF_INT16, 0x00, LE16, 2};
    int64_t v = 0;
    TEST_ASSERT_TRUE(protocore_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(0x1234, v);

    static const uint8_t ONES[2] = {0xFF, 0xFF};
    r.data = ONES;
    TEST_ASSERT_TRUE(protocore_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(-1, v);

    static const uint8_t MIN16[2] = {0x00, 0x80};
    r.data = MIN16;
    TEST_ASSERT_TRUE(protocore_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(-32768, v);

    static const uint8_t LE32[4] = {0x78, 0x56, 0x34, 0x12};
    MbusRecord r32 = {0x04, MBUS_DIF_INT32, 0x00, LE32, 4};
    TEST_ASSERT_TRUE(protocore_mbus_record_value_int(&r32, &v));
    TEST_ASSERT_EQUAL_INT64(0x12345678, v);
}

// A REAL32 record is IEEE-754 little-endian, and only that coding answers the real accessor.
// 1.0f is 3F800000 by the IEEE-754 single format's own definition (sign 0, biased exponent 127,
// zero mantissa), so its octets little-endian are 00 00 80 3F.
void test_real32_record(void)
{
    static const uint8_t ONE[4] = {0x00, 0x00, 0x80, 0x3F};
    MbusRecord r = {0x05, MBUS_DIF_REAL32, 0x00, ONE, 4};
    float f = 0.0f;
    TEST_ASSERT_TRUE(protocore_mbus_record_value_real(&r, &f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, f);

    int64_t v = 0;
    TEST_ASSERT_FALSE(protocore_mbus_record_value_int(&r, &v)); // a real is not an integer coding

    MbusRecord i = {0x02, MBUS_DIF_INT16, 0x00, ONE, 2};
    TEST_ASSERT_FALSE(protocore_mbus_record_value_real(&i, &f)); // and an integer is not a real
}

// MBDOC48 6.3.2: the LVAR octet before a variable-length record's data carries its length, and the
// walker must land on the record after it. 0Dh coding, LVAR 04, four octets of data.
void test_variable_length_record(void)
{
    static const uint8_t BODY[] = {
        0x0D, 0x7C, 0x04, 'A', 'B', 'C', 'D', // variable-length record, LVAR = 4
        0x01, 0x13, 0x2A,                     // an 8-bit integer record after it
    };
    size_t pos = 0;
    MbusRecord r;
    TEST_ASSERT_TRUE(protocore_mbus_record_next(BODY, sizeof(BODY), &pos, &r));
    TEST_ASSERT_EQUAL_HEX8(MBUS_DIF_VARIABLE, r.coding);
    TEST_ASSERT_EQUAL_UINT8(4, r.data_len);
    TEST_ASSERT_EQUAL_HEX8('A', r.data[0]);

    TEST_ASSERT_TRUE(protocore_mbus_record_next(BODY, sizeof(BODY), &pos, &r));
    TEST_ASSERT_EQUAL_HEX8(MBUS_DIF_INT8, r.coding);
    int64_t v = 0;
    TEST_ASSERT_TRUE(protocore_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(42, v);
    TEST_ASSERT_EQUAL_UINT(sizeof(BODY), pos);
}

// A record whose declared data runs past the end of the body is refused rather than read out of it.
void test_record_walk_refuses_an_overrun(void)
{
    static const uint8_t SHORT_BODY[] = {0x04, 0x13, 0x01, 0x02}; // 32-bit coding, only 2 octets left
    size_t pos = 0;
    MbusRecord r;
    TEST_ASSERT_FALSE(protocore_mbus_record_next(SHORT_BODY, sizeof(SHORT_BODY), &pos, &r));

    static const uint8_t DANGLING_DIFE[] = {0x83, 0x84}; // an extension chain that never terminates
    pos = 0;
    TEST_ASSERT_FALSE(protocore_mbus_record_next(DANGLING_DIFE, sizeof(DANGLING_DIFE), &pos, &r));

    static const uint8_t NO_VIF[] = {0x03}; // a DIF with no VIF after it
    pos = 0;
    TEST_ASSERT_FALSE(protocore_mbus_record_next(NO_VIF, sizeof(NO_VIF), &pos, &r));
}

// MBDOC48 6.3.1: the manufacturer field packs three uppercase letters at five bits each,
// letter = value + 64. "ABC" is therefore 1*1024 + 2*32 + 3 = 1091 = 0443h.
void test_manufacturer_code_packing(void)
{
    uint8_t body[MBUS_VAR_HEADER_LEN] = {0};
    body[4] = 0x43; // 0443h little-endian
    body[5] = 0x04;
    MbusVarHeader h;
    TEST_ASSERT_TRUE(protocore_mbus_parse_var_header(body, sizeof(body), &h));
    TEST_ASSERT_EQUAL_HEX16(0x0443, h.manufacturer_raw);
    TEST_ASSERT_EQUAL_STRING("ABC", h.manufacturer);
    TEST_ASSERT_EQUAL_UINT32(0u, h.id);
}

// The fixed header is exactly twelve octets, so anything shorter cannot be decoded, and a non-BCD
// nibble in the identification number is a decode failure rather than a wrong number.
void test_var_header_bounds_and_bcd_validation(void)
{
    MbusVarHeader h;
    uint8_t body[MBUS_VAR_HEADER_LEN] = {0x78, 0x56, 0x34, 0x12, 0x24, 0x40, 0x01, 0x07, 0x55, 0, 0, 0};
    TEST_ASSERT_TRUE(protocore_mbus_parse_var_header(body, MBUS_VAR_HEADER_LEN, &h));
    TEST_ASSERT_FALSE(protocore_mbus_parse_var_header(body, MBUS_VAR_HEADER_LEN - 1, &h));

    body[1] = 0x5A; // A is not a decimal digit
    TEST_ASSERT_FALSE(protocore_mbus_parse_var_header(body, MBUS_VAR_HEADER_LEN, &h));
}

// A builder given less room than the frame needs writes nothing and reports 0.
void test_builders_refuse_a_short_buffer(void)
{
    uint8_t buf[300];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_mbus_build_ack(buf, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_mbus_build_short(buf, 4, MBUS_C_SND_NKE, 0x01));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_mbus_build_long(buf, 8, 0x53, 0x01, 0x51, NULL, 0));

    // MBDOC48 5.2: user data is 0..252 octets, since L is one octet and counts C + A + CI as well
    static uint8_t big[MBUS_MAX_DATA];
    // L = 3 + 252 = 255 and the frame is 6 + L octets, so the largest M-Bus frame is 261 octets
    TEST_ASSERT_EQUAL_UINT(261u, protocore_mbus_build_long(buf, sizeof(buf), 0x53, 0x01, 0x51, big, MBUS_MAX_DATA));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_mbus_build_long(buf, sizeof(buf), 0x53, 0x01, 0x51, big, MBUS_MAX_DATA + 1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_mbus_build_long(buf, sizeof(buf), 0x53, 0x01, 0x51, NULL, 4));
}
