// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the listen-only CAN capture framer (server/signaling/bus_capture.h).
//
// The frame this builds is not ours: it is the record body libpcap's LINKTYPE_CAN_SOCKETCAN (227)
// defines, so a capture opens in Wireshark or not at all. That layout publishes every field - a
// 4-octet BIG-ENDIAN identifier word whose bottom 29 bits are the CAN ID and whose top bits are
// CAN_EFF_FLAG 0x80000000, CAN_RTR_FLAG 0x40000000 and CAN_ERR_FLAG 0x20000000, then the payload
// length octet, an FD-flags octet, a reserved octet, a len8_dlc octet, and eight data octets.
// test_the_socketcan_layout_is_the_published_one is the load-bearing case: it spells one frame out
// octet by octet against that layout, so a little-endian identifier or a flag in the wrong bit
// produces a file no capture tool can read, and this is what catches it.
//
// ISO 11898-1 fixes the identifier widths the flags select between: 11 bits standard, 29 extended.

#include "server/signaling/bus_capture.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// A frame with @p n data octets counting up from 1.
static CanFrame frame_of(uint32_t id, proto_bool extended, proto_bool rtr, uint8_t n)
{
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = id;
    f.extended = extended;
    f.rtr = rtr;
    f.dlc = n;
    for (uint8_t i = 0; i < PROTOCORE_CAN_MAX_DLC; i++)
    {
        f.data[i] = (uint8_t)(i + 1);
    }
    return f;
}

// One standard 11-bit data frame, id 0x123, carrying "\x11\x22\x33", written out field by field.
//
//   octets 0..3  the identifier word, big-endian: 0x00000123 with no flags set
//   octet  4     the payload length, 3
//   octets 5..7  the FD-flags, reserved and len8_dlc octets, all zero for a classic frame
//   octets 8..15 the eight data octets, the payload followed by zero padding
void test_the_socketcan_layout_is_the_published_one(void)
{
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x123;
    f.dlc = 3;
    f.data[0] = 0x11;
    f.data[1] = 0x22;
    f.data[2] = 0x33;

    uint8_t out[PROTOCORE_SOCKETCAN_FRAME_LEN];
    memset(out, 0xEE, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN, can_to_socketcan(&f, out, sizeof(out)));

    static const uint8_t WANT[PROTOCORE_SOCKETCAN_FRAME_LEN] = {
        0x00, 0x00, 0x01, 0x23,                        // can_id, big-endian, no flags
        0x03,                                          // length
        0x00, 0x00, 0x00,                              // fd flags, reserved, len8_dlc
        0x11, 0x22, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00 // data, zero padded to eight
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));
}

// The record is exactly 16 octets for a classic CAN frame: 4 + 1 + 1 + 1 + 1 + 8.
void test_the_record_is_sixteen_octets(void)
{
    TEST_ASSERT_EQUAL_UINT(16u, (unsigned)PROTOCORE_SOCKETCAN_FRAME_LEN);
    TEST_ASSERT_EQUAL_UINT(227u, (unsigned)PROTOCORE_DLT_CAN_SOCKETCAN);
}

// The flag values, at the bit positions the link type publishes.
void test_the_published_flag_bits(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x80000000u, PROTOCORE_CAN_EFF_FLAG);
    TEST_ASSERT_EQUAL_HEX32(0x40000000u, PROTOCORE_CAN_RTR_FLAG);
    TEST_ASSERT_EQUAL_HEX32(0x20000000u, PROTOCORE_CAN_ERR_FLAG);
}

// Reads the identifier word back out of a record.
static uint32_t id_word(const uint8_t *rec)
{
    return ((uint32_t)rec[0] << 24) | ((uint32_t)rec[1] << 16) | ((uint32_t)rec[2] << 8) | (uint32_t)rec[3];
}

// An extended frame sets CAN_EFF_FLAG and keeps all 29 identifier bits; a standard frame sets no
// flag and keeps 11. The identifier is masked to its width, so a caller's stray high bits do not
// land on the flags.
void test_the_identifier_width_and_the_extended_flag(void)
{
    uint8_t out[PROTOCORE_SOCKETCAN_FRAME_LEN];

    CanFrame std = frame_of(0x7FF, PROTO_FALSE, PROTO_FALSE, 0);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN, can_to_socketcan(&std, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX32(0x000007FFu, id_word(out));

    CanFrame ext = frame_of(0x1FFFFFFFu, PROTO_TRUE, PROTO_FALSE, 0);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN, can_to_socketcan(&ext, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX32(PROTOCORE_CAN_EFF_FLAG | 0x1FFFFFFFu, id_word(out));

    // an 11-bit frame carrying a wider id keeps only the 11 bits, and sets no flag
    CanFrame wide_std = frame_of(0x1FFFFFFFu, PROTO_FALSE, PROTO_FALSE, 0);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN, can_to_socketcan(&wide_std, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX32(0x000007FFu, id_word(out));

    // a 29-bit frame carrying a wider id keeps only the 29 bits under the flag
    CanFrame wide_ext = frame_of(0xFFFFFFFFu, PROTO_TRUE, PROTO_FALSE, 0);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN, can_to_socketcan(&wide_ext, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX32(PROTOCORE_CAN_EFF_FLAG | 0x1FFFFFFFu, id_word(out));
}

// A remote-transmission-request frame sets CAN_RTR_FLAG and carries no data: the length still names
// how many octets were requested, but every data octet is zero.
void test_a_remote_frame_sets_its_flag_and_carries_no_data(void)
{
    uint8_t out[PROTOCORE_SOCKETCAN_FRAME_LEN];
    CanFrame rtr = frame_of(0x100, PROTO_FALSE, PROTO_TRUE, 8);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN, can_to_socketcan(&rtr, out, sizeof(out)));

    TEST_ASSERT_EQUAL_HEX32(PROTOCORE_CAN_RTR_FLAG | 0x100u, id_word(out));
    TEST_ASSERT_EQUAL_UINT8(8u, out[4]);
    for (int i = 0; i < PROTOCORE_CAN_MAX_DLC; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, out[8 + i]);
    }

    // both flags together, on an extended remote frame
    CanFrame both = frame_of(0x1ABCDEFu, PROTO_TRUE, PROTO_TRUE, 4);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN, can_to_socketcan(&both, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX32(PROTOCORE_CAN_EFF_FLAG | PROTOCORE_CAN_RTR_FLAG | 0x1ABCDEFu, id_word(out));
}

// ISO 11898-1: a classic CAN frame carries at most eight data octets, so a longer length is clamped
// and the record never grows past its sixteen octets.
void test_the_length_is_clamped_to_eight(void)
{
    uint8_t out[PROTOCORE_SOCKETCAN_FRAME_LEN];
    for (uint8_t n = 0; n <= 20u; n++)
    {
        CanFrame f = frame_of(0x200, PROTO_FALSE, PROTO_FALSE, n);
        TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN, can_to_socketcan(&f, out, sizeof(out)));
        const uint8_t want = n > PROTOCORE_CAN_MAX_DLC ? (uint8_t)PROTOCORE_CAN_MAX_DLC : n;
        TEST_ASSERT_EQUAL_UINT8(want, out[4]);
        // exactly want octets of payload, the rest zero
        for (uint8_t i = 0; i < PROTOCORE_CAN_MAX_DLC; i++)
        {
            TEST_ASSERT_EQUAL_HEX8(i < want ? (uint8_t)(i + 1) : 0x00, out[8 + i]);
        }
    }
}

// The three octets between the length and the data are reserved and written as zero, so a reader
// that interprets them (FD flags, len8_dlc) sees a classic frame rather than a random one.
void test_the_reserved_octets_are_zeroed(void)
{
    uint8_t out[PROTOCORE_SOCKETCAN_FRAME_LEN];
    memset(out, 0xFF, sizeof(out));
    CanFrame f = frame_of(0x321, PROTO_TRUE, PROTO_FALSE, 8);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN, can_to_socketcan(&f, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x00, out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[6]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[7]);
}

// The identifier word is written most significant octet first, which is what makes the record
// readable on a little-endian host: the same id in the other order would name a different frame.
void test_the_identifier_is_big_endian(void)
{
    uint8_t out[PROTOCORE_SOCKETCAN_FRAME_LEN];
    CanFrame f = frame_of(0x0A0B0C0Du & PROTOCORE_CAN_EXT_ID_MASK, PROTO_TRUE, PROTO_FALSE, 0);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN, can_to_socketcan(&f, out, sizeof(out)));
    // 0x0A0B0C0D & 0x1FFFFFFF = 0x0A0B0C0D, with the EFF flag on top
    TEST_ASSERT_EQUAL_HEX8(0x8A, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0B, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x0C, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x0D, out[3]);
}

// A buffer that cannot hold a whole record produces nothing, and writes nothing: a short record in
// a PCAP file corrupts every record after it.
void test_a_short_buffer_writes_nothing(void)
{
    CanFrame f = frame_of(0x123, PROTO_FALSE, PROTO_FALSE, 3);
    uint8_t out[PROTOCORE_SOCKETCAN_FRAME_LEN];

    for (size_t cap = 0; cap < PROTOCORE_SOCKETCAN_FRAME_LEN; cap++)
    {
        memset(out, 0xEE, sizeof(out));
        TEST_ASSERT_EQUAL_UINT(0u, can_to_socketcan(&f, out, cap));
        for (size_t i = 0; i < sizeof(out); i++)
        {
            TEST_ASSERT_EQUAL_HEX8(0xEE, out[i]);
        }
    }
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN,
                           can_to_socketcan(&f, out, PROTOCORE_SOCKETCAN_FRAME_LEN));
}

// A null frame or a null destination produces nothing rather than a dereference.
void test_null_arguments_are_refused(void)
{
    uint8_t out[PROTOCORE_SOCKETCAN_FRAME_LEN];
    CanFrame f = frame_of(0x123, PROTO_FALSE, PROTO_FALSE, 3);
    TEST_ASSERT_EQUAL_UINT(0u, can_to_socketcan(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, can_to_socketcan(&f, NULL, sizeof(out)));
}

// The framer holds nothing: the same frame produces the same record every time, and a frame
// formatted after a different one is unaffected by it.
void test_the_framer_holds_nothing(void)
{
    uint8_t a[PROTOCORE_SOCKETCAN_FRAME_LEN];
    uint8_t b[PROTOCORE_SOCKETCAN_FRAME_LEN];
    uint8_t other[PROTOCORE_SOCKETCAN_FRAME_LEN];

    CanFrame f = frame_of(0x123, PROTO_FALSE, PROTO_FALSE, 3);
    CanFrame g = frame_of(0x1FFFFFFFu, PROTO_TRUE, PROTO_TRUE, 8);

    (void)can_to_socketcan(&f, a, sizeof(a));
    (void)can_to_socketcan(&g, other, sizeof(other));
    (void)can_to_socketcan(&f, b, sizeof(b));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, b, sizeof(a));
}

// Distinct frames produce distinct records: the id, the flags, the length and the data all reach
// the wire, so none of them can be silently dropped.
static void records_differ(const CanFrame *x, const CanFrame *y)
{
    uint8_t a[PROTOCORE_SOCKETCAN_FRAME_LEN];
    uint8_t b[PROTOCORE_SOCKETCAN_FRAME_LEN];
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN, can_to_socketcan(x, a, sizeof(a)));
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_SOCKETCAN_FRAME_LEN, can_to_socketcan(y, b, sizeof(b)));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, b, sizeof(a)));
}

void test_every_frame_field_reaches_the_record(void)
{
    const CanFrame base = frame_of(0x123, PROTO_FALSE, PROTO_FALSE, 4);

    CanFrame other_id = base;
    other_id.id = 0x124;
    records_differ(&base, &other_id);

    CanFrame extended = base;
    extended.extended = PROTO_TRUE;
    records_differ(&base, &extended);

    CanFrame remote = base;
    remote.rtr = PROTO_TRUE;
    records_differ(&base, &remote);

    CanFrame shorter = base;
    shorter.dlc = 3;
    records_differ(&base, &shorter);

    CanFrame other_data = base;
    other_data.data[2] ^= 0xFFu;
    records_differ(&base, &other_data);
}

// A capture with nowhere to send its frames is refused, and draining or stopping a capture that
// never started is safe rather than a walk through a null sink.
void test_a_capture_with_no_sink_is_refused(void)
{
    TEST_ASSERT_FALSE(bus_capture_begin(4, 5, 500000u, NULL));
    bus_capture_poll();
    bus_capture_end();
}
