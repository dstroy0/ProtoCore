// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// mmgr/membuild.h: the bounded no-heap builder. The float and integer renderings are diffed
// against libc printf, which is the format these replace byte for byte.

#include "mmgr/membuild.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#define CAP 128u

static char out[CAP];
static pc_sb b;

static void sb_reset(size_t cap)
{
    memset(out, 0x7F, sizeof(out)); // poison, so an unwritten byte is visible
    b.p = out;
    b.cap = cap;
    b.len = 0;
    b.ok = PROTO_TRUE;
}

void setUp(void)
{
    sb_reset(CAP);
}

void tearDown(void)
{
}

// ---- append and the capacity edge -----------------------------------------

// Bytes land in order and the length counts them.
void test_put_n_appends()
{
    pc_sb_put_n(&b, "abc", 3);
    pc_sb_put_n(&b, "de", 2);
    TEST_ASSERT_EQUAL_size_t(5, pc_sb_finish(&b));
    TEST_ASSERT_EQUAL_STRING("abcde", out);
    TEST_ASSERT_TRUE(b.ok);
}

// A literal takes its length from the array type.
void test_lit_takes_the_length_from_the_type()
{
    pc_sb_lit(&b, "HTTP/1.1 ");
    pc_sb_lit(&b, "200 OK");
    TEST_ASSERT_EQUAL_size_t(15, pc_sb_finish(&b));
    TEST_ASSERT_EQUAL_STRING("HTTP/1.1 200 OK", out);
}

// The capacity holds cap-1 bytes: one is reserved so the terminator always fits.
void test_capacity_reserves_the_terminator()
{
    sb_reset(8);
    pc_sb_put(&b, "1234567"); // 7 bytes into a cap of 8
    TEST_ASSERT_TRUE(b.ok);
    TEST_ASSERT_EQUAL_size_t(7, pc_sb_finish(&b));
    TEST_ASSERT_EQUAL_STRING("1234567", out);

    sb_reset(8);
    pc_sb_put(&b, "12345678"); // one more than fits
    TEST_ASSERT_FALSE(b.ok);
    TEST_ASSERT_EQUAL_size_t(0, pc_sb_finish(&b));
}

// An append that does not fit writes nothing and latches; every later append is a no-op.
void test_overflow_latches_and_writes_nothing()
{
    sb_reset(8);
    pc_sb_put(&b, "abcd");
    size_t len_before = b.len;
    pc_sb_put(&b, "efghijkl"); // will not fit
    TEST_ASSERT_FALSE(b.ok);
    TEST_ASSERT_EQUAL_size_t(len_before, b.len); // the refused append did not advance
    TEST_ASSERT_EQUAL_CHAR('\x7F', out[4]);      // nor did it write

    pc_sb_put(&b, "x"); // a later append that would fit is still a no-op
    TEST_ASSERT_EQUAL_size_t(len_before, b.len);
    TEST_ASSERT_EQUAL_size_t(0, pc_sb_finish(&b));
}

// A zero-capacity builder owns no bytes, so even the terminator is out of bounds.
void test_zero_capacity_writes_nothing()
{
    sb_reset(0);
    pc_sb_ch(&b, 'x');
    TEST_ASSERT_EQUAL_size_t(0, pc_sb_finish(&b));
    TEST_ASSERT_EQUAL_CHAR('\x7F', out[0]);
}

// A single character appends, and latches at the edge.
void test_ch_appends_and_latches()
{
    sb_reset(3);
    pc_sb_ch(&b, 'a');
    pc_sb_ch(&b, 'b');
    TEST_ASSERT_TRUE(b.ok);
    pc_sb_ch(&b, 'c');
    TEST_ASSERT_FALSE(b.ok);
}

// ---- clipping (display text, no latch) ------------------------------------

// Clip writes what fits and leaves ok alone, so a later append still works.
void test_clip_truncates_without_latching()
{
    sb_reset(8);
    pc_sb_put_clip(&b, "abcdefghijkl");
    TEST_ASSERT_TRUE(b.ok);
    TEST_ASSERT_EQUAL_size_t(7, pc_sb_finish(&b));
    TEST_ASSERT_EQUAL_STRING("abcdefg", out);
}

// A clipped number is all-or-nothing: a half-written number reads as a different number.
void test_u64_clip_is_all_or_nothing()
{
    sb_reset(8);
    pc_sb_u64_clip(&b, 123u, 0);
    TEST_ASSERT_EQUAL_size_t(3, b.len);

    sb_reset(4);
    pc_sb_u64_clip(&b, 123456789u, 0); // does not fit
    TEST_ASSERT_TRUE(b.ok);
    TEST_ASSERT_EQUAL_size_t(0, b.len);
}

// Columns pad on the left with spaces, and a value wider than the column is not cut down to it.
void test_u64_clip_right_aligns_in_columns()
{
    pc_sb_u64_clip(&b, 7u, 5);
    TEST_ASSERT_EQUAL_size_t(5, pc_sb_finish(&b));
    TEST_ASSERT_EQUAL_STRING("    7", out);

    sb_reset(CAP);
    pc_sb_u64_clip(&b, 123456u, 3); // wider than the column
    TEST_ASSERT_EQUAL_size_t(6, pc_sb_finish(&b));
    TEST_ASSERT_EQUAL_STRING("123456", out);
}

// ---- integers, against printf ---------------------------------------------

static const uint64_t U_VALS[] = {0u,
                                  1u,
                                  7u,
                                  9u,
                                  10u,
                                  99u,
                                  100u,
                                  255u,
                                  1000u,
                                  65535u,
                                  1000000u,
                                  0x7FFFFFFFu,
                                  0x80000000u,
                                  0xFFFFFFFFu,
                                  0x100000000ull,
                                  0x0123456789ABCDEFull,
                                  0xFFFFFFFFFFFFFFFFull};

#define N_U (sizeof(U_VALS) / sizeof(U_VALS[0]))

// Decimal matches %llu at every width, including the 32-bit fast path and the value just past it.
void test_u64_matches_printf_decimal()
{
    char want[64];
    for (unsigned i = 0; i < N_U; i++)
    {
        sb_reset(CAP);
        pc_sb_u64(&b, U_VALS[i]);
        pc_sb_finish(&b);
        (void)snprintf(want, sizeof(want), "%llu", (unsigned long long)U_VALS[i]);
        TEST_ASSERT_EQUAL_STRING(want, out);
    }
}

// Hex matches %llx, and min_digits carries a printf zero-pad width.
void test_hex_matches_printf()
{
    char want[64];
    for (unsigned i = 0; i < N_U; i++)
    {
        sb_reset(CAP);
        pc_sb_hex(&b, U_VALS[i], 1);
        pc_sb_finish(&b);
        (void)snprintf(want, sizeof(want), "%llx", (unsigned long long)U_VALS[i]);
        TEST_ASSERT_EQUAL_STRING(want, out);

        sb_reset(CAP);
        pc_sb_hex(&b, U_VALS[i], 8);
        pc_sb_finish(&b);
        (void)snprintf(want, sizeof(want), "%08llx", (unsigned long long)U_VALS[i]);
        TEST_ASSERT_EQUAL_STRING(want, out);
    }
}

// A zero-padded decimal matches %0Nu.
void test_u32w_matches_printf_zero_pad()
{
    char want[64];
    static const uint32_t vals[] = {0u, 5u, 42u, 999u, 100000u};
    for (unsigned d = 1; d <= 8u; d++)
    {
        for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
        {
            sb_reset(CAP);
            pc_sb_u32w(&b, vals[i], d);
            pc_sb_finish(&b);
            (void)snprintf(want, sizeof(want), "%0*u", (int)d, vals[i]);
            TEST_ASSERT_EQUAL_STRING(want, out);
        }
    }
}

// Signed decimal matches %lld, including the minimum where negating the value would overflow.
void test_i64_matches_printf_including_the_minimum()
{
    char want[64];
    static const int64_t vals[] = {
        0, 1, -1, 42, -42, 2147483647, -2147483648ll, 9223372036854775807ll, (-9223372036854775807ll - 1)};
    for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
    {
        sb_reset(CAP);
        pc_sb_i64(&b, vals[i]);
        pc_sb_finish(&b);
        (void)snprintf(want, sizeof(want), "%lld", (long long)vals[i]);
        TEST_ASSERT_EQUAL_STRING(want, out);
    }
}

// ---- floats, against printf -----------------------------------------------

static const double D_VALS[] = {0.0,
                                1.0,
                                0.5,
                                2.5,
                                3.5,
                                1.25,
                                9.0,
                                10.0,
                                99.5,
                                100.0,
                                123.456,
                                0.1,
                                0.001,
                                0.0001,
                                0.00001,
                                1e-5,
                                1e-4,
                                1e6,
                                1e7,
                                1e15,
                                1e16,
                                1e-10,
                                9.9995,
                                999999.5,
                                1e300,
                                1e-300,
                                2.2250738585072014e-308,
                                4.9e-324,
                                1234567890.0,
                                3.14159265358979,
                                6.02214076e23,
                                1.602176634e-19};

#define N_D (sizeof(D_VALS) / sizeof(D_VALS[0]))

// %g is a wire format here (SCPI NR2/NR3, SenML/JSON numbers), so the rendering must match printf
// byte for byte at every precision the library asks for.
void test_g_matches_printf()
{
    char want[64];
    char msg[160];
    for (unsigned sig = 1; sig <= 10u; sig++)
    {
        for (unsigned i = 0; i < N_D; i++)
        {
            for (int neg = 0; neg < 2; neg++)
            {
                double v = neg ? -D_VALS[i] : D_VALS[i];
                sb_reset(CAP);
                pc_sb_g(&b, v, sig);
                pc_sb_finish(&b);
                (void)snprintf(want, sizeof(want), "%.*g", (int)sig, v);
                if (strcmp(want, out) != 0)
                {
                    (void)snprintf(msg, sizeof(msg), "%%.%ug of %.17g: printf \"%s\", built \"%s\"", sig, v, want, out);
                    TEST_FAIL_MESSAGE(msg);
                }
            }
        }
    }
}

// Negative zero carries the sign through the encoding's sign bit, the way printf does.
void test_g_renders_negative_zero()
{
    char want[64];
    double nz = -0.0;
    TEST_ASSERT_TRUE(pc_signbit(nz));
    TEST_ASSERT_FALSE(pc_signbit(0.0));
    pc_sb_g(&b, nz, 6);
    pc_sb_finish(&b);
    (void)snprintf(want, sizeof(want), "%.6g", nz);
    TEST_ASSERT_EQUAL_STRING(want, out);
}

// The non-finite values render as printf's words.
void test_g_renders_infinity_and_nan()
{
    double inf = 1e308 * 10.0;
    double nan_v = inf - inf;

    TEST_ASSERT_TRUE(pc_isinf(inf));
    TEST_ASSERT_FALSE(pc_isinf(nan_v));
    TEST_ASSERT_FALSE(pc_isinf(1.0));

    pc_sb_g(&b, inf, 6);
    pc_sb_finish(&b);
    TEST_ASSERT_EQUAL_STRING("inf", out);

    sb_reset(CAP);
    pc_sb_g(&b, -inf, 6);
    pc_sb_finish(&b);
    TEST_ASSERT_EQUAL_STRING("-inf", out);

    sb_reset(CAP);
    pc_sb_g(&b, nan_v, 6);
    pc_sb_finish(&b);
    TEST_ASSERT_EQUAL_STRING("nan", out);
}

// A fixed-decimal reading matches %.<n>f over the range the form occupies.
void test_fixed_matches_printf()
{
    static const double vals[] = {0.0,     1.0, 0.5,  2.5, 0.05,   0.005,     1.005, 99.995,
                                  123.456, 1e6, 1e15, 0.1, 0.9999, 1234.5678, 1e18,  0.0};
    char want[64];
    char msg[160];
    for (unsigned d = 0; d <= 6u; d++)
    {
        for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
        {
            for (int neg = 0; neg < 2; neg++)
            {
                double v = neg ? -vals[i] : vals[i];
                sb_reset(CAP);
                pc_sb_fixed(&b, v, d);
                pc_sb_finish(&b);
                (void)snprintf(want, sizeof(want), "%.*f", (int)d, v);
                if (strcmp(want, out) != 0)
                {
                    (void)snprintf(msg, sizeof(msg), "%%.%uf of %.17g: printf \"%s\", built \"%s\"", d, v, want, out);
                    TEST_FAIL_MESSAGE(msg);
                }
            }
        }
    }
}

// ---- escaping -------------------------------------------------------------

// The four XML metacharacters are replaced and everything else passes through.
void test_xml_escapes_the_metacharacters()
{
    pc_sb_xml(&b, "a&b<c>d\"e");
    TEST_ASSERT_EQUAL_size_t(pc_sb_finish(&b), strlen(out));
    TEST_ASSERT_EQUAL_STRING("a&amp;b&lt;c&gt;d&quot;e", out);

    sb_reset(CAP);
    pc_sb_xml(&b, NULL); // a null appends nothing
    TEST_ASSERT_EQUAL_size_t(0, pc_sb_finish(&b));
    TEST_ASSERT_TRUE(b.ok);
}

// A JSON string is quoted with the quote and the backslash escaped.
void test_json_quotes_and_escapes()
{
    pc_sb_json(&b, "a\"b\\c");
    pc_sb_finish(&b);
    TEST_ASSERT_EQUAL_STRING("\"a\\\"b\\\\c\"", out);

    sb_reset(CAP);
    pc_sb_json(&b, NULL); // a null renders as the empty string
    pc_sb_finish(&b);
    TEST_ASSERT_EQUAL_STRING("\"\"", out);
}

// An escape that would not fit latches rather than emitting half of it.
void test_json_escape_that_does_not_fit_latches()
{
    sb_reset(4);
    pc_sb_json(&b, "\"\"\"\"");
    TEST_ASSERT_FALSE(b.ok);
    TEST_ASSERT_EQUAL_size_t(0, pc_sb_finish(&b));
}

// ---- finish ---------------------------------------------------------------

// Finish terminates and reports the built length; after an overflow it reports zero.
void test_finish_terminates_and_reports()
{
    pc_sb_put(&b, "abc");
    size_t n = pc_sb_finish(&b);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_CHAR('\0', out[3]);

    sb_reset(4);
    pc_sb_put(&b, "abcdefg");
    TEST_ASSERT_EQUAL_size_t(0, pc_sb_finish(&b));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_put_n_appends);
    RUN_TEST(test_lit_takes_the_length_from_the_type);
    RUN_TEST(test_capacity_reserves_the_terminator);
    RUN_TEST(test_overflow_latches_and_writes_nothing);
    RUN_TEST(test_zero_capacity_writes_nothing);
    RUN_TEST(test_ch_appends_and_latches);
    RUN_TEST(test_clip_truncates_without_latching);
    RUN_TEST(test_u64_clip_is_all_or_nothing);
    RUN_TEST(test_u64_clip_right_aligns_in_columns);
    RUN_TEST(test_u64_matches_printf_decimal);
    RUN_TEST(test_hex_matches_printf);
    RUN_TEST(test_u32w_matches_printf_zero_pad);
    RUN_TEST(test_i64_matches_printf_including_the_minimum);
    RUN_TEST(test_g_matches_printf);
    RUN_TEST(test_g_renders_negative_zero);
    RUN_TEST(test_g_renders_infinity_and_nan);
    RUN_TEST(test_fixed_matches_printf);
    RUN_TEST(test_xml_escapes_the_metacharacters);
    RUN_TEST(test_json_quotes_and_escapes);
    RUN_TEST(test_json_escape_that_does_not_fit_latches);
    RUN_TEST(test_finish_terminates_and_reports);
    return UNITY_END();
}
