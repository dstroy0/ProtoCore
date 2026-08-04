// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/j2735: the ASN.1 UPER primitive codec + the BSMcore block.

#include "services/transportation/j2735/j2735.h"
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_cint_bits(void)
{
    TEST_ASSERT_EQUAL_UINT(0, pc_uper_cint_bits(5, 5));   // single value -> 0 bits
    TEST_ASSERT_EQUAL_UINT(1, pc_uper_cint_bits(0, 1));   // 2 values -> 1 bit
    TEST_ASSERT_EQUAL_UINT(7, pc_uper_cint_bits(0, 127)); // 128 values -> 7 bits
    TEST_ASSERT_EQUAL_UINT(8, pc_uper_cint_bits(0, 128)); // 129 values -> 8 bits
    TEST_ASSERT_EQUAL_UINT(16, pc_uper_cint_bits(0, 65535));
}

void test_bit_writer_pattern(void)
{
    // Write 0b101 (3 bits) then 0b11 (2 bits): stream 10111 000 -> 0xB8.
    uint8_t buf[4];
    UperWriter w;
    pc_uper_writer_init(&w, buf, sizeof(buf));
    pc_uper_put_bits(&w, 0b101, 3);
    pc_uper_put_bits(&w, 0b11, 2);
    size_t n = pc_uper_writer_finish(&w);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_HEX8(0xB8, buf[0]);
}

void test_writer_null_and_zero(void)
{
    // A null buffer (or zero cap) leaves the writer not-ok and must not dereference it.
    UperWriter w;
    pc_uper_writer_init(&w, NULL, 8);
    TEST_ASSERT_FALSE(w.ok);
    uint8_t buf[4];
    pc_uper_writer_init(&w, buf, 0);
    TEST_ASSERT_FALSE(w.ok);
}

void test_cint_roundtrip(void)
{
    uint8_t buf[8];
    UperWriter w;
    pc_uper_writer_init(&w, buf, sizeof(buf));
    pc_uper_put_cint(&w, 100, 0, 127); // 7 bits: 1100100
    pc_uper_put_cint(&w, -5, -10, 10); // offset 5 in 5 bits: 00101
    size_t n = pc_uper_writer_finish(&w);

    UperReader r;
    pc_uper_reader_init(&r, buf, n * 8);
    TEST_ASSERT_EQUAL_INT64(100, pc_uper_get_cint(&r, 0, 127));
    TEST_ASSERT_EQUAL_INT64(-5, pc_uper_get_cint(&r, -10, 10));
    TEST_ASSERT_TRUE(r.ok);
}

void test_bsm_core_roundtrip(void)
{
    J2735BsmCore c;
    c.msg_count = 12;
    c.id = 0xDEADBEEF;
    c.sec_mark = 34000;
    c.lat = 407127370;  // ~40.7127370 N (NYC)
    c.lon = -740059730; // ~-74.0059730 W
    c.elev = 100;
    c.speed = 500;    // 10 m/s
    c.heading = 7200; // 90 deg
    uint8_t buf[64];
    size_t n = pc_j2735_bsm_core_encode(&c, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    J2735BsmCore d;
    memset(&d, 0, sizeof(d));
    TEST_ASSERT_TRUE(pc_j2735_bsm_core_decode(buf, n, &d));
    TEST_ASSERT_EQUAL_UINT8(12, d.msg_count);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, d.id);
    TEST_ASSERT_EQUAL_UINT16(34000, d.sec_mark);
    TEST_ASSERT_EQUAL_INT32(407127370, d.lat);
    TEST_ASSERT_EQUAL_INT32(-740059730, d.lon);
    TEST_ASSERT_EQUAL_INT32(100, d.elev);
    TEST_ASSERT_EQUAL_UINT16(500, d.speed);
    TEST_ASSERT_EQUAL_UINT16(7200, d.heading);
}

void test_bsm_core_bit_length(void)
{
    // msgCnt 7 + id 32 + secMark 16 + lat 31 + long 32 + elev 16 + speed 13 + heading 15 = 162 bits
    // -> ceil(162/8) = 21 octets.
    J2735BsmCore c;
    memset(&c, 0, sizeof(c));
    uint8_t buf[64];
    size_t n = pc_j2735_bsm_core_encode(&c, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(21, n);
}

void test_spat_roundtrip(void)
{
    J2735MovementState st[3];
    st[0] = (J2735MovementState){1, (uint8_t)J2735_PHASE_PROTECTED_MOVEMENT_ALLOWED, 100, 250};
    st[1] = (J2735MovementState){2, (uint8_t)J2735_PHASE_STOP_AND_REMAIN, 0, 36000};
    st[2] = (J2735MovementState){17, (uint8_t)J2735_PHASE_PERMISSIVE_CLEARANCE, 300, 320};
    uint8_t buf[64];
    size_t n = pc_j2735_spat_encode(st, 3, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    J2735MovementState out[8];
    size_t count = 0;
    TEST_ASSERT_TRUE(pc_j2735_spat_decode(buf, n, out, 8, &count));
    TEST_ASSERT_EQUAL_size_t(3, count);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].signal_group);
    TEST_ASSERT_EQUAL_UINT8(J2735_PHASE_PROTECTED_MOVEMENT_ALLOWED, out[0].phase);
    TEST_ASSERT_EQUAL_UINT16(100, out[0].min_end_time);
    TEST_ASSERT_EQUAL_UINT16(250, out[0].max_end_time);
    TEST_ASSERT_EQUAL_UINT8(17, out[2].signal_group);
    TEST_ASSERT_EQUAL_UINT16(36000, out[1].max_end_time);
}

void test_spat_decode_too_many(void)
{
    J2735MovementState st[2] = {{1, 6, 0, 0}, {2, 3, 0, 0}};
    uint8_t buf[32];
    size_t n = pc_j2735_spat_encode(st, 2, buf, sizeof(buf));
    J2735MovementState out[1];
    size_t count = 0;
    // Only room for 1 but 2 encoded -> false.
    TEST_ASSERT_FALSE(pc_j2735_spat_decode(buf, n, out, 1, &count));
}

void test_map_roundtrip(void)
{
    J2735MapIntersection isect = {12345, 40000, 55000};
    J2735Lane lanes[3];
    lanes[0] = (J2735Lane){1, PROTO_TRUE, -100, 200};
    lanes[1] = (J2735Lane){2, PROTO_FALSE, 2047, -2048};
    lanes[2] = (J2735Lane){9, PROTO_TRUE, 0, 0};
    uint8_t buf[64];
    size_t n = pc_j2735_map_encode(&isect, lanes, 3, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    J2735MapIntersection di;
    J2735Lane out[8];
    size_t count = 0;
    TEST_ASSERT_TRUE(pc_j2735_map_decode(buf, n, &di, out, 8, &count));
    TEST_ASSERT_EQUAL_UINT16(12345, di.intersection_id);
    TEST_ASSERT_EQUAL_UINT16(40000, di.ref_lat);
    TEST_ASSERT_EQUAL_size_t(3, count);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].lane_id);
    TEST_ASSERT_TRUE(out[0].is_ingress);
    TEST_ASSERT_EQUAL_INT16(-100, out[0].node_x);
    TEST_ASSERT_EQUAL_INT16(200, out[0].node_y);
    TEST_ASSERT_FALSE(out[1].is_ingress);
    TEST_ASSERT_EQUAL_INT16(2047, out[1].node_x);
    TEST_ASSERT_EQUAL_INT16(-2048, out[1].node_y);
}

void test_uper_overflow_and_bsm_guard()
{
    uint8_t buf[4];
    UperWriter w;
    pc_uper_writer_init(&w, buf, sizeof(buf));
    pc_uper_put_bits(&w, 0xFFFFFFFFu, 32);
    pc_uper_put_bits(&w, 0xFFFFFFFFu, 32); // past the 4-byte buffer -> not ok
    TEST_ASSERT_EQUAL_size_t(0, pc_uper_writer_finish(&w));
    J2735BsmCore c = {0};
    TEST_ASSERT_EQUAL_size_t(0, pc_j2735_bsm_core_encode(&c, buf, 1)); // tiny cap fails closed
}

void test_j2735_guards_and_truncation()
{
    uint8_t buf[64];

    // pc_uper_put_cint / pc_uper_get_cint with a single-value (zero-bit) range: nothing on the wire.
    UperWriter w;
    pc_uper_writer_init(&w, buf, sizeof(buf));
    pc_uper_put_cint(&w, 5, 5, 5); // hi <= lo -> 0 bits
    TEST_ASSERT_EQUAL_size_t(0, pc_uper_writer_finish(&w));
    UperReader r;
    pc_uper_reader_init(&r, buf, sizeof(buf) * 8);
    TEST_ASSERT_TRUE(pc_uper_get_cint(&r, 5, 5) == 5); // 0 bits -> returns lo
    // pc_uper_get_bits: nbits == 0, and a not-ok reader (null buffer), both return 0.
    TEST_ASSERT_EQUAL_UINT32(0, pc_uper_get_bits(&r, 0));
    UperReader rn;
    pc_uper_reader_init(&rn, NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0, pc_uper_get_bits(&rn, 4));

    // encode/decode null-argument guards.
    J2735BsmCore c = {0};
    TEST_ASSERT_EQUAL_size_t(0, pc_j2735_bsm_core_encode(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, pc_j2735_bsm_core_encode(&c, NULL, sizeof(buf)));
    TEST_ASSERT_FALSE(pc_j2735_bsm_core_decode(NULL, 16, &c));
    TEST_ASSERT_FALSE(pc_j2735_bsm_core_decode(buf, 16, NULL));

    J2735MovementState st[2] = {{1, 6, 0, 0}, {2, 3, 0, 0}};
    J2735MovementState sout[4];
    size_t count = 0;
    TEST_ASSERT_EQUAL_size_t(0, pc_j2735_spat_encode(st, 2, NULL, 16));          // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_j2735_spat_encode(st, 32, buf, sizeof(buf))); // count > 31
    TEST_ASSERT_FALSE(pc_j2735_spat_decode(NULL, 16, sout, 4, &count));          // null in

    J2735MapIntersection isect = {1, 2, 3};
    J2735Lane lanes[2] = {{1, PROTO_TRUE, 0, 0}, {2, PROTO_FALSE, 0, 0}};
    J2735Lane lout[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_j2735_map_encode(NULL, lanes, 2, buf, sizeof(buf)));    // null isect
    TEST_ASSERT_EQUAL_size_t(0, pc_j2735_map_encode(&isect, lanes, 32, buf, sizeof(buf))); // count > 31
    TEST_ASSERT_FALSE(pc_j2735_map_decode(NULL, 16, &isect, lout, 4, &count));             // null in

    // Truncated SPAT: the count reads, but the per-state fields underrun (post-loop r.ok guard).
    size_t sn = pc_j2735_spat_encode(st, 2, buf, sizeof(buf));
    TEST_ASSERT_TRUE(sn > 1);
    TEST_ASSERT_FALSE(pc_j2735_spat_decode(buf, 1, sout, 4, &count));

    // MAP decode: count exceeds the caller's lane buffer, then a truncated buffer underruns.
    size_t mn = pc_j2735_map_encode(&isect, lanes, 2, buf, sizeof(buf));
    TEST_ASSERT_TRUE(mn > 7);
    TEST_ASSERT_FALSE(pc_j2735_map_decode(buf, mn, &isect, lout, 1, &count)); // count 2 > max 1
    TEST_ASSERT_FALSE(pc_j2735_map_decode(buf, 7, &isect, lout, 4, &count));  // header ok, lanes underrun
}

void test_j2735_extra_branch_coverage(void)
{
    // pc_uper_put_bits: nbits == 0 on an otherwise-ok writer is a no-op (the guard's second operand,
    // never exercised elsewhere since every other caller only ever passes a nonzero width).
    uint8_t buf[8];
    UperWriter w;
    pc_uper_writer_init(&w, buf, sizeof(buf));
    pc_uper_put_bits(&w, 0xFF, 0);
    TEST_ASSERT_EQUAL_size_t(0, w.bit_pos);
    TEST_ASSERT_TRUE(w.ok);

    // pc_j2735_spat_encode: count == 0 (states may be null - never dereferenced) succeeds, while
    // count > 0 with null states trips the (count && !states) guard.
    uint8_t sbuf[8];
    TEST_ASSERT_TRUE(pc_j2735_spat_encode(NULL, 0, sbuf, sizeof(sbuf)) > 0);
    TEST_ASSERT_EQUAL_size_t(0, pc_j2735_spat_encode(NULL, 5, sbuf, sizeof(sbuf)));

    // pc_j2735_spat_decode: null out_states / null out_count guard branches.
    J2735MovementState sst[1] = {{1, 6, 0, 0}};
    uint8_t sbuf2[8];
    size_t sn = pc_j2735_spat_encode(sst, 1, sbuf2, sizeof(sbuf2));
    TEST_ASSERT_TRUE(sn > 0);
    J2735MovementState sout[1];
    size_t scount = 0;
    TEST_ASSERT_FALSE(pc_j2735_spat_decode(sbuf2, sn, NULL, 1, &scount)); // null out_states
    TEST_ASSERT_FALSE(pc_j2735_spat_decode(sbuf2, sn, sout, 1, NULL));    // null out_count

    // pc_j2735_spat_decode: a zero-length buffer can't even read the count field, so r.ok is already
    // false by the time the post-count "count > max_states" guard runs.
    TEST_ASSERT_FALSE(pc_j2735_spat_decode(sbuf2, 0, sout, 1, &scount));

    // pc_j2735_map_encode: null out, count == 0 (lanes may be null), and count > 0 with null lanes.
    J2735MapIntersection isect = {1, 2, 3};
    uint8_t mbuf[16];
    J2735Lane lanes[1] = {{1, PROTO_TRUE, 0, 0}};
    TEST_ASSERT_EQUAL_size_t(0, pc_j2735_map_encode(&isect, lanes, 1, NULL, sizeof(mbuf))); // null out
    TEST_ASSERT_TRUE(pc_j2735_map_encode(&isect, NULL, 0, mbuf, sizeof(mbuf)) > 0);         // count == 0
    TEST_ASSERT_EQUAL_size_t(0, pc_j2735_map_encode(&isect, NULL, 5, mbuf, sizeof(mbuf)));  // count > 0, null lanes

    // pc_j2735_map_decode: null isect / null out_lanes / null out_count guard branches.
    uint8_t mbuf2[16];
    size_t mn = pc_j2735_map_encode(&isect, lanes, 1, mbuf2, sizeof(mbuf2));
    TEST_ASSERT_TRUE(mn > 0);
    J2735MapIntersection di;
    J2735Lane lout[1];
    size_t mcount = 0;
    TEST_ASSERT_FALSE(pc_j2735_map_decode(mbuf2, mn, NULL, lout, 1, &mcount)); // null isect
    TEST_ASSERT_FALSE(pc_j2735_map_decode(mbuf2, mn, &di, NULL, 1, &mcount));  // null out_lanes
    TEST_ASSERT_FALSE(pc_j2735_map_decode(mbuf2, mn, &di, lout, 1, NULL));     // null out_count

    // pc_j2735_map_decode: a zero-length buffer can't even read the header, so r.ok is already false
    // by the time the post-count "count > max_lanes" guard runs.
    TEST_ASSERT_FALSE(pc_j2735_map_decode(mbuf2, 0, &di, lout, 1, &mcount));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cint_bits);
    RUN_TEST(test_bit_writer_pattern);
    RUN_TEST(test_writer_null_and_zero);
    RUN_TEST(test_cint_roundtrip);
    RUN_TEST(test_bsm_core_roundtrip);
    RUN_TEST(test_bsm_core_bit_length);
    RUN_TEST(test_spat_roundtrip);
    RUN_TEST(test_spat_decode_too_many);
    RUN_TEST(test_map_roundtrip);
    RUN_TEST(test_uper_overflow_and_bsm_guard);
    RUN_TEST(test_j2735_guards_and_truncation);
    RUN_TEST(test_j2735_extra_branch_coverage);
    return UNITY_END();
}
