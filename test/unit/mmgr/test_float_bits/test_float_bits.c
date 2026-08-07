// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// mmgr/float_bits.h: a double read as sign, exponent and mantissa, and merged back from them.
// The oracle is the bit layout itself, written out here.

#include "mmgr/float_bits.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

void setUp(void)
{
}

void tearDown(void)
{
}

// The raw bits of v, read here rather than through the header under test.
static uint64_t oracle_bits(double v)
{
    uint64_t b = 0;
    memcpy(&b, &v, sizeof(b));
    return b;
}

static double oracle_double(uint64_t b)
{
    double v = 0.0;
    memcpy(&v, &b, sizeof(v));
    return v;
}

// ---- the layout -----------------------------------------------------------

// The masks cover all 64 bits exactly once.
void test_masks_partition_the_word()
{
    TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFFFFFFull, PROTO_DBL_SIGN_MASK | PROTO_DBL_EXP_MASK | PROTO_DBL_MANT_MASK);
    TEST_ASSERT_EQUAL_HEX64(0, PROTO_DBL_SIGN_MASK & PROTO_DBL_EXP_MASK);
    TEST_ASSERT_EQUAL_HEX64(0, PROTO_DBL_SIGN_MASK & PROTO_DBL_MANT_MASK);
    TEST_ASSERT_EQUAL_HEX64(0, PROTO_DBL_EXP_MASK & PROTO_DBL_MANT_MASK);
    TEST_ASSERT_EQUAL_UINT(63, PROTO_DBL_SIGN_SHIFT);
    TEST_ASSERT_EQUAL_UINT(52, PROTO_DBL_MANT_BITS);
    TEST_ASSERT_EQUAL_HEX64(PROTO_DBL_EXP_MASK, PROTO_DBL_EXP_ALL << PROTO_DBL_MANT_BITS);
    TEST_ASSERT_EQUAL_HEX64(PROTO_DBL_SIGN_MASK, PROTO_DBL_SIGN_ONE << PROTO_DBL_SIGN_SHIFT);
}

// A double is 64 bits, or the masks describe something else.
void test_double_is_sixty_four_bits()
{
    TEST_ASSERT_EQUAL_UINT(8, sizeof(double));
}

// ---- the known encoding ---------------------------------------------------

// The worked value: 3.14159 is sign 0, exponent 1024, mantissa 0x921F9F01B866E.
void test_known_encoding()
{
    double v = 3.14159;
    TEST_ASSERT_EQUAL_HEX64(0, proto_dbl_sign(v));
    TEST_ASSERT_EQUAL_HEX64(1024, proto_dbl_exp(v));
    TEST_ASSERT_EQUAL_HEX64(oracle_bits(v) & PROTO_DBL_MANT_MASK, proto_dbl_mant(v));

    // 1.0 is exponent 1023 (the bias) and a zero mantissa.
    TEST_ASSERT_EQUAL_HEX64(0, proto_dbl_sign(1.0));
    TEST_ASSERT_EQUAL_HEX64(1023, proto_dbl_exp(1.0));
    TEST_ASSERT_EQUAL_HEX64(0, proto_dbl_mant(1.0));

    // 2.0 is the next exponent up, mantissa still zero.
    TEST_ASSERT_EQUAL_HEX64(1024, proto_dbl_exp(2.0));
    TEST_ASSERT_EQUAL_HEX64(0, proto_dbl_mant(2.0));

    // Zero is every field zero; negative zero differs in the sign alone.
    TEST_ASSERT_EQUAL_HEX64(0, proto_dbl_sign(0.0));
    TEST_ASSERT_EQUAL_HEX64(0, proto_dbl_exp(0.0));
    TEST_ASSERT_EQUAL_HEX64(0, proto_dbl_mant(0.0));
    TEST_ASSERT_EQUAL_HEX64(1, proto_dbl_sign(-0.0));
    TEST_ASSERT_EQUAL_HEX64(0, proto_dbl_exp(-0.0));
    TEST_ASSERT_EQUAL_HEX64(0, proto_dbl_mant(-0.0));
}

// An infinity is the exponent field all ones with a zero mantissa; a NaN is the same exponent with
// a nonzero one.
void test_non_finite_encodings()
{
    double inf = oracle_double(PROTO_DBL_EXP_MASK);
    double nan_v = oracle_double(PROTO_DBL_EXP_MASK | 1ull);

    TEST_ASSERT_EQUAL_HEX64(PROTO_DBL_EXP_ALL, proto_dbl_exp(inf));
    TEST_ASSERT_EQUAL_HEX64(0, proto_dbl_mant(inf));
    TEST_ASSERT_EQUAL_HEX64(PROTO_DBL_EXP_ALL, proto_dbl_exp(nan_v));
    TEST_ASSERT_TRUE(proto_dbl_mant(nan_v) != 0u);
}

// A subnormal has a zero exponent field and a nonzero mantissa.
void test_subnormal_encoding()
{
    double small = oracle_double(1ull); // the smallest subnormal
    TEST_ASSERT_EQUAL_HEX64(0, proto_dbl_exp(small));
    TEST_ASSERT_EQUAL_HEX64(1, proto_dbl_mant(small));
    TEST_ASSERT_EQUAL_HEX64(0, proto_dbl_sign(small));
}

// ---- the round trip -------------------------------------------------------

static const double VALS[] = {0.0,
                              -0.0,
                              1.0,
                              -1.0,
                              2.0,
                              0.5,
                              3.14159,
                              -3.14159,
                              1e-5,
                              1e6,
                              1e300,
                              -1e300,
                              1e-300,
                              123456789.0,
                              2.2250738585072014e-308,
                              1.7976931348623157e308};

#define NVALS (sizeof(VALS) / sizeof(VALS[0]))

// Split into three fields and merged back is the value that went in.
void test_split_and_merge_round_trips()
{
    for (unsigned i = 0; i < NVALS; i++)
    {
        double v = VALS[i];
        proto_u64 bits = proto_dbl_merge(proto_dbl_sign(v), proto_dbl_exp(v), proto_dbl_mant(v));
        TEST_ASSERT_EQUAL_HEX64(oracle_bits(v), bits);
        TEST_ASSERT_EQUAL_HEX64(oracle_bits(v), oracle_bits(proto_dbl_from_bits(bits)));
    }
}

// The merge masks each field to its own width, so a field carrying junk above it cannot reach into
// a neighbour.
void test_merge_masks_each_field()
{
    proto_u64 clean = proto_dbl_merge(1u, 1024u, 0x921F9F01B866Eull);
    proto_u64 dirty =
        proto_dbl_merge(0xFFFFFFFFFFFFFFFEull | 1u, 0xFFFFF800u | 1024u, 0xFFF0000000000000ull | 0x921F9F01B866Eull);
    TEST_ASSERT_EQUAL_HEX64(clean, dirty);
}

// Every bit position survives the round trip, so no field is one bit short or one bit wide.
void test_every_bit_position_survives()
{
    for (unsigned i = 0; i < 64u; i++)
    {
        proto_u64 bits = 1ull << i;
        double v = proto_dbl_from_bits(bits);
        proto_u64 back = proto_dbl_merge(proto_dbl_sign(v), proto_dbl_exp(v), proto_dbl_mant(v));
        if (back != bits)
        {
            char msg[64];
            (void)snprintf(msg, sizeof(msg), "bit %u did not survive", i);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

// In and out at one point of the space: merge the three fields, read them back, and merge again.
// Fails with the exact fields rather than only the word, so a mask a bit too wide names itself.
static void assert_in_out(proto_u64 s, proto_u64 e, proto_u64 m)
{
    proto_u64 in = proto_dbl_merge(s, e, m);
    double v = proto_dbl_from_bits(in);
    proto_u64 rs = proto_dbl_sign(v);
    proto_u64 re = proto_dbl_exp(v);
    proto_u64 rm = proto_dbl_mant(v);
    proto_u64 out = proto_dbl_merge(rs, re, rm);
    char msg[160];

    if (rs != s || re != e || rm != m || out != in)
    {
        (void)snprintf(msg, sizeof(msg), "in s=%llu e=%llu m=%llx -> out s=%llu e=%llu m=%llx (%llx vs %llx)",
                       (unsigned long long)s, (unsigned long long)e, (unsigned long long)m, (unsigned long long)rs,
                       (unsigned long long)re, (unsigned long long)rm, (unsigned long long)out, (unsigned long long)in);
        TEST_FAIL_MESSAGE(msg);
    }
}

// The exponent field walked one step at a time from zero to all ones, both signs. Stepping the
// whole range rather than sampling it puts every knee under the sweep: the subnormal boundary at
// 0 to 1, the bias, and the non-finite boundary at 0x7FE to 0x7FF.
void test_exponent_knee_in_and_out()
{
    static const proto_u64 mants[] = {0ull, 1ull, 0x8000000000000ull, 0x921F9F01B866Eull, PROTO_DBL_MANT_MASK};
    for (proto_u64 e = 0; e <= PROTO_DBL_EXP_ALL; e++)
    {
        for (proto_u64 s = 0; s <= 1ull; s++)
        {
            for (unsigned m = 0; m < sizeof(mants) / sizeof(mants[0]); m++)
            {
                assert_in_out(s, e, mants[m]);
            }
        }
    }
}

// A repeating pattern puts a boundary between a set and a clear bit at every position in turn, so
// a shift off by one carries a neighbour's bit in where a mostly-zero mantissa would carry a zero.
void test_repeating_mantissa_patterns_in_and_out()
{
    static const proto_u64 patterns[] = {
        0x0000000000000ull, 0xFFFFFFFFFFFFFull, 0x5555555555555ull, 0xAAAAAAAAAAAAAull,
        0x3333333333333ull, 0xCCCCCCCCCCCCCull, 0x0F0F0F0F0F0F0ull, 0xF0F0F0F0F0F0Full,
        0x1111111111111ull, 0xEEEEEEEEEEEEEull, 0x7777777777777ull, 0x8888888888888ull,
        0x00FF00FF00FF0ull, 0xFF00FF00FF00Full, 0x6666666666666ull, 0x9999999999999ull};
    // The exponents either side of every boundary the field has.
    static const proto_u64 knees[] = {0ull, 1ull, 2ull, 1022ull, 1023ull, 1024ull, 2045ull, 2046ull, 2047ull};

    for (unsigned p = 0; p < sizeof(patterns) / sizeof(patterns[0]); p++)
    {
        for (unsigned k = 0; k < sizeof(knees) / sizeof(knees[0]); k++)
        {
            assert_in_out(0ull, knees[k], patterns[p] & PROTO_DBL_MANT_MASK);
            assert_in_out(1ull, knees[k], patterns[p] & PROTO_DBL_MANT_MASK);
        }
    }
}

// A single set bit walked through the mantissa, and a single clear bit walked through a full one:
// each isolates one position with no neighbour to mask a shift that lost it.
void test_walking_mantissa_bit_in_and_out()
{
    for (unsigned i = 0; i < PROTO_DBL_MANT_BITS; i++)
    {
        proto_u64 one = 1ull << i;
        assert_in_out(0ull, 1023ull, one);
        assert_in_out(1ull, 1023ull, PROTO_DBL_MANT_MASK & ~one);
        assert_in_out(0ull, 0ull, one);              // subnormal
        assert_in_out(0ull, PROTO_DBL_EXP_ALL, one); // NaN payload
    }
}

// The bits go to a double and back unchanged, including a NaN payload a comparison could not check.
void test_from_bits_is_the_inverse_of_the_read()
{
    static const uint64_t patterns[] = {0ull,
                                        1ull,
                                        PROTO_DBL_SIGN_MASK,
                                        PROTO_DBL_EXP_MASK,
                                        PROTO_DBL_MANT_MASK,
                                        PROTO_DBL_EXP_MASK | 0xDEADBEEFull,
                                        0xFFFFFFFFFFFFFFFFull,
                                        0x400921F9F01B866Eull};
    for (unsigned i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++)
    {
        double v = proto_dbl_from_bits(patterns[i]);
        TEST_ASSERT_EQUAL_HEX64(patterns[i], oracle_bits(v));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_masks_partition_the_word);
    RUN_TEST(test_double_is_sixty_four_bits);
    RUN_TEST(test_known_encoding);
    RUN_TEST(test_non_finite_encodings);
    RUN_TEST(test_subnormal_encoding);
    RUN_TEST(test_split_and_merge_round_trips);
    RUN_TEST(test_merge_masks_each_field);
    RUN_TEST(test_every_bit_position_survives);
    RUN_TEST(test_exponent_knee_in_and_out);
    RUN_TEST(test_repeating_mantissa_patterns_in_and_out);
    RUN_TEST(test_walking_mantissa_bit_in_and_out);
    RUN_TEST(test_from_bits_is_the_inverse_of_the_read);
    return UNITY_END();
}
