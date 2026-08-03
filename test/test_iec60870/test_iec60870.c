// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the IEC 60870-5-101/-104 codec (services/energy/iec60870): the -104 APCI (I/S/U
// formats), the shared ASDU header + 3-octet IOA, and the -101 FT1.2 link frames (fixed +
// variable, sum checksum). Frame layout checked against IEC 60870-5-101/-104. Pure host tests.

#include "services/energy/iec60870/iec60870.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// -104 I-format: numbered transfer carrying an ASDU.
void test_104_i_format_roundtrip()
{
    const uint8_t asdu[6] = {0x09, 0x01, 0x03, 0x00, 0x0A, 0x00}; // a tiny ASDU header
    uint8_t buf[32];
    size_t n = pc_iec104_build_i(buf, sizeof(buf), 100, 50, asdu, 6);
    TEST_ASSERT_EQUAL_size_t(6 + 6, n);
    TEST_ASSERT_EQUAL_HEX8(0x68, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(4 + 6, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2] & 0x01); // I-format: bit0 = 0

    Iec104Apci a;
    size_t c;
    TEST_ASSERT_TRUE(pc_iec104_parse(buf, n, &a, &c));
    TEST_ASSERT_EQUAL_INT(IEC104_I, a.format);
    TEST_ASSERT_EQUAL_UINT16(100, a.ns);
    TEST_ASSERT_EQUAL_UINT16(50, a.nr);
    TEST_ASSERT_EQUAL_size_t(6, a.asdu_len);
    TEST_ASSERT_EQUAL_MEMORY(asdu, a.asdu, 6);
    TEST_ASSERT_EQUAL_size_t(n, c);
}

void test_104_s_format()
{
    uint8_t buf[8];
    size_t n = pc_iec104_build_s(buf, sizeof(buf), 1234);
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[2]);
    Iec104Apci a;
    size_t c;
    TEST_ASSERT_TRUE(pc_iec104_parse(buf, n, &a, &c));
    TEST_ASSERT_EQUAL_INT(IEC104_S, a.format);
    TEST_ASSERT_EQUAL_UINT16(1234, a.nr);
}

void test_104_u_format()
{
    uint8_t buf[8];
    size_t n = pc_iec104_build_u(buf, sizeof(buf), IEC104_STARTDT_ACT);
    TEST_ASSERT_EQUAL_size_t(6, n);
    Iec104Apci a;
    size_t c;
    TEST_ASSERT_TRUE(pc_iec104_parse(buf, n, &a, &c));
    TEST_ASSERT_EQUAL_INT(IEC104_U, a.format);
    TEST_ASSERT_EQUAL_HEX8(IEC104_STARTDT_ACT, a.u_cmd);
}

// The 15-bit sequence numbers survive values above one octet.
void test_104_sequence_numbers_15bit()
{
    uint8_t buf[8];
    pc_iec104_build_i(buf, sizeof(buf), 0x7FFF, 0x4001, NULL, 0);
    Iec104Apci a;
    size_t c;
    TEST_ASSERT_TRUE(pc_iec104_parse(buf, 6, &a, &c));
    TEST_ASSERT_EQUAL_UINT16(0x7FFF, a.ns);
    TEST_ASSERT_EQUAL_UINT16(0x4001, a.nr);
}

void test_asdu_header_roundtrip()
{
    IecAsduHeader h;
    h.type_id = IEC_TYPE_M_ME_NC_1;
    h.sq = PROTO_FALSE;
    h.count = 3;
    h.test = PROTO_FALSE;
    h.negative = PROTO_FALSE;
    h.cot = IEC_COT_SPONTANEOUS;
    h.orig_addr = 0;
    h.common_addr = 0x000A;
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_size_t(6, pc_iec_asdu_build_header(buf, sizeof(buf), &h));
    TEST_ASSERT_EQUAL_HEX8(13, buf[0]);   // M_ME_NC_1
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[1]); // SQ=0, count=3
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[2]); // COT spontaneous

    IecAsduHeader g;
    size_t c;
    TEST_ASSERT_TRUE(pc_iec_asdu_parse_header(buf, 6, &g, &c));
    TEST_ASSERT_EQUAL_UINT8(IEC_TYPE_M_ME_NC_1, g.type_id);
    TEST_ASSERT_EQUAL_UINT8(3, g.count);
    TEST_ASSERT_FALSE(g.sq);
    TEST_ASSERT_EQUAL_UINT8(IEC_COT_SPONTANEOUS, g.cot);
    TEST_ASSERT_EQUAL_UINT16(0x000A, g.common_addr);
}

void test_ioa_roundtrip()
{
    uint8_t buf[4] = {0};
    TEST_ASSERT_EQUAL_size_t(3, pc_iec_put_ioa(buf, sizeof(buf), 0x123456));
    TEST_ASSERT_EQUAL_HEX8(0x56, buf[0]); // little-endian
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[2]);
    TEST_ASSERT_EQUAL_HEX32(0x123456u, pc_iec_get_ioa(buf));
}

void test_101_fixed_frame()
{
    uint8_t buf[8];
    size_t n = pc_iec101_build_fixed(buf, sizeof(buf), IEC_FC_REQUEST_CLASS2, 0x01);
    TEST_ASSERT_EQUAL_size_t(5, n);
    const uint8_t expect[] = {0x10, IEC_FC_REQUEST_CLASS2, 0x01, (uint8_t)(IEC_FC_REQUEST_CLASS2 + 1), 0x16};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, 5);

    Iec101Frame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_iec101_parse(buf, n, &f, &c));
    TEST_ASSERT_TRUE(f.fixed);
    TEST_ASSERT_EQUAL_HEX8(IEC_FC_REQUEST_CLASS2, f.control);
    TEST_ASSERT_EQUAL_HEX8(0x01, f.addr);
}

void test_101_variable_frame_roundtrip()
{
    const uint8_t asdu[6] = {0x01, 0x01, 0x03, 0x00, 0x0A, 0x00};
    uint8_t buf[32];
    size_t n = pc_iec101_build_variable(buf, sizeof(buf), IEC_FC_USER_DATA_CONFIRM, 0x01, asdu, 6);
    TEST_ASSERT_EQUAL_size_t(6 + (2 + 6), n);
    TEST_ASSERT_EQUAL_HEX8(0x68, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(8, buf[1]); // L = 2 + 6
    TEST_ASSERT_EQUAL_HEX8(0x16, buf[n - 1]);

    Iec101Frame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_iec101_parse(buf, n, &f, &c));
    TEST_ASSERT_FALSE(f.fixed);
    TEST_ASSERT_EQUAL_HEX8(IEC_FC_USER_DATA_CONFIRM, f.control);
    TEST_ASSERT_EQUAL_HEX8(0x01, f.addr);
    TEST_ASSERT_EQUAL_UINT8(6, f.asdu_len);
    TEST_ASSERT_EQUAL_MEMORY(asdu, f.asdu, 6);

    // a corrupted checksum is rejected.
    buf[n - 2] ^= 0xFF;
    TEST_ASSERT_FALSE(pc_iec101_parse(buf, n, &f, &c));
}

// -104 builders reject null buffers, oversize ASDUs, and buffers too small.
void test_104_build_guards()
{
    uint8_t buf[32];
    const uint8_t asdu[6] = {0};
    TEST_ASSERT_EQUAL_size_t(0, pc_iec104_build_i(NULL, sizeof(buf), 0, 0, asdu, 6)); // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_iec104_build_i(buf, sizeof(buf), 0, 0, NULL, 6));  // len but null asdu
    TEST_ASSERT_EQUAL_size_t(0, pc_iec104_build_i(buf, 512, 0, 0, asdu, 250));        // asdu_len > 249
    TEST_ASSERT_EQUAL_size_t(0, pc_iec104_build_i(buf, 8, 0, 0, asdu, 6));            // cap < 6 + asdu_len
    TEST_ASSERT_EQUAL_size_t(0, pc_iec104_build_s(NULL, sizeof(buf), 0));             // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_iec104_build_s(buf, 4, 0));                        // cap < APCI len
    TEST_ASSERT_EQUAL_size_t(0, pc_iec104_build_u(NULL, sizeof(buf), 0));             // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_iec104_build_u(buf, 4, 0));                        // cap < APCI len
}

// -104 parse rejects a wrong start octet, an APDU length below 4, and a truncated frame.
void test_104_parse_rejects()
{
    Iec104Apci a;
    size_t c;
    uint8_t bad_start[6] = {0x00, 0x04, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(pc_iec104_parse(bad_start, sizeof(bad_start), &a, &c)); // buf[0] != 0x68
    TEST_ASSERT_FALSE(pc_iec104_parse(NULL, 6, &a, &c));                      // null buf
    uint8_t short_l[6] = {IEC_START_104, 0x03, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(pc_iec104_parse(short_l, sizeof(short_l), &a, &c)); // L < 4
    uint8_t trunc[2] = {IEC_START_104, 0x0A};
    TEST_ASSERT_FALSE(pc_iec104_parse(trunc, sizeof(trunc), &a, &c)); // len < 2 + L
}

// ASDU header / IOA helpers reject null buffers and buffers too small.
void test_asdu_ioa_guards()
{
    uint8_t buf[8];
    IecAsduHeader h = {0};
    IecAsduHeader g;
    size_t c;
    TEST_ASSERT_EQUAL_size_t(0, pc_iec_asdu_build_header(buf, 4, &h)); // cap < 6
    TEST_ASSERT_EQUAL_size_t(0, pc_iec_asdu_build_header(NULL, 8, &h));
    TEST_ASSERT_FALSE(pc_iec_asdu_parse_header(buf, 5, &g, &c));   // len < 6
    TEST_ASSERT_EQUAL_size_t(0, pc_iec_put_ioa(buf, 2, 0x123456)); // cap < 3
    TEST_ASSERT_EQUAL_size_t(0, pc_iec_put_ioa(NULL, 8, 0));
}

// -101 builders reject null buffers, oversize ASDUs, and buffers too small.
void test_101_build_guards()
{
    uint8_t buf[32];
    const uint8_t asdu[6] = {0};
    TEST_ASSERT_EQUAL_size_t(0, pc_iec101_build_fixed(buf, 4, 0x49, 0x01));                 // cap < 5
    TEST_ASSERT_EQUAL_size_t(0, pc_iec101_build_fixed(NULL, sizeof(buf), 0x49, 0x01));      // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_iec101_build_variable(buf, sizeof(buf), 0, 0, NULL, 6)); // len but null asdu
    TEST_ASSERT_EQUAL_size_t(0, pc_iec101_build_variable(buf, 512, 0, 0, asdu, 254));       // asdu_len > 253
    TEST_ASSERT_EQUAL_size_t(0, pc_iec101_build_variable(buf, 10, 0, 0, asdu, 6));          // cap < 6 + L
}

// -101 parse rejects an empty buffer, a corrupt fixed frame, and malformed variable frames.
void test_101_parse_rejects()
{
    Iec101Frame f;
    size_t c;
    TEST_ASSERT_FALSE(pc_iec101_parse(NULL, 5, &f, &c)); // null buf
    uint8_t empty[1] = {0};
    TEST_ASSERT_FALSE(pc_iec101_parse(empty, 0, &f, &c)); // len < 1

    uint8_t bad_cksum[5] = {IEC_START_FIXED, 0x49, 0x01, 0xFF, IEC_STOP};
    TEST_ASSERT_FALSE(pc_iec101_parse(bad_cksum, sizeof(bad_cksum), &f, &c)); // checksum mismatch
    uint8_t bad_stop[5] = {IEC_START_FIXED, 0x49, 0x01, 0x4A, 0x00};
    TEST_ASSERT_FALSE(pc_iec101_parse(bad_stop, sizeof(bad_stop), &f, &c)); // no 0x16 stop

    uint8_t var_trunc[3] = {IEC_START_104, 0x08, 0x08};
    TEST_ASSERT_FALSE(pc_iec101_parse(var_trunc, sizeof(var_trunc), &f, &c)); // len < 4
    uint8_t var_badhdr[8] = {IEC_START_104, 0x01, 0x01, IEC_START_104, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(pc_iec101_parse(var_badhdr, sizeof(var_badhdr), &f, &c)); // L < 2
    uint8_t var_mismatch[8] = {IEC_START_104, 0x08, 0x09, IEC_START_104, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(pc_iec101_parse(var_mismatch, sizeof(var_mismatch), &f, &c)); // buf[2] != L

    uint8_t unknown[5] = {0x99, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(pc_iec101_parse(unknown, sizeof(unknown), &f, &c)); // neither 0x10 nor 0x68
}

// -104 parse rejects a null 'out' and a buffer shorter than 2 octets (both branches of the
// guard's short-circuit chain, isolated from each other).
void test_104_parse_null_out_and_too_short()
{
    Iec104Apci a;
    uint8_t buf[6] = {IEC_START_104, 4, 0, 0, 0, 0};
    size_t c;
    TEST_ASSERT_FALSE(pc_iec104_parse(buf, sizeof(buf), NULL, &c)); // null out
    TEST_ASSERT_FALSE(pc_iec104_parse(buf, 1, &a, &c));             // len < 2
}

// -104 parse accepts a null 'consumed' output pointer.
void test_104_parse_consumed_null()
{
    uint8_t buf[6];
    size_t n = pc_iec104_build_s(buf, sizeof(buf), 5);
    Iec104Apci a;
    TEST_ASSERT_TRUE(pc_iec104_parse(buf, n, &a, NULL));
}

// ASDU header build rejects a null header pointer, and both flag bits (sq / test / negative)
// take their 'true' branch at least once.
void test_asdu_header_build_null_h_and_flag_branches()
{
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_iec_asdu_build_header(buf, sizeof(buf), NULL)); // null h

    IecAsduHeader h;
    h.type_id = IEC_TYPE_C_SC_NA_1;
    h.sq = PROTO_TRUE; // VSQ 'sq' true branch
    h.count = 1;
    h.test = PROTO_TRUE;     // COT 'test' true branch
    h.negative = PROTO_TRUE; // COT 'negative' true branch
    h.cot = IEC_COT_ACTIVATION;
    h.orig_addr = 0;
    h.common_addr = 1;
    TEST_ASSERT_EQUAL_size_t(6, pc_iec_asdu_build_header(buf, sizeof(buf), &h));
    TEST_ASSERT_EQUAL_HEX8(0x80 | 1, buf[1]);                         // sq set
    TEST_ASSERT_EQUAL_HEX8(0x80 | 0x40 | IEC_COT_ACTIVATION, buf[2]); // test + negative set
}

// ASDU header parse rejects a null buf and a null out (isolated), and accepts a null consumed.
void test_asdu_header_parse_null_args_and_consumed_null()
{
    uint8_t buf[6] = {0};
    IecAsduHeader g;
    size_t c;
    TEST_ASSERT_FALSE(pc_iec_asdu_parse_header(NULL, 6, &g, &c));  // null buf
    TEST_ASSERT_FALSE(pc_iec_asdu_parse_header(buf, 6, NULL, &c)); // null out
    TEST_ASSERT_TRUE(pc_iec_asdu_parse_header(buf, 6, &g, NULL));  // consumed == NULL accepted
}

// -101 variable-frame build rejects a null buf, and a zero-length ASDU is a legal frame
// carrying only control + address (round-tripped back through parse).
void test_101_build_variable_null_buf_and_zero_len_roundtrip()
{
    uint8_t buf[16];
    const uint8_t asdu[6] = {0};
    TEST_ASSERT_EQUAL_size_t(0, pc_iec101_build_variable(NULL, sizeof(buf), 0, 0, asdu, 6)); // null buf

    size_t n = pc_iec101_build_variable(buf, sizeof(buf), IEC_FC_TEST_LINK, 0x07, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(6 + 2, n);
    TEST_ASSERT_EQUAL_HEX8(2, buf[1]); // L = 2 + 0

    Iec101Frame f;
    TEST_ASSERT_TRUE(pc_iec101_parse(buf, n, &f, NULL)); // consumed == NULL accepted
    TEST_ASSERT_FALSE(f.fixed);
    TEST_ASSERT_EQUAL_UINT8(0, f.asdu_len);
    TEST_ASSERT_NULL(f.asdu);
}

// -101 parse rejects a null 'out'.
void test_101_parse_null_out()
{
    uint8_t buf[5] = {IEC_START_FIXED, 0x49, 0x01, 0x4A, IEC_STOP};
    size_t c;
    TEST_ASSERT_FALSE(pc_iec101_parse(buf, sizeof(buf), NULL, &c));
}

// A fixed frame shorter than 5 octets is rejected; a successful fixed-frame parse accepts a
// null 'consumed'.
void test_101_parse_fixed_too_short_and_consumed_null()
{
    uint8_t short_fixed[4] = {IEC_START_FIXED, 0, 0, 0};
    Iec101Frame f;
    size_t c;
    TEST_ASSERT_FALSE(pc_iec101_parse(short_fixed, sizeof(short_fixed), &f, &c)); // len < 5

    uint8_t buf[8];
    size_t n = pc_iec101_build_fixed(buf, sizeof(buf), IEC_FC_TEST_LINK, 0x02);
    TEST_ASSERT_TRUE(pc_iec101_parse(buf, n, &f, NULL)); // consumed == NULL accepted
    TEST_ASSERT_TRUE(f.fixed);
}

// Variable-frame parse rejects a bad repeated start octet (buf[3]) and a buffer truncated
// before the full frame (6 + L), and rejects a corrupted stop octet with an otherwise-valid
// checksum.
void test_101_parse_variable_bad_second_start_and_truncated_and_bad_stop()
{
    Iec101Frame f;
    size_t c;

    uint8_t bad_second_start[4] = {IEC_START_104, 2, 2, 0x00}; // buf[3] must repeat 0x68
    TEST_ASSERT_FALSE(pc_iec101_parse(bad_second_start, sizeof(bad_second_start), &f, &c));

    uint8_t truncated[6] = {IEC_START_104, 2, 2, IEC_START_104, 0x00, 0x00}; // len < 6 + L
    TEST_ASSERT_FALSE(pc_iec101_parse(truncated, sizeof(truncated), &f, &c));

    const uint8_t asdu[3] = {0x01, 0x02, 0x03};
    uint8_t buf[32];
    size_t n = pc_iec101_build_variable(buf, sizeof(buf), IEC_FC_USER_DATA_NOREPLY, 0x03, asdu, 3);
    buf[n - 1] ^= 0xFF; // corrupt only the stop octet; checksum stays valid
    TEST_ASSERT_FALSE(pc_iec101_parse(buf, n, &f, &c));
}

// --- typed information objects ---
void test_io_single_point()
{
    uint8_t buf[8];
    size_t n = pc_iec_io_build_sp(buf, sizeof(buf), 0x001234, PROTO_TRUE, IEC_QUAL_IV);
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[0]); // IOA little-endian
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x81, buf[3]); // SPI (0x01) | IV (0x80)

    uint32_t ioa;
    proto_bool on;
    uint8_t q;
    TEST_ASSERT_TRUE(pc_iec_io_parse_sp(buf, n, &ioa, &on, &q));
    TEST_ASSERT_EQUAL_HEX32(0x1234, ioa);
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_EQUAL_HEX8(IEC_QUAL_IV, q);
    TEST_ASSERT_EQUAL_size_t(0, pc_iec_io_build_sp(buf, 3, 0, PROTO_FALSE, 0)); // too small
    TEST_ASSERT_FALSE(pc_iec_io_parse_sp(buf, 3, &ioa, &on, &q));
}

void test_io_measured_float()
{
    uint8_t buf[16];
    size_t n = pc_iec_io_build_float(buf, sizeof(buf), 100, 23.5f, IEC_QUAL_NT);
    TEST_ASSERT_EQUAL_size_t(8, n);
    uint32_t ioa;
    float v;
    uint8_t qds;
    TEST_ASSERT_TRUE(pc_iec_io_parse_float(buf, n, &ioa, &v, &qds));
    TEST_ASSERT_EQUAL_UINT32(100, ioa);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.5f, v);
    TEST_ASSERT_EQUAL_HEX8(IEC_QUAL_NT, qds);
    // A negative value round-trips through the IEEE-754 bytes.
    pc_iec_io_build_float(buf, sizeof(buf), 100, -0.125f, 0);
    TEST_ASSERT_TRUE(pc_iec_io_parse_float(buf, 8, NULL, &v, NULL));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.125f, v);
    TEST_ASSERT_EQUAL_size_t(0, pc_iec_io_build_float(buf, 7, 0, 1.0f, 0)); // too small
    TEST_ASSERT_FALSE(pc_iec_io_parse_float(buf, 7, &ioa, &v, &qds));
}

void test_io_measured_normalized()
{
    uint8_t buf[16];
    // M_ME_NA_1: IOA(3) + signed 16-bit NVA (LE) + QDS(1); 0.5 -> 0.5*32768 = 16384 = 0x4000 -> bytes 00 40.
    size_t n = pc_iec_io_build_normalized(buf, sizeof(buf), 200, 0.5f, IEC_QUAL_NT);
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]); // NVA low
    TEST_ASSERT_EQUAL_HEX8(0x40, buf[4]); // NVA high
    uint32_t ioa;
    float v;
    uint8_t qds;
    TEST_ASSERT_TRUE(pc_iec_io_parse_normalized(buf, n, &ioa, &v, &qds));
    TEST_ASSERT_EQUAL_UINT32(200, ioa);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, v);
    TEST_ASSERT_EQUAL_HEX8(IEC_QUAL_NT, qds);

    // A negative fraction round-trips; the value clamps to the signed-16-bit range at the extremes.
    pc_iec_io_build_normalized(buf, sizeof(buf), 200, -0.5f, 0);
    TEST_ASSERT_TRUE(pc_iec_io_parse_normalized(buf, 6, NULL, &v, NULL));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.5f, v);
    pc_iec_io_build_normalized(buf, sizeof(buf), 200, 2.0f, 0); // over-range clamps to +32767
    pc_iec_io_parse_normalized(buf, 6, NULL, &v, NULL);
    TEST_ASSERT_TRUE(v > 0.999f);
    TEST_ASSERT_EQUAL_size_t(0, pc_iec_io_build_normalized(buf, 5, 0, 0.5f, 0)); // too small
    TEST_ASSERT_FALSE(pc_iec_io_parse_normalized(buf, 5, &ioa, &v, &qds));
}

void test_io_measured_scaled()
{
    uint8_t buf[16];
    // M_ME_NB_1: IOA(3) + signed 16-bit SVA (LE) + QDS(1); 12345 = 0x3039 -> bytes 39 30.
    size_t n = pc_iec_io_build_scaled(buf, sizeof(buf), 200, 12345, IEC_QUAL_NT);
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_HEX8(0x39, buf[3]); // SVA low
    TEST_ASSERT_EQUAL_HEX8(0x30, buf[4]); // SVA high
    uint32_t ioa;
    int16_t v;
    uint8_t qds;
    TEST_ASSERT_TRUE(pc_iec_io_parse_scaled(buf, n, &ioa, &v, &qds));
    TEST_ASSERT_EQUAL_UINT32(200, ioa);
    TEST_ASSERT_EQUAL_INT16(12345, v);
    TEST_ASSERT_EQUAL_HEX8(IEC_QUAL_NT, qds);
    // A negative value round-trips through the two's-complement bytes.
    pc_iec_io_build_scaled(buf, sizeof(buf), 200, -1000, 0);
    TEST_ASSERT_TRUE(pc_iec_io_parse_scaled(buf, 6, NULL, &v, NULL));
    TEST_ASSERT_EQUAL_INT16(-1000, v);
    TEST_ASSERT_EQUAL_size_t(0, pc_iec_io_build_scaled(buf, 5, 0, 1, 0)); // too small
    TEST_ASSERT_FALSE(pc_iec_io_parse_scaled(buf, 5, &ioa, &v, &qds));
}

void test_io_integrated_totals()
{
    uint8_t buf[16];
    // M_IT_NA_1: IOA(3) + BCR = signed 32-bit counter (LE) + sequence-notation octet.
    // 0x12345678 = 305419896 -> bytes 78 56 34 12; seq = SQ 5 + CY (0x20) -> 0x25.
    size_t n = pc_iec_io_build_counter(buf, sizeof(buf), 300, 0x12345678, (uint8_t)(5u | IEC_BCR_CY));
    TEST_ASSERT_EQUAL_size_t(8, n);
    TEST_ASSERT_EQUAL_HEX8(0x78, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x56, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0x25, buf[7]);
    uint32_t ioa;
    int32_t v;
    uint8_t seq;
    TEST_ASSERT_TRUE(pc_iec_io_parse_counter(buf, n, &ioa, &v, &seq));
    TEST_ASSERT_EQUAL_UINT32(300, ioa);
    TEST_ASSERT_EQUAL_INT32(0x12345678, v);
    TEST_ASSERT_EQUAL_UINT8(5, seq & IEC_BCR_SQ_MASK);
    TEST_ASSERT_TRUE((seq & IEC_BCR_CY) != 0);
    TEST_ASSERT_TRUE((seq & IEC_BCR_CA) == 0);
    TEST_ASSERT_TRUE((seq & IEC_BCR_IV) == 0);
    // A negative counter round-trips through the two's-complement bytes; the adjusted + invalid flags set.
    pc_iec_io_build_counter(buf, sizeof(buf), 300, -2000000000, (uint8_t)(IEC_BCR_IV | IEC_BCR_CA));
    TEST_ASSERT_TRUE(pc_iec_io_parse_counter(buf, 8, NULL, &v, &seq));
    TEST_ASSERT_EQUAL_INT32(-2000000000, v);
    TEST_ASSERT_TRUE((seq & IEC_BCR_IV) != 0);
    TEST_ASSERT_TRUE((seq & IEC_BCR_CA) != 0);
    TEST_ASSERT_EQUAL_size_t(0, pc_iec_io_build_counter(buf, 7, 0, 1, 0)); // too small
    TEST_ASSERT_FALSE(pc_iec_io_parse_counter(buf, 7, &ioa, &v, &seq));
}

void test_io_single_command_in_asdu()
{
    // Assemble a C_SC_NA_1 ASDU: the 6-octet header + one single-command object (select, ON).
    IecAsduHeader h;
    memset(&h, 0, sizeof(h));
    h.type_id = IEC_TYPE_C_SC_NA_1;
    h.count = 1;
    h.cot = 6; // activation
    h.common_addr = 1;
    uint8_t buf[32];
    size_t p = pc_iec_asdu_build_header(buf, sizeof(buf), &h);
    TEST_ASSERT_TRUE(p > 0);
    size_t io = pc_iec_io_build_sc(buf + p, sizeof(buf) - p, 0x00000A, PROTO_TRUE, PROTO_TRUE);
    TEST_ASSERT_EQUAL_size_t(4, io);
    p += io;

    IecAsduHeader g;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(pc_iec_asdu_parse_header(buf, p, &g, &consumed));
    TEST_ASSERT_EQUAL_UINT8(IEC_TYPE_C_SC_NA_1, g.type_id);
    uint32_t ioa;
    proto_bool on, sel;
    TEST_ASSERT_TRUE(pc_iec_io_parse_sc(buf + consumed, p - consumed, &ioa, &on, &sel));
    TEST_ASSERT_EQUAL_HEX32(0x0A, ioa);
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_TRUE(sel);
    // An execute (not select) with OFF clears both flags.
    pc_iec_io_build_sc(buf, sizeof(buf), 0x0A, PROTO_FALSE, PROTO_FALSE);
    pc_iec_io_parse_sc(buf, 4, &ioa, &on, &sel);
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_FALSE(sel);
}

void test_io_double_point()
{
    uint8_t buf[8];
    // DPI = ON (2), quality = not-topical.
    size_t n = pc_iec_io_build_dp(buf, sizeof(buf), 0x001234, IEC_DP_ON, IEC_QUAL_NT);
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[0]); // IOA little-endian
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x42, buf[3]); // DPI ON (0x02) | NT (0x40)

    uint32_t ioa;
    uint8_t dpi, q;
    TEST_ASSERT_TRUE(pc_iec_io_parse_dp(buf, n, &ioa, &dpi, &q));
    TEST_ASSERT_EQUAL_HEX32(0x1234, ioa);
    TEST_ASSERT_EQUAL_UINT8(IEC_DP_ON, dpi);
    TEST_ASSERT_EQUAL_HEX8(IEC_QUAL_NT, q);

    // An OFF value with an invalid flag round-trips, and the reserved bits 2..3 do not leak into the value.
    pc_iec_io_build_dp(buf, sizeof(buf), 0x0A, IEC_DP_OFF, IEC_QUAL_IV);
    TEST_ASSERT_TRUE(pc_iec_io_parse_dp(buf, 4, &ioa, &dpi, &q));
    TEST_ASSERT_EQUAL_UINT8(IEC_DP_OFF, dpi);
    TEST_ASSERT_EQUAL_HEX8(IEC_QUAL_IV, q);
    TEST_ASSERT_EQUAL_size_t(0, pc_iec_io_build_dp(buf, 3, 0, IEC_DP_ON, 0)); // too small
    TEST_ASSERT_FALSE(pc_iec_io_parse_dp(buf, 3, &ioa, &dpi, &q));
}

void test_io_double_command_in_asdu()
{
    // Assemble a C_DC_NA_1 ASDU: the 6-octet header + one double-command object (select, ON, QU 3).
    IecAsduHeader h;
    memset(&h, 0, sizeof(h));
    h.type_id = IEC_TYPE_C_DC_NA_1;
    h.count = 1;
    h.cot = 6; // activation
    h.common_addr = 1;
    uint8_t buf[32];
    size_t p = pc_iec_asdu_build_header(buf, sizeof(buf), &h);
    TEST_ASSERT_TRUE(p > 0);
    size_t io = pc_iec_io_build_dc(buf + p, sizeof(buf) - p, 0x00000A, IEC_DP_ON, 3, PROTO_TRUE);
    TEST_ASSERT_EQUAL_size_t(4, io);
    // DCO = DCS ON (0x02) | QU 3 << 2 (0x0C) | S/E (0x80) = 0x8E.
    TEST_ASSERT_EQUAL_HEX8(0x8E, buf[p + 3]);
    p += io;

    IecAsduHeader g;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(pc_iec_asdu_parse_header(buf, p, &g, &consumed));
    TEST_ASSERT_EQUAL_UINT8(IEC_TYPE_C_DC_NA_1, g.type_id);
    uint32_t ioa;
    uint8_t dcs, qu;
    proto_bool sel;
    TEST_ASSERT_TRUE(pc_iec_io_parse_dc(buf + consumed, p - consumed, &ioa, &dcs, &qu, &sel));
    TEST_ASSERT_EQUAL_HEX32(0x0A, ioa);
    TEST_ASSERT_EQUAL_UINT8(IEC_DP_ON, dcs);
    TEST_ASSERT_EQUAL_UINT8(3, qu);
    TEST_ASSERT_TRUE(sel);

    // An execute (not select) with OFF and QU 0 clears the flag and qualifier.
    pc_iec_io_build_dc(buf, sizeof(buf), 0x0A, IEC_DP_OFF, 0, PROTO_FALSE);
    pc_iec_io_parse_dc(buf, 4, &ioa, &dcs, &qu, &sel);
    TEST_ASSERT_EQUAL_UINT8(IEC_DP_OFF, dcs);
    TEST_ASSERT_EQUAL_UINT8(0, qu);
    TEST_ASSERT_FALSE(sel);
    TEST_ASSERT_EQUAL_size_t(0, pc_iec_io_build_dc(buf, 3, 0, IEC_DP_ON, 0, PROTO_FALSE)); // too small
    TEST_ASSERT_FALSE(pc_iec_io_parse_dc(buf, 3, &ioa, &dcs, &qu, &sel));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_104_i_format_roundtrip);
    RUN_TEST(test_104_s_format);
    RUN_TEST(test_104_u_format);
    RUN_TEST(test_104_sequence_numbers_15bit);
    RUN_TEST(test_asdu_header_roundtrip);
    RUN_TEST(test_ioa_roundtrip);
    RUN_TEST(test_101_fixed_frame);
    RUN_TEST(test_101_variable_frame_roundtrip);
    RUN_TEST(test_104_build_guards);
    RUN_TEST(test_104_parse_rejects);
    RUN_TEST(test_asdu_ioa_guards);
    RUN_TEST(test_101_build_guards);
    RUN_TEST(test_101_parse_rejects);
    RUN_TEST(test_104_parse_null_out_and_too_short);
    RUN_TEST(test_104_parse_consumed_null);
    RUN_TEST(test_asdu_header_build_null_h_and_flag_branches);
    RUN_TEST(test_asdu_header_parse_null_args_and_consumed_null);
    RUN_TEST(test_101_build_variable_null_buf_and_zero_len_roundtrip);
    RUN_TEST(test_101_parse_null_out);
    RUN_TEST(test_101_parse_fixed_too_short_and_consumed_null);
    RUN_TEST(test_101_parse_variable_bad_second_start_and_truncated_and_bad_stop);
    RUN_TEST(test_io_single_point);
    RUN_TEST(test_io_measured_float);
    RUN_TEST(test_io_measured_scaled);
    RUN_TEST(test_io_measured_normalized);
    RUN_TEST(test_io_integrated_totals);
    RUN_TEST(test_io_single_command_in_asdu);
    RUN_TEST(test_io_double_point);
    RUN_TEST(test_io_double_command_in_asdu);
    return UNITY_END();
}
