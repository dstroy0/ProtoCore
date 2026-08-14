// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the binary64 field reads (mmgr/float_bits.h).
//
// IEEE 754 sec 3.4 fixes the binary64 interchange encoding: one sign bit, an 11-bit biased
// exponent with bias 1023, and a 52-bit trailing significand, and it fixes the value a triple
// denotes.
//
// test_ieee754_published_encodings is the load-bearing case. Every expected word there is derived
// from that definition in the comment beside it - 1.0 is 2^0 so its exponent field is 0 + 1023 -
// rather than read out of this module or any other float formatter, so a bias off by one or a
// shift off by a bit shows up as a wrong word instead of agreeing with itself.

#include "mmgr/float_bits.h"

#include <unity.h>

void setUp(void)
{
}

void tearDown(void)
{
}

// The three fields of @p v, merged back into the word they came from.
static proto_u64 split_and_merge(double v)
{
    return dbl.merge(dbl.sign(v), dbl.exp(v), dbl.mant(v));
}

// ---- the interchange layout ------------------------------------------------

// IEEE 754 sec 3.4: k = 64 bits, w = 11 exponent bits, t = 52 trailing significand bits, and
// 1 + 11 + 52 = 64, so the three masks partition the word with nothing left over and no overlap.
void test_ieee754_binary64_field_layout(void)
{
    TEST_ASSERT_EQUAL_UINT(8, sizeof(double));
    TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFFFFFFull, PROTO_DBL_SIGN_MASK | PROTO_DBL_EXP_MASK | PROTO_DBL_MANT_MASK);
    TEST_ASSERT_EQUAL_HEX64(0, PROTO_DBL_SIGN_MASK & PROTO_DBL_EXP_MASK);
    TEST_ASSERT_EQUAL_HEX64(0, PROTO_DBL_SIGN_MASK & PROTO_DBL_MANT_MASK);
    TEST_ASSERT_EQUAL_HEX64(0, PROTO_DBL_EXP_MASK & PROTO_DBL_MANT_MASK);

    TEST_ASSERT_EQUAL_UINT(63, PROTO_DBL_SIGN_SHIFT);
    TEST_ASSERT_EQUAL_UINT(52, PROTO_DBL_MANT_BITS);
    TEST_ASSERT_EQUAL_HEX64(0x7FFull, PROTO_DBL_EXP_ALL); // 11 bits, all set
    TEST_ASSERT_EQUAL_INT(1023, PROTO_DBL_BIAS);          // sec 3.3: bias = 2^(w-1) - 1
    TEST_ASSERT_EQUAL_HEX64(PROTO_DBL_EXP_MASK, PROTO_DBL_EXP_ALL << PROTO_DBL_MANT_BITS);
    TEST_ASSERT_EQUAL_HEX64(PROTO_DBL_SIGN_MASK, PROTO_DBL_SIGN_ONE << PROTO_DBL_SIGN_SHIFT);
}

// ---- encodings derived from the definition ---------------------------------

// A finite normal value is (-1)^s * 2^(E - 1023) * (1 + t/2^52), so the biased exponent field is
// the power of two plus 1023 and the trailing significand is the fraction after the leading 1.
//
//    1.0 = +2^0  * 1.0   -> s 0, E = 0 + 1023 = 1023 = 0x3FF, t = 0 -> 0x3FF0000000000000
//    2.0 = +2^1  * 1.0   -> s 0, E = 1 + 1023 = 1024 = 0x400, t = 0 -> 0x4000000000000000
//   -2.0 = -2^1  * 1.0   -> s 1                                     -> 0xC000000000000000
//    0.5 = +2^-1 * 1.0   -> s 0, E = -1 + 1023 = 1022 = 0x3FE, t = 0 -> 0x3FE0000000000000
//    3.0 = +2^1  * 1.5   -> s 0, E = 0x400, t = 0.5 * 2^52 = 0x8000000000000
//                                                                   -> 0x4008000000000000
void test_ieee754_published_encodings(void)
{
    TEST_ASSERT_EQUAL_HEX64(0x3FF0000000000000ull, split_and_merge(1.0));
    TEST_ASSERT_EQUAL_HEX64(0x4000000000000000ull, split_and_merge(2.0));
    TEST_ASSERT_EQUAL_HEX64(0xC000000000000000ull, split_and_merge(-2.0));
    TEST_ASSERT_EQUAL_HEX64(0x3FE0000000000000ull, split_and_merge(0.5));
    TEST_ASSERT_EQUAL_HEX64(0x4008000000000000ull, split_and_merge(3.0));

    // The same words, decoded: from_bits is the inverse of the field reads.
    TEST_ASSERT_TRUE(dbl.from_bits(0x3FF0000000000000ull) == 1.0);
    TEST_ASSERT_TRUE(dbl.from_bits(0x4000000000000000ull) == 2.0);
    TEST_ASSERT_TRUE(dbl.from_bits(0xC000000000000000ull) == -2.0);
    TEST_ASSERT_TRUE(dbl.from_bits(0x3FE0000000000000ull) == 0.5);
    TEST_ASSERT_TRUE(dbl.from_bits(0x4008000000000000ull) == 3.0);
}

// sec 3.4: E = 0 with t = 0 is a zero, and the sign bit is the only thing that distinguishes the
// two of them - which is why a sign test cannot be a comparison against 0.0.
void test_the_two_zeros_differ_only_in_the_sign(void)
{
    TEST_ASSERT_EQUAL_HEX64(0, dbl.sign(0.0));
    TEST_ASSERT_EQUAL_HEX64(0, dbl.exp(0.0));
    TEST_ASSERT_EQUAL_HEX64(0, dbl.mant(0.0));
    TEST_ASSERT_EQUAL_HEX64(0x0000000000000000ull, split_and_merge(0.0));

    TEST_ASSERT_EQUAL_HEX64(1, dbl.sign(-0.0));
    TEST_ASSERT_EQUAL_HEX64(0, dbl.exp(-0.0));
    TEST_ASSERT_EQUAL_HEX64(0, dbl.mant(-0.0));
    TEST_ASSERT_EQUAL_HEX64(0x8000000000000000ull, split_and_merge(-0.0));
}

// sec 3.4: E = 2^w - 1 = 2047 with t = 0 is an infinity, and with t nonzero a NaN. The exponent
// field alone does not separate them; the significand does.
void test_infinity_and_nan_share_the_all_ones_exponent(void)
{
    double inf = dbl.from_bits(0x7FF0000000000000ull);
    double neg_inf = dbl.from_bits(0xFFF0000000000000ull);
    double nan_v = dbl.from_bits(0x7FF0000000000001ull);

    TEST_ASSERT_EQUAL_HEX64(PROTO_DBL_EXP_ALL, dbl.exp(inf));
    TEST_ASSERT_EQUAL_HEX64(0, dbl.mant(inf));
    TEST_ASSERT_EQUAL_HEX64(0, dbl.sign(inf));

    TEST_ASSERT_EQUAL_HEX64(PROTO_DBL_EXP_ALL, dbl.exp(neg_inf));
    TEST_ASSERT_EQUAL_HEX64(0, dbl.mant(neg_inf));
    TEST_ASSERT_EQUAL_HEX64(1, dbl.sign(neg_inf));

    TEST_ASSERT_EQUAL_HEX64(PROTO_DBL_EXP_ALL, dbl.exp(nan_v));
    TEST_ASSERT_EQUAL_HEX64(1, dbl.mant(nan_v));

    // A NaN payload survives the split even though no comparison could check it.
    TEST_ASSERT_EQUAL_HEX64(0x7FF00000DEADBEEFull, split_and_merge(dbl.from_bits(0x7FF00000DEADBEEFull)));
}

// sec 3.4: E = 0 with t nonzero is subnormal, value 2^-1022 * (t / 2^52) with no leading 1. The
// two ends of that range and the smallest normal above it:
//
//   smallest positive subnormal  2^-1074  -> E = 0,    t = 1              -> 0x0000000000000001
//   largest  positive subnormal           -> E = 0,    t = 2^52 - 1       -> 0x000FFFFFFFFFFFFF
//   smallest positive normal     2^-1022  -> E = 1,    t = 0              -> 0x0010000000000000
void test_the_subnormal_boundary(void)
{
    double tiny = dbl.from_bits(0x0000000000000001ull);
    TEST_ASSERT_EQUAL_HEX64(0, dbl.exp(tiny));
    TEST_ASSERT_EQUAL_HEX64(1, dbl.mant(tiny));
    TEST_ASSERT_EQUAL_HEX64(0x0000000000000001ull, split_and_merge(tiny));

    double biggest_sub = dbl.from_bits(0x000FFFFFFFFFFFFFull);
    TEST_ASSERT_EQUAL_HEX64(0, dbl.exp(biggest_sub));
    TEST_ASSERT_EQUAL_HEX64(PROTO_DBL_MANT_MASK, dbl.mant(biggest_sub));

    double smallest_normal = dbl.from_bits(0x0010000000000000ull);
    TEST_ASSERT_EQUAL_HEX64(1, dbl.exp(smallest_normal));
    TEST_ASSERT_EQUAL_HEX64(0, dbl.mant(smallest_normal));
}

// The largest finite binary64 is (2 - 2^-52) * 2^1023: the highest exponent that is not the
// all-ones pattern, E = 2046 = 0x7FE, with every significand bit set.
//   0x7FEFFFFFFFFFFFFF
void test_the_largest_finite_value(void)
{
    double big = dbl.from_bits(0x7FEFFFFFFFFFFFFFull);
    TEST_ASSERT_EQUAL_HEX64(2046, dbl.exp(big));
    TEST_ASSERT_EQUAL_HEX64(PROTO_DBL_MANT_MASK, dbl.mant(big));
    TEST_ASSERT_EQUAL_HEX64(0x7FEFFFFFFFFFFFFFull, split_and_merge(big));
}

// ---- the merge -------------------------------------------------------------

// Each field is masked to its own width on the way in, so bits above a field are dropped rather
// than carried into its neighbour.
void test_merge_masks_each_field(void)
{
    proto_u64 clean = dbl.merge(1u, 1024u, 0x921F9F01B866Eull);
    proto_u64 dirty = dbl.merge(0xFFFFFFFFFFFFFFFFull, 0xFFFFF800u | 1024u, ~PROTO_DBL_MANT_MASK | 0x921F9F01B866Eull);
    TEST_ASSERT_EQUAL_HEX64(clean, dirty);
    TEST_ASSERT_EQUAL_HEX64(0xC00921F9F01B866Eull, clean); // sign 1, E 0x400, t 0x921F9F01B866E
}

// ---- exhaustive round trips ------------------------------------------------

// Every one of the 64 bit positions survives split and merge, so no field is a bit short or a bit
// wide at either boundary.
void test_every_bit_position_survives_the_split(void)
{
    for (unsigned i = 0; i < 64u; i++)
    {
        proto_u64 bits = 1ull << i;
        TEST_ASSERT_EQUAL_HEX64(bits, split_and_merge(dbl.from_bits(bits)));
    }
}

// The whole exponent field, stepped one value at a time from 0 to 2047, against five significands
// and both signs. Stepping rather than sampling puts every knee under the sweep: the subnormal
// boundary at 0 to 1, the bias, and the non-finite boundary at 2046 to 2047.
void test_the_exponent_field_walks_its_whole_range(void)
{
    static const proto_u64 MANTS[] = {0ull, 1ull, 0x8000000000000ull, 0x921F9F01B866Eull, PROTO_DBL_MANT_MASK};
    for (proto_u64 e = 0; e <= PROTO_DBL_EXP_ALL; e++)
    {
        for (proto_u64 s = 0; s <= 1ull; s++)
        {
            for (unsigned m = 0; m < sizeof(MANTS) / sizeof(MANTS[0]); m++)
            {
                proto_u64 in = dbl.merge(s, e, MANTS[m]);
                double v = dbl.from_bits(in);
                TEST_ASSERT_EQUAL_HEX64(s, dbl.sign(v));
                TEST_ASSERT_EQUAL_HEX64(e, dbl.exp(v));
                TEST_ASSERT_EQUAL_HEX64(MANTS[m], dbl.mant(v));
                TEST_ASSERT_EQUAL_HEX64(in, split_and_merge(v));
            }
        }
    }
}

// A single set bit walked through the significand, and a single clear bit walked through a full
// one: each isolates a position with no neighbour to cover for a shift that lost it.
void test_a_walking_significand_bit_survives(void)
{
    for (unsigned i = 0; i < PROTO_DBL_MANT_BITS; i++)
    {
        proto_u64 one = 1ull << i;
        proto_u64 cases[4];
        cases[0] = dbl.merge(0ull, 1023ull, one);
        cases[1] = dbl.merge(1ull, 1023ull, PROTO_DBL_MANT_MASK & ~one);
        cases[2] = dbl.merge(0ull, 0ull, one);              // subnormal
        cases[3] = dbl.merge(0ull, PROTO_DBL_EXP_ALL, one); // NaN payload
        for (unsigned c = 0; c < 4u; c++)
        {
            TEST_ASSERT_EQUAL_HEX64(cases[c], split_and_merge(dbl.from_bits(cases[c])));
        }
    }
}

// A repeating pattern puts a boundary between a set and a clear bit at every position in turn, so
// a shift off by one carries a neighbour's bit in where a mostly-zero significand carries a zero.
void test_repeating_significand_patterns_survive(void)
{
    static const proto_u64 PATTERNS[] = {0x0000000000000ull, 0xFFFFFFFFFFFFFull, 0x5555555555555ull,
                                         0xAAAAAAAAAAAAAull, 0x3333333333333ull, 0xCCCCCCCCCCCCCull,
                                         0x0F0F0F0F0F0F0ull, 0xF0F0F0F0F0F0Full, 0x1111111111111ull,
                                         0xEEEEEEEEEEEEEull, 0x00FF00FF00FF0ull, 0xFF00FF00FF00Full};
    static const proto_u64 KNEES[] = {0ull, 1ull, 2ull, 1022ull, 1023ull, 1024ull, 2045ull, 2046ull, 2047ull};

    for (unsigned p = 0; p < sizeof(PATTERNS) / sizeof(PATTERNS[0]); p++)
    {
        for (unsigned k = 0; k < sizeof(KNEES) / sizeof(KNEES[0]); k++)
        {
            for (proto_u64 s = 0; s <= 1ull; s++)
            {
                proto_u64 in = dbl.merge(s, KNEES[k], PATTERNS[p] & PROTO_DBL_MANT_MASK);
                TEST_ASSERT_EQUAL_HEX64(in, split_and_merge(dbl.from_bits(in)));
            }
        }
    }
}
