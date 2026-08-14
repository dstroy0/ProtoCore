// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the bounded builder (mmgr/membuild.h).
//
// The integer appends render the printf conversions ISO C11 sec 7.21.6.1 defines: %0Nu is decimal
// left-padded with zeros to at least N digits, %0Nx is lowercase hex the same way, and neither ever
// truncates a value that is wider than N. The escapes follow XML 1.0 sec 4.6 (the predefined
// entities) and RFC 8259 sec 7 (a JSON string escapes the quotation mark and the reverse solidus).
//
// test_ok_latches_and_every_later_append_is_a_noop is the load-bearing case. The whole design is
// "check one flag at the end instead of a return value per call", and that is only safe if the
// latch is unconditional: one append that would not fit has to make every later append a no-op and
// make finish report zero, so a caller that ignores the flag emits nothing rather than a frame
// missing its middle.

#include "mmgr/float_bits.h" // dbl.from_bits - the bit patterns the predicates are asked about
#include "mmgr/membuild.h"
#include <string.h>

#include <unity.h>

// A builder over @p cap bytes of the shared scratch buffer.
static char g_buf[128];

static protocore_sb sb(size_t cap)
{
    memset(g_buf, 0x7F, sizeof(g_buf));
    protocore_sb b = {g_buf, cap, 0, PROTO_TRUE};
    return b;
}

void setUp(void)
{
}

void tearDown(void)
{
}

// ---- the primitive append --------------------------------------------------

// put_n takes the length rather than scanning, so a run holding a NUL is appended whole.
void test_put_n_takes_the_length_and_not_a_terminator(void)
{
    protocore_sb b = sb(sizeof(g_buf));
    Sb.put_n(&b, "ab\0cd", 5);
    TEST_ASSERT_TRUE(b.ok);
    TEST_ASSERT_EQUAL_size_t(5, Sb.finish(&b));
    TEST_ASSERT_EQUAL_HEX8_ARRAY("ab\0cd", g_buf, 5);
}

// protocore_sb_lit deduces the length from the array type, so the terminator is not appended.
void test_lit_takes_the_array_extent(void)
{
    protocore_sb b = sb(sizeof(g_buf));
    protocore_sb_lit(&b, "HTTP/1.1 ");
    TEST_ASSERT_EQUAL_size_t(9, Sb.finish(&b));
    TEST_ASSERT_EQUAL_STRING("HTTP/1.1 ", g_buf);
}

// put measures the NUL-terminated source within cap and appends it whole.
void test_put_appends_a_runtime_string(void)
{
    protocore_sb b = sb(sizeof(g_buf));
    Sb.put(&b, "Content-Type: ");
    Sb.put(&b, "text/plain");
    TEST_ASSERT_EQUAL_size_t(24, Sb.finish(&b));
    TEST_ASSERT_EQUAL_STRING("Content-Type: text/plain", g_buf);
}

// One character, and the same bound.
void test_ch_appends_one_character(void)
{
    protocore_sb b = sb(sizeof(g_buf));
    Sb.ch(&b, 'x');
    Sb.ch(&b, '|');
    TEST_ASSERT_EQUAL_size_t(2, Sb.finish(&b));
    TEST_ASSERT_EQUAL_STRING("x|", g_buf);
}

// ---- the bound and the latch -----------------------------------------------

// The capacity includes the terminator: n bytes plus the NUL fit a cap of n + 1, and one byte less
// does not.
void test_an_append_is_all_or_nothing_at_the_exact_boundary(void)
{
    protocore_sb fit = sb(6);
    Sb.put(&fit, "abcde");
    TEST_ASSERT_TRUE(fit.ok);
    TEST_ASSERT_EQUAL_size_t(5, Sb.finish(&fit));
    TEST_ASSERT_EQUAL_STRING("abcde", g_buf);

    protocore_sb over = sb(5);
    Sb.put(&over, "abcde");
    TEST_ASSERT_FALSE(over.ok);
    TEST_ASSERT_EQUAL_size_t(0, over.len); // nothing was written, not a truncated prefix
    TEST_ASSERT_EQUAL_size_t(0, Sb.finish(&over));
}

// Once ok is false every later append does nothing and finish reports zero, whatever would have fit.
void test_ok_latches_and_every_later_append_is_a_noop(void)
{
    protocore_sb b = sb(8);
    Sb.put(&b, "abcdef"); // 6 + NUL fits
    TEST_ASSERT_TRUE(b.ok);
    size_t before = b.len;

    Sb.u32(&b, 12345u); // would need five more: latches
    TEST_ASSERT_FALSE(b.ok);
    TEST_ASSERT_EQUAL_size_t(before, b.len);

    Sb.ch(&b, 'x');
    Sb.put(&b, "y");
    Sb.put_n(&b, "z", 1);
    Sb.u32(&b, 1u);
    Sb.hex(&b, 0u, 1);
    Sb.i64(&b, -1);
    Sb.json(&b, "q");
    Sb.xml(&b, "q");
    Sb.put_clip(&b, "q");
    Sb.u64_clip(&b, 1u, 0);
    TEST_ASSERT_EQUAL_size_t(before, b.len);
    TEST_ASSERT_FALSE(b.ok);
    TEST_ASSERT_EQUAL_size_t(0, Sb.finish(&b));
}

// A capacity of zero owns no bytes, so not even the terminator may be written.
void test_zero_capacity_writes_nothing(void)
{
    char sentinel[2] = {0x7F, 0x7F};
    protocore_sb b = {sentinel, 0, 0, PROTO_TRUE};
    Sb.put(&b, "abc");
    TEST_ASSERT_EQUAL_size_t(0, Sb.finish(&b));
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, (uint8_t)sentinel[0]);
}

// ---- the clipping appends --------------------------------------------------

// put_clip fills what fits and stops, without latching: display text, never a protocol field.
void test_put_clip_fills_what_fits_without_latching(void)
{
    protocore_sb b = sb(6);
    Sb.put_clip(&b, "abcdefghij");
    TEST_ASSERT_TRUE(b.ok);
    TEST_ASSERT_EQUAL_size_t(5, Sb.finish(&b));
    TEST_ASSERT_EQUAL_STRING("abcde", g_buf);

    // A NULL source appends nothing and still does not latch.
    protocore_sb n = sb(8);
    Sb.put_clip(&n, NULL);
    TEST_ASSERT_TRUE(n.ok);
    TEST_ASSERT_EQUAL_size_t(0, Sb.finish(&n));
}

// u64_clip is all-or-nothing where put_clip is byte-wise, because half a number reads as a
// different number. It right-aligns in at least `columns` with leading spaces.
void test_u64_clip_right_aligns_or_appends_nothing(void)
{
    protocore_sb b = sb(sizeof(g_buf));
    Sb.u64_clip(&b, 42u, 5);
    Sb.ch(&b, '|');
    Sb.u64_clip(&b, 123456u, 3); // wider than the column: never truncated
    Sb.ch(&b, '|');
    Sb.u64_clip(&b, 0u, 0); // natural width
    TEST_ASSERT_EQUAL_size_t(14, Sb.finish(&b));
    TEST_ASSERT_EQUAL_STRING("   42|123456|0", g_buf);

    // A field that does not fit appends nothing at all, and does not latch.
    protocore_sb t = sb(4);
    Sb.u64_clip(&t, 12345u, 0);
    TEST_ASSERT_TRUE(t.ok);
    TEST_ASSERT_EQUAL_size_t(0, Sb.finish(&t));
}

// ---- the integer conversions -----------------------------------------------

// C11 sec 7.21.6.1: the u conversion is decimal, the x conversion is lowercase hex, and a zero
// precision pads on the left with '0' to the stated width. A value wider than the width keeps all
// its digits.
void test_zero_padded_widths_follow_the_printf_conversions(void)
{
    protocore_sb b = sb(sizeof(g_buf));
    Sb.hex(&b, 0xdeadbeefu, 8); // %08x
    Sb.ch(&b, '|');
    Sb.hex(&b, 0x5u, 4); // %04x
    Sb.ch(&b, '|');
    Sb.hex(&b, 0xabcu, 1); // %x
    Sb.ch(&b, '|');
    Sb.u32w(&b, 7u, 2); // %02u
    Sb.ch(&b, '|');
    Sb.u32w(&b, 12345u, 2); // wider than the width, so nothing is dropped
    TEST_ASSERT_EQUAL_size_t(26, Sb.finish(&b));
    TEST_ASSERT_EQUAL_STRING("deadbeef|0005|abc|07|12345", g_buf);
}

// The one engine behind the decimal and hex appends, driven directly at both bases and at base 8.
void test_uint_renders_base_8_10_and_16(void)
{
    protocore_sb b = sb(sizeof(g_buf));
    Sb.uint(&b, 255u, 16, 1);
    Sb.ch(&b, ',');
    Sb.uint(&b, 255u, 10, 1);
    Sb.ch(&b, ',');
    Sb.uint(&b, 255u, 8, 1);
    Sb.ch(&b, ',');
    Sb.uint(&b, 8u, 8, 4); // %04o
    TEST_ASSERT_EQUAL_size_t(15, Sb.finish(&b));
    TEST_ASSERT_EQUAL_STRING("ff,255,377,0010", g_buf);
}

// The full range of each width, so no digit-count path is left unrendered.
void test_the_full_integer_ranges(void)
{
    protocore_sb u = sb(sizeof(g_buf));
    Sb.u32(&u, 0u);
    Sb.ch(&u, ',');
    Sb.u32(&u, 4294967295u); // UINT32_MAX, ten digits
    TEST_ASSERT_EQUAL_size_t(12, Sb.finish(&u));
    TEST_ASSERT_EQUAL_STRING("0,4294967295", g_buf);

    protocore_sb w = sb(sizeof(g_buf));
    Sb.u64(&w, 18446744073709551615ull); // UINT64_MAX, twenty digits
    TEST_ASSERT_EQUAL_size_t(20, Sb.finish(&w));
    TEST_ASSERT_EQUAL_STRING("18446744073709551615", g_buf);

    protocore_sb s = sb(sizeof(g_buf));
    Sb.i64(&s, -4096);
    Sb.ch(&s, ',');
    // INT64_MIN: taking the magnitude by negating the signed value would overflow, so this is the
    // case the unsigned path exists for.
    Sb.i64(&s, (int64_t)(-9223372036854775807LL - 1));
    Sb.ch(&s, ',');
    Sb.i64(&s, 9223372036854775807LL);
    TEST_ASSERT_EQUAL_size_t(46, Sb.finish(&s));
    TEST_ASSERT_EQUAL_STRING("-4096,-9223372036854775808,9223372036854775807", g_buf);
}

// Every digit count either side of its exact fit: the field is measured and then filled
// back-to-front, so an off-by-one lands at the field edge.
void test_each_digit_count_at_its_exact_fit(void)
{
    static const uint32_t VALS[] = {9u, 99u, 999u, 123456789u, 4294967295u};
    static const size_t WIDTH[] = {1, 2, 3, 9, 10};
    for (unsigned i = 0; i < sizeof(VALS) / sizeof(VALS[0]); i++)
    {
        protocore_sb fit = sb(WIDTH[i] + 1u);
        Sb.u32(&fit, VALS[i]);
        TEST_ASSERT_TRUE(fit.ok);
        TEST_ASSERT_EQUAL_size_t(WIDTH[i], Sb.finish(&fit));

        protocore_sb over = sb(WIDTH[i]);
        Sb.u32(&over, VALS[i]);
        TEST_ASSERT_FALSE(over.ok);
        TEST_ASSERT_EQUAL_size_t(0, over.len);
        TEST_ASSERT_EQUAL_size_t(0, Sb.finish(&over));
    }
}

// ---- the escapes -----------------------------------------------------------

// XML 1.0 sec 4.6 names the predefined entities; this appender uses four of them.
void test_xml_writes_the_predefined_entities(void)
{
    protocore_sb b = sb(sizeof(g_buf));
    Sb.xml(&b, "a&b<c>d\"e");
    TEST_ASSERT_EQUAL_size_t(24, Sb.finish(&b));
    TEST_ASSERT_EQUAL_STRING("a&amp;b&lt;c&gt;d&quot;e", g_buf);

    protocore_sb n = sb(sizeof(g_buf));
    Sb.xml(&n, NULL);
    TEST_ASSERT_TRUE(n.ok);
    TEST_ASSERT_EQUAL_size_t(0, Sb.finish(&n));
}

// RFC 8259 sec 7: a string is wrapped in quotation marks, and the quotation mark and the reverse
// solidus MUST be escaped.
void test_json_quotes_and_escapes_the_two_required_characters(void)
{
    protocore_sb b = sb(sizeof(g_buf));
    Sb.json(&b, "a\"b\\c");
    TEST_ASSERT_EQUAL_size_t(9, Sb.finish(&b));
    TEST_ASSERT_EQUAL_STRING("\"a\\\"b\\\\c\"", g_buf);

    // A NULL source is the empty JSON string, not a crash and not "null".
    protocore_sb n = sb(sizeof(g_buf));
    Sb.json(&n, NULL);
    TEST_ASSERT_EQUAL_size_t(2, Sb.finish(&n));
    TEST_ASSERT_EQUAL_STRING("\"\"", g_buf);
}

// An escape pair that would straddle the end latches rather than writing one half of it: half an
// escape is a different string.
void test_a_json_escape_that_would_straddle_the_end_fails_closed(void)
{
    protocore_sb b = sb(6);
    Sb.json(&b, "ab\"");
    TEST_ASSERT_FALSE(b.ok);
    TEST_ASSERT_EQUAL_size_t(0, Sb.finish(&b));
}

// ---- the IEEE 754 predicates -----------------------------------------------

// IEEE 754 sec 3.4: the sign bit is a field of the encoding, so it is set for -0.0 too - which is
// what a comparison against zero cannot see.
void test_sign_bit_reads_the_encoding_not_the_value(void)
{
    TEST_ASSERT_TRUE(Sb.sign_bit(-0.0));
    TEST_ASSERT_FALSE(Sb.sign_bit(0.0));
    TEST_ASSERT_TRUE(Sb.sign_bit(-1.0));
    TEST_ASSERT_FALSE(Sb.sign_bit(1.0));
}

// IEEE 754 sec 3.4: an all-ones exponent field with a zero significand is an infinity, and with a
// nonzero one a NaN. Neither predicate is true of a finite value.
void test_is_inf_and_is_nan_split_the_all_ones_exponent(void)
{
    // 0x7FF0000000000000 is +inf and 0x7FF0000000000001 a NaN, built from the field layout.
    double inf = dbl.from_bits(0x7FF0000000000000ull);
    double neg_inf = dbl.from_bits(0xFFF0000000000000ull);
    double nan_v = dbl.from_bits(0x7FF8000000000000ull);

    TEST_ASSERT_TRUE(Sb.is_inf(inf));
    TEST_ASSERT_TRUE(Sb.is_inf(neg_inf));
    TEST_ASSERT_FALSE(Sb.is_nan(inf));

    TEST_ASSERT_TRUE(Sb.is_nan(nan_v));
    TEST_ASSERT_FALSE(Sb.is_inf(nan_v));

    TEST_ASSERT_FALSE(Sb.is_inf(0.0));
    TEST_ASSERT_FALSE(Sb.is_nan(0.0));
    TEST_ASSERT_FALSE(Sb.is_inf(1.7976931348623157e308)); // the largest finite binary64
    TEST_ASSERT_FALSE(Sb.is_nan(1.7976931348623157e308));
}
