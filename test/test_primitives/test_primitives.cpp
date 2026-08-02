// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the shared no-stdlib primitives: the base-10 number parsers
// (shared_primitives/numparse.h - pc_strtol / pc_strtoul / pc_strtof, the
// strtol-family endptr contract) and the strict RFC 3629 UTF-8 validator
// (utf8.h). Pure host tests.

#include "mmgr/membuild.h"
#include "shared_primitives/numparse.h"
#include "shared_primitives/utf8.h"
#include <stdio.h> // snprintf: the libc reference these tests check pc_sb_g/pc_sb_fixed against
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// pc_sb_u32 measures the field then fills it back-to-front directly in the destination.
// The failure mode that replaces would be an off-by-one at the field edge, so the exact-fit
// and one-past-fit cases are checked at every digit count rather than only on a round number.
void test_sb_u32()
{
    char buf[32];
    pc_sb b = {buf, sizeof(buf), 0, true};
    pc_sb_u32(&b, 0);
    pc_sb_put(&b, ",");
    pc_sb_u32(&b, 7);
    pc_sb_put(&b, ",");
    pc_sb_u32(&b, 4294967295u); // UINT32_MAX: the 10-digit path
    TEST_ASSERT_EQUAL_size_t(14, pc_sb_finish(&b));
    TEST_ASSERT_EQUAL_STRING("0,7,4294967295", buf);
}

void test_sb_u32_boundaries()
{
    // For each digit count, a buffer holding exactly the digits + NUL must succeed, and one
    // byte less must latch ok=false and leave the length unchanged.
    uint32_t vals[] = {9u, 99u, 999u, 123456789u, 4294967295u};
    size_t widths[] = {1, 2, 3, 9, 10};
    for (unsigned i = 0; i < 5; i++)
    {
        char tight[16];
        pc_sb fit = {tight, widths[i] + 1, 0, true};
        pc_sb_u32(&fit, vals[i]);
        TEST_ASSERT_TRUE_MESSAGE(fit.ok, "exact fit must succeed");
        TEST_ASSERT_EQUAL_size_t(widths[i], pc_sb_finish(&fit));

        pc_sb tooSmall = {tight, widths[i], 0, true};
        pc_sb_u32(&tooSmall, vals[i]);
        TEST_ASSERT_FALSE_MESSAGE(tooSmall.ok, "one byte short must fail closed");
        TEST_ASSERT_EQUAL_size_t(0, tooSmall.len);
        TEST_ASSERT_EQUAL_size_t(0, pc_sb_finish(&tooSmall));
    }
}

void test_sb_overflow_latches()
{
    // Once ok latches false every later append is a no-op, so callers test one flag at the end
    // instead of checking each call - and a truncated frame never reports a length.
    char buf[8];
    pc_sb b = {buf, sizeof(buf), 0, true};
    pc_sb_put(&b, "abcdef"); // fits (6 + NUL)
    TEST_ASSERT_TRUE(b.ok);
    pc_sb_u32(&b, 12345); // does not
    TEST_ASSERT_FALSE(b.ok);
    size_t after = b.len;
    pc_sb_put(&b, "x"); // no-op while latched
    pc_sb_u32(&b, 1);
    TEST_ASSERT_EQUAL_size_t(after, b.len);
    TEST_ASSERT_EQUAL_size_t(0, pc_sb_finish(&b));
}

void test_sb_widths_and_bases()
{
    char buf[64];
    pc_sb b = {buf, sizeof(buf), 0, true};
    pc_sb_hex(&b, 0xdeadbeefu, 8); // %08lx
    pc_sb_ch(&b, '|');
    pc_sb_hex(&b, 0x5u, 4); // %04x
    pc_sb_ch(&b, '|');
    pc_sb_hex(&b, 0xabcu, 1); // %x, no padding
    pc_sb_ch(&b, '|');
    pc_sb_u32w(&b, 7, 2); // %02d
    TEST_ASSERT_EQUAL_size_t(20, pc_sb_finish(&b));
    TEST_ASSERT_EQUAL_STRING("deadbeef|0005|abc|07", buf);

    // a value wider than its min_digits is never truncated to the width
    pc_sb w = {buf, sizeof(buf), 0, true};
    pc_sb_u32w(&w, 12345, 2);
    pc_sb_finish(&w);
    TEST_ASSERT_EQUAL_STRING("12345", buf);
}

void test_sb_64bit()
{
    char buf[64];
    pc_sb b = {buf, sizeof(buf), 0, true};
    pc_sb_u64(&b, 18446744073709551615ull); // UINT64_MAX, 20 digits
    TEST_ASSERT_EQUAL_size_t(20, pc_sb_finish(&b));
    TEST_ASSERT_EQUAL_STRING("18446744073709551615", buf);

    pc_sb s = {buf, sizeof(buf), 0, true};
    pc_sb_i64(&s, -4096);
    pc_sb_ch(&s, ',');
    // INT64_MIN: taking the magnitude by negating the signed value would overflow, so this is
    // the case that proves the unsigned path.
    pc_sb_i64(&s, (int64_t)(-9223372036854775807LL - 1));
    pc_sb_finish(&s);
    TEST_ASSERT_EQUAL_STRING("-4096,-9223372036854775808", buf);
}

// pc_sb_g replaces printf %g on wire formats (SCPI NR2/NR3, SenML/JSON numbers), so the bar is
// byte-identical output, not "close". libc is the reference implementation here - an independent
// one - which is what makes this a conformance check rather than a restatement of my own math.
void test_sb_g_matches_libc()
{
    static const double vals[] = {0.0,
                                  -0.0,
                                  1.0,
                                  -1.0,
                                  0.5,
                                  2.5,
                                  3.5,
                                  100.0,
                                  1e6,
                                  1e-6,
                                  123456.0,
                                  1234567.0,
                                  0.0001,
                                  0.00001,
                                  9.9999995,
                                  999999.5,
                                  1e20,
                                  1e-20,
                                  3.14159265358979,
                                  0.1,
                                  0.3,
                                  6.02214076e23};
    static const unsigned sigs[] = {1, 2, 6, 10};
    for (unsigned vi = 0; vi < sizeof(vals) / sizeof(vals[0]); vi++)
    {
        for (unsigned si = 0; si < 4; si++)
        {
            char mine[64], theirs[64], fmt[16];
            pc_sb b = {mine, sizeof(mine), 0, true};
            pc_sb_g(&b, vals[vi], sigs[si]);
            pc_sb_finish(&b);
            snprintf(fmt, sizeof(fmt), "%%.%ug", sigs[si]);
            snprintf(theirs, sizeof(theirs), fmt, vals[vi]);
            TEST_ASSERT_EQUAL_STRING(theirs, mine);
        }
    }
}

void test_sb_fixed_matches_libc()
{
    static const double vals[] = {0.0, 1.0, -1.5, 0.05, 2.675, 1234.5678, 99.995, 1e6};
    for (unsigned vi = 0; vi < sizeof(vals) / sizeof(vals[0]); vi++)
    {
        for (unsigned d = 0; d <= 4; d++)
        {
            char mine[64], theirs[64];
            pc_sb b = {mine, sizeof(mine), 0, true};
            pc_sb_fixed(&b, vals[vi], d);
            pc_sb_finish(&b);
            snprintf(theirs, sizeof(theirs), "%.*f", (int)d, vals[vi]);
            TEST_ASSERT_EQUAL_STRING(theirs, mine);
        }
    }
}

void test_sb_json_escapes()
{
    char buf[32];
    pc_sb b = {buf, sizeof(buf), 0, true};
    pc_sb_json(&b, "a\"b\\c");
    TEST_ASSERT_EQUAL_size_t(9, pc_sb_finish(&b));
    TEST_ASSERT_EQUAL_STRING("\"a\\\"b\\\\c\"", buf);

    // an escape that would straddle the end must fail closed, not write one half of the pair
    char tight[6];
    pc_sb t = {tight, sizeof(tight), 0, true};
    pc_sb_json(&t, "ab\"");
    TEST_ASSERT_FALSE(t.ok);
    TEST_ASSERT_EQUAL_size_t(0, pc_sb_finish(&t));
}

void test_strtol()
{
    const char *s = "  -42xyz";
    const char *end = nullptr;
    TEST_ASSERT_EQUAL_INT(-42, pc_strtol(s, &end)); // leading ws + sign + digits
    TEST_ASSERT_EQUAL_PTR(s + 5, end);              // stopped at 'x'
    TEST_ASSERT_EQUAL_INT(7, pc_strtol("+7", nullptr));

    const char *bad = "abc";
    const char *e2 = nullptr;
    TEST_ASSERT_EQUAL_INT(0, pc_strtol(bad, &e2));
    TEST_ASSERT_EQUAL_PTR(bad, e2); // no digit converted -> end == s
}

void test_strtoul()
{
    const char *s = "  +123abc";
    const char *end = nullptr;
    TEST_ASSERT_EQUAL_UINT32(123, pc_strtoul(s, &end)); // ws + '+' + digits
    TEST_ASSERT_EQUAL_PTR(s + 6, end);

    const char *bad = "  x";
    const char *e2 = nullptr;
    TEST_ASSERT_EQUAL_UINT32(0, pc_strtoul(bad, &e2));
    TEST_ASSERT_EQUAL_PTR(bad, e2); // no digits -> end == s
}

void test_strtof()
{
    const char *end = nullptr;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14f, pc_strtof("  3.14", &end)); // ws + int + frac
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -2.5f, pc_strtof("-2.5", nullptr));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 1500.0f, pc_strtof("1.5e3", &end));      // exponent
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0125f, pc_strtof("1.25E-2", &end)); // negative exponent

    const char *bad = "abc";
    const char *e2 = nullptr;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, pc_strtof(bad, &e2));
    TEST_ASSERT_EQUAL_PTR(bad, e2); // no digits -> end == s
}

void test_numparse_branches()
{
    // pc_np_ws: exercise every whitespace operand (line 24) - a run of each
    // recognized whitespace char, then a digit that fails them all.
    const char *end = nullptr;
    TEST_ASSERT_EQUAL_INT(42, pc_strtol("\t\n\r\f\v42", &end));

    // pc_strtoul with a null endptr - the `if (end)` false arm (line 61).
    TEST_ASSERT_EQUAL_UINT32(9, pc_strtoul("9", nullptr));

    // pc_strtod main sign: explicit '+' (line 105 first-operand-true, line 106
    // `== '-'` false). Negative and no-sign are covered by test_strtof.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14f, pc_strtof("+3.14", &end));

    // pc_strtod_exp sign: explicit '+' exponent (line 84 first-operand-true,
    // line 85 `== '-'` false). Negative exponent is covered by test_strtof.
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 1500.0f, pc_strtof("1.5e+3", &end));

    // pc_strtod_exp clamp: a 4-digit exponent drives ex past 400 so the
    // `ex < 400 ? ... : ex` else (clamp) arm fires (line 89). 10^500 -> inf.
    float big = pc_strtof("1e5000", &end);
    TEST_ASSERT_TRUE(big > 1e30f); // clamped/overflowed to a huge value
}

void test_utf8_valid()
{
    TEST_ASSERT_TRUE(pc_utf8_valid((const uint8_t *)"hello", 5)); // ASCII
    const uint8_t two[] = {0xC3, 0xA9};                           // U+00E9 e-acute
    TEST_ASSERT_TRUE(pc_utf8_valid(two, 2));
    const uint8_t three[] = {0xE2, 0x82, 0xAC}; // U+20AC euro
    TEST_ASSERT_TRUE(pc_utf8_valid(three, 3));
    const uint8_t four[] = {0xF0, 0x9F, 0x98, 0x80}; // U+1F600 emoji
    TEST_ASSERT_TRUE(pc_utf8_valid(four, 4));
    TEST_ASSERT_TRUE(pc_utf8_valid(nullptr, 0)); // empty is valid
}

void test_utf8_invalid()
{
    const uint8_t lead_cont[] = {0x80}; // a continuation byte as a lead
    TEST_ASSERT_FALSE(pc_utf8_valid(lead_cont, 1));
    const uint8_t lead_f8[] = {0xF8, 0x80, 0x80, 0x80}; // 0xF8 is not a valid lead
    TEST_ASSERT_FALSE(pc_utf8_valid(lead_f8, 4));
    const uint8_t truncated[] = {0xE2, 0x82}; // 3-byte lead, sequence cut short
    TEST_ASSERT_FALSE(pc_utf8_valid(truncated, 2));
    const uint8_t bad_cont[] = {0xC3, 0x00}; // second byte is not 10xxxxxx
    TEST_ASSERT_FALSE(pc_utf8_valid(bad_cont, 2));
    const uint8_t overlong[] = {0xC0, 0x80}; // overlong encoding of U+0000
    TEST_ASSERT_FALSE(pc_utf8_valid(overlong, 2));
    const uint8_t surrogate[] = {0xED, 0xA0, 0x80}; // U+D800 surrogate
    TEST_ASSERT_FALSE(pc_utf8_valid(surrogate, 3));
    const uint8_t too_big[] = {0xF4, 0x90, 0x80, 0x80}; // U+110000 > U+10FFFF
    TEST_ASSERT_FALSE(pc_utf8_valid(too_big, 4));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_sb_u32);
    RUN_TEST(test_sb_u32_boundaries);
    RUN_TEST(test_sb_overflow_latches);
    RUN_TEST(test_sb_widths_and_bases);
    RUN_TEST(test_sb_64bit);
    RUN_TEST(test_sb_g_matches_libc);
    RUN_TEST(test_sb_fixed_matches_libc);
    RUN_TEST(test_sb_json_escapes);
    RUN_TEST(test_strtol);
    RUN_TEST(test_strtoul);
    RUN_TEST(test_strtof);
    RUN_TEST(test_numparse_branches);
    RUN_TEST(test_utf8_valid);
    RUN_TEST(test_utf8_invalid);
    return UNITY_END();
}
