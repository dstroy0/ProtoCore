// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for UTF-8 well-formedness (shared/utf8/utf8.h).
//
// RFC 3629 sec 3 gives the only well-formed octet sequences, and sec 10 states what a validator
// must refuse: an overlong encoding, a surrogate code point, and anything above U+10FFFF. Those
// three are the security-relevant ones - an overlong "/" (C0 AF) or a surrogate half that a lax
// decoder accepts is how a path check gets walked past - so each is asserted from the octets the
// RFC prints rather than from whatever this decoder happens to do.
//
// The caller that matters is the WebSocket close path: RFC 6455 sec 8.1 requires a TEXT frame's
// payload to be well-formed UTF-8 and the connection failed if it is not.

#include "shared/utf8/utf8.h"
#include <string.h>

#include <unity.h>

static uint8_t utf8_work[16]; // the borrow an entry takes; Utf8 never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static proto_bool ok_of(const void *p, size_t n)
{
    Utf8.args.s = (const uint8_t *)p;
    Utf8.args.n = n;
    Utf8.valid(utf8_work);
    return Utf8.ok;
}

// RFC 3629 sec 3, the four well-formed lengths, at their lowest legal code point each:
//   U+0000    00
//   U+0080    C2 80
//   U+0800    E0 A0 80
//   U+10000   F0 90 80 80
void test_shortest_form_boundaries_are_accepted(void)
{
    TEST_ASSERT_TRUE(ok_of("\x00", 1));
    TEST_ASSERT_TRUE(ok_of("\xC2\x80", 2));
    TEST_ASSERT_TRUE(ok_of("\xE0\xA0\x80", 3));
    TEST_ASSERT_TRUE(ok_of("\xF0\x90\x80\x80", 4));
}

// ...and at their highest: U+007F, U+07FF, U+FFFF, U+10FFFF.
void test_upper_boundaries_are_accepted(void)
{
    TEST_ASSERT_TRUE(ok_of("\x7F", 1));
    TEST_ASSERT_TRUE(ok_of("\xDF\xBF", 2));
    TEST_ASSERT_TRUE(ok_of("\xEF\xBF\xBF", 3));
    TEST_ASSERT_TRUE(ok_of("\xF4\x8F\xBF\xBF", 4));
}

// sec 10: "the shortest form ... is the only valid encoding". These encode U+002F, U+0000 and
// U+07FF in more octets than needed. C0 AF is the overlong solidus a path check must never see.
void test_overlong_forms_are_refused(void)
{
    TEST_ASSERT_FALSE(ok_of("\xC0\xAF", 2));         // U+002F as two octets
    TEST_ASSERT_FALSE(ok_of("\xC1\xBF", 2));         // U+007F as two octets
    TEST_ASSERT_FALSE(ok_of("\xE0\x80\xAF", 3));     // U+002F as three
    TEST_ASSERT_FALSE(ok_of("\xE0\x9F\xBF", 3));     // U+07FF as three
    TEST_ASSERT_FALSE(ok_of("\xF0\x80\x80\xAF", 4)); // U+002F as four
    TEST_ASSERT_FALSE(ok_of("\xF0\x8F\xBF\xBF", 4)); // U+FFFF as four
}

// sec 3: the surrogate range U+D800..U+DFFF has no UTF-8 encoding. ED A0 80 is U+D800 and
// ED BF BF is U+DFFF; the code point either side of the range is well-formed.
void test_surrogates_are_refused(void)
{
    TEST_ASSERT_FALSE(ok_of("\xED\xA0\x80", 3)); // U+D800, first surrogate
    TEST_ASSERT_FALSE(ok_of("\xED\xBF\xBF", 3)); // U+DFFF, last surrogate
    TEST_ASSERT_TRUE(ok_of("\xED\x9F\xBF", 3));  // U+D7FF, just below
    TEST_ASSERT_TRUE(ok_of("\xEE\x80\x80", 3));  // U+E000, just above
}

// sec 3: "the maximum value is U+10FFFF". F4 90 80 80 would be U+110000.
void test_above_max_code_point_is_refused(void)
{
    TEST_ASSERT_FALSE(ok_of("\xF4\x90\x80\x80", 4));
    TEST_ASSERT_FALSE(ok_of("\xF5\x80\x80\x80", 4)); // F5..FF can never start a sequence
    TEST_ASSERT_FALSE(ok_of("\xFF", 1));
}

// A continuation octet cannot start a sequence, and a lead octet needs its full tail.
void test_stray_and_truncated_sequences_are_refused(void)
{
    TEST_ASSERT_FALSE(ok_of("\x80", 1)); // continuation with no lead
    TEST_ASSERT_FALSE(ok_of("\xBF", 1));
    TEST_ASSERT_FALSE(ok_of("\xC2", 1));         // lead promising one more octet
    TEST_ASSERT_FALSE(ok_of("\xE0\xA0", 2));     // three-octet lead, one short
    TEST_ASSERT_FALSE(ok_of("\xF0\x90\x80", 3)); // four-octet lead, one short
    TEST_ASSERT_FALSE(ok_of("\xC2\x41", 2));     // tail is not a continuation
}

// Mixed ASCII and multi-byte text, the ordinary case.
void test_mixed_text_is_accepted(void)
{
    static const char S[] = "ok \xC2\xA3 \xE2\x82\xAC \xF0\x9F\x92\xA9 end";
    TEST_ASSERT_TRUE(ok_of(S, sizeof(S) - 1));
}

// A run that is well-formed on its own stays well-formed when the length stops short of a
// sequence boundary - the caller's length bounds the walk, not a terminator.
void test_length_bounds_the_walk(void)
{
    static const char S[] = "a\xC2\xA3";
    TEST_ASSERT_TRUE(ok_of(S, 1));  // just the 'a'
    TEST_ASSERT_FALSE(ok_of(S, 2)); // 'a' plus a lead with no tail
    TEST_ASSERT_TRUE(ok_of(S, 3));  // the whole thing
}

// An empty run is vacuously well-formed.
void test_empty_run_is_valid(void)
{
    TEST_ASSERT_TRUE(ok_of("", 0));
}
