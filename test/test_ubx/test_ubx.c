// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the UBX codec (services/timing_position/ubx): the 8-bit Fletcher checksum, frame build / poll,
// one-shot parse (sync + length + checksum validation), the ACK + little-endian readers, and the
// streaming NMEA/UBX demultiplexer. Independent known-answer vectors are the published u-blox poll
// frames UBX-MON-VER (B5 62 0A 04 00 00 0E 34) and UBX-CFG-PRT (B5 62 06 00 00 00 06 18). Pure host
// tests.

#include "services/timing_position/ubx/ubx.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// -- checksum + build (external KAT: the MON-VER / CFG-PRT poll frames) --

void test_checksum_known_vector()
{
    const uint8_t body[] = {0x0A, 0x04, 0x00, 0x00}; // MON-VER poll: class id len
    uint8_t a = 0, b = 0;
    pc_ubx_checksum(body, sizeof(body), &a, &b);
    TEST_ASSERT_EQUAL_HEX8(0x0E, a);
    TEST_ASSERT_EQUAL_HEX8(0x34, b);
}

void test_build_poll_mon_ver()
{
    const uint8_t want[] = {0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};
    uint8_t buf[16];
    size_t n = pc_ubx_build_poll(buf, sizeof(buf), 0x0A, 0x04);
    TEST_ASSERT_EQUAL_UINT(sizeof(want), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, sizeof(want));
}

void test_build_poll_cfg_prt()
{
    const uint8_t want[] = {0xB5, 0x62, 0x06, 0x00, 0x00, 0x00, 0x06, 0x18};
    uint8_t buf[16];
    size_t n = pc_ubx_build_poll(buf, sizeof(buf), 0x06, 0x00);
    TEST_ASSERT_EQUAL_UINT(sizeof(want), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, sizeof(want));
}

void test_build_with_payload()
{
    // UBX-CFG-MSG set-rate: enable UBX-NAV-PVT (01 07) at rate 1.
    const uint8_t pay[] = {0x01, 0x07, 0x01};
    uint8_t buf[32];
    size_t n = pc_ubx_build(buf, sizeof(buf), 0x06, 0x01, pay, sizeof(pay));
    TEST_ASSERT_EQUAL_UINT(8u + sizeof(pay), n);
    TEST_ASSERT_EQUAL_HEX8(0xB5, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x62, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x06, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[3]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)sizeof(pay), buf[4]); // len LE low
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[5]);                 // len LE high
    // Independent checksum over class..payload end.
    uint8_t a = 0, b = 0;
    pc_ubx_checksum(buf + 2, sizeof(pay) + 4u, &a, &b);
    TEST_ASSERT_EQUAL_HEX8(a, buf[n - 2]);
    TEST_ASSERT_EQUAL_HEX8(b, buf[n - 1]);
}

void test_build_rejects_bad_args()
{
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_UINT(0, pc_ubx_build(NULL, sizeof(buf), 0, 0, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0, pc_ubx_build(buf, 4, 0, 0, NULL, 0)); // cap too small
    const uint8_t p[] = {1};
    TEST_ASSERT_EQUAL_UINT(0, pc_ubx_build(buf, sizeof(buf), 0, 0, NULL, 1)); // len>0, null payload
    TEST_ASSERT_EQUAL_UINT(0, pc_ubx_build(buf, sizeof(buf), 0, 0, p, 1));       // needs 9, cap 8
}

// -- one-shot parse --

void test_parse_roundtrip()
{
    const uint8_t pay[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[32];
    size_t n = pc_ubx_build(buf, sizeof(buf), 0x01, 0x07, pay, sizeof(pay));
    pc_ubx m;
    TEST_ASSERT_TRUE(pc_ubx_parse(buf, n, &m));
    TEST_ASSERT_EQUAL_HEX8(0x01, m.cls);
    TEST_ASSERT_EQUAL_HEX8(0x07, m.id);
    TEST_ASSERT_EQUAL_UINT16(sizeof(pay), m.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pay, m.payload, sizeof(pay));
}

void test_parse_rejects()
{
    uint8_t buf[16];
    size_t n = pc_ubx_build(buf, sizeof(buf), 0x0A, 0x04, NULL, 0);
    pc_ubx m;
    TEST_ASSERT_FALSE(pc_ubx_parse(buf, 7, &m));     // too short
    TEST_ASSERT_FALSE(pc_ubx_parse(NULL, n, &m)); // null
    uint8_t bad_sync[8];
    memcpy(bad_sync, buf, 8);
    bad_sync[1] = 0x63; // wrong sync2
    TEST_ASSERT_FALSE(pc_ubx_parse(bad_sync, 8, &m));
    uint8_t bad_ck[8];
    memcpy(bad_ck, buf, 8);
    bad_ck[7] ^= 0xFF; // corrupt CK_B
    TEST_ASSERT_FALSE(pc_ubx_parse(bad_ck, 8, &m));
    // Declared length longer than the buffer.
    uint8_t long_len[8];
    memcpy(long_len, buf, 8);
    long_len[4] = 0x10; // claim 16-byte payload but only 8 bytes present
    TEST_ASSERT_FALSE(pc_ubx_parse(long_len, 8, &m));
}

// -- ACK + LE readers --

void test_ack()
{
    uint8_t buf[16];
    const uint8_t acked[] = {0x06, 0x01};
    uint8_t ac = 0, ai = 0;
    // ACK-ACK (05 01)
    size_t n = pc_ubx_build(buf, sizeof(buf), 0x05, 0x01, acked, 2);
    pc_ubx m;
    TEST_ASSERT_TRUE(pc_ubx_parse(buf, n, &m));
    TEST_ASSERT_EQUAL_INT(1, pc_ubx_ack(&m, &ac, &ai));
    TEST_ASSERT_EQUAL_HEX8(0x06, ac);
    TEST_ASSERT_EQUAL_HEX8(0x01, ai);
    // ACK-NAK (05 00)
    n = pc_ubx_build(buf, sizeof(buf), 0x05, 0x00, acked, 2);
    TEST_ASSERT_TRUE(pc_ubx_parse(buf, n, &m));
    TEST_ASSERT_EQUAL_INT(0, pc_ubx_ack(&m, NULL, NULL));
    // Not an ACK: wrong class.
    n = pc_ubx_build(buf, sizeof(buf), 0x01, 0x07, acked, 2);
    TEST_ASSERT_TRUE(pc_ubx_parse(buf, n, &m));
    TEST_ASSERT_EQUAL_INT(-1, pc_ubx_ack(&m, NULL, NULL));
    // Class 05 but unknown id, and too-short payload.
    n = pc_ubx_build(buf, sizeof(buf), 0x05, 0x02, acked, 2);
    TEST_ASSERT_TRUE(pc_ubx_parse(buf, n, &m));
    TEST_ASSERT_EQUAL_INT(-1, pc_ubx_ack(&m, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(-1, pc_ubx_ack(NULL, NULL, NULL));
}

void test_le_readers()
{
    const uint8_t p[] = {0x78, 0x56, 0x34, 0x12};
    TEST_ASSERT_EQUAL_HEX16(0x5678, pc_ubx_u16(p, 0));
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, pc_ubx_u32(p, 0));
    const uint8_t neg[] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_INT32(-1, pc_ubx_i32(neg, 0));
}

// -- streaming demultiplexer --

// Feed a byte buffer through the demux; collect passthrough bytes and the last decoded frame.
static int drive(pc_ubx_stream *st, const uint8_t *in, size_t n, char *pass, size_t *pass_n, pc_ubx *last,
                 int *overflow)
{
    int frames = 0;
    *pass_n = 0;
    *overflow = 0;
    for (size_t i = 0; i < n; i++)
    {
        uint8_t pb = 0;
        int r = pc_ubx_stream_feed(st, in[i], last, &pb);
        if (r == PC_UBX_FRAME)
        {
            frames++;
        }
        else if (r == PC_UBX_PASSTHROUGH)
        {
            pass[(*pass_n)++] = (char)pb;
        }
        else if (r == PC_UBX_OVERFLOW)
        {
            (*overflow)++;
        }
    }
    pass[*pass_n] = 0;
    return frames;
}

void test_stream_demux_mixed()
{
    uint8_t frame[16];
    const uint8_t acked[] = {0x06, 0x01};
    size_t fn = pc_ubx_build(frame, sizeof(frame), 0x05, 0x01, acked, 2);

    // ASCII NMEA, then a UBX frame, then more ASCII.
    uint8_t stream[128];
    size_t sn = 0;
    const char *pre = "$GPGGA,1\r\n";
    const char *post = "$GPRMC,2\r\n";
    for (const char *c = pre; *c; c++)
    {
        stream[sn++] = (uint8_t)*c;
    }
    memcpy(stream + sn, frame, fn);
    sn += fn;
    for (const char *c = post; *c; c++)
    {
        stream[sn++] = (uint8_t)*c;
    }

    pc_ubx_stream st;
    pc_ubx_stream_init(&st);
    char pass[128];
    size_t pn = 0;
    pc_ubx last;
    int ov = 0;
    int frames = drive(&st, stream, sn, pass, &pn, &last, &ov);

    TEST_ASSERT_EQUAL_INT(1, frames);
    TEST_ASSERT_EQUAL_INT(0, ov);
    TEST_ASSERT_EQUAL_HEX8(0x05, last.cls);
    TEST_ASSERT_EQUAL_HEX8(0x01, last.id);
    TEST_ASSERT_EQUAL_UINT16(2, last.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(acked, last.payload, 2);
    // Every non-UBX byte passed through, reconstructing the two NMEA lines verbatim.
    char want[64];
    snprintf(want, sizeof(want), "%s%s", pre, post);
    TEST_ASSERT_EQUAL_STRING(want, pass);
}

void test_stream_bad_checksum_resyncs()
{
    uint8_t frame[16];
    size_t fn = pc_ubx_build(frame, sizeof(frame), 0x0A, 0x04, NULL, 0);
    frame[fn - 1] ^= 0xFF; // corrupt CK_B

    pc_ubx_stream st;
    pc_ubx_stream_init(&st);
    char pass[64];
    size_t pn = 0;
    pc_ubx last;
    int ov = 0;
    int frames = drive(&st, frame, fn, pass, &pn, &last, &ov);
    TEST_ASSERT_EQUAL_INT(0, frames); // bad checksum: no frame emitted
    TEST_ASSERT_EQUAL_INT(0, ov);

    // After the bad frame the demux must still accept a good one.
    uint8_t good[16];
    size_t gn = pc_ubx_build(good, sizeof(good), 0x0A, 0x04, NULL, 0);
    frames = drive(&st, good, gn, pass, &pn, &last, &ov);
    TEST_ASSERT_EQUAL_INT(1, frames);
}

void test_stream_overflow_skips()
{
    // Hand-craft a frame declaring a payload length above PC_UBX_MAX_PAYLOAD.
    uint8_t big[8];
    big[0] = PC_UBX_SYNC1;
    big[1] = PC_UBX_SYNC2;
    big[2] = 0x01;
    big[3] = 0x02;
    uint16_t big_len = (uint16_t)(PC_UBX_MAX_PAYLOAD + 1);
    big[4] = (uint8_t)(big_len & 0xFF);
    big[5] = (uint8_t)(big_len >> 8);
    // payload + checksum are just discarded; supply that many filler bytes.
    uint8_t stream[8 + PC_UBX_MAX_PAYLOAD + 1 + 2];
    memcpy(stream, big, 6);
    size_t sn = 6;
    for (uint16_t i = 0; i < big_len + 2; i++)
    {
        stream[sn++] = 0xAA;
    }

    pc_ubx_stream st;
    pc_ubx_stream_init(&st);
    char pass[8];
    size_t pn = 0;
    pc_ubx last;
    int ov = 0;
    int frames = drive(&st, stream, sn, pass, &pn, &last, &ov);
    TEST_ASSERT_EQUAL_INT(0, frames);
    TEST_ASSERT_EQUAL_INT(1, ov); // reported exactly one overflow, then resynced

    // A good frame right after is still parsed.
    uint8_t good[16];
    size_t gn = pc_ubx_build(good, sizeof(good), 0x0A, 0x04, NULL, 0);
    frames = drive(&st, good, gn, pass, &pn, &last, &ov);
    TEST_ASSERT_EQUAL_INT(1, frames);
}

void test_stream_false_and_double_sync()
{
    pc_ubx_stream st;
    pc_ubx_stream_init(&st);
    char pass[16];
    size_t pn = 0;
    pc_ubx last;
    int ov = 0;
    // 0xB5 followed by a non-0x62 => the stray byte passes through; no frame.
    const uint8_t s1[] = {PC_UBX_SYNC1, 'A'};
    int frames = drive(&st, s1, sizeof(s1), pass, &pn, &last, &ov);
    TEST_ASSERT_EQUAL_INT(0, frames);
    TEST_ASSERT_EQUAL_STRING("A", pass);

    // 0xB5 0xB5 0x62 ... : the doubled sync1 must still open a frame.
    uint8_t frame[8];
    size_t fn = pc_ubx_build(frame, sizeof(frame), 0x0A, 0x04, NULL, 0);
    uint8_t doubled[16];
    doubled[0] = PC_UBX_SYNC1;
    memcpy(doubled + 1, frame, fn); // frame already starts with SYNC1 SYNC2 ...
    pc_ubx_stream_init(&st);
    frames = drive(&st, doubled, fn + 1, pass, &pn, &last, &ov);
    TEST_ASSERT_EQUAL_INT(1, frames);
    TEST_ASSERT_EQUAL_INT(0, (int)pn); // nothing passed through
}

void test_stream_null_safe()
{
    TEST_ASSERT_EQUAL_INT(PC_UBX_NONE, pc_ubx_stream_feed(NULL, 0x00, NULL, NULL));
    pc_ubx_stream_init(NULL); // must not crash
}

// A full UBX-NAV-PVT frame from a Python u-blox-8 reference (San Francisco-ish fix, 2026-07-24 12:30:45).
static const uint8_t navpvt_frame[100] = {
    0xb5, 0x62, 0x01, 0x07, 0x5c, 0x00, 0x15, 0xcd, 0x5b, 0x07, 0xea, 0x07, 0x07, 0x18, 0x0c, 0x1e, 0x2d,
    0x07, 0x19, 0x00, 0x00, 0x00, 0x20, 0xa1, 0x07, 0x00, 0x03, 0x01, 0x00, 0x09, 0xd8, 0x71, 0x3b, 0xb7,
    0x08, 0xf5, 0x46, 0x16, 0x24, 0x77, 0x00, 0x00, 0x60, 0x6d, 0x00, 0x00, 0xb0, 0x04, 0x00, 0x00, 0x08,
    0x07, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0xce, 0xff, 0xff, 0xff, 0x05, 0x00, 0x00, 0x00, 0x70, 0x00,
    0x00, 0x00, 0x20, 0xaa, 0x44, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x80, 0x84, 0x1e, 0x00, 0xb4, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x69, 0xbe};

void test_nav_pvt_decode()
{
    pc_ubx m;
    TEST_ASSERT_TRUE(pc_ubx_parse(navpvt_frame, sizeof(navpvt_frame), &m));
    TEST_ASSERT_EQUAL_HEX8(PC_UBX_CLASS_NAV, m.cls);
    TEST_ASSERT_EQUAL_HEX8(PC_UBX_NAV_PVT, m.id);

    pc_ubx_nav_pvt pvt;
    TEST_ASSERT_TRUE(pc_ubx_parse_nav_pvt(&m, &pvt));
    TEST_ASSERT_EQUAL_UINT32(123456789u, pvt.itow_ms);
    TEST_ASSERT_EQUAL_UINT16(2026, pvt.year);
    TEST_ASSERT_EQUAL_UINT8(7, pvt.month);
    TEST_ASSERT_EQUAL_UINT8(24, pvt.day);
    TEST_ASSERT_EQUAL_UINT8(12, pvt.hour);
    TEST_ASSERT_EQUAL_UINT8(30, pvt.minute);
    TEST_ASSERT_EQUAL_UINT8(45, pvt.second);
    TEST_ASSERT_EQUAL_HEX8(0x07, pvt.valid);
    TEST_ASSERT_EQUAL_INT32(500000, pvt.nano);
    TEST_ASSERT_EQUAL_UINT32(25, pvt.time_acc_ns);
    TEST_ASSERT_EQUAL_UINT8(PC_UBX_FIX_3D, pvt.fix_type);
    TEST_ASSERT_TRUE(pvt.flags & PC_UBX_PVT_FIX_OK);
    TEST_ASSERT_EQUAL_UINT8(9, pvt.num_sv);
    TEST_ASSERT_EQUAL_INT32(-1220841000, pvt.lon_1e7); // -122.0841 deg
    TEST_ASSERT_EQUAL_INT32(373749000, pvt.lat_1e7);   // 37.3749 deg
    TEST_ASSERT_EQUAL_INT32(30500, pvt.height_mm);
    TEST_ASSERT_EQUAL_INT32(28000, pvt.hmsl_mm);
    TEST_ASSERT_EQUAL_UINT32(1200, pvt.h_acc_mm);
    TEST_ASSERT_EQUAL_UINT32(1800, pvt.v_acc_mm);
    TEST_ASSERT_EQUAL_INT32(100, pvt.vel_n_mm_s);
    TEST_ASSERT_EQUAL_INT32(-50, pvt.vel_e_mm_s);
    TEST_ASSERT_EQUAL_INT32(5, pvt.vel_d_mm_s);
    TEST_ASSERT_EQUAL_INT32(112, pvt.gspeed_mm_s);
    TEST_ASSERT_EQUAL_INT32(4500000, pvt.head_mot_1e5);
    TEST_ASSERT_EQUAL_UINT32(30, pvt.s_acc_mm_s);
    TEST_ASSERT_EQUAL_UINT32(2000000, pvt.head_acc_1e5);
    TEST_ASSERT_EQUAL_UINT16(180, pvt.pdop_1e2);
}

void test_nav_pvt_rejects()
{
    pc_ubx_nav_pvt pvt;
    // Wrong class/id: an ACK-ACK frame is not a NAV-PVT.
    uint8_t ackpl[2] = {0x06, 0x01};
    uint8_t ackbuf[16];
    size_t an = pc_ubx_build(ackbuf, sizeof(ackbuf), 0x05, 0x01, ackpl, 2);
    pc_ubx ack;
    TEST_ASSERT_TRUE(pc_ubx_parse(ackbuf, an, &ack));
    TEST_ASSERT_FALSE(pc_ubx_parse_nav_pvt(&ack, &pvt));

    // Right class/id but a short payload (e.g. an older 84-byte protocol) is rejected.
    uint8_t shortpl[84] = {0};
    uint8_t sbuf[128];
    size_t sn = pc_ubx_build(sbuf, sizeof(sbuf), PC_UBX_CLASS_NAV, PC_UBX_NAV_PVT, shortpl, sizeof(shortpl));
    pc_ubx sm;
    TEST_ASSERT_TRUE(pc_ubx_parse(sbuf, sn, &sm));
    TEST_ASSERT_FALSE(pc_ubx_parse_nav_pvt(&sm, &pvt));

    // Null guards.
    TEST_ASSERT_FALSE(pc_ubx_parse_nav_pvt(NULL, &pvt));
    pc_ubx m;
    pc_ubx_parse(navpvt_frame, sizeof(navpvt_frame), &m);
    TEST_ASSERT_FALSE(pc_ubx_parse_nav_pvt(&m, NULL));
}

// A UBX-NAV-SAT frame (2 satellites: a used GPS SV and an unused GLONASS SV) from a Python reference.
static const uint8_t navsat_frame[40] = {0xb5, 0x62, 0x01, 0x35, 0x20, 0x00, 0x15, 0xcd, 0x5b, 0x07,
                                         0x01, 0x02, 0x00, 0x00, 0x00, 0x05, 0x2a, 0x2d, 0xb4, 0x00,
                                         0xfd, 0xff, 0x0c, 0x00, 0x00, 0x00, 0x06, 0x0c, 0x1e, 0xa6,
                                         0x0e, 0x01, 0x0f, 0x00, 0x07, 0x00, 0x00, 0x00, 0xb0, 0xe1};

void test_nav_sat_decode()
{
    pc_ubx m;
    TEST_ASSERT_TRUE(pc_ubx_parse(navsat_frame, sizeof(navsat_frame), &m));
    pc_ubx_nav_sat_hdr hdr;
    TEST_ASSERT_TRUE(pc_ubx_parse_nav_sat(&m, &hdr));
    TEST_ASSERT_EQUAL_UINT32(123456789u, hdr.itow_ms);
    TEST_ASSERT_EQUAL_UINT8(1, hdr.version);
    TEST_ASSERT_EQUAL_UINT8(2, hdr.num_svs);

    pc_ubx_sat s0;
    TEST_ASSERT_TRUE(pc_ubx_nav_sat_get(&m, 0, &s0));
    TEST_ASSERT_EQUAL_UINT8(0, s0.gnss_id); // GPS
    TEST_ASSERT_EQUAL_UINT8(5, s0.sv_id);
    TEST_ASSERT_EQUAL_UINT8(42, s0.cno_dbhz);
    TEST_ASSERT_EQUAL_INT8(45, s0.elev_deg);
    TEST_ASSERT_EQUAL_INT16(180, s0.azim_deg);
    TEST_ASSERT_EQUAL_INT16(-3, s0.pr_res_01m);
    TEST_ASSERT_TRUE(s0.flags & PC_UBX_SAT_USED); // used in the solution
    TEST_ASSERT_EQUAL_UINT32(4, s0.flags & PC_UBX_SAT_QUALITY_MASK);

    pc_ubx_sat s1;
    TEST_ASSERT_TRUE(pc_ubx_nav_sat_get(&m, 1, &s1));
    TEST_ASSERT_EQUAL_UINT8(6, s1.gnss_id); // GLONASS
    TEST_ASSERT_EQUAL_UINT8(12, s1.sv_id);
    TEST_ASSERT_EQUAL_INT8(-90, s1.elev_deg);
    TEST_ASSERT_EQUAL_INT16(270, s1.azim_deg);
    TEST_ASSERT_EQUAL_INT16(15, s1.pr_res_01m);
    TEST_ASSERT_FALSE(s1.flags & PC_UBX_SAT_USED); // not used
    TEST_ASSERT_EQUAL_UINT32(7, s1.flags & PC_UBX_SAT_QUALITY_MASK);

    pc_ubx_sat s2;
    TEST_ASSERT_FALSE(pc_ubx_nav_sat_get(&m, 2, &s2)); // index out of range
}

void test_nav_sat_rejects()
{
    pc_ubx_nav_sat_hdr hdr;
    pc_ubx_sat sat;
    // Wrong class/id (a NAV-PVT frame is not NAV-SAT).
    pc_ubx pvt;
    pc_ubx_parse(navpvt_frame, sizeof(navpvt_frame), &pvt);
    TEST_ASSERT_FALSE(pc_ubx_parse_nav_sat(&pvt, &hdr));

    // A NAV-SAT header that claims 2 satellites but whose payload only holds one block is rejected.
    uint8_t pl[PC_UBX_NAV_SAT_HDR_LEN + PC_UBX_NAV_SAT_ENTRY_LEN] = {0};
    pl[5] = 2; // numSvs = 2, but only room for 1
    uint8_t buf[64];
    size_t n = pc_ubx_build(buf, sizeof(buf), PC_UBX_CLASS_NAV, PC_UBX_NAV_SAT, pl, sizeof(pl));
    pc_ubx sm;
    TEST_ASSERT_TRUE(pc_ubx_parse(buf, n, &sm));
    TEST_ASSERT_FALSE(pc_ubx_parse_nav_sat(&sm, &hdr));
    TEST_ASSERT_FALSE(pc_ubx_nav_sat_get(&sm, 0, &sat));

    // Null guards.
    TEST_ASSERT_FALSE(pc_ubx_parse_nav_sat(NULL, &hdr));
    pc_ubx m;
    pc_ubx_parse(navsat_frame, sizeof(navsat_frame), &m);
    TEST_ASSERT_FALSE(pc_ubx_parse_nav_sat(&m, NULL));
    TEST_ASSERT_FALSE(pc_ubx_nav_sat_get(&m, 0, NULL));
}

// Golden CFG frames from a Python reference: enable NAV-PVT at rate 1; set a 5 Hz nav rate (GPS time).
static const uint8_t cfgmsg_frame[11] = {0xb5, 0x62, 0x06, 0x01, 0x03, 0x00, 0x01, 0x07, 0x01, 0x13, 0x51};
static const uint8_t cfgrate_frame[14] = {0xb5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xc8,
                                          0x00, 0x01, 0x00, 0x01, 0x00, 0xde, 0x6a};

void test_build_cfg_msg()
{
    uint8_t buf[32];
    size_t n = pc_ubx_build_cfg_msg(buf, sizeof(buf), PC_UBX_CLASS_NAV, PC_UBX_NAV_PVT, 1);
    TEST_ASSERT_EQUAL_size_t(sizeof(cfgmsg_frame), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cfgmsg_frame, buf, n);
    // Round-trips through the parser: it is a CFG-MSG whose payload names the message + rate.
    pc_ubx m;
    TEST_ASSERT_TRUE(pc_ubx_parse(buf, n, &m));
    TEST_ASSERT_EQUAL_HEX8(PC_UBX_CLASS_CFG, m.cls);
    TEST_ASSERT_EQUAL_HEX8(PC_UBX_CFG_MSG, m.id);
    TEST_ASSERT_EQUAL_UINT16(3, m.len);
    TEST_ASSERT_EQUAL_HEX8(PC_UBX_CLASS_NAV, m.payload[0]);
    TEST_ASSERT_EQUAL_HEX8(PC_UBX_NAV_PVT, m.payload[1]);
    TEST_ASSERT_EQUAL_HEX8(1, m.payload[2]);
    // A too-small buffer fails closed.
    uint8_t small[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_ubx_build_cfg_msg(small, sizeof(small), PC_UBX_CLASS_NAV, PC_UBX_NAV_PVT, 1));
}

void test_build_cfg_rate()
{
    uint8_t buf[32];
    size_t n = pc_ubx_build_cfg_rate(buf, sizeof(buf), 200, 1, PC_UBX_TIME_REF_GPS); // 5 Hz
    TEST_ASSERT_EQUAL_size_t(sizeof(cfgrate_frame), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cfgrate_frame, buf, n);
    pc_ubx m;
    TEST_ASSERT_TRUE(pc_ubx_parse(buf, n, &m));
    TEST_ASSERT_EQUAL_HEX8(PC_UBX_CFG_RATE, m.id);
    TEST_ASSERT_EQUAL_UINT16(200, pc_ubx_u16(m.payload, 0)); // measRate ms
    TEST_ASSERT_EQUAL_UINT16(1, pc_ubx_u16(m.payload, 2));   // navRate
    TEST_ASSERT_EQUAL_UINT16(PC_UBX_TIME_REF_GPS, pc_ubx_u16(m.payload, 4));
    TEST_ASSERT_EQUAL_size_t(0, pc_ubx_build_cfg_rate(buf, 8, 200, 1, PC_UBX_TIME_REF_GPS)); // too small
}

// A UBX-NAV-TIMEUTC frame from a Python reference (2026-07-24 12:30:45 UTC, all time fields valid).
static const uint8_t timeutc_frame[28] = {0xb5, 0x62, 0x01, 0x21, 0x14, 0x00, 0x15, 0xcd, 0x5b, 0x07,
                                          0x14, 0x00, 0x00, 0x00, 0x20, 0xa1, 0x07, 0x00, 0xea, 0x07,
                                          0x07, 0x18, 0x0c, 0x1e, 0x2d, 0x07, 0xc4, 0x7e};

void test_nav_timeutc_decode()
{
    pc_ubx m;
    TEST_ASSERT_TRUE(pc_ubx_parse(timeutc_frame, sizeof(timeutc_frame), &m));
    pc_ubx_nav_time_utc t;
    TEST_ASSERT_TRUE(pc_ubx_parse_nav_timeutc(&m, &t));
    TEST_ASSERT_EQUAL_UINT32(123456789u, t.itow_ms);
    TEST_ASSERT_EQUAL_UINT32(20, t.time_acc_ns);
    TEST_ASSERT_EQUAL_INT32(500000, t.nano);
    TEST_ASSERT_EQUAL_UINT16(2026, t.year);
    TEST_ASSERT_EQUAL_UINT8(7, t.month);
    TEST_ASSERT_EQUAL_UINT8(24, t.day);
    TEST_ASSERT_EQUAL_UINT8(12, t.hour);
    TEST_ASSERT_EQUAL_UINT8(30, t.minute);
    TEST_ASSERT_EQUAL_UINT8(45, t.second);
    TEST_ASSERT_TRUE(t.valid & PC_UBX_TIMEUTC_VALID_TOW);
    TEST_ASSERT_TRUE(t.valid & PC_UBX_TIMEUTC_VALID_WKN);
    TEST_ASSERT_TRUE(t.utc_valid); // leap seconds resolved

    // Wrong class/id (a NAV-PVT frame), a short payload, and null args are rejected.
    pc_ubx pvt;
    pc_ubx_parse(navpvt_frame, sizeof(navpvt_frame), &pvt);
    TEST_ASSERT_FALSE(pc_ubx_parse_nav_timeutc(&pvt, &t));
    uint8_t shortpl[16] = {0};
    uint8_t buf[64];
    size_t sn = pc_ubx_build(buf, sizeof(buf), PC_UBX_CLASS_NAV, PC_UBX_NAV_TIMEUTC, shortpl, sizeof(shortpl));
    pc_ubx sm;
    pc_ubx_parse(buf, sn, &sm);
    TEST_ASSERT_FALSE(pc_ubx_parse_nav_timeutc(&sm, &t));
    TEST_ASSERT_FALSE(pc_ubx_parse_nav_timeutc(&m, NULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_checksum_known_vector);
    RUN_TEST(test_build_poll_mon_ver);
    RUN_TEST(test_build_poll_cfg_prt);
    RUN_TEST(test_build_with_payload);
    RUN_TEST(test_build_rejects_bad_args);
    RUN_TEST(test_parse_roundtrip);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_ack);
    RUN_TEST(test_le_readers);
    RUN_TEST(test_stream_demux_mixed);
    RUN_TEST(test_stream_bad_checksum_resyncs);
    RUN_TEST(test_stream_overflow_skips);
    RUN_TEST(test_stream_false_and_double_sync);
    RUN_TEST(test_stream_null_safe);
    RUN_TEST(test_nav_pvt_decode);
    RUN_TEST(test_nav_pvt_rejects);
    RUN_TEST(test_nav_sat_decode);
    RUN_TEST(test_nav_sat_rejects);
    RUN_TEST(test_build_cfg_msg);
    RUN_TEST(test_build_cfg_rate);
    RUN_TEST(test_nav_timeutc_decode);
    return UNITY_END();
}
