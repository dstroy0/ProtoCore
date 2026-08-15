// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the J2735 UPER primitive codec and its BSMcore / SPaT / MAP blocks
// (services/transportation/j2735/j2735.h).
//
// TWO DOCUMENTS SUPPLY EVERY NUMBER THAT IS NOT A PROPERTY.
//
// 1. ITU-T Rec. X.691 (07/2002) | ISO/IEC 8825-2:2003, the UPER rules themselves. The clauses used:
//    6.2 (bit 8 of an octet is the most significant), 10.1.3 (UNALIGNED: fields concatenated with no
//    padding, then zero to seven zero bits appended to reach an octet boundary), 10.3.2-10.3.4 (the
//    leading bit of a bit-field is its most significant bit), 10.5.3 (range = ub - lb + 1), 10.5.4
//    (range 1 encodes to an empty bit-field), 10.5.6 and its NOTE (UNALIGNED: value - lb in the
//    minimum number of bits for the range; if 2^m < range <= 2^(m+1) the width is m+1),
//    10.5.7.1 (the published bit-field size table for range <= 255: 2 -> 1; 3,4 -> 2; 5..8 -> 3;
//    9..16 -> 4; 17..32 -> 5; 33..64 -> 6; 65..128 -> 7; 129..255 -> 8), 11.1-11.2 (BOOLEAN is one
//    bit, 1 for TRUE), 13.1-13.2 (an ENUMERATED with no extension marker encodes its enumeration
//    INDEX, indexes assigned from 0 in ascending order of enumeration value, as a constrained
//    integer with lb 0 and ub the largest index), 16.7 (a fixed-size OCTET STRING longer than two
//    octets and shorter than 64K is a bit-field of that many octets with no length determinant).
//
// 2. The ASN.1 constraints of the data elements, from ISO/TS 19091's DSRC module as published on
//    the ETSI forge (rep ITS/ITS_ASN1, IS_TS103301/ISO_TS_19091.asn, and the identical LibIts copy
//    of the original DSRC.asn), plus ETSI TS 102 894-2 (CDD) v1.3.1 ITS-Container.asn, which is
//    where that DSRC module imports Latitude from. Verbatim:
//      MsgCount ::= INTEGER (0..127)                     Elevation ::= INTEGER (-4096..61439)
//      DSecond ::= INTEGER (0..65535)                    Velocity ::= INTEGER (0..8191)
//      TemporaryID ::= OCTET STRING (SIZE(4))            Angle ::= INTEGER (0..28800)
//      TimeMark ::= INTEGER (0..36001)                   SignalGroupID ::= INTEGER (0..255)
//      LaneID ::= INTEGER (0..255)                       IntersectionID ::= INTEGER (0..65535)
//      Offset-B12 ::= INTEGER (-2048..2047)              Latitude ::= INTEGER (-900000000..900000001)
//      MovementPhaseState ::= ENUMERATED { unavailable (0), dark (1), stop-Then-Proceed (2),
//        stop-And-Remain (3), pre-Movement (4), permissive-Movement-Allowed (5),
//        protected-Movement-Allowed (6), permissive-clearance (7), protected-clearance (8),
//        caution-Conflicting-Traffic (9) }
//
// WHAT COULD NOT BE OBTAINED. SAE J2735 itself is a paid SAE document and no copy was reachable, so
// where ISO/TS 19091 and the ETSI CDD do not carry the element, the constraint is unsourced:
//   - Longitude. The CDD module ISO/TS 19091 imports publishes (-1800000000..1800000001); j2735.h
//     uses (-1799999999..1800000001). Both give a 32-bit field, so the WIDTH below is anchored
//     under either, but the offset differs by one LSB. The case says which is which.
//   - The BSMcore field SET and ORDER, the 5-bit SPaT/MAP element counts, and the MAP refLat/refLon
//     surrogates are this module's reduction, not a J2735 PDU. Their cases are round-trip identity,
//     bounds refusals and octet-count arithmetic only.
//
// THE LOAD-BEARING CASE IS test_movement_phase_state_reds_match_the_published_enumeration, AND IT
// FAILS. j2735.h numbers J2735_PHASE_DARK = 0 and J2735_PHASE_STOP_THEN_PROCEED = 1; the published
// enumeration above numbers dark (1) and stop-Then-Proceed (2), with unavailable (0) and
// pre-Movement (4) occupying the two values j2735.h leaves out. Under X.691 13.1-13.2 the wire
// field is the index in that ten-item list, so this module puts a red-flashing phase on the wire as
// the index a conforming peer reads as "dark" and puts "dark" where the peer reads "unavailable".
// The assertion is left as the standard requires it rather than as the header spells it.

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

// X.691 10.5.7.1 prints the widths for range <= 255 as a table, and 10.5.6 gives the same widths as
// "the minimum number of bits necessary to represent the range". These six read straight off it,
// with range = ub - lb + 1 (10.5.3):
//   0..1   range 2   -> table row "2"        -> 1
//   0..3   range 4   -> table row "3, 4"     -> 2
//   0..4   range 5   -> table row "5, 6, 7, 8" -> 3
//   0..9   range 10  -> table row "9 to 16"  -> 4
//   0..31  range 32  -> table row "17 to 32" -> 5
//   0..127 range 128 -> table row "65 to 128" -> 7
void test_x691_published_bit_field_size_table(void)
{
    TEST_ASSERT_EQUAL_UINT(1u, protocore_uper_cint_bits(0, 1));
    TEST_ASSERT_EQUAL_UINT(2u, protocore_uper_cint_bits(0, 2));
    TEST_ASSERT_EQUAL_UINT(2u, protocore_uper_cint_bits(0, 3));
    TEST_ASSERT_EQUAL_UINT(3u, protocore_uper_cint_bits(0, 4));
    TEST_ASSERT_EQUAL_UINT(4u, protocore_uper_cint_bits(0, 9));
    TEST_ASSERT_EQUAL_UINT(5u, protocore_uper_cint_bits(0, 31));
    TEST_ASSERT_EQUAL_UINT(7u, protocore_uper_cint_bits(0, 127));
}

// X.691 10.5.4: a range of 1 is an empty bit-field, and the decoder recovers the single permitted
// value from the type alone.
void test_x691_a_range_of_one_occupies_no_bits(void)
{
    uint8_t buf[8];
    UperWriter w;
    UperReader r;

    TEST_ASSERT_EQUAL_UINT(0u, protocore_uper_cint_bits(5, 5));

    protocore_uper_writer_init(&w, buf, sizeof(buf));
    protocore_uper_put_cint(&w, 42, 42, 42);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_uper_writer_finish(&w));
    protocore_uper_reader_init(&r, buf, 0);
    TEST_ASSERT_EQUAL_INT64((int64_t)42, protocore_uper_get_cint(&r, 42, 42));
}

// The widths of the published J2735 / ISO-TS-19091 elements, each from X.691 10.5.3 + 10.5.6's NOTE
// (2^m < range <= 2^(m+1) gives m+1 bits):
//   SignalGroupID / LaneID   0..255            range 256        2^7  < 256        <= 2^8   -> 8
//   MovementPhaseState index 0..9              range 10         2^3  < 10         <= 2^4   -> 4
//   MsgCount                 0..127            range 128        2^6  < 128        <= 2^7   -> 7
//   DSecond / IntersectionID 0..65535          range 65536      2^15 < 65536      <= 2^16  -> 16
//   Elevation                -4096..61439      range 65536      2^15 < 65536      <= 2^16  -> 16
//   Velocity                 0..8191           range 8192       2^12 < 8192       <= 2^13  -> 13
//   Angle                    0..28800          range 28801      2^14 < 28801      <= 2^15  -> 15
//   TimeMark                 0..36001          range 36002      2^15 < 36002      <= 2^16  -> 16
//   Offset-B12               -2048..2047       range 4096       2^11 < 4096       <= 2^12  -> 12
//   Latitude (CDD)           -9e8..900000001   range 1800000002 2^30 < 1800000002 <= 2^31  -> 31
//   Longitude (CDD)          -18e8..1800000001 range 3600000002 2^31 < 3600000002 <= 2^32  -> 32
void test_x691_widths_of_the_published_element_ranges(void)
{
    TEST_ASSERT_EQUAL_UINT(8u, protocore_uper_cint_bits(0, 255));
    TEST_ASSERT_EQUAL_UINT(4u, protocore_uper_cint_bits(0, 9));
    TEST_ASSERT_EQUAL_UINT(7u, protocore_uper_cint_bits(0, 127));
    TEST_ASSERT_EQUAL_UINT(16u, protocore_uper_cint_bits(0, 65535));
    TEST_ASSERT_EQUAL_UINT(16u, protocore_uper_cint_bits(-4096, 61439));
    TEST_ASSERT_EQUAL_UINT(13u, protocore_uper_cint_bits(0, 8191));
    TEST_ASSERT_EQUAL_UINT(15u, protocore_uper_cint_bits(0, 28800));
    TEST_ASSERT_EQUAL_UINT(16u, protocore_uper_cint_bits(0, 36001));
    TEST_ASSERT_EQUAL_UINT(12u, protocore_uper_cint_bits(-2048, 2047));
    TEST_ASSERT_EQUAL_UINT(31u, protocore_uper_cint_bits(-900000000, 900000001));
    TEST_ASSERT_EQUAL_UINT(32u, protocore_uper_cint_bits(-1800000000, 1800000001));
}

// The two bounds j2735.h narrows from the published ones sit inside the same width, so neither
// changes the size of the field:
//   TimeMark   published 0..36001         range 36002      -> 16 bits
//              j2735.h   0..36000         range 36001      -> 16 bits  (2^15 < 36001 <= 2^16)
//   Longitude  CDD       -18e8..1800000001 range 3600000002 -> 32 bits
//              j2735.h   -1799999999..1800000001 range 3600000001 -> 32 bits (2^31 < it <= 2^32)
// The longitude LOWER BOUND still differs, so the offset written for a given longitude differs by
// one LSB between the two; SAE J2735's own Longitude constraint could not be obtained to settle it.
void test_the_modules_narrowed_bounds_keep_the_published_widths(void)
{
    TEST_ASSERT_EQUAL_UINT(protocore_uper_cint_bits(0, 36001), protocore_uper_cint_bits(0, 36000));
    TEST_ASSERT_EQUAL_UINT(protocore_uper_cint_bits(-1800000000, 1800000001),
                           protocore_uper_cint_bits(-1799999999, 1800000001));
    TEST_ASSERT_EQUAL_UINT(16u, protocore_uper_cint_bits(0, 36000));
    TEST_ASSERT_EQUAL_UINT(32u, protocore_uper_cint_bits(-1799999999, 1800000001));
}

// X.691 10.3.2: the leading bit of a bit-field is the most significant bit of the first octet.
// 6.2: bit 8 of an octet is the most significant. 10.1.3: UNALIGNED concatenates without padding
// and appends zero to seven zero bits at the end.
//   1 in 1 bit    -> 1        + 7 pad zeros   -> 1000 0000 = 0x80
//   5 in 3 bits   -> 101      + 5 pad zeros   -> 1010 0000 = 0xA0
//   0xABC in 12   -> 1010 1011 1100 + 4 zeros -> 0xAB 0xC0
// X.691 11.1-11.2: a BOOLEAN is one bit, 1 for TRUE.
//   TRUE FALSE TRUE -> 101   + 5 pad zeros    -> 1010 0000 = 0xA0
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

// X.691 10.5.6: the UNALIGNED encoding is ("n" - "lb") as a non-negative-binary-integer. With
// Latitude's published bounds (-900000000..900000001) the field is 31 bits, so:
//   n = -900000000 -> offset 0            -> 31 zero bits + 1 pad zero -> 00 00 00 00
//   n = 0          -> offset  900000000
//     900000000 = 0x35A4E900 = 0011 0101 1010 0100 1110 1001 0000 0000 (32 bits)
//     in 31 bits, dropping the leading zero:  011 0101 1010 0100 1110 1001 0000 0000
//     octet 0 = 0110 1011                    = 0x6B
//     octet 1 = 0100 1001                    = 0x49
//     octet 2 = 1101 0010                    = 0xD2
//     octet 3 = 0000 000 + one pad zero      = 0x00
//   31 bits -> ceil(31/8) = 4 octets, and reading the same 31 bits back gives 0 again.
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
}

// The eight fields j2735.h encodes, at their widths from the ranges above (X.691 16.7 gives the
// 4-octet TemporaryID a 32-bit bit-field with no length determinant):
//   msgCnt 7 + id 32 + secMark 16 + lat 31 + lon 32 + elev 16 + speed 13 + heading 15 = 162 bits
//   162 bits -> 10.1.3 appends 6 zero bits -> 21 octets
// Every field but id is at its lower bound, so every offset but id's is zero and the stream is
//   0000000 | 1010 0101 1010 0101 1010 0101 1010 0101 | 123 zero bits
// which cuts into octets as
//   octet 0 = 0000000 + id bit 1            = 0000 0001 = 0x01
//   octet 1 = id bits 2..8 + id bit 9       = 0100 1011 = 0x4B
//   octet 2 = id bits 10..16 + id bit 17    = 0100 1011 = 0x4B
//   octet 3 = id bits 18..24 + id bit 25    = 0100 1011 = 0x4B
//   octet 4 = id bits 26..32 + secMark bit 1= 0100 1010 = 0x4A
//   octets 5..20                            = 0x00
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

// Round-trip identity at both ends of every published range and at an interior point.
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

// A buffer one octet short of the 21 the field widths require is refused, and null arguments do not
// write or read.
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

// A bit past the end of the caller's buffer is refused and stays refused, so a truncated encode
// cannot be mistaken for a short one.
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

// A read that would cross the end of the declared bit count yields nothing and clears ok, whether
// it overruns by one bit or asks for more than the whole field at once.
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

// One MovementState is signalGroup 8 + eventState 4 + minEndTime 16 + maxEndTime 16 = 44 bits at
// the published widths. The element count is this module's own 5-bit field, not a J2735 length
// determinant, so the octet count is (5 + 44n + 7) / 8: n = 1 -> 7, n = 3 -> 18, n = 0 -> 1.
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

// More states than the 5-bit count can hold, null arguments, a buffer short of the 18 octets three
// states need, a decode into too few slots, and a truncated stream are all refused.
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

// The intersection head is IntersectionID 16 + two 16-bit surrogates = 48 bits, and one lane is
// LaneID 8 + a BOOLEAN 1 (X.691 11.1) + two Offset-B12 12 = 33 bits, so the octet count is
// (48 + 5 + 33n + 7) / 8: n = 0 -> 7, n = 1 -> 11, n = 2 -> 15.
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

// More lanes than the 5-bit count can hold, null arguments, a buffer short of the 11 octets one
// lane needs, a decode into zero slots, and a truncated stream are all refused.
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

// ISO/TS 19091 DSRC, MovementPhaseState: dark (1), stop-Then-Proceed (2), stop-And-Remain (3), with
// unavailable (0) ahead of them. j2735.h numbers dark 0 and stop-Then-Proceed 1 and defines neither
// unavailable nor pre-Movement, so its values are the published ones shifted down by one across the
// red group. X.691 13.1-13.2 encodes the enumeration index of the ten-item list, and the module
// writes the enum value straight into that field, so these numbers go on the wire.
void test_movement_phase_state_reds_match_the_published_enumeration(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, J2735_PHASE_DARK, "ISO/TS 19091 DSRC: dark (1), unavailable is (0)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, J2735_PHASE_STOP_THEN_PROCEED, "ISO/TS 19091 DSRC: stop-Then-Proceed (2)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, J2735_PHASE_STOP_AND_REMAIN, "ISO/TS 19091 DSRC: stop-And-Remain (3)");
}

// The same enumeration above pre-Movement (4): permissive-Movement-Allowed (5),
// protected-Movement-Allowed (6), permissive-clearance (7), protected-clearance (8),
// caution-Conflicting-Traffic (9).
void test_movement_phase_state_greens_and_clearances_match_the_published_enumeration(void)
{
    TEST_ASSERT_EQUAL_INT(5, J2735_PHASE_PERMISSIVE_MOVEMENT_ALLOWED);
    TEST_ASSERT_EQUAL_INT(6, J2735_PHASE_PROTECTED_MOVEMENT_ALLOWED);
    TEST_ASSERT_EQUAL_INT(7, J2735_PHASE_PERMISSIVE_CLEARANCE);
    TEST_ASSERT_EQUAL_INT(8, J2735_PHASE_PROTECTED_CLEARANCE);
    TEST_ASSERT_EQUAL_INT(9, J2735_PHASE_CAUTION_CONFLICTING_TRAFFIC);
}

// The enumeration has ten root items, so X.691 13.2 makes the eventState field a constrained
// integer with lb 0 and ub 9, and every index in it survives the round trip.
void test_every_movement_phase_index_survives_the_round_trip(void)
{
    uint8_t out[8];
    J2735MovementState got[1];
    size_t count = 0;

    TEST_ASSERT_EQUAL_UINT(4u, protocore_uper_cint_bits(0, 9));
    for (uint8_t idx = 0; idx <= 9; idx++)
    {
        J2735MovementState in[1] = {{1, idx, 0, 0}};
        size_t n = protocore_j2735_spat_encode(in, 1, out, sizeof(out));
        TEST_ASSERT_EQUAL_UINT(7u, n);
        TEST_ASSERT_TRUE(protocore_j2735_spat_decode(out, n, got, 1, &count));
        TEST_ASSERT_EQUAL_UINT(1u, count);
        TEST_ASSERT_EQUAL_UINT8(idx, got[0].phase);
    }
}
