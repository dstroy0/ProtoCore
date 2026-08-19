// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the no-stdlib floating-point renderers (mmgr/membuild.h: Sb.g and Sb.fixed).
//
// These replace printf on the wire - an SCPI NR3 response, a SenML number, a JSON body - so the bar
// is the exact octets ISO C11 sec 7.21.6.1 specifies, not "close". That clause states the whole
// rule, and every expected string below is derived from it in the comment beside it rather than
// captured from a libc run:
//
//   f: decimal notation with `precision` digits after the point, the value rounded; a precision of
//      zero emits no point.
//   g: let P be the precision (1 if it is zero). If a style-e conversion would have exponent X,
//      then P > X >= -4 selects style f with precision P-(X+1), and otherwise style e with
//      precision P-1. Trailing zeros are then removed from the fraction, and the point with them
//      when no fraction remains. A style-e exponent carries at least two digits.
//
// test_c11_g_selects_the_style_by_the_exponent is the load-bearing case: it walks X across both
// thresholds the clause names, X = P and X = -4, which is where a renderer that hardcodes one style
// or gets the inequality inclusive-vs-exclusive wrong first disagrees.

#include "mmgr/float_bits/float_bits.h" // dbl.from_bits - the non-finite encodings, built from their fields
#include "mmgr/membuild/membuild.h"
#include <string.h>

#include <unity.h>

static char g_buf[64];

void setUp(void)
{
}

void tearDown(void)
{
}

// Render @p v with @p sig significant digits and return the buffer.
static const char *g_of(double v, unsigned sig)
{
    memset(g_buf, 0, sizeof(g_buf));
    protocore_sb b = {g_buf, sizeof(g_buf), 0, PROTO_TRUE};
    Sb.g(&b, v, sig);
    Sb.finish(&b);
    return g_buf;
}

// Render @p v with exactly @p decimals digits after the point and return the buffer.
static const char *f_of(double v, unsigned decimals)
{
    memset(g_buf, 0, sizeof(g_buf));
    protocore_sb b = {g_buf, sizeof(g_buf), 0, PROTO_TRUE};
    Sb.fixed(&b, v, decimals);
    Sb.finish(&b);
    return g_buf;
}

// ---- %g: the style selection ----------------------------------------------

// P = 6 throughout, so the clause reads "style f while 6 > X >= -4, style e otherwise".
//
//   X =  6  1e6        6 > 6 false            -> e, precision 5 -> 1.00000e+06 -> "1e+06"
//   X =  5  123456     6 > 5 >= -4            -> f, precision 0 -> "123456"
//   X =  0  1.0        6 > 0 >= -4            -> f, precision 5 -> 1.00000     -> "1"
//   X = -1  0.5        6 > -1 >= -4           -> f, precision 6 -> 0.500000    -> "0.5"
//   X = -4  0.0001     6 > -4 >= -4           -> f, precision 9 -> 0.000100000 -> "0.0001"
//   X = -5  0.00001    -5 >= -4 false         -> e, precision 5 -> 1.00000e-05 -> "1e-05"
void test_c11_g_selects_the_style_by_the_exponent(void)
{
    TEST_ASSERT_EQUAL_STRING("1e+06", g_of(1e6, 6));
    TEST_ASSERT_EQUAL_STRING("123456", g_of(123456.0, 6));
    TEST_ASSERT_EQUAL_STRING("1", g_of(1.0, 6));
    TEST_ASSERT_EQUAL_STRING("0.5", g_of(0.5, 6));
    TEST_ASSERT_EQUAL_STRING("0.0001", g_of(0.0001, 6));
    TEST_ASSERT_EQUAL_STRING("1e-05", g_of(0.00001, 6));
    TEST_ASSERT_EQUAL_STRING("1e-06", g_of(1e-6, 6));
}

// The same threshold at P = 1, which moves it: style f now only while 1 > X >= -4.
//
//   X = 2  100.0   1 > 2 false     -> e, precision 0 -> "1e+02"
//   X = 0  1.0     1 > 0 >= -4     -> f, precision 0 -> "1"
//   X = -1 0.5     1 > -1 >= -4    -> f, precision 1 -> "0.5"
void test_g_threshold_moves_with_the_precision(void)
{
    TEST_ASSERT_EQUAL_STRING("1e+02", g_of(100.0, 1));
    TEST_ASSERT_EQUAL_STRING("1", g_of(1.0, 1));
    TEST_ASSERT_EQUAL_STRING("0.5", g_of(0.5, 1));

    // A precision of zero is taken as 1, so it renders the same as P = 1.
    TEST_ASSERT_EQUAL_STRING("1e+02", g_of(100.0, 0));
}

// The digits themselves, rounded to P significant figures.
//
//   1234567 at P = 6: X = 6 -> style e, precision 5. 1.234567 rounded to 5 decimals is 1.23457.
//   6.02214076e23 at P = 6: X = 23 -> style e. 6.02214076 rounded to 5 decimals is 6.02214.
//   pi at P = 6: X = 0 -> style f, precision 5 -> 3.14159
//   pi at P = 10: X = 0 -> style f, precision 9. 3.141592653|58979 rounds up at the 9th -> 3.141592654
void test_g_rounds_to_the_significant_digits(void)
{
    TEST_ASSERT_EQUAL_STRING("1.23457e+06", g_of(1234567.0, 6));
    TEST_ASSERT_EQUAL_STRING("6.02214e+23", g_of(6.02214076e23, 6));
    TEST_ASSERT_EQUAL_STRING("3.14159", g_of(3.14159265358979, 6));
    TEST_ASSERT_EQUAL_STRING("3.141592654", g_of(3.14159265358979, 10));
}

// "trailing zeros are removed from the fractional portion of the result and the decimal-point
// character is removed if there is no fractional portion remaining."
//
//   100.0 at P = 6: style f precision 3 -> 100.000 -> "100"
//   1e20  at P = 10: style e            -> 1.000000000e+20 -> "1e+20"
//   0.1   at P = 6: style f precision 6 -> 0.100000 -> "0.1"
//   1234567.0 at P = 10: X = 6, 10 > 6 -> style f precision 3 -> 1234567.000 -> "1234567"
void test_g_strips_trailing_zeros_and_a_bare_point(void)
{
    TEST_ASSERT_EQUAL_STRING("100", g_of(100.0, 6));
    TEST_ASSERT_EQUAL_STRING("1e+20", g_of(1e20, 10));
    TEST_ASSERT_EQUAL_STRING("0.1", g_of(0.1, 6));
    TEST_ASSERT_EQUAL_STRING("0.3", g_of(0.3, 6));
    TEST_ASSERT_EQUAL_STRING("1234567", g_of(1234567.0, 10));
}

// sec 7.21.6.1 gives a style-e exponent "at least two digits", with a sign always present.
void test_g_exponent_carries_a_sign_and_two_digits(void)
{
    TEST_ASSERT_EQUAL_STRING("1e+06", g_of(1e6, 6));
    TEST_ASSERT_EQUAL_STRING("1e-05", g_of(1e-5, 6));
    TEST_ASSERT_EQUAL_STRING("1e+300", g_of(1e300, 6)); // three digits when the value needs them
    TEST_ASSERT_EQUAL_STRING("1e-300", g_of(1e-300, 6));
}

// IEEE 754 sec 3.4 gives -0.0 its own encoding, and sec 7.21.6.1 prints a negative value with a
// leading '-'. The sign is a bit of the encoding, so it survives a value that compares equal to 0.
void test_g_renders_the_sign_from_the_encoding(void)
{
    TEST_ASSERT_EQUAL_STRING("0", g_of(0.0, 6));
    TEST_ASSERT_EQUAL_STRING("-0", g_of(-0.0, 6));
    TEST_ASSERT_EQUAL_STRING("-1", g_of(-1.0, 6));
    TEST_ASSERT_EQUAL_STRING("-3.14159", g_of(-3.14159265358979, 6));
}

// ---- %f: the fixed form ----------------------------------------------------

// A binary64 holds the nearest double to the decimal it was written as, and the conversion rounds
// THAT value, not the decimal. Each ulp below is 2^(e-52) for the binade the value sits in, and
// the scaled product says which way the nearest double fell:
//
//   2.675  in [2,4),   ulp 2^-51: 2.675 * 2^51 = 6023564501608038.4, rounds DOWN, so the stored
//                                 value is below 2.675 and two decimals give "2.67".
//   0.05   in [2^-5,2^-4), ulp 2^-57: 0.05 * 2^57 = 7205759403792793.6, rounds UP, so the stored
//                                 value is above 0.05 and one decimal gives "0.1".
//   99.995 in [64,128), ulp 2^-46: 99.995 * 2^46 = 7036522574045511.68, rounds UP, so the stored
//                                 value is above 99.995 and two decimals carry into the integer
//                                 part: "100.00", not the "99.99" the decimal alone suggests.
void test_c11_f_rounds_the_stored_binary_value(void)
{
    TEST_ASSERT_EQUAL_STRING("2.67", f_of(2.675, 2));
    TEST_ASSERT_EQUAL_STRING("0.1", f_of(0.05, 1));
    TEST_ASSERT_EQUAL_STRING("100.00", f_of(99.995, 2));
    TEST_ASSERT_EQUAL_STRING("1234.57", f_of(1234.5678, 2));
}

// IEEE 754 sec 4.3.1 makes roundTiesToEven the default, and a value that is exactly representable
// can land exactly on the midpoint of the decimal field. 1.0625 is 1 + 2^-4 and 1.1875 is 1 + 3/16,
// both exact, and both sit halfway at three decimals:
//   1.0625 -> between 1.062 and 1.063, the even last digit is 2 -> "1.062"
//   1.1875 -> between 1.187 and 1.188, the even last digit is 8 -> "1.188"
void test_an_exact_midpoint_rounds_to_the_even_digit(void)
{
    TEST_ASSERT_EQUAL_STRING("1.062", f_of(1.0625, 3));
    TEST_ASSERT_EQUAL_STRING("1.188", f_of(1.1875, 3));
}

// "the number of digits after the decimal-point character is equal to the precision specification.
// If the precision is zero ... no decimal-point character appears."
void test_f_emits_exactly_the_requested_decimals(void)
{
    TEST_ASSERT_EQUAL_STRING("1", f_of(1.0, 0));
    TEST_ASSERT_EQUAL_STRING("1.0", f_of(1.0, 1));
    TEST_ASSERT_EQUAL_STRING("1.00", f_of(1.0, 2));
    TEST_ASSERT_EQUAL_STRING("1.0000", f_of(1.0, 4));
    TEST_ASSERT_EQUAL_STRING("0", f_of(0.0, 0));
    TEST_ASSERT_EQUAL_STRING("0.000", f_of(0.0, 3));
    TEST_ASSERT_EQUAL_STRING("-1.5", f_of(-1.5, 1));
    TEST_ASSERT_EQUAL_STRING("1000000", f_of(1e6, 0));
    TEST_ASSERT_EQUAL_STRING("-0.00", f_of(-0.0, 2)); // the sign bit again
}

// "at least one digit appears before it" - a magnitude below one still leads with a zero.
void test_f_always_leads_with_a_digit(void)
{
    TEST_ASSERT_EQUAL_STRING("0.50", f_of(0.5, 2));
    TEST_ASSERT_EQUAL_STRING("0.0625", f_of(0.0625, 4));
    TEST_ASSERT_EQUAL_STRING("-0.25", f_of(-0.25, 2));
}

// At or above 2^64 the integer part cannot go through a uint64 at all, and an exact expansion would
// need big-integer arithmetic, so the module documents a fallback to the significant-digit form.
// Pinned so the old failure - a wrapped cast rendering 1e20 as "0.00" - cannot come back as a small
// number.
void test_f_above_the_64_bit_range_falls_back_to_the_g_form(void)
{
    TEST_ASSERT_EQUAL_STRING("1e+20", f_of(1e20, 2));
    TEST_ASSERT_EQUAL_STRING("1e+300", f_of(1e300, 2));
    // Just below the boundary it is still an exact expansion: 2^63 is 9223372036854775808.
    TEST_ASSERT_EQUAL_STRING("9223372036854775808", f_of(9223372036854775808.0, 0));
}

// ---- the non-finite encodings ----------------------------------------------

// IEEE 754 sec 3.4: an all-ones exponent field with a zero significand is an infinity and with a
// nonzero one a NaN. Both renderers name them rather than emitting digits.
void test_non_finite_values_are_named(void)
{
    double inf = dbl.from_bits(0x7FF0000000000000ull);
    double neg_inf = dbl.from_bits(0xFFF0000000000000ull);
    double nan_v = dbl.from_bits(0x7FF8000000000000ull);

    TEST_ASSERT_EQUAL_STRING("inf", g_of(inf, 6));
    TEST_ASSERT_EQUAL_STRING("-inf", g_of(neg_inf, 6));
    TEST_ASSERT_EQUAL_STRING("nan", g_of(nan_v, 6));

    TEST_ASSERT_EQUAL_STRING("inf", f_of(inf, 2));
    TEST_ASSERT_EQUAL_STRING("-inf", f_of(neg_inf, 2));
    TEST_ASSERT_EQUAL_STRING("nan", f_of(nan_v, 2));
}

// ---- the bound -------------------------------------------------------------

// A rendering that does not fit latches like every other append, so a half-written number never
// reaches the wire.
void test_a_number_that_does_not_fit_latches(void)
{
    char tight[4];
    protocore_sb b = {tight, sizeof(tight), 0, PROTO_TRUE};
    Sb.g(&b, 3.14159265358979, 6); // needs seven bytes plus the NUL
    TEST_ASSERT_FALSE(b.ok);
    TEST_ASSERT_EQUAL_size_t(0, Sb.finish(&b));

    protocore_sb f = {tight, sizeof(tight), 0, PROTO_TRUE};
    Sb.fixed(&f, 1234.5678, 2);
    TEST_ASSERT_FALSE(f.ok);
    TEST_ASSERT_EQUAL_size_t(0, Sb.finish(&f));
}

// The decimals are clamped to 18, which is the last power of ten that stays inside the 64-bit
// arithmetic the carry check is done in.
void test_the_decimal_count_is_clamped(void)
{
    // 0.5 is exact, so every decimal past the first is a zero however many are asked for.
    TEST_ASSERT_EQUAL_STRING("0.500000000000000000", f_of(0.5, 18));
    TEST_ASSERT_EQUAL_STRING("0.500000000000000000", f_of(0.5, 25));
}
