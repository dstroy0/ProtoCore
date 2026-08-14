// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/transportation/j2735/j2735.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static void assert_bsm_equal(const J2735BsmCore *a, const J2735BsmCore *b)
{
    TEST_ASSERT_EQUAL_UINT8(a->msg_count, b->msg_count);
    TEST_ASSERT_EQUAL_HEX32(a->id, b->id);
    TEST_ASSERT_EQUAL_UINT16(a->sec_mark, b->sec_mark);
    TEST_ASSERT_EQUAL_INT32(a->lat, b->lat);
    TEST_ASSERT_EQUAL_INT32(a->lon, b->lon);
    TEST_ASSERT_EQUAL_INT32(a->elev, b->elev);
    TEST_ASSERT_EQUAL_UINT16(a->speed, b->speed);
    TEST_ASSERT_EQUAL_UINT16(a->heading, b->heading);
}

static void assert_state_equal(const J2735MovementState *a, const J2735MovementState *b)
{
    TEST_ASSERT_EQUAL_UINT8(a->signal_group, b->signal_group);
    TEST_ASSERT_EQUAL_UINT8(a->phase, b->phase);
    TEST_ASSERT_EQUAL_UINT16(a->min_end_time, b->min_end_time);
    TEST_ASSERT_EQUAL_UINT16(a->max_end_time, b->max_end_time);
}

static void assert_lane_equal(const J2735Lane *a, const J2735Lane *b)
{
    TEST_ASSERT_EQUAL_UINT8(a->lane_id, b->lane_id);
    TEST_ASSERT_EQUAL_INT(a->is_ingress ? 1 : 0, b->is_ingress ? 1 : 0);
    TEST_ASSERT_EQUAL_INT16(a->node_x, b->node_x);
    TEST_ASSERT_EQUAL_INT16(a->node_y, b->node_y);
}

void test_x691_constrained_integer_width(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, protocore_uper_cint_bits(5, 5));
    TEST_ASSERT_EQUAL_UINT(1u, protocore_uper_cint_bits(0, 1));
    TEST_ASSERT_EQUAL_UINT(2u, protocore_uper_cint_bits(0, 2));
    TEST_ASSERT_EQUAL_UINT(2u, protocore_uper_cint_bits(0, 3));
    TEST_ASSERT_EQUAL_UINT(3u, protocore_uper_cint_bits(0, 4));

    TEST_ASSERT_EQUAL_UINT(7u, protocore_uper_cint_bits(0, 127));

    TEST_ASSERT_EQUAL_UINT(16u, protocore_uper_cint_bits(0, 65535));

    TEST_ASSERT_EQUAL_UINT(31u, protocore_uper_cint_bits(-900000000, 900000001));

    TEST_ASSERT_EQUAL_UINT(32u, protocore_uper_cint_bits(-1799999999, 1800000001));

    TEST_ASSERT_EQUAL_UINT(16u, protocore_uper_cint_bits(-4096, 61439));

    TEST_ASSERT_EQUAL_UINT(13u, protocore_uper_cint_bits(0, 8191));

    TEST_ASSERT_EQUAL_UINT(15u, protocore_uper_cint_bits(0, 28800));

    TEST_ASSERT_EQUAL_UINT(8u, protocore_uper_cint_bits(0, 255));
    TEST_ASSERT_EQUAL_UINT(4u, protocore_uper_cint_bits(0, 9));
    TEST_ASSERT_EQUAL_UINT(16u, protocore_uper_cint_bits(0, 36000));
    TEST_ASSERT_EQUAL_UINT(12u, protocore_uper_cint_bits(-2048, 2047));
    TEST_ASSERT_EQUAL_UINT(5u, protocore_uper_cint_bits(0, 31));
}

void test_bits_are_packed_msb_first(void)
{
    uint8_t buf[4];
    UperWriter w;

    protocore_uper_writer_init(&w, buf, sizeof(buf));
    protocore_uper_put_bits(&w, 1u, 1);
    TEST_ASSERT_EQUAL_UINT(1u, protocore_uper_writer_finish(&w));
    TEST_ASSERT_EQUAL_HEX8(0x80, buf[0]);

    protocore_uper_writer_init(&w, buf, sizeof(buf));
    protocore_uper_put_bits(&w, 0x5u, 3);
    TEST_ASSERT_EQUAL_UINT(1u, protocore_uper_writer_finish(&w));
    TEST_ASSERT_EQUAL_HEX8(0xA0, buf[0]);

    protocore_uper_writer_init(&w, buf, sizeof(buf));
    protocore_uper_put_bits(&w, 0xABCu, 12);
    TEST_ASSERT_EQUAL_UINT(2u, protocore_uper_writer_finish(&w));
    TEST_ASSERT_EQUAL_HEX8(0xAB, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xC0, buf[1]);

    protocore_uper_writer_init(&w, buf, sizeof(buf));
    protocore_uper_put_bool(&w, PROTO_TRUE);
    protocore_uper_put_bool(&w, PROTO_FALSE);
    protocore_uper_put_bool(&w, PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT(1u, protocore_uper_writer_finish(&w));
    TEST_ASSERT_EQUAL_HEX8(0xA0, buf[0]);
}

void test_cint_is_the_offset_from_the_lower_bound(void)
{
    uint8_t buf[8];
    UperWriter w;
    UperReader r;

    protocore_uper_writer_init(&w, buf, sizeof(buf));
    protocore_uper_put_cint(&w, -900000000, -900000000, 900000001);
    TEST_ASSERT_EQUAL_UINT(4u, protocore_uper_writer_finish(&w));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]);

    protocore_uper_writer_init(&w, buf, sizeof(buf));
    protocore_uper_put_cint(&w, 0, -900000000, 900000001);
    TEST_ASSERT_EQUAL_UINT(4u, protocore_uper_writer_finish(&w));
    TEST_ASSERT_EQUAL_HEX8(0x6B, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x49, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xD2, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]);

    protocore_uper_reader_init(&r, buf, 31);
    TEST_ASSERT_EQUAL_INT64((int64_t)0, protocore_uper_get_cint(&r, -900000000, 900000001));

    protocore_uper_writer_init(&w, buf, sizeof(buf));
    protocore_uper_put_cint(&w, 42, 42, 42);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_uper_writer_finish(&w));
    protocore_uper_reader_init(&r, buf, 0);
    TEST_ASSERT_EQUAL_INT64((int64_t)42, protocore_uper_get_cint(&r, 42, 42));
}

void test_bsm_core_bit_layout(void)
{
    static const uint8_t WANT[21] = {0x01, 0x4B, 0x4B, 0x4B, 0x4A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    J2735BsmCore c;
    uint8_t out[32];

    c.msg_count = 0;
    c.id = 0xA5A5A5A5u;
    c.sec_mark = 0;
    c.lat = -900000000;
    c.lon = -1799999999;
    c.elev = -4096;
    c.speed = 0;
    c.heading = 0;

    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), protocore_j2735_bsm_core_encode(&c, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));
}

void test_bsm_core_round_trip_at_the_range_bounds(void)
{
    J2735BsmCore lo = {0, 0x00000000u, 0, -900000000, -1799999999, -4096, 0, 0};
    J2735BsmCore hi = {127, 0xFFFFFFFFu, 65535, 900000001, 1800000001, 61439, 8191, 28800};
    J2735BsmCore got;
    uint8_t out[21];

    TEST_ASSERT_EQUAL_UINT(sizeof(out), protocore_j2735_bsm_core_encode(&lo, out, sizeof(out)));
    TEST_ASSERT_TRUE(protocore_j2735_bsm_core_decode(out, sizeof(out), &got));
    assert_bsm_equal(&lo, &got);

    TEST_ASSERT_EQUAL_UINT(sizeof(out), protocore_j2735_bsm_core_encode(&hi, out, sizeof(out)));
    TEST_ASSERT_TRUE(protocore_j2735_bsm_core_decode(out, sizeof(out), &got));
    assert_bsm_equal(&hi, &got);

    J2735BsmCore mid = {42, 0x1234ABCDu, 30000, 377749000, -1224194000, 100, 1250, 14400};
    TEST_ASSERT_EQUAL_UINT(sizeof(out), protocore_j2735_bsm_core_encode(&mid, out, sizeof(out)));
    TEST_ASSERT_TRUE(protocore_j2735_bsm_core_decode(out, sizeof(out), &got));
    assert_bsm_equal(&mid, &got);
}

void test_bsm_core_bounds(void)
{
    J2735BsmCore c = {1, 2, 3, 4, 5, 6, 7, 8};
    J2735BsmCore got;
    uint8_t out[21];

    TEST_ASSERT_EQUAL_UINT(0u, protocore_j2735_bsm_core_encode(&c, out, 20));
    TEST_ASSERT_EQUAL_UINT(21u, protocore_j2735_bsm_core_encode(&c, out, sizeof(out)));
    TEST_ASSERT_FALSE(protocore_j2735_bsm_core_decode(out, 20, &got));

    TEST_ASSERT_EQUAL_UINT(0u, protocore_j2735_bsm_core_encode(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j2735_bsm_core_encode(&c, NULL, sizeof(out)));
    TEST_ASSERT_FALSE(protocore_j2735_bsm_core_decode(NULL, sizeof(out), &got));
    TEST_ASSERT_FALSE(protocore_j2735_bsm_core_decode(out, sizeof(out), NULL));
}

void test_writer_overflow_latches(void)
{
    uint8_t buf[1];
    UperWriter w;

    protocore_uper_writer_init(&w, buf, sizeof(buf));
    protocore_uper_put_bits(&w, 0xFFu, 8);
    TEST_ASSERT_TRUE(w.ok);
    protocore_uper_put_bits(&w, 1u, 1);
    TEST_ASSERT_FALSE(w.ok);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_uper_writer_finish(&w));

    protocore_uper_writer_init(&w, NULL, 8);
    TEST_ASSERT_FALSE(w.ok);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_uper_writer_finish(&w));
}

void test_reader_refuses_a_read_past_the_end(void)
{
    static const uint8_t BUF[2] = {0xFF, 0xFF};
    UperReader r;

    protocore_uper_reader_init(&r, BUF, 8);
    TEST_ASSERT_EQUAL_HEX32(0xFFu, protocore_uper_get_bits(&r, 8));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_HEX32(0u, protocore_uper_get_bits(&r, 1));
    TEST_ASSERT_FALSE(r.ok);

    protocore_uper_reader_init(&r, BUF, 8);
    TEST_ASSERT_EQUAL_HEX32(0u, protocore_uper_get_bits(&r, 9));
    TEST_ASSERT_FALSE(r.ok);
}

void test_spat_round_trip(void)
{
    J2735MovementState in[3] = {
        {1, J2735_PHASE_PROTECTED_MOVEMENT_ALLOWED, 100, 250},
        {2, J2735_PHASE_STOP_AND_REMAIN, 0, 36000},
        {255, J2735_PHASE_CAUTION_CONFLICTING_TRAFFIC, 36000, 36000},
    };
    J2735MovementState got[3];
    uint8_t out[32];
    size_t count = 0;

    TEST_ASSERT_EQUAL_UINT(7u, protocore_j2735_spat_encode(in, 1, out, sizeof(out)));

    size_t n = protocore_j2735_spat_encode(in, 3, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT((5u + 3u * 44u + 7u) / 8u, n);
    TEST_ASSERT_TRUE(protocore_j2735_spat_decode(out, n, got, 3, &count));
    TEST_ASSERT_EQUAL_UINT(3u, count);
    for (size_t i = 0; i < 3; i++)
    {
        assert_state_equal(&in[i], &got[i]);
    }

    TEST_ASSERT_EQUAL_UINT(1u, protocore_j2735_spat_encode(in, 0, out, sizeof(out)));
    TEST_ASSERT_TRUE(protocore_j2735_spat_decode(out, 1, got, 3, &count));
    TEST_ASSERT_EQUAL_UINT(0u, count);
}

void test_spat_count_bounds(void)
{
    J2735MovementState in[3] = {{1, 3, 0, 0}, {2, 3, 0, 0}, {3, 3, 0, 0}};
    J2735MovementState got[3];
    uint8_t out[32];
    size_t count = 0;

    TEST_ASSERT_EQUAL_UINT(0u, protocore_j2735_spat_encode(in, 32, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j2735_spat_encode(NULL, 1, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j2735_spat_encode(in, 1, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j2735_spat_encode(in, 3, out, 17));

    size_t n = protocore_j2735_spat_encode(in, 3, out, sizeof(out));
    TEST_ASSERT_FALSE(protocore_j2735_spat_decode(out, n, got, 2, &count));
    TEST_ASSERT_FALSE(protocore_j2735_spat_decode(out, 4, got, 3, &count));
}

void test_map_round_trip(void)
{
    J2735MapIntersection isect = {0xBEEF, 0x1234, 0x5678};
    J2735Lane lanes[2] = {
        {7, PROTO_TRUE, -2048, 2047},
        {200, PROTO_FALSE, 0, -1},
    };
    J2735MapIntersection got_isect;
    J2735Lane got_lanes[2];
    uint8_t out[32];
    size_t count = 0;

    TEST_ASSERT_EQUAL_UINT(11u, protocore_j2735_map_encode(&isect, lanes, 1, out, sizeof(out)));

    size_t n = protocore_j2735_map_encode(&isect, lanes, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT((48u + 5u + 2u * 33u + 7u) / 8u, n);
    TEST_ASSERT_TRUE(protocore_j2735_map_decode(out, n, &got_isect, got_lanes, 2, &count));
    TEST_ASSERT_EQUAL_UINT(2u, count);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, got_isect.intersection_id);
    TEST_ASSERT_EQUAL_HEX16(0x1234, got_isect.ref_lat);
    TEST_ASSERT_EQUAL_HEX16(0x5678, got_isect.ref_lon);
    for (size_t i = 0; i < 2; i++)
    {
        assert_lane_equal(&lanes[i], &got_lanes[i]);
    }

    TEST_ASSERT_EQUAL_UINT(7u, protocore_j2735_map_encode(&isect, lanes, 0, out, sizeof(out)));
    TEST_ASSERT_TRUE(protocore_j2735_map_decode(out, 7, &got_isect, got_lanes, 2, &count));
    TEST_ASSERT_EQUAL_UINT(0u, count);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, got_isect.intersection_id);
}

void test_map_bounds(void)
{
    J2735MapIntersection isect = {1, 2, 3};
    J2735Lane lanes[1] = {{5, PROTO_TRUE, 0, 0}};
    J2735MapIntersection got_isect;
    J2735Lane got_lanes[1];
    uint8_t out[32];
    size_t count = 0;

    TEST_ASSERT_EQUAL_UINT(0u, protocore_j2735_map_encode(&isect, lanes, 32, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j2735_map_encode(NULL, lanes, 1, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j2735_map_encode(&isect, NULL, 1, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j2735_map_encode(&isect, lanes, 1, out, 10));

    size_t n = protocore_j2735_map_encode(&isect, lanes, 1, out, sizeof(out));
    TEST_ASSERT_FALSE(protocore_j2735_map_decode(out, n, &got_isect, got_lanes, 0, &count));
    TEST_ASSERT_FALSE(protocore_j2735_map_decode(out, 8, &got_isect, got_lanes, 1, &count));
    TEST_ASSERT_FALSE(protocore_j2735_map_decode(NULL, n, &got_isect, got_lanes, 1, &count));
    TEST_ASSERT_FALSE(protocore_j2735_map_decode(out, n, &got_isect, got_lanes, 1, NULL));
}

void test_phase_state_values(void)
{
    TEST_ASSERT_EQUAL_INT(0, J2735_PHASE_DARK);
    TEST_ASSERT_EQUAL_INT(1, J2735_PHASE_STOP_THEN_PROCEED);
    TEST_ASSERT_EQUAL_INT(3, J2735_PHASE_STOP_AND_REMAIN);
    TEST_ASSERT_EQUAL_INT(5, J2735_PHASE_PERMISSIVE_MOVEMENT_ALLOWED);
    TEST_ASSERT_EQUAL_INT(6, J2735_PHASE_PROTECTED_MOVEMENT_ALLOWED);
    TEST_ASSERT_EQUAL_INT(7, J2735_PHASE_PERMISSIVE_CLEARANCE);
    TEST_ASSERT_EQUAL_INT(8, J2735_PHASE_PROTECTED_CLEARANCE);
    TEST_ASSERT_EQUAL_INT(9, J2735_PHASE_CAUTION_CONFLICTING_TRAFFIC);
}
