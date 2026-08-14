// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the lane math (mmgr/swar.h).
//
// No standard publishes SWAR; every expectation here is PROPERTIES. Each branchless lane test is
// diffed against a scalar restatement of the definition it implements, written in this file and run
// over every byte value the operation is defined for, so no lane test is checked against a constant
// somebody wrote down from the same arithmetic it is meant to prove.
//
// test_has_zero_is_exact_in_every_lane is the load-bearing case. The cheap spelling of a has-zero
// test lets a lane's borrow run on into the lanes above it, so a word whose lower lane is 0x00 and
// whose next lane is 0x01 reports BOTH lanes zero. Reading only the lowest set lane hides that, and
// this header promises exactness so two masks may be ANDed and any lane read.

#include "mmgr/swar.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

#define LANES PROTOCORE_SWAR_BYTES

// One word from LANES bytes in address order, through the module's own load.
static protocore_swar_word from_lanes(const uint8_t *lanes)
{
    char buf[LANES];
    memcpy(buf, lanes, sizeof buf);
    return swar.load(buf);
}

// The same byte in every lane.
static protocore_swar_word broadcast(uint8_t b)
{
    uint8_t lanes[LANES];
    memset(lanes, b, sizeof lanes);
    return from_lanes(lanes);
}

// A guard-bit mask: 0x80 in the lanes @p set names, 0x00 elsewhere.
static protocore_swar_word guard_of(const int *set)
{
    uint8_t lanes[LANES];
    for (size_t i = 0; i < LANES; i++)
    {
        lanes[i] = set[i] ? 0x80u : 0x00u;
    }
    return from_lanes(lanes);
}

// The widened form of the same mask: 0xFF where 0x80 was.
static protocore_swar_word full_of(const int *set)
{
    uint8_t lanes[LANES];
    for (size_t i = 0; i < LANES; i++)
    {
        lanes[i] = set[i] ? 0xFFu : 0x00u;
    }
    return from_lanes(lanes);
}

// The scalar definition swar.eq folds ASCII case by: cancel the case bit out of the syndrome, and
// only on a lane whose haystack byte is a letter once bit 5 is forced on (which keeps a byte at or
// above 0x80 out, since bit 7 is not the case bit).
static int lane_eq_ci(uint8_t w, uint8_t c)
{
    uint8_t x = (uint8_t)(w ^ c);
    const uint8_t lo = (uint8_t)(w | 0x20u);
    if (lo >= 'a' && lo <= 'z' && (lo & 0x80u) == 0u)
    {
        x = (uint8_t)(x & (uint8_t)~0x20u);
    }
    return x == 0u;
}

// Every constant is derived from the carrier's width, so each lane of ONES holds 1, of HIGH holds
// 0x80 and of LOW7 holds 0x7F - and 0x80 + 0x7F is a whole lane.
void test_the_lane_constants_derive_from_the_carrier_width(void)
{
    TEST_ASSERT_EQUAL_UINT(PROTO_SWAR_BITS, (unsigned)(sizeof(protocore_swar_word) * 8u));
    TEST_ASSERT_EQUAL_size_t((size_t)(PROTO_SWAR_BITS / 8u), LANES);

    uint8_t ones[LANES], high[LANES], low7[LANES];
    const protocore_swar_word o = PROTOCORE_SWAR_ONES, h = PROTOCORE_SWAR_HIGH, l = PROTOCORE_SWAR_LOW7;
    memcpy(ones, &o, sizeof ones);
    memcpy(high, &h, sizeof high);
    memcpy(low7, &l, sizeof low7);
    for (size_t i = 0; i < LANES; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x01, ones[i]);
        TEST_ASSERT_EQUAL_HEX8(0x80, high[i]);
        TEST_ASSERT_EQUAL_HEX8(0x7F, low7[i]);
    }
    TEST_ASSERT_TRUE((protocore_swar_word)(h | l) == (protocore_swar_word) ~(protocore_swar_word)0);
    TEST_ASSERT_TRUE((protocore_swar_word)(h & l) == 0);
}

// Exact per lane: the mask names the zero lanes and only those, whatever the neighbours hold.
void test_has_zero_is_exact_in_every_lane(void)
{
    static const uint8_t FILL[] = {0x01, 0x02, 0x7F, 0x80, 0xFE, 0xFF};

    for (size_t f = 0; f < sizeof FILL / sizeof FILL[0]; f++)
    {
        for (size_t at = 0; at < LANES; at++)
        {
            uint8_t lanes[LANES];
            int set[LANES];
            memset(lanes, FILL[f], sizeof lanes);
            for (size_t i = 0; i < LANES; i++)
            {
                set[i] = 0;
            }
            lanes[at] = 0x00;
            set[at] = 1;
            TEST_ASSERT_TRUE(swar.has_zero(from_lanes(lanes)) == guard_of(set));
        }
    }

    // The regression the exact form exists for: a zero lane next to a lane holding 0x01. A borrow
    // that leaves its lane marks the neighbour as zero too.
    if (LANES >= 2)
    {
        uint8_t lanes[LANES];
        int set[LANES];
        memset(lanes, 0xFF, sizeof lanes);
        for (size_t i = 0; i < LANES; i++)
        {
            set[i] = 0;
        }
        lanes[0] = 0x00;
        lanes[1] = 0x01;
        set[0] = 1;
        TEST_ASSERT_TRUE(swar.has_zero(from_lanes(lanes)) == guard_of(set));
    }

    // Every lane zero, and no lane zero.
    TEST_ASSERT_TRUE(swar.has_zero(broadcast(0x00)) == PROTOCORE_SWAR_HIGH);
    TEST_ASSERT_TRUE(swar.has_zero(broadcast(0xFF)) == 0);
    TEST_ASSERT_TRUE(swar.has_zero(broadcast(0x80)) == 0); // a set guard bit is not a zero lane
    TEST_ASSERT_TRUE(swar.has_zero(broadcast(0x01)) == 0);
}

// The mask states the lane, in address order: the lowest-addressed zero byte. That is the one place
// byte order enters the module, so it is pinned against bytes at known positions.
void test_zero_lane_is_the_first_in_address_order(void)
{
    for (size_t at = 0; at < LANES; at++)
    {
        char buf[LANES];
        memset(buf, 'z', sizeof buf);
        buf[at] = '\0';
        const protocore_swar_word m = swar.has_zero(swar.load(buf));
        TEST_ASSERT_TRUE(m != 0);
        TEST_ASSERT_EQUAL_size_t(at, swar.zero_lane(m));
    }

    // With two zero lanes the answer is the earlier address, not the other one.
    for (size_t a = 0; a + 1 < LANES; a++)
    {
        for (size_t b = a + 1; b < LANES; b++)
        {
            char buf[LANES];
            memset(buf, 'z', sizeof buf);
            buf[a] = '\0';
            buf[b] = '\0';
            TEST_ASSERT_EQUAL_size_t(a, swar.zero_lane(swar.has_zero(swar.load(buf))));
        }
    }
}

// The exact byte test, against equality itself, over every (lane, target) pair.
void test_eq_matches_equality_on_every_byte(void)
{
    for (unsigned c = 0; c < 256u; c++)
    {
        for (unsigned x = 0; x < 256u; x++)
        {
            const protocore_swar_word got = swar.eq(broadcast((uint8_t)x), (uint8_t)c, PROTO_FALSE);
            const protocore_swar_word want = (x == c) ? PROTOCORE_SWAR_HIGH : (protocore_swar_word)0;
            TEST_ASSERT_TRUE(got == want);
        }
    }
}

// The case-folding test, against the scalar restatement of its rule, over every pair. The pairs that
// matter are the ones a naive "mask bit 5 off both sides" would merge: '0' with DLE, '@' with '`',
// and a byte at or above 0x80 with the one 0x20 away from it.
void test_eq_ci_matches_the_scalar_rule_on_every_byte(void)
{
    for (unsigned c = 0; c < 256u; c++)
    {
        for (unsigned x = 0; x < 256u; x++)
        {
            const protocore_swar_word got = swar.eq(broadcast((uint8_t)x), (uint8_t)c, PROTO_TRUE);
            const protocore_swar_word want = lane_eq_ci((uint8_t)x, (uint8_t)c) ? PROTOCORE_SWAR_HIGH : 0;
            TEST_ASSERT_TRUE(got == want);
        }
    }

    // The named pairs, stated rather than left implicit in the sweep above.
    TEST_ASSERT_TRUE(swar.eq(broadcast('A'), 'a', PROTO_TRUE) == PROTOCORE_SWAR_HIGH);
    TEST_ASSERT_TRUE(swar.eq(broadcast('a'), 'A', PROTO_TRUE) == PROTOCORE_SWAR_HIGH);
    TEST_ASSERT_TRUE(swar.eq(broadcast('A'), 'a', PROTO_FALSE) == 0);
    TEST_ASSERT_TRUE(swar.eq(broadcast(0x30), 0x10, PROTO_TRUE) == 0); // '0' is not DLE
    TEST_ASSERT_TRUE(swar.eq(broadcast(0x40), 0x60, PROTO_TRUE) == 0); // '@' is not '`'
    TEST_ASSERT_TRUE(swar.eq(broadcast(0xDB), 0xFB, PROTO_TRUE) == 0); // not ASCII, so no case
}

// Lane independence: several different bytes in one word, and only the matching lanes fire.
void test_the_lane_tests_are_independent_across_lanes(void)
{
    uint8_t lanes[LANES];
    int set[LANES];
    for (size_t i = 0; i < LANES; i++)
    {
        lanes[i] = (i % 2u) ? (uint8_t)'a' : (uint8_t)'A';
        set[i] = 0;
    }
    const protocore_swar_word w = from_lanes(lanes);

    for (size_t i = 0; i < LANES; i++)
    {
        set[i] = (lanes[i] == 'a');
    }
    TEST_ASSERT_TRUE(swar.eq(w, 'a', PROTO_FALSE) == guard_of(set));

    for (size_t i = 0; i < LANES; i++)
    {
        set[i] = 1; // case folded, every lane matches
    }
    TEST_ASSERT_TRUE(swar.eq(w, 'a', PROTO_TRUE) == guard_of(set));

    // The syndrome is zero in exactly the lanes that match, so ORing two exact masks answers for two
    // delimiters off one load.
    TEST_ASSERT_TRUE(swar.xor_(w, w, PROTO_FALSE) == 0);
    TEST_ASSERT_TRUE(swar.xor_(w, broadcast('a'), PROTO_FALSE) == (protocore_swar_word)(w ^ broadcast('a')));
    TEST_ASSERT_TRUE(swar.has_zero(swar.xor_(w, broadcast('a'), PROTO_TRUE)) == swar.eq(w, 'a', PROTO_TRUE));
}

// The guard-bit compares answer for 7-bit lanes: above 0x7F the lane's own guard bit is already set,
// so the borrow the comparison rests on has nowhere to go. Every pair in that range is checked.
void test_ge_and_le_match_the_scalar_compares_on_seven_bit_lanes(void)
{
    for (unsigned v = 0; v < 128u; v++)
    {
        for (unsigned a = 0; a < 128u; a++)
        {
            const protocore_swar_word w = broadcast((uint8_t)a);
            TEST_ASSERT_TRUE(swar.ge(w, (protocore_swar_word)v) == ((a >= v) ? PROTOCORE_SWAR_HIGH : 0));
            TEST_ASSERT_TRUE(swar.le(w, (protocore_swar_word)v) == ((a <= v) ? PROTOCORE_SWAR_HIGH : 0));
        }
    }

    // The window the base64 decoder classifies with: A-Z selected and nothing else, per lane.
    uint8_t lanes[LANES];
    int set[LANES];
    static const uint8_t SAMPLE[4] = {'A', '0', 'Z', 'a'};
    for (size_t i = 0; i < LANES; i++)
    {
        lanes[i] = SAMPLE[i % 4u];
        set[i] = (lanes[i] >= 'A' && lanes[i] <= 'Z');
    }
    const protocore_swar_word w = from_lanes(lanes);
    TEST_ASSERT_TRUE((swar.ge(w, 'A') & swar.le(w, 'Z')) == guard_of(set));
}

// Widening a guard mask to a whole lane must not carry into the lane above it.
void test_spread_widens_a_guard_mask_without_carrying(void)
{
    int set[LANES];
    for (size_t at = 0; at < LANES; at++)
    {
        for (size_t i = 0; i < LANES; i++)
        {
            set[i] = 0;
        }
        set[at] = 1;
        TEST_ASSERT_TRUE(swar.spread(guard_of(set)) == full_of(set));
    }
    for (size_t i = 0; i < LANES; i++)
    {
        set[i] = 1;
    }
    TEST_ASSERT_TRUE(swar.spread(guard_of(set)) == full_of(set));
    TEST_ASSERT_TRUE(swar.spread(0) == 0);
    TEST_ASSERT_TRUE(swar.spread(PROTOCORE_SWAR_HIGH) == (protocore_swar_word) ~(protocore_swar_word)0);
}

// The lane-local subtraction: (lane - lo) in the low seven bits, the guard bit taking the borrow, so
// a lane below lo wraps inside itself instead of reaching the lane above.
void test_sub7_is_the_lane_local_subtraction(void)
{
    for (unsigned lo = 0; lo < 128u; lo++)
    {
        for (unsigned a = 0; a < 128u; a++)
        {
            // (0x80 | a) - lo, keeping the low seven bits: the guard bit absorbs the borrow.
            const uint8_t want = (uint8_t)((0x80u + a - lo) & 0x7Fu);
            uint8_t lanes[LANES];
            memset(lanes, (int)(uint8_t)a, sizeof lanes);
            const protocore_swar_word got = swar.sub7(from_lanes(lanes), (protocore_swar_word)lo);
            uint8_t out[LANES];
            memcpy(out, &got, sizeof out);
            for (size_t i = 0; i < LANES; i++)
            {
                TEST_ASSERT_EQUAL_HEX8(want, out[i]);
            }
        }
    }
}

// The aligned load is the same value for an address that carries the alignment; only the disclaimer
// differs. The unaligned one answers at every offset.
void test_the_two_loads_agree_where_both_are_legal(void)
{
    static char buf[64] __attribute__((aligned(16)));
    for (size_t i = 0; i < sizeof buf; i++)
    {
        buf[i] = (char)(0x21 + i);
    }
    for (size_t off = 0; off + LANES <= sizeof buf; off += LANES)
    {
        TEST_ASSERT_TRUE(swar.load(&buf[off]) == swar.load_al(&buf[off]));
    }
    // Whatever the alignment, the unaligned load reads the bytes that are there.
    for (size_t off = 0; off + LANES <= sizeof buf; off++)
    {
        uint8_t lanes[LANES];
        memcpy(lanes, &buf[off], sizeof lanes);
        TEST_ASSERT_TRUE(swar.load(&buf[off]) == from_lanes(lanes));
    }
}

// The table names ten lane tests and six of them share the (word, word) -> word shape, so a swapped
// pair type-checks and links; only pointer identity catches it.
void test_the_table_is_wired_to_the_named_lane_tests(void)
{
    TEST_ASSERT_EQUAL_PTR(protocore_swar_ge, swar.ge);
    TEST_ASSERT_EQUAL_PTR(protocore_swar_le, swar.le);
    TEST_ASSERT_EQUAL_PTR(protocore_swar_spread, swar.spread);
    TEST_ASSERT_EQUAL_PTR(protocore_swar_sub7, swar.sub7);
    TEST_ASSERT_EQUAL_PTR(protocore_swar_has_zero, swar.has_zero);
    TEST_ASSERT_EQUAL_PTR(protocore_swar_eq_sel, swar.eq);
    TEST_ASSERT_EQUAL_PTR(protocore_swar_xor_sel, swar.xor_);
    TEST_ASSERT_EQUAL_PTR(protocore_swar_zero_lane, swar.zero_lane);
    TEST_ASSERT_EQUAL_PTR(protocore_swar_load, swar.load);
    TEST_ASSERT_EQUAL_PTR(protocore_swar_load_al, swar.load_al);
}
