// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the wired M-Bus codec (services/fieldbus/mbus): the ACK / short / long frame builders
// and parser (start/stop octets, doubled length, 8-bit sum checksum), and the EN 13757-3
// variable-data record walker (DIF coding lengths, DIFE/VIFE extension chains, LVAR strings).
// Pure host tests.

#include "services/fieldbus/mbus/mbus.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_ack()
{
    uint8_t buf[4];
    TEST_ASSERT_EQUAL_size_t(1, pc_mbus_build_ack(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0xE5, buf[0]);
    MbusFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_mbus_parse(buf, 1, &f, &c));
    TEST_ASSERT_EQUAL_INT(MBUS_FRAME_ACK, f.type);
    TEST_ASSERT_EQUAL_size_t(1, c);
}

void test_short_frame_roundtrip()
{
    uint8_t buf[8];
    size_t n = pc_mbus_build_snd_nke(buf, sizeof(buf), 0x05);
    TEST_ASSERT_EQUAL_size_t(5, n);
    const uint8_t expect[] = {0x10, 0x40, 0x05, 0x45, 0x16}; // CS = 0x40 + 0x05
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, 5);

    MbusFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_mbus_parse(buf, n, &f, &c));
    TEST_ASSERT_EQUAL_INT(MBUS_FRAME_SHORT, f.type);
    TEST_ASSERT_EQUAL_HEX8(MBUS_C_SND_NKE, f.c);
    TEST_ASSERT_EQUAL_HEX8(0x05, f.a);
    TEST_ASSERT_EQUAL_size_t(5, c);
}

void test_req_ud2_fcb()
{
    uint8_t buf[8];
    pc_mbus_build_req_ud2(buf, sizeof(buf), 0x01, PROTO_FALSE);
    TEST_ASSERT_EQUAL_HEX8(0x5B, buf[1]);
    pc_mbus_build_req_ud2(buf, sizeof(buf), 0x01, PROTO_TRUE);
    TEST_ASSERT_EQUAL_HEX8(0x7B, buf[1]); // FCB set
}

void test_req_ud1_fcb()
{
    uint8_t buf[8];
    // REQ_UD1 (class-1 / alarm data): C = 0x5A, or 0x7A with the FCB bit set.
    size_t n = pc_mbus_build_req_ud1(buf, sizeof(buf), 0x01, PROTO_FALSE);
    TEST_ASSERT_EQUAL_size_t(5, n);
    const uint8_t expect[] = {0x10, 0x5A, 0x01, 0x5B, 0x16}; // CS = 0x5A + 0x01
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, 5);
    MbusFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_mbus_parse(buf, n, &f, &c));
    TEST_ASSERT_EQUAL_HEX8(MBUS_C_REQ_UD1, f.c);
    TEST_ASSERT_EQUAL_HEX8(0x01, f.a);

    pc_mbus_build_req_ud1(buf, sizeof(buf), 0x01, PROTO_TRUE);
    TEST_ASSERT_EQUAL_HEX8(0x7A, buf[1]); // FCB set
}

void test_long_frame_roundtrip()
{
    const uint8_t data[4] = {0x2A, 0x00, 0x00, 0x00};
    uint8_t buf[32];
    size_t n = pc_mbus_build_long(buf, sizeof(buf), MBUS_C_RSP_UD, 0x01, MBUS_CI_RSP_VARIABLE, data, 4);
    TEST_ASSERT_EQUAL_size_t(6 + (3 + 4), n); // 6 framing octets + L (=7)
    TEST_ASSERT_EQUAL_HEX8(0x68, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(7, buf[1]); // L = 3 + 4
    TEST_ASSERT_EQUAL_HEX8(7, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x68, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x16, buf[n - 1]);

    MbusFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_mbus_parse(buf, n, &f, &c));
    TEST_ASSERT_EQUAL_INT(MBUS_FRAME_LONG, f.type);
    TEST_ASSERT_EQUAL_HEX8(MBUS_C_RSP_UD, f.c);
    TEST_ASSERT_EQUAL_HEX8(0x01, f.a);
    TEST_ASSERT_EQUAL_HEX8(MBUS_CI_RSP_VARIABLE, f.ci);
    TEST_ASSERT_EQUAL_UINT8(4, f.data_len);
    TEST_ASSERT_EQUAL_MEMORY(data, f.data, 4);
    TEST_ASSERT_EQUAL_size_t(n, c);
}

void test_parse_rejects_corruption()
{
    const uint8_t data[2] = {0xAA, 0xBB};
    uint8_t buf[32];
    size_t n = pc_mbus_build_long(buf, sizeof(buf), 0x08, 0x01, 0x72, data, 2);
    MbusFrame f;
    size_t c;

    uint8_t bad_cs[32];
    memcpy(bad_cs, buf, n);
    bad_cs[n - 2] ^= 0xFF; // corrupt the checksum
    TEST_ASSERT_FALSE(pc_mbus_parse(bad_cs, n, &f, &c));

    uint8_t bad_stop[32];
    memcpy(bad_stop, buf, n);
    bad_stop[n - 1] = 0x00; // wrong stop octet
    TEST_ASSERT_FALSE(pc_mbus_parse(bad_stop, n, &f, &c));

    uint8_t bad_len[32];
    memcpy(bad_len, buf, n);
    bad_len[2] ^= 0x01; // the two L octets disagree
    TEST_ASSERT_FALSE(pc_mbus_parse(bad_len, n, &f, &c));
}

void test_dif_data_len()
{
    TEST_ASSERT_EQUAL_UINT8(1, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_INT8));
    TEST_ASSERT_EQUAL_UINT8(4, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_INT32));
    TEST_ASSERT_EQUAL_UINT8(4, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_REAL32));
    TEST_ASSERT_EQUAL_UINT8(6, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_INT48));
    TEST_ASSERT_EQUAL_UINT8(8, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_INT64));
    TEST_ASSERT_EQUAL_UINT8(3, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_BCD6));
    TEST_ASSERT_EQUAL_UINT8(0, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_VARIABLE));
}

// Walk a record stream: INT32, INT16, an INT32 with a DIFE, and an LVAR ASCII string.
void test_record_walk()
{
    const uint8_t body[] = {
        0x04, 0x13, 0x2A, 0x00, 0x00, 0x00,       // DIF INT32, VIF 0x13, value 42
        0x02, 0x5A, 0x10, 0x27,                   // DIF INT16, VIF 0x5A, value 0x2710
        0x84, 0x01, 0x13, 0x01, 0x00, 0x00, 0x00, // DIF INT32 + DIFE 0x01, VIF 0x13, value 1
        0x0D, 0x7C, 0x03, 'A',  'B',  'C',        // DIF variable, VIF 0x7C, LVAR 3, "ABC"
    };
    size_t pos = 0;
    MbusRecord r;

    TEST_ASSERT_TRUE(pc_mbus_record_next(body, sizeof(body), &pos, &r));
    TEST_ASSERT_EQUAL_HEX8(MBUS_DIF_INT32, r.coding);
    TEST_ASSERT_EQUAL_HEX8(0x13, r.vif);
    TEST_ASSERT_EQUAL_UINT8(4, r.data_len);
    TEST_ASSERT_EQUAL_HEX8(0x2A, r.data[0]);

    TEST_ASSERT_TRUE(pc_mbus_record_next(body, sizeof(body), &pos, &r));
    TEST_ASSERT_EQUAL_HEX8(MBUS_DIF_INT16, r.coding);
    TEST_ASSERT_EQUAL_HEX8(0x5A, r.vif);
    TEST_ASSERT_EQUAL_UINT8(2, r.data_len);

    TEST_ASSERT_TRUE(pc_mbus_record_next(body, sizeof(body), &pos, &r));
    TEST_ASSERT_EQUAL_HEX8(MBUS_DIF_INT32, r.coding); // DIFE skipped
    TEST_ASSERT_EQUAL_HEX8(0x13, r.vif);
    TEST_ASSERT_EQUAL_UINT8(4, r.data_len);

    TEST_ASSERT_TRUE(pc_mbus_record_next(body, sizeof(body), &pos, &r));
    TEST_ASSERT_EQUAL_HEX8(MBUS_DIF_VARIABLE, r.coding);
    TEST_ASSERT_EQUAL_UINT8(3, r.data_len);
    TEST_ASSERT_EQUAL_MEMORY("ABC", r.data, 3);

    TEST_ASSERT_FALSE(pc_mbus_record_next(body, sizeof(body), &pos, &r)); // end of data
}

void test_record_truncated_fails()
{
    const uint8_t body[] = {0x04, 0x13, 0x2A, 0x00}; // INT32 claims 4 octets, only 2 present
    size_t pos = 0;
    MbusRecord r;
    TEST_ASSERT_FALSE(pc_mbus_record_next(body, sizeof(body), &pos, &r));
}

void test_build_and_parse_guards()
{
    uint8_t buf[32];
    const uint8_t d[4] = {1, 2, 3, 4};
    // Builder guards.
    TEST_ASSERT_EQUAL_size_t(0, pc_mbus_build_ack(NULL, 4));
    TEST_ASSERT_EQUAL_size_t(0, pc_mbus_build_ack(buf, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_mbus_build_short(NULL, 8, 0x40, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_mbus_build_short(buf, 4, 0x40, 1)); // cap < 5
    TEST_ASSERT_EQUAL_size_t(0, pc_mbus_build_long(NULL, 32, 0, 0, 0, d, 4));
    TEST_ASSERT_EQUAL_size_t(0, pc_mbus_build_long(buf, 32, 0, 0, 0, NULL, 4));            // data_len && !data
    TEST_ASSERT_EQUAL_size_t(0, pc_mbus_build_long(buf, 5, 0, 0, 0, d, 4));                // cap < total
    TEST_ASSERT_EQUAL_size_t(0, pc_mbus_build_long(buf, sizeof(buf), 0, 0, 0, NULL, 253)); // data_len > MBUS_MAX_DATA

    // Parser guards.
    MbusFrame f;
    size_t c;
    TEST_ASSERT_FALSE(pc_mbus_parse(NULL, 5, &f, &c));
    TEST_ASSERT_FALSE(pc_mbus_parse(buf, 0, &f, &c));
    TEST_ASSERT_FALSE(pc_mbus_parse(buf, 5, NULL, &c)); // out == NULL
    const uint8_t unknown[] = {0x99, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(pc_mbus_parse(unknown, sizeof(unknown), &f, &c)); // unrecognized start octet

    uint8_t s[5];
    pc_mbus_build_short(s, sizeof(s), 0x40, 0x05);
    TEST_ASSERT_FALSE(pc_mbus_parse(s, 4, &f, &c)); // len < 5
    uint8_t s2[5];
    memcpy(s2, s, 5);
    s2[4] = 0x00; // bad stop
    TEST_ASSERT_FALSE(pc_mbus_parse(s2, 5, &f, &c));
    memcpy(s2, s, 5);
    s2[3] ^= 0xFF; // bad checksum
    TEST_ASSERT_FALSE(pc_mbus_parse(s2, 5, &f, &c));

    const uint8_t l[] = {0x68, 0x07, 0x07}; // long start, header truncated (< 4)
    TEST_ASSERT_FALSE(pc_mbus_parse(l, sizeof(l), &f, &c));

    const uint8_t l_short_L[] = {0x68, 0x02, 0x02, 0x68}; // L < 3 (below the control-frame minimum)
    TEST_ASSERT_FALSE(pc_mbus_parse(l_short_L, sizeof(l_short_L), &f, &c));
    const uint8_t l_bad_repeat[] = {0x68, 0x03, 0x03, 0x00}; // second start-repeat octet wrong
    TEST_ASSERT_FALSE(pc_mbus_parse(l_bad_repeat, sizeof(l_bad_repeat), &f, &c));

    uint8_t ctrl[16];
    size_t ctrl_n = pc_mbus_build_long(ctrl, sizeof(ctrl), 0x08, 0x01, 0x51, NULL, 0);
    TEST_ASSERT_FALSE(pc_mbus_parse(ctrl, ctrl_n - 1, &f, &c)); // len < total
}

void test_long_frame_control()
{
    // data_len == 0 builds a control frame: a long frame carrying no user data.
    uint8_t buf[16];
    size_t n = pc_mbus_build_long(buf, sizeof(buf), MBUS_C_SND_UD, 0x03, MBUS_CI_DATA_SEND, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(9, n);    // 6 framing octets + L(=3)
    TEST_ASSERT_EQUAL_HEX8(3, buf[1]); // L = 3 + 0
    TEST_ASSERT_EQUAL_HEX8(3, buf[2]);

    MbusFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_mbus_parse(buf, n, &f, &c));
    TEST_ASSERT_EQUAL_INT(MBUS_FRAME_LONG, f.type);
    TEST_ASSERT_EQUAL_UINT8(0, f.data_len);
    TEST_ASSERT_NULL(f.data);
}

void test_parse_null_consumed()
{
    // consumed may be NULL on all three successful-parse paths.
    uint8_t ack[4];
    pc_mbus_build_ack(ack, sizeof(ack));
    MbusFrame f;
    TEST_ASSERT_TRUE(pc_mbus_parse(ack, 1, &f, NULL));

    uint8_t s[8];
    size_t sn = pc_mbus_build_snd_nke(s, sizeof(s), 0x05);
    TEST_ASSERT_TRUE(pc_mbus_parse(s, sn, &f, NULL));

    uint8_t l[32];
    const uint8_t data[2] = {0x01, 0x02};
    size_t ln = pc_mbus_build_long(l, sizeof(l), MBUS_C_RSP_UD, 0x01, MBUS_CI_RSP_VARIABLE, data, 2);
    TEST_ASSERT_TRUE(pc_mbus_parse(l, ln, &f, NULL));
}

void test_dif_data_len_remaining()
{
    TEST_ASSERT_EQUAL_UINT8(0, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_NONE));
    TEST_ASSERT_EQUAL_UINT8(3, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_INT24));
    TEST_ASSERT_EQUAL_UINT8(0, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_READOUT));
    TEST_ASSERT_EQUAL_UINT8(1, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_BCD2));
    TEST_ASSERT_EQUAL_UINT8(2, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_BCD4));
    TEST_ASSERT_EQUAL_UINT8(4, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_BCD8));
    TEST_ASSERT_EQUAL_UINT8(6, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_BCD12));
    TEST_ASSERT_EQUAL_UINT8(0, pc_mbus_dif_data_len((uint8_t)MBUS_DIF_SPECIAL));
}

void test_record_edges()
{
    size_t pos;
    MbusRecord r;

    const uint8_t dife_trunc[] = {0x84}; // continuation bit set, DIFE missing
    pos = 0;
    TEST_ASSERT_FALSE(pc_mbus_record_next(dife_trunc, sizeof(dife_trunc), &pos, &r));

    const uint8_t special[] = {0x0F, 0xAB, 0xCD}; // SPECIAL coding: no VIF, no data
    pos = 0;
    TEST_ASSERT_TRUE(pc_mbus_record_next(special, sizeof(special), &pos, &r));
    TEST_ASSERT_EQUAL_HEX8(MBUS_DIF_SPECIAL, r.coding);
    TEST_ASSERT_EQUAL_size_t(1, pos); // consumed only the DIF

    const uint8_t no_vif[] = {0x04}; // INT32 DIF with no VIF
    pos = 0;
    TEST_ASSERT_FALSE(pc_mbus_record_next(no_vif, sizeof(no_vif), &pos, &r));

    const uint8_t vife_trunc[] = {0x04, 0x93}; // VIF continuation bit but VIFE missing
    pos = 0;
    TEST_ASSERT_FALSE(pc_mbus_record_next(vife_trunc, sizeof(vife_trunc), &pos, &r));

    const uint8_t lvar_missing[] = {0x0D, 0x7C}; // VARIABLE coding, LVAR octet missing
    pos = 0;
    TEST_ASSERT_FALSE(pc_mbus_record_next(lvar_missing, sizeof(lvar_missing), &pos, &r));

    const uint8_t lvar_big[] = {0x0D, 0x7C, 0xC0}; // LVAR > 0xBF unsupported
    pos = 0;
    TEST_ASSERT_FALSE(pc_mbus_record_next(lvar_big, sizeof(lvar_big), &pos, &r));

    // Pointer guards.
    const uint8_t any[] = {0x00, 0x00};
    pos = 0;
    TEST_ASSERT_FALSE(pc_mbus_record_next(NULL, sizeof(any), &pos, &r)); // body == NULL
    TEST_ASSERT_FALSE(pc_mbus_record_next(any, sizeof(any), NULL, &r));  // pos == NULL
    pos = 0;
    TEST_ASSERT_FALSE(pc_mbus_record_next(any, sizeof(any), &pos, NULL)); // out == NULL

    // DIF PC_NONE coding: VIF present but zero-length value data.
    pos = 0;
    TEST_ASSERT_TRUE(pc_mbus_record_next(any, sizeof(any), &pos, &r));
    TEST_ASSERT_EQUAL_HEX8(MBUS_DIF_NONE, r.coding);
    TEST_ASSERT_EQUAL_UINT8(0, r.data_len);
    TEST_ASSERT_NULL(r.data);
}

// A VIF with the extension bit set is followed by a present VIFE octet (the chain read).
void test_record_vife_chain()
{
    const uint8_t body[] = {(uint8_t)MBUS_DIF_INT8, 0x93, 0x13, 0x42}; // DIF INT8, VIF 0x93 (ext), VIFE 0x13, data
    size_t pos = 0;
    MbusRecord r;
    TEST_ASSERT_TRUE(pc_mbus_record_next(body, sizeof(body), &pos, &r));
    TEST_ASSERT_EQUAL_HEX8(0x93, r.vif);
    TEST_ASSERT_EQUAL_UINT8(1, r.data_len);
    TEST_ASSERT_EQUAL_HEX8(0x42, r.data[0]);
}

// --- record value + VIF unit decoding ---
static MbusRecord rec(uint8_t coding, const uint8_t *data, uint8_t len)
{
    MbusRecord r;
    r.dif = 0;
    r.coding = coding;
    r.vif = 0;
    r.data = data;
    r.data_len = len;
    return r;
}

void test_value_int()
{
    int64_t v = 0;
    const uint8_t i32[4] = {0x87, 0xD6, 0x12, 0x00}; // 1234567 little-endian
    MbusRecord r = rec((uint8_t)MBUS_DIF_INT32, i32, 4);
    TEST_ASSERT_TRUE(pc_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(1234567, v);

    const uint8_t i8[1] = {0xFF}; // -1 signed
    r = rec((uint8_t)MBUS_DIF_INT8, i8, 1);
    TEST_ASSERT_TRUE(pc_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(-1, v);

    const uint8_t i16[2] = {0x00, 0x80}; // -32768
    r = rec((uint8_t)MBUS_DIF_INT16, i16, 2);
    TEST_ASSERT_TRUE(pc_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(-32768, v);

    const uint8_t bcd8[4] = {0x78, 0x56, 0x34, 0x12}; // BCD 12345678, little-endian
    r = rec((uint8_t)MBUS_DIF_BCD8, bcd8, 4);
    TEST_ASSERT_TRUE(pc_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(12345678, v);

    const uint8_t bcdneg[2] = {0x99, 0xF0}; // 0xF top nibble = negative -> -99
    r = rec((uint8_t)MBUS_DIF_BCD4, bcdneg, 2);
    TEST_ASSERT_TRUE(pc_mbus_record_value_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(-99, v);

    // An invalid BCD nibble is rejected; a REAL coding is not an integer.
    const uint8_t bad[1] = {0x1A};
    r = rec((uint8_t)MBUS_DIF_BCD2, bad, 1);
    TEST_ASSERT_FALSE(pc_mbus_record_value_int(&r, &v));
    r = rec((uint8_t)MBUS_DIF_REAL32, i32, 4);
    TEST_ASSERT_FALSE(pc_mbus_record_value_int(&r, &v));
}

void test_value_real()
{
    float f = 0;
    const uint8_t r32[4] = {0x00, 0x00, 0xC0, 0x3F}; // 1.5f little-endian
    MbusRecord r = rec((uint8_t)MBUS_DIF_REAL32, r32, 4);
    TEST_ASSERT_TRUE(pc_mbus_record_value_real(&r, &f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, f);
    // An integer coding is not a real.
    r = rec((uint8_t)MBUS_DIF_INT32, r32, 4);
    TEST_ASSERT_FALSE(pc_mbus_record_value_real(&r, &f));
}

void test_vif_decode()
{
    MbusUnit u;
    int8_t e;
    TEST_ASSERT_TRUE(pc_mbus_vif_decode(0x13, &u, &e)); // volume 0.001 m3 (litres)
    TEST_ASSERT_EQUAL(MBUS_UNIT_M3, u);
    TEST_ASSERT_EQUAL_INT8(-3, e);
    TEST_ASSERT_TRUE(pc_mbus_vif_decode(0x06, &u, &e)); // energy 10^3 Wh (kWh)
    TEST_ASSERT_EQUAL(MBUS_UNIT_WH, u);
    TEST_ASSERT_EQUAL_INT8(3, e);
    TEST_ASSERT_TRUE(pc_mbus_vif_decode(0x2E, &u, &e)); // power 10^3 W (kW)
    TEST_ASSERT_EQUAL(MBUS_UNIT_W, u);
    TEST_ASSERT_EQUAL_INT8(3, e);
    TEST_ASSERT_TRUE(pc_mbus_vif_decode(0x5A, &u, &e)); // flow temperature 0.1 degC
    TEST_ASSERT_EQUAL(MBUS_UNIT_CELSIUS, u);
    TEST_ASSERT_EQUAL_INT8(-1, e);
    TEST_ASSERT_TRUE(pc_mbus_vif_decode(0x68, &u, &e)); // pressure 0.001 bar
    TEST_ASSERT_EQUAL(MBUS_UNIT_BAR, u);
    // The extension bit is ignored (0x93 decodes like 0x13).
    TEST_ASSERT_TRUE(pc_mbus_vif_decode(0x93, &u, &e));
    TEST_ASSERT_EQUAL(MBUS_UNIT_M3, u);
    // A non-measurement VIF (fabrication number 0x78) is unknown.
    TEST_ASSERT_FALSE(pc_mbus_vif_decode(0x78, &u, &e));
    TEST_ASSERT_EQUAL(MBUS_UNIT_UNKNOWN, u);
}

void test_var_header()
{
    // A CI=0x72 fixed header: serial 12345678, manufacturer LUG (Landis+Gyr, 0x32A7),
    // version 1, medium water, access 42, status 0, no signature.
    const uint8_t hdr[12] = {0x78, 0x56, 0x34, 0x12, 0xA7, 0x32, 0x01, 0x07, 0x2A, 0x00, 0x00, 0x00};
    MbusVarHeader h;
    TEST_ASSERT_TRUE(pc_mbus_parse_var_header(hdr, sizeof(hdr), &h));
    TEST_ASSERT_EQUAL_UINT32(12345678u, h.id);
    TEST_ASSERT_EQUAL_STRING("LUG", h.manufacturer);
    TEST_ASSERT_EQUAL_HEX16(0x32A7, h.manufacturer_raw);
    TEST_ASSERT_EQUAL_UINT8(1, h.version);
    TEST_ASSERT_EQUAL_UINT8(MBUS_MEDIUM_WATER, h.medium);
    TEST_ASSERT_EQUAL_UINT8(42, h.access_no);
    TEST_ASSERT_EQUAL_UINT8(0, h.status);
    TEST_ASSERT_EQUAL_UINT16(0, h.signature);

    // A non-BCD nibble in the identification number is rejected.
    uint8_t bad[12];
    memcpy(bad, hdr, sizeof(bad));
    bad[0] = 0xAB; // 0xA and 0xB are not BCD digits
    TEST_ASSERT_FALSE(pc_mbus_parse_var_header(bad, sizeof(bad), &h));
    // A short body and null args are rejected.
    TEST_ASSERT_FALSE(pc_mbus_parse_var_header(hdr, 11, &h));
    TEST_ASSERT_FALSE(pc_mbus_parse_var_header(NULL, 12, &h));
    TEST_ASSERT_FALSE(pc_mbus_parse_var_header(hdr, 12, NULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_ack);
    RUN_TEST(test_short_frame_roundtrip);
    RUN_TEST(test_req_ud2_fcb);
    RUN_TEST(test_req_ud1_fcb);
    RUN_TEST(test_long_frame_roundtrip);
    RUN_TEST(test_parse_rejects_corruption);
    RUN_TEST(test_dif_data_len);
    RUN_TEST(test_record_walk);
    RUN_TEST(test_record_truncated_fails);
    RUN_TEST(test_build_and_parse_guards);
    RUN_TEST(test_long_frame_control);
    RUN_TEST(test_parse_null_consumed);
    RUN_TEST(test_dif_data_len_remaining);
    RUN_TEST(test_record_edges);
    RUN_TEST(test_record_vife_chain);
    RUN_TEST(test_value_int);
    RUN_TEST(test_value_real);
    RUN_TEST(test_vif_decode);
    RUN_TEST(test_var_header);
    return UNITY_END();
}
