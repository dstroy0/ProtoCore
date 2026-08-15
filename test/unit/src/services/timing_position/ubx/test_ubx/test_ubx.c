// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the u-blox UBX binary protocol codec (services/timing_position/ubx/ubx.h).
//
// The load-bearing case is test_published_poll_frames. u-blox publishes the poll requests for
// UBX-MON-VER and UBX-CFG-PRT as literal octet strings, and the CFG-RATE / CFG-MSG commands in
// test_published_cfg_frames are likewise the published forms. Each frame's two checksum octets are
// re-derived here from the 8-bit Fletcher definition in the comment that carries it, so neither the
// framing nor the checksum can be wrong and still reproduce them.
//
// The message decoders are anchored on the field offsets the u-blox interface description publishes
// for NAV-PVT (92 octets), NAV-TIMEUTC (20) and NAV-SAT (8 + 12*numSvs): each test plants a distinct
// value at a published offset and asserts the decoder reads it from there.

#include "services/timing_position/ubx/ubx.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static void put_u16(uint8_t *p, size_t off, uint16_t v)
{
    p[off] = (uint8_t)v;
    p[off + 1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, size_t off, uint32_t v)
{
    p[off] = (uint8_t)v;
    p[off + 1] = (uint8_t)(v >> 8);
    p[off + 2] = (uint8_t)(v >> 16);
    p[off + 3] = (uint8_t)(v >> 24);
}

// The two published poll frames, octet for octet.
//
// A poll is the frame with a zero-length payload. The checksum spans class..payload end, so for
// MON-VER that span is 0A 04 00 00 and the 8-bit Fletcher runs:
//   a = 0x0A            b = 0x0A
//   a = 0x0A+0x04=0x0E  b = 0x0A+0x0E=0x18
//   a = 0x0E            b = 0x18+0x0E=0x26
//   a = 0x0E            b = 0x26+0x0E=0x34   -> CK_A 0x0E, CK_B 0x34
// and for CFG-PRT the span 06 00 00 00 gives a = 0x06 throughout and b = 0x06,0x0C,0x12,0x18
//   -> CK_A 0x06, CK_B 0x18.
void test_published_poll_frames(void)
{
    static const uint8_t MON_VER[] = {0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};
    static const uint8_t CFG_PRT[] = {0xB5, 0x62, 0x06, 0x00, 0x00, 0x00, 0x06, 0x18};
    uint8_t buf[16];

    TEST_ASSERT_EQUAL_UINT(sizeof(MON_VER), protocore_ubx_build_poll(buf, sizeof(buf), 0x0A, 0x04));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MON_VER, buf, sizeof(MON_VER));

    TEST_ASSERT_EQUAL_UINT(sizeof(CFG_PRT), protocore_ubx_build_poll(buf, sizeof(buf), 0x06, 0x00));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CFG_PRT, buf, sizeof(CFG_PRT));
}

// The checksum call on its own, over the same MON-VER span derived above.
void test_fletcher_checksum_over_the_published_span(void)
{
    static const uint8_t BODY[] = {0x0A, 0x04, 0x00, 0x00};
    uint8_t a = 0xFF, b = 0xFF;
    protocore_ubx_checksum(BODY, sizeof(BODY), &a, &b);
    TEST_ASSERT_EQUAL_HEX8(0x0E, a);
    TEST_ASSERT_EQUAL_HEX8(0x34, b);

    // An empty span leaves both accumulators at their zero start.
    protocore_ubx_checksum(BODY, 0, &a, &b);
    TEST_ASSERT_EQUAL_HEX8(0x00, a);
    TEST_ASSERT_EQUAL_HEX8(0x00, b);
}

// CFG-RATE at 1 Hz and CFG-MSG enabling NAV-PVT every solution, both published commands.
//
// CFG-RATE payload is measRate(U2) navRate(U2) timeRef(U2) little-endian, so 1000 ms / 1 / GPS is
// E8 03 01 00 01 00. Over the span 06 08 06 00 E8 03 01 00 01 00 the Fletcher accumulators run
//   a: 06 0E 14 14 FC FF 00 00 01 01
//   b: 06 14 28 3C 38 37 37 37 38 39   -> CK_A 0x01, CK_B 0x39
//
// CFG-MSG's short form is msgClass msgID rate, so NAV(0x01) PVT(0x07) at rate 1 is 01 07 01. Over
// the span 06 01 03 00 01 07 01 the accumulators run
//   a: 06 07 0A 0A 0B 12 13
//   b: 06 0D 17 21 2C 3E 51           -> CK_A 0x13, CK_B 0x51
void test_published_cfg_frames(void)
{
    static const uint8_t CFG_RATE_1HZ[] = {0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xE8,
                                           0x03, 0x01, 0x00, 0x01, 0x00, 0x01, 0x39};
    static const uint8_t CFG_MSG_PVT[] = {0xB5, 0x62, 0x06, 0x01, 0x03, 0x00, 0x01, 0x07, 0x01, 0x13, 0x51};
    uint8_t buf[32];

    TEST_ASSERT_EQUAL_UINT(14u, protocore_ubx_build_cfg_rate(buf, sizeof(buf), 1000, 1, PROTOCORE_UBX_TIME_REF_GPS));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CFG_RATE_1HZ, buf, sizeof(CFG_RATE_1HZ));

    TEST_ASSERT_EQUAL_UINT(
        11u, protocore_ubx_build_cfg_msg(buf, sizeof(buf), PROTOCORE_UBX_CLASS_NAV, PROTOCORE_UBX_NAV_PVT, 1));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CFG_MSG_PVT, buf, sizeof(CFG_MSG_PVT));
}

// The header is sync1 sync2 class id then a little-endian U2 length: 0x0102 is 02 01 on the wire.
void test_length_field_is_little_endian(void)
{
    uint8_t payload[258];
    uint8_t buf[512];
    memset(payload, 0x5A, sizeof(payload));

    size_t n = protocore_ubx_build(buf, sizeof(buf), 0x01, 0x07, payload, (uint16_t)sizeof(payload));
    TEST_ASSERT_EQUAL_UINT(8u + sizeof(payload), n);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_UBX_SYNC1, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_UBX_SYNC2, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x07, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[4]); // 258 = 0x0102, low octet first
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[5]);
}

// What build produces, parse accepts, with every header field and the payload recovered.
void test_build_parse_round_trip(void)
{
    static const uint8_t PL[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00};
    uint8_t buf[32];
    protocore_ubx m;

    size_t n = protocore_ubx_build(buf, sizeof(buf), 0x13, 0x40, PL, sizeof(PL));
    TEST_ASSERT_EQUAL_UINT(13u, n);
    TEST_ASSERT_TRUE(protocore_ubx_parse(buf, n, &m));
    TEST_ASSERT_EQUAL_HEX8(0x13, m.cls);
    TEST_ASSERT_EQUAL_HEX8(0x40, m.id);
    TEST_ASSERT_EQUAL_UINT16(sizeof(PL), m.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PL, m.payload, sizeof(PL));
    TEST_ASSERT_EQUAL_PTR(buf + 6, m.payload); // aliases the caller's buffer, never copied
}

// Every octet of a valid frame is load bearing: flipping any single bit must make parse refuse it.
void test_parse_refuses_a_corrupted_frame(void)
{
    static const uint8_t PL[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t good[12];
    uint8_t bad[12];
    protocore_ubx m;

    size_t n = protocore_ubx_build(good, sizeof(good), 0x0A, 0x04, PL, sizeof(PL));
    TEST_ASSERT_EQUAL_UINT(sizeof(good), n);

    for (size_t i = 0; i < n; i++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            memcpy(bad, good, n);
            bad[i] ^= (uint8_t)(1u << bit);
            TEST_ASSERT_FALSE(protocore_ubx_parse(bad, n, &m));
        }
    }
}

// Short, unsynchronized and truncated inputs are refused rather than read past.
void test_parse_refuses_malformed_input(void)
{
    static const uint8_t PL[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t buf[12];
    protocore_ubx m;
    size_t n = protocore_ubx_build(buf, sizeof(buf), 0x0A, 0x04, PL, sizeof(PL));

    for (size_t shorter = 0; shorter < n; shorter++)
    {
        TEST_ASSERT_FALSE(protocore_ubx_parse(buf, shorter, &m));
    }
    TEST_ASSERT_FALSE(protocore_ubx_parse(NULL, n, &m));
    TEST_ASSERT_FALSE(protocore_ubx_parse(buf, n, NULL));
}

// A frame longer than the buffer is refused, and a null payload is legal only at length 0.
void test_build_bounds(void)
{
    static const uint8_t PL[4] = {1, 2, 3, 4};
    uint8_t buf[11]; // one short of 8 + 4
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ubx_build(buf, sizeof(buf), 0x0A, 0x04, PL, sizeof(PL)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ubx_build(NULL, 16, 0x0A, 0x04, PL, sizeof(PL)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_ubx_build(buf, sizeof(buf), 0x0A, 0x04, NULL, 4));
    TEST_ASSERT_EQUAL_UINT(8u, protocore_ubx_build(buf, sizeof(buf), 0x0A, 0x04, NULL, 0));
}

// UBX-ACK is class 0x05, id 0x01 for ACK and 0x00 for NAK; its payload is the acknowledged class/id.
void test_ack_helper(void)
{
    uint8_t acked_cls = 0, acked_id = 0;
    static const uint8_t PL[2] = {PROTOCORE_UBX_CLASS_CFG, PROTOCORE_UBX_CFG_MSG};
    protocore_ubx m = {0x05, 0x01, 2, PL};

    TEST_ASSERT_EQUAL_INT(1, protocore_ubx_ack(&m, &acked_cls, &acked_id));
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_UBX_CLASS_CFG, acked_cls);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_UBX_CFG_MSG, acked_id);

    m.id = 0x00;
    TEST_ASSERT_EQUAL_INT(0, protocore_ubx_ack(&m, &acked_cls, &acked_id));

    m.id = 0x02; // not an ACK id
    TEST_ASSERT_EQUAL_INT(-1, protocore_ubx_ack(&m, NULL, NULL));

    m.cls = 0x01; // NAV, not ACK
    m.id = 0x01;
    TEST_ASSERT_EQUAL_INT(-1, protocore_ubx_ack(&m, NULL, NULL));

    m.cls = 0x05;
    m.len = 1; // an ACK payload is two octets
    TEST_ASSERT_EQUAL_INT(-1, protocore_ubx_ack(&m, NULL, NULL));
}

// UBX integers are little-endian, and the signed readers are the two's-complement reinterpretation.
void test_little_endian_readers(void)
{
    static const uint8_t P[8] = {0x34, 0x12, 0xFF, 0xFF, 0x78, 0x56, 0x34, 0x12};
    TEST_ASSERT_EQUAL_HEX16(0x1234u, protocore_ubx_u16(P, 0));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, protocore_ubx_u16(P, 2));
    TEST_ASSERT_EQUAL_INT16(-1, protocore_ubx_i16(P, 2));
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, protocore_ubx_u32(P, 4));
    TEST_ASSERT_EQUAL_HEX32(0xFFFF1234u, protocore_ubx_u32(P, 0));
    TEST_ASSERT_EQUAL_INT32(-60876, protocore_ubx_i32(P, 0)); // 0xFFFF1234 = -(0x10000 - 0x1234) = -60876
}

// UBX-NAV-PVT, the published 92-octet layout: iTOW@0, year@4, month@6, day@7, hour@8, min@9, sec@10,
// valid@11, tAcc@12, nano@16, fixType@20, flags@21, numSV@23, lon@24, lat@28, height@32, hMSL@36,
// hAcc@40, vAcc@44, velN@48, velE@52, velD@56, gSpeed@60, headMot@64, sAcc@68, headAcc@72, pDOP@76.
void test_nav_pvt_published_field_offsets(void)
{
    uint8_t pl[PROTOCORE_UBX_NAV_PVT_LEN];
    protocore_ubx m = {PROTOCORE_UBX_CLASS_NAV, PROTOCORE_UBX_NAV_PVT, PROTOCORE_UBX_NAV_PVT_LEN, pl};
    protocore_ubx_nav_pvt pvt;
    memset(pl, 0, sizeof(pl));

    put_u32(pl, 0, 432000000u); // iTOW: noon of the GPS week in ms
    put_u16(pl, 4, 2026u);
    pl[6] = 8;
    pl[7] = 13;
    pl[8] = 23;
    pl[9] = 45;
    pl[10] = 59;
    pl[11] = 0x07; // validDate | validTime | fullyResolved
    put_u32(pl, 12, 25000000u);
    put_u32(pl, 16, (uint32_t)(int32_t)(-500000)); // nano may be negative
    pl[20] = PROTOCORE_UBX_FIX_3D;
    pl[21] = PROTOCORE_UBX_PVT_FIX_OK;
    pl[23] = 11;
    put_u32(pl, 24, (uint32_t)(int32_t)(-1224194000)); // lon -122.4194 deg in 1e-7
    put_u32(pl, 28, (uint32_t)377749000);              // lat  37.7749 deg in 1e-7
    put_u32(pl, 32, (uint32_t)(int32_t)(-12345));
    put_u32(pl, 36, (uint32_t)16000);
    put_u32(pl, 40, 3500u);
    put_u32(pl, 44, 5100u);
    put_u32(pl, 48, (uint32_t)(int32_t)(-100));
    put_u32(pl, 52, (uint32_t)200);
    put_u32(pl, 56, (uint32_t)(int32_t)(-300));
    put_u32(pl, 60, (uint32_t)224);
    put_u32(pl, 64, (uint32_t)(int32_t)(-4500000));
    put_u32(pl, 68, 77u);
    put_u32(pl, 72, 1500000u);
    put_u16(pl, 76, 145u);

    TEST_ASSERT_TRUE(protocore_ubx_parse_nav_pvt(&m, &pvt));
    TEST_ASSERT_EQUAL_UINT32(432000000u, pvt.itow_ms);
    TEST_ASSERT_EQUAL_UINT16(2026u, pvt.year);
    TEST_ASSERT_EQUAL_UINT8(8, pvt.month);
    TEST_ASSERT_EQUAL_UINT8(13, pvt.day);
    TEST_ASSERT_EQUAL_UINT8(23, pvt.hour);
    TEST_ASSERT_EQUAL_UINT8(45, pvt.minute);
    TEST_ASSERT_EQUAL_UINT8(59, pvt.second);
    TEST_ASSERT_EQUAL_HEX8(0x07, pvt.valid);
    TEST_ASSERT_EQUAL_UINT32(25000000u, pvt.time_acc_ns);
    TEST_ASSERT_EQUAL_INT32(-500000, pvt.nano);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_UBX_FIX_3D, pvt.fix_type);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_UBX_PVT_FIX_OK, pvt.flags & PROTOCORE_UBX_PVT_FIX_OK);
    TEST_ASSERT_EQUAL_UINT8(11, pvt.num_sv);
    TEST_ASSERT_EQUAL_INT32(-1224194000, pvt.lon_1e7);
    TEST_ASSERT_EQUAL_INT32(377749000, pvt.lat_1e7);
    TEST_ASSERT_EQUAL_INT32(-12345, pvt.height_mm);
    TEST_ASSERT_EQUAL_INT32(16000, pvt.hmsl_mm);
    TEST_ASSERT_EQUAL_UINT32(3500u, pvt.h_acc_mm);
    TEST_ASSERT_EQUAL_UINT32(5100u, pvt.v_acc_mm);
    TEST_ASSERT_EQUAL_INT32(-100, pvt.vel_n_mm_s);
    TEST_ASSERT_EQUAL_INT32(200, pvt.vel_e_mm_s);
    TEST_ASSERT_EQUAL_INT32(-300, pvt.vel_d_mm_s);
    TEST_ASSERT_EQUAL_INT32(224, pvt.gspeed_mm_s);
    TEST_ASSERT_EQUAL_INT32(-4500000, pvt.head_mot_1e5);
    TEST_ASSERT_EQUAL_UINT32(77u, pvt.s_acc_mm_s);
    TEST_ASSERT_EQUAL_UINT32(1500000u, pvt.head_acc_1e5);
    TEST_ASSERT_EQUAL_UINT16(145u, pvt.pdop_1e2);

    // The wrong class, the wrong id, or a payload shorter than the published length is refused.
    m.cls = 0x06;
    TEST_ASSERT_FALSE(protocore_ubx_parse_nav_pvt(&m, &pvt));
    m.cls = PROTOCORE_UBX_CLASS_NAV;
    m.id = PROTOCORE_UBX_NAV_SAT;
    TEST_ASSERT_FALSE(protocore_ubx_parse_nav_pvt(&m, &pvt));
    m.id = PROTOCORE_UBX_NAV_PVT;
    m.len = PROTOCORE_UBX_NAV_PVT_LEN - 1;
    TEST_ASSERT_FALSE(protocore_ubx_parse_nav_pvt(&m, &pvt));
}

// UBX-NAV-TIMEUTC, the published 20-octet layout: iTOW@0, tAcc@4, nano@8, year@12, month@14, day@15,
// hour@16, min@17, sec@18, valid@19. Bit 2 of valid is the UTC-valid flag.
void test_nav_timeutc_published_field_offsets(void)
{
    uint8_t pl[PROTOCORE_UBX_NAV_TIMEUTC_LEN];
    protocore_ubx m = {PROTOCORE_UBX_CLASS_NAV, PROTOCORE_UBX_NAV_TIMEUTC, PROTOCORE_UBX_NAV_TIMEUTC_LEN, pl};
    protocore_ubx_nav_time_utc t;
    memset(pl, 0, sizeof(pl));

    put_u32(pl, 0, 86400000u);
    put_u32(pl, 4, 30u);
    put_u32(pl, 8, (uint32_t)(int32_t)(-999999999));
    put_u16(pl, 12, 2026u);
    pl[14] = 12;
    pl[15] = 31;
    pl[16] = 23;
    pl[17] = 59;
    pl[18] = 60; // a leap second is a legal 60
    pl[19] = PROTOCORE_UBX_TIMEUTC_VALID_TOW | PROTOCORE_UBX_TIMEUTC_VALID_WKN | PROTOCORE_UBX_TIMEUTC_VALID_UTC;

    TEST_ASSERT_TRUE(protocore_ubx_parse_nav_timeutc(&m, &t));
    TEST_ASSERT_EQUAL_UINT32(86400000u, t.itow_ms);
    TEST_ASSERT_EQUAL_UINT32(30u, t.time_acc_ns);
    TEST_ASSERT_EQUAL_INT32(-999999999, t.nano);
    TEST_ASSERT_EQUAL_UINT16(2026u, t.year);
    TEST_ASSERT_EQUAL_UINT8(12, t.month);
    TEST_ASSERT_EQUAL_UINT8(31, t.day);
    TEST_ASSERT_EQUAL_UINT8(23, t.hour);
    TEST_ASSERT_EQUAL_UINT8(59, t.minute);
    TEST_ASSERT_EQUAL_UINT8(60, t.second);
    TEST_ASSERT_TRUE(t.utc_valid);

    // Without the UTC-valid bit the leap seconds are unresolved, so the convenience flag is false.
    pl[19] = PROTOCORE_UBX_TIMEUTC_VALID_TOW | PROTOCORE_UBX_TIMEUTC_VALID_WKN;
    TEST_ASSERT_TRUE(protocore_ubx_parse_nav_timeutc(&m, &t));
    TEST_ASSERT_FALSE(t.utc_valid);

    m.len = PROTOCORE_UBX_NAV_TIMEUTC_LEN - 1;
    TEST_ASSERT_FALSE(protocore_ubx_parse_nav_timeutc(&m, &t));
}

// UBX-NAV-SAT: an 8-octet header (iTOW@0, version@4, numSvs@5) then 12 octets per satellite
// (gnssId@0, svId@1, cno@2, elev@3, azim@4, prRes@6, flags@8).
void test_nav_sat_header_and_blocks(void)
{
    uint8_t pl[PROTOCORE_UBX_NAV_SAT_HDR_LEN + 2 * PROTOCORE_UBX_NAV_SAT_ENTRY_LEN];
    protocore_ubx m = {PROTOCORE_UBX_CLASS_NAV, PROTOCORE_UBX_NAV_SAT, (uint16_t)sizeof(pl), pl};
    protocore_ubx_nav_sat_hdr hdr;
    protocore_ubx_sat sat;
    memset(pl, 0, sizeof(pl));

    put_u32(pl, 0, 100u);
    pl[4] = 1; // version
    pl[5] = 2; // numSvs

    uint8_t *s0 = pl + PROTOCORE_UBX_NAV_SAT_HDR_LEN;
    s0[0] = 0; // GPS
    s0[1] = 31;
    s0[2] = 44;
    s0[3] = (uint8_t)(int8_t)(-15);
    put_u16(s0, 4, (uint16_t)(int16_t)350);
    put_u16(s0, 6, (uint16_t)(int16_t)(-7));
    put_u32(s0, 8, PROTOCORE_UBX_SAT_USED | 0x04u); // used, quality 4

    uint8_t *s1 = s0 + PROTOCORE_UBX_NAV_SAT_ENTRY_LEN;
    s1[0] = 6; // GLONASS
    s1[1] = 12;
    s1[2] = 0;
    s1[3] = (uint8_t)(int8_t)90;
    put_u16(s1, 4, 0);
    put_u16(s1, 6, 0);
    put_u32(s1, 8, 0);

    TEST_ASSERT_TRUE(protocore_ubx_parse_nav_sat(&m, &hdr));
    TEST_ASSERT_EQUAL_UINT32(100u, hdr.itow_ms);
    TEST_ASSERT_EQUAL_UINT8(1, hdr.version);
    TEST_ASSERT_EQUAL_UINT8(2, hdr.num_svs);

    TEST_ASSERT_TRUE(protocore_ubx_nav_sat_get(&m, 0, &sat));
    TEST_ASSERT_EQUAL_UINT8(0, sat.gnss_id);
    TEST_ASSERT_EQUAL_UINT8(31, sat.sv_id);
    TEST_ASSERT_EQUAL_UINT8(44, sat.cno_dbhz);
    TEST_ASSERT_EQUAL_INT8(-15, sat.elev_deg);
    TEST_ASSERT_EQUAL_INT16(350, sat.azim_deg);
    TEST_ASSERT_EQUAL_INT16(-7, sat.pr_res_01m);
    TEST_ASSERT_EQUAL_HEX32(PROTOCORE_UBX_SAT_USED, sat.flags & PROTOCORE_UBX_SAT_USED);
    TEST_ASSERT_EQUAL_UINT32(4u, sat.flags & PROTOCORE_UBX_SAT_QUALITY_MASK);

    TEST_ASSERT_TRUE(protocore_ubx_nav_sat_get(&m, 1, &sat));
    TEST_ASSERT_EQUAL_UINT8(6, sat.gnss_id);
    TEST_ASSERT_EQUAL_INT8(90, sat.elev_deg);
    TEST_ASSERT_EQUAL_UINT32(0u, sat.flags & PROTOCORE_UBX_SAT_USED);

    // Past the declared block count, and a declared length that cannot hold numSvs blocks.
    TEST_ASSERT_FALSE(protocore_ubx_nav_sat_get(&m, 2, &sat));
    m.len = PROTOCORE_UBX_NAV_SAT_HDR_LEN + PROTOCORE_UBX_NAV_SAT_ENTRY_LEN; // claims 2, carries 1
    TEST_ASSERT_FALSE(protocore_ubx_parse_nav_sat(&m, &hdr));
}

// The demux hands back every octet that is not part of a UBX frame, and reports the frame when the
// checksum agrees. A receiver multiplexes ASCII NMEA with UBX on one UART, so both must survive.
void test_stream_separates_nmea_from_ubx(void)
{
    static const char NMEA[] = "$GPGGA,";
    static const uint8_t PL[3] = {0x11, 0x22, 0x33};
    uint8_t frame[16];
    protocore_ubx_stream st;
    protocore_ubx out;
    uint8_t pass = 0;
    size_t n = protocore_ubx_build(frame, sizeof(frame), 0x01, 0x07, PL, sizeof(PL));

    protocore_ubx_stream_init(&st);
    for (size_t i = 0; i < sizeof(NMEA) - 1; i++)
    {
        pass = 0;
        TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_PASSTHROUGH, protocore_ubx_stream_feed(&st, (uint8_t)NMEA[i], &out, &pass));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)NMEA[i], pass);
    }

    for (size_t i = 0; i + 1 < n; i++)
    {
        TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_NONE, protocore_ubx_stream_feed(&st, frame[i], &out, &pass));
    }
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_FRAME, protocore_ubx_stream_feed(&st, frame[n - 1], &out, &pass));
    TEST_ASSERT_EQUAL_HEX8(0x01, out.cls);
    TEST_ASSERT_EQUAL_HEX8(0x07, out.id);
    TEST_ASSERT_EQUAL_UINT16(sizeof(PL), out.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PL, out.payload, sizeof(PL));

    // and the demux is back to hunting, so the next stray octet passes through
    pass = 0;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_PASSTHROUGH, protocore_ubx_stream_feed(&st, 'A', &out, &pass));
    TEST_ASSERT_EQUAL_HEX8('A', pass);
}

// A doubled sync1 still opens a frame: the first 0xB5 was a false start, the second is the real one.
void test_stream_doubled_sync1_still_opens_a_frame(void)
{
    static const uint8_t PL[1] = {0x99};
    uint8_t frame[16];
    protocore_ubx_stream st;
    protocore_ubx out;
    uint8_t pass = 0;
    size_t n = protocore_ubx_build(frame, sizeof(frame), 0x05, 0x01, PL, sizeof(PL));

    protocore_ubx_stream_init(&st);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_NONE, protocore_ubx_stream_feed(&st, PROTOCORE_UBX_SYNC1, &out, &pass));
    for (size_t i = 0; i + 1 < n; i++)
    {
        TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_NONE, protocore_ubx_stream_feed(&st, frame[i], &out, &pass));
    }
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_FRAME, protocore_ubx_stream_feed(&st, frame[n - 1], &out, &pass));
    TEST_ASSERT_EQUAL_HEX8(0x05, out.cls);
}

// A bad checksum is discarded rather than reported, and the demux resumes hunting for sync.
void test_stream_discards_a_bad_checksum(void)
{
    static const uint8_t PL[2] = {0xAA, 0xBB};
    uint8_t frame[16];
    protocore_ubx_stream st;
    protocore_ubx out;
    uint8_t pass = 0;
    size_t n = protocore_ubx_build(frame, sizeof(frame), 0x01, 0x07, PL, sizeof(PL));
    frame[n - 1] ^= 0xFF;

    protocore_ubx_stream_init(&st);
    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_NONE, protocore_ubx_stream_feed(&st, frame[i], &out, &pass));
    }
    pass = 0;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_PASSTHROUGH, protocore_ubx_stream_feed(&st, 'Z', &out, &pass));
    TEST_ASSERT_EQUAL_HEX8('Z', pass);
}

// A declared length past the demux buffer is skipped whole, payload and checksum, and reported once.
void test_stream_skips_an_over_long_frame(void)
{
    const uint16_t too_long = (uint16_t)(PROTOCORE_UBX_MAX_PAYLOAD + 1);
    protocore_ubx_stream st;
    protocore_ubx out;
    uint8_t pass = 0;

    protocore_ubx_stream_init(&st);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_NONE, protocore_ubx_stream_feed(&st, PROTOCORE_UBX_SYNC1, &out, &pass));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_NONE, protocore_ubx_stream_feed(&st, PROTOCORE_UBX_SYNC2, &out, &pass));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_NONE, protocore_ubx_stream_feed(&st, 0x01, &out, &pass));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_NONE, protocore_ubx_stream_feed(&st, 0x07, &out, &pass));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_NONE, protocore_ubx_stream_feed(&st, (uint8_t)too_long, &out, &pass));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_NONE, protocore_ubx_stream_feed(&st, (uint8_t)(too_long >> 8), &out, &pass));

    // payload + the two checksum octets are discarded; only the last one reports the overflow
    const uint32_t skip = (uint32_t)too_long + 2u;
    for (uint32_t i = 0; i + 1 < skip; i++)
    {
        TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_NONE, protocore_ubx_stream_feed(&st, 0x5A, &out, &pass));
    }
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_OVERFLOW, protocore_ubx_stream_feed(&st, 0x5A, &out, &pass));

    pass = 0;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_PASSTHROUGH, protocore_ubx_stream_feed(&st, 'Q', &out, &pass));
    TEST_ASSERT_EQUAL_HEX8('Q', pass);
}

// A zero-length frame carries no payload octets, so the demux goes straight from the length to the
// checksum: the poll frames a receiver echoes back look exactly like this.
void test_stream_accepts_a_zero_length_frame(void)
{
    uint8_t frame[8];
    protocore_ubx_stream st;
    protocore_ubx out;
    uint8_t pass = 0;
    size_t n = protocore_ubx_build_poll(frame, sizeof(frame), 0x0A, 0x04);
    TEST_ASSERT_EQUAL_UINT(8u, n);

    protocore_ubx_stream_init(&st);
    for (size_t i = 0; i + 1 < n; i++)
    {
        TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_NONE, protocore_ubx_stream_feed(&st, frame[i], &out, &pass));
    }
    TEST_ASSERT_EQUAL_INT(PROTOCORE_UBX_FRAME, protocore_ubx_stream_feed(&st, frame[n - 1], &out, &pass));
    TEST_ASSERT_EQUAL_UINT16(0u, out.len);
    TEST_ASSERT_EQUAL_HEX8(0x0A, out.cls);
    TEST_ASSERT_EQUAL_HEX8(0x04, out.id);
}
