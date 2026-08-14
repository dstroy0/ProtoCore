// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the fixed-width serializers (mmgr/endian.h).
//
// test_rfc1071_normal_order_is_the_big_endian_read is the load-bearing case. RFC 1071 sec 3 prints
// one octet string, 00 01 f2 03 f4 f5 f6 f7, and states what each 16- and 32-bit field of it is in
// "Normal" (network) order and in "Swapped" order. Those are exactly the big- and little-endian
// readings, published as numbers rather than as a rule, so a reader whose shift ladder is inverted
// or off by a byte disagrees with the RFC's own table.
//
// RFC 4251 sec 5 supplies the second anchor: "the value 699921578 (0x29b7f4aa) is stored as
// 29 b7 f4 aa", which is the writer's side of the same claim.

#include "mmgr/endian.h"
#include <string.h>

#include <unity.h>

#define GUARD 8u
#define BODY 16u
#define POISON 0xAAu

static uint8_t buf[GUARD + BODY + GUARD];

void setUp(void)
{
    memset(buf, POISON, sizeof(buf));
}

void tearDown(void)
{
}

// The body byte at @p off, with a poisoned guard on either side.
static uint8_t *at(size_t off)
{
    return buf + GUARD + off;
}

// Every byte outside [off, off + n) still reads POISON.
static void assert_only_the_width_moved(size_t off, size_t n)
{
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        if (i >= GUARD + off && i < GUARD + off + n)
        {
            continue;
        }
        TEST_ASSERT_EQUAL_HEX8(POISON, buf[i]);
    }
}

// RFC 1071 sec 3's octet string.
static const uint8_t RFC1071[8] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};

// ---- the published readings -----------------------------------------------

// RFC 1071 sec 3, "Normal" order column:
//   Byte 0/1: 00 01 -> 0001    Byte 2/3: f2 03 -> f203
//   Byte 4/5: f4 f5 -> f4f5    Byte 6/7: f6 f7 -> f6f7
//   Byte 0/1/2/3 -> 0001f203   Byte 4/5/6/7 -> f4f5f6f7
void test_rfc1071_normal_order_is_the_big_endian_read(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0001u, endian.rd16be(RFC1071 + 0));
    TEST_ASSERT_EQUAL_HEX16(0xf203u, endian.rd16be(RFC1071 + 2));
    TEST_ASSERT_EQUAL_HEX16(0xf4f5u, endian.rd16be(RFC1071 + 4));
    TEST_ASSERT_EQUAL_HEX16(0xf6f7u, endian.rd16be(RFC1071 + 6));
    TEST_ASSERT_EQUAL_HEX32(0x0001f203u, endian.rd32be(RFC1071 + 0));
    TEST_ASSERT_EQUAL_HEX32(0xf4f5f6f7u, endian.rd32be(RFC1071 + 4));
    // The whole string as one 64-bit field: the two 32-bit halves in the same order.
    TEST_ASSERT_EQUAL_HEX64(0x0001f203f4f5f6f7ull, endian.rd64be(RFC1071));
}

// RFC 1071 sec 3, "Swapped" order column:
//   Byte 0/1 -> 0100   Byte 2/3 -> 03f2   Byte 4/5 -> f5f4   Byte 6/7 -> f7f6
//   Byte 0/1/2/3 -> 03f20100              Byte 4/5/6/7 -> f7f6f5f4
void test_rfc1071_swapped_order_is_the_little_endian_read(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0100u, endian.rd16le(RFC1071 + 0));
    TEST_ASSERT_EQUAL_HEX16(0x03f2u, endian.rd16le(RFC1071 + 2));
    TEST_ASSERT_EQUAL_HEX16(0xf5f4u, endian.rd16le(RFC1071 + 4));
    TEST_ASSERT_EQUAL_HEX16(0xf7f6u, endian.rd16le(RFC1071 + 6));
    TEST_ASSERT_EQUAL_HEX32(0x03f20100u, endian.rd32le(RFC1071 + 0));
    TEST_ASSERT_EQUAL_HEX32(0xf7f6f5f4u, endian.rd32le(RFC1071 + 4));
    TEST_ASSERT_EQUAL_HEX64(0xf7f6f5f403f20100ull, endian.rd64le(RFC1071));
}

// RFC 4251 sec 5: "the value 699921578 (0x29b7f4aa) is stored as 29 b7 f4 aa".
void test_rfc4251_uint32_octets(void)
{
    static const uint8_t WANT[4] = {0x29, 0xb7, 0xf4, 0xaa};
    TEST_ASSERT_EQUAL_size_t(4, endian.wr32be(at(0), 699921578u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, at(0), 4);
    assert_only_the_width_moved(0, 4);
    TEST_ASSERT_EQUAL_HEX32(699921578u, endian.rd32be(at(0)));
}

// RFC 4251 sec 5: a uint64 is "eight bytes in the order of decreasing significance", and the
// little-endian form of the same value is that string reversed.
void test_uint64_octets_in_decreasing_significance(void)
{
    static const uint8_t BE[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    static const uint8_t LE[8] = {0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01};

    TEST_ASSERT_EQUAL_size_t(8, endian.wr64be(at(0), 0x0123456789abcdefull));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BE, at(0), 8);
    assert_only_the_width_moved(0, 8);

    memset(buf, POISON, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(8, endian.wr64le(at(0), 0x0123456789abcdefull));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(LE, at(0), 8);
    assert_only_the_width_moved(0, 8);
}

// ---- what the writers promise ---------------------------------------------

// Each writer returns the width it moved, so a caller advances a cursor by the return value.
void test_writers_return_their_width(void)
{
    TEST_ASSERT_EQUAL_size_t(2, endian.wr16be(at(0), 0x1122u));
    TEST_ASSERT_EQUAL_size_t(4, endian.wr32be(at(0), 0x11223344u));
    TEST_ASSERT_EQUAL_size_t(8, endian.wr64be(at(0), 0x1122334455667788ull));
    TEST_ASSERT_EQUAL_size_t(2, endian.wr16le(at(0), 0x1122u));
    TEST_ASSERT_EQUAL_size_t(4, endian.wr32le(at(0), 0x11223344u));
    TEST_ASSERT_EQUAL_size_t(8, endian.wr64le(at(0), 0x1122334455667788ull));
}

// A narrow write leaves the bytes above it alone, so packed fields do not clobber each other.
void test_adjacent_fields_do_not_overlap(void)
{
    endian.wr16be(at(0), 0x1122u);
    endian.wr32be(at(2), 0x33445566u);
    endian.wr16be(at(6), 0x7788u);
    TEST_ASSERT_EQUAL_HEX16(0x1122u, endian.rd16be(at(0)));
    TEST_ASSERT_EQUAL_HEX32(0x33445566u, endian.rd32be(at(2)));
    TEST_ASSERT_EQUAL_HEX16(0x7788u, endian.rd16be(at(6)));
    assert_only_the_width_moved(0, 8);
}

static const uint64_t VALS[] = {0u,
                                1u,
                                0xFFu,
                                0x0100u,
                                0xFFFFu,
                                0x00010203u,
                                0x89ABCDEFu,
                                0xFFFFFFFFu,
                                0x0123456789ABCDEFull,
                                0xFEDCBA9876543210ull,
                                0xFFFFFFFFFFFFFFFFull};

#define NVALS (sizeof(VALS) / sizeof(VALS[0]))

// A byte-at-a-time move needs no alignment, so every width round-trips at every offset in a word.
void test_round_trip_at_every_offset(void)
{
    for (size_t off = 0; off < 8u; off++)
    {
        for (unsigned v = 0; v < NVALS; v++)
        {
            endian.wr16be(at(off), (uint16_t)VALS[v]);
            TEST_ASSERT_EQUAL_HEX16((uint16_t)VALS[v], endian.rd16be(at(off)));
            endian.wr32be(at(off), (uint32_t)VALS[v]);
            TEST_ASSERT_EQUAL_HEX32((uint32_t)VALS[v], endian.rd32be(at(off)));
            endian.wr64be(at(off), VALS[v]);
            TEST_ASSERT_EQUAL_HEX64(VALS[v], endian.rd64be(at(off)));

            endian.wr16le(at(off), (uint16_t)VALS[v]);
            TEST_ASSERT_EQUAL_HEX16((uint16_t)VALS[v], endian.rd16le(at(off)));
            endian.wr32le(at(off), (uint32_t)VALS[v]);
            TEST_ASSERT_EQUAL_HEX32((uint32_t)VALS[v], endian.rd32le(at(off)));
            endian.wr64le(at(off), VALS[v]);
            TEST_ASSERT_EQUAL_HEX64(VALS[v], endian.rd64le(at(off)));
        }
    }
}

// The two orders are byte reversals of each other at every width, whatever the value.
void test_big_endian_is_the_byte_reverse_of_little_endian(void)
{
    uint8_t b[8];
    uint8_t l[8];
    for (unsigned v = 0; v < NVALS; v++)
    {
        endian.wr16be(b, (uint16_t)VALS[v]);
        endian.wr16le(l, (uint16_t)VALS[v]);
        for (unsigned i = 0; i < 2u; i++)
        {
            TEST_ASSERT_EQUAL_HEX8(b[i], l[1u - i]);
        }
        endian.wr32be(b, (uint32_t)VALS[v]);
        endian.wr32le(l, (uint32_t)VALS[v]);
        for (unsigned i = 0; i < 4u; i++)
        {
            TEST_ASSERT_EQUAL_HEX8(b[i], l[3u - i]);
        }
        endian.wr64be(b, VALS[v]);
        endian.wr64le(l, VALS[v]);
        for (unsigned i = 0; i < 8u; i++)
        {
            TEST_ASSERT_EQUAL_HEX8(b[i], l[7u - i]);
        }
    }
}

// A narrow write takes the low bits of the value and drops the rest rather than carrying them out.
void test_a_narrow_write_drops_the_bits_above_its_width(void)
{
    endian.wr16be(at(0), (uint16_t)0x12345678u);
    TEST_ASSERT_EQUAL_HEX16(0x5678u, endian.rd16be(at(0)));
    endian.wr32le(at(0), (uint32_t)0x123456789ABCDEF0ull);
    TEST_ASSERT_EQUAL_HEX32(0x9ABCDEF0u, endian.rd32le(at(0)));
}
