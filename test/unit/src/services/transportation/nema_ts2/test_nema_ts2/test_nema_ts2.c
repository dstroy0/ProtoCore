// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NEMA TS 2 traffic-cabinet SDLC frame codec
// (services/transportation/nema_ts2/nema_ts2.h).
//
// The load-bearing case is test_x25_check_value_frames_the_fcs. The FCS is the HDLC/X.25 CRC-16,
// whose published catalogue check value - the CRC of the ASCII octets "123456789" - is 0x906E. A
// frame whose address, control, frame type and data spell exactly those nine octets must therefore
// carry 6E 90 as its FCS, so one assertion pins both the CRC parameters and the low-octet-first
// transmission order.
//
// test_hdlc_good_fcs_residue is the same check from the receiver's side: RFC 1662 sec C.2 states
// that running the FCS over the frame INCLUDING its transmitted FCS yields 0xF0B8, "the complement
// of 0x0F47", and 0x0F47 is what this engine reports because its final stage already applies the
// 0xFFFF output XOR.
//
// The NEMA TS 2 standard itself is not obtainable here, so the frame-type numbering is asserted only
// as the relation the module documents (a status frame is its command frame plus 128), not against a
// published table.

#include "services/transportation/nema_ts2/nema_ts2.h"
#include "shared/crc/crc.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static uint32_t x25_of(const uint8_t *d, size_t n)
{
    Crc.args.params = &PROTOCORE_CRC16_X25;
    Crc.args.data = d;
    Crc.args.len = n;
    Crc.compute(Crc.internal);
    return Crc.value;
}

// The nine ASCII octets the CRC catalogue publishes a check value for, laid out as a TS 2 frame:
// address '1', control '2', frame type '3', data "456789". Its FCS is therefore 0x906E, low octet
// first on the wire.
void test_x25_check_value_frames_the_fcs(void)
{
    static const uint8_t DATA[6] = {'4', '5', '6', '7', '8', '9'};
    static const uint8_t WANT[11] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', 0x6E, 0x90};
    uint8_t out[16];

    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), protocore_nema_ts2_build('1', '2', '3', DATA, sizeof(DATA), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    // and the helper on its own reports the same published check value
    TEST_ASSERT_EQUAL_HEX16(0x906Eu, protocore_nema_ts2_crc(WANT, 9));
}

// RFC 1662 sec C.2: the FCS taken over the frame plus its own transmitted FCS is a fixed value. Only
// the correct parameters and the correct low-octet-first order produce it.
void test_hdlc_good_fcs_residue(void)
{
    static const uint8_t DATA[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t out[16];
    size_t n = protocore_nema_ts2_build(0x0A, 0x11, NEMA_TS2_FT_CMD_LOADSWITCH, DATA, sizeof(DATA), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(9u, n);
    TEST_ASSERT_EQUAL_HEX16(0x0F47u, (uint16_t)x25_of(out, n));

    // swapping the two FCS octets destroys the residue, which is what makes the order load bearing
    uint8_t swapped[16];
    memcpy(swapped, out, n);
    uint8_t t = swapped[n - 2];
    swapped[n - 2] = swapped[n - 1];
    swapped[n - 1] = t;
    TEST_ASSERT_NOT_EQUAL(0x0F47u, (uint16_t)x25_of(swapped, n));
}

// The frame is [address][control][frame_type][data...][FCS lo][FCS hi], and parse recovers all four.
void test_build_parse_round_trip(void)
{
    static const uint8_t DATA[8] = {0xFF, 0x00, 0x80, 0x7F, 0x55, 0xAA, 0x01, 0xFE};
    uint8_t out[32];
    NemaTs2Frame f;

    size_t n = protocore_nema_ts2_build(0x03, 0x13, NEMA_TS2_FT_DETECTOR, DATA, sizeof(DATA), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(3u + sizeof(DATA) + 2u, n);
    TEST_ASSERT_EQUAL_HEX8(0x03, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x13, out[1]);
    TEST_ASSERT_EQUAL_HEX8(NEMA_TS2_FT_DETECTOR, out[2]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, out + 3, sizeof(DATA));

    TEST_ASSERT_TRUE(protocore_nema_ts2_parse(out, n, &f));
    TEST_ASSERT_EQUAL_HEX8(0x03, f.address);
    TEST_ASSERT_EQUAL_HEX8(0x13, f.control);
    TEST_ASSERT_EQUAL_HEX8(NEMA_TS2_FT_DETECTOR, f.frame_type);
    TEST_ASSERT_EQUAL_UINT(sizeof(DATA), f.data_len);
    TEST_ASSERT_EQUAL_PTR(out + 3, f.data); // aliases the input, never copied
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, f.data, sizeof(DATA));
}

// A CRC-16 detects every single-bit error, which is the whole reason the cabinet bus carries one.
void test_parse_refuses_any_single_bit_error(void)
{
    static const uint8_t DATA[5] = {0x10, 0x20, 0x30, 0x40, 0x50};
    uint8_t good[16];
    uint8_t bad[16];
    NemaTs2Frame f;

    size_t n = protocore_nema_ts2_build(0x01, 0x02, NEMA_TS2_FT_CMD_MMU, DATA, sizeof(DATA), good, sizeof(good));
    TEST_ASSERT_EQUAL_UINT(10u, n);

    for (size_t i = 0; i < n; i++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            memcpy(bad, good, n);
            bad[i] ^= (uint8_t)(1u << bit);
            TEST_ASSERT_FALSE(protocore_nema_ts2_parse(bad, n, &f));
        }
    }
}

// Fewer than address + control + frame type + a two-octet FCS is not a frame at all.
void test_parse_refuses_short_frames(void)
{
    uint8_t out[16];
    NemaTs2Frame f;
    size_t n = protocore_nema_ts2_build(0x01, 0x02, NEMA_TS2_FT_CMD_MMU, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(5u, n);

    for (size_t shorter = 0; shorter < 5; shorter++)
    {
        TEST_ASSERT_FALSE(protocore_nema_ts2_parse(out, shorter, &f));
    }
    TEST_ASSERT_FALSE(protocore_nema_ts2_parse(NULL, n, &f));
    TEST_ASSERT_FALSE(protocore_nema_ts2_parse(out, n, NULL));
}

// A frame with no data is the shortest legal one: five octets, and no data pointer to follow.
void test_zero_length_data_frame(void)
{
    uint8_t out[16];
    NemaTs2Frame f;
    size_t n = protocore_nema_ts2_build(0x02, 0x33, NEMA_TS2_FT_CMD_MMU, NULL, 0, out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT(5u, n);
    TEST_ASSERT_TRUE(protocore_nema_ts2_parse(out, n, &f));
    TEST_ASSERT_EQUAL_HEX8(0x02, f.address);
    TEST_ASSERT_EQUAL_HEX8(0x33, f.control);
    TEST_ASSERT_EQUAL_HEX8(NEMA_TS2_FT_CMD_MMU, f.frame_type);
    TEST_ASSERT_EQUAL_UINT(0u, f.data_len);
    TEST_ASSERT_NULL(f.data);
}

// A buffer that cannot hold the whole frame produces nothing, and a null data pointer is legal only
// at length zero.
void test_build_bounds(void)
{
    static const uint8_t DATA[4] = {1, 2, 3, 4};
    uint8_t out[16];

    TEST_ASSERT_EQUAL_UINT(0u, protocore_nema_ts2_build(0, 0, 0, DATA, sizeof(DATA), out, 8)); // needs 9
    TEST_ASSERT_EQUAL_UINT(9u, protocore_nema_ts2_build(0, 0, 0, DATA, sizeof(DATA), out, 9));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_nema_ts2_build(0, 0, 0, DATA, sizeof(DATA), NULL, 16));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_nema_ts2_build(0, 0, 0, NULL, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(5u, protocore_nema_ts2_build(0, 0, 0, NULL, 0, out, sizeof(out)));
}

// An empty span folds nothing into the register, so the FCS is the init through the output stage:
// 0xFFFF reflected is 0xFFFF, XORed with 0xFFFF gives 0.
void test_crc_of_an_empty_span(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000u, protocore_nema_ts2_crc(NULL, 0));
}

// A status frame is its command frame plus 128 - the TS 2 convention the module documents.
void test_frame_type_response_offset(void)
{
    TEST_ASSERT_EQUAL_INT(NEMA_TS2_FT_CMD_LOADSWITCH + 128, NEMA_TS2_FT_STATUS_LOADSWITCH);
    TEST_ASSERT_NOT_EQUAL(NEMA_TS2_FT_CMD_MMU, NEMA_TS2_FT_DETECTOR);
    TEST_ASSERT_NOT_EQUAL(NEMA_TS2_FT_CMD_LOADSWITCH, NEMA_TS2_FT_CMD_MMU);
}
