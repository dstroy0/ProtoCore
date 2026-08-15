// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the bounded-run operations (mmgr/protostr.h).
//
// Three specifications carry this module: POSIX.1-2008 strnlen for the bounded length, ISO C11
// sec 7.24.4/7.24.5 for the comparison and search semantics, ISO C11 sec 7.22.1.4 for the
// strtol-family endptr contract, and ISO C11 sec 7.4.1.10 for the six characters that are ASCII
// white space.
//
// test_ci_folds_only_ascii_letters is the load-bearing case. The case-insensitive walk folds bit 5,
// and bit 5 separates far more pairs than letters: '_' (0x5F) and '?' (0x3F), '@' (0x40) and '`'
// (0x60), '[' (0x5B) and '{' (0x7B). A fold applied to every byte instead of to letters alone makes
// a header name, a token or a path match something it is not, which is a security property and not
// a formatting one.

#include "mmgr/protostr.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}

void tearDown(void)
{
}

// ---- the bounded length ----------------------------------------------------

// POSIX.1-2008 strnlen: the number of bytes before the NUL, or maxlen when there is none among the
// first maxlen bytes.
void test_len_is_the_bounded_strnlen(void)
{
    TEST_ASSERT_EQUAL_size_t(0, str.len("", 8));
    TEST_ASSERT_EQUAL_size_t(5, str.len("hello", 8));
    TEST_ASSERT_EQUAL_size_t(5, str.len("hello", 5)); // the NUL sits exactly at the bound
    TEST_ASSERT_EQUAL_size_t(3, str.len("hello", 3)); // no NUL within the bound
    TEST_ASSERT_EQUAL_size_t(0, str.len("hello", 0));
}

// The bound is a willingness to look, not a promise that many bytes exist: a cap far past a short
// literal still stops at its terminator.
void test_len_stops_at_the_terminator_whatever_the_cap(void)
{
    static const char SHORT[] = "abcde";
    TEST_ASSERT_EQUAL_size_t(5, str.len(SHORT, 0xFFFFu));
    TEST_ASSERT_EQUAL_size_t(5, str.len(SHORT, (size_t)-1));
}

// A run of every length up to a few words, so the head, the whole-word body and the tail are each
// the deciding lane in turn.
void test_len_at_every_length(void)
{
    char s[40];
    for (size_t n = 0; n < sizeof(s); n++)
    {
        memset(s, 'x', sizeof(s));
        s[n] = '\0';
        TEST_ASSERT_EQUAL_size_t(n, str.len(s, sizeof(s)));
    }
}

// ---- where two runs part company -------------------------------------------

// diff names the index of the first differing byte, or the bound when they agree throughout.
void test_diff_names_the_first_differing_index(void)
{
    TEST_ASSERT_EQUAL_size_t(0, str.diff("abc", "xbc", 3, PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(1, str.diff("abc", "axc", 3, PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(2, str.diff("abc", "abx", 3, PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(3, str.diff("abc", "abc", 3, PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(0, str.diff("abc", "abc", 0, PROTO_FALSE));
}

// The same walk over a run long enough to cross several words, with the difference stepped through
// every position so no lane is decided by its neighbour.
void test_diff_at_every_position(void)
{
    char a[40];
    char b[40];
    for (size_t i = 0; i < sizeof(a); i++)
    {
        memset(a, 'q', sizeof(a));
        memset(b, 'q', sizeof(b));
        b[i] = 'Z';
        TEST_ASSERT_EQUAL_size_t(i, str.diff(a, b, sizeof(a), PROTO_FALSE));
    }
}

// ---- equality and prefix ---------------------------------------------------

// C11 sec 7.24.4.2 compares up to the terminator: a prefix never passes as the whole string, so the
// terminator has to arrive strictly before the first disagreement.
void test_eq_requires_the_terminator_before_the_difference(void)
{
    TEST_ASSERT_TRUE(str.eq("token", "token", 16, PROTO_FALSE));
    TEST_ASSERT_TRUE(str.eq("", "", 16, PROTO_FALSE));
    TEST_ASSERT_FALSE(str.eq("token", "tokens", 16, PROTO_FALSE));
    TEST_ASSERT_FALSE(str.eq("tokens", "token", 16, PROTO_FALSE));
    TEST_ASSERT_FALSE(str.eq("token", "toKen", 16, PROTO_FALSE));
    TEST_ASSERT_TRUE(str.eq("token", "toKen", 16, PROTO_TRUE));
}

// starts reads the same tie the other way: the pattern ending is a match, whatever the subject does
// next.
void test_starts_reads_the_tie_as_a_match(void)
{
    TEST_ASSERT_TRUE(str.starts("Content-Length: 42", "Content-", 32, PROTO_FALSE));
    TEST_ASSERT_TRUE(str.starts("abc", "abc", 8, PROTO_FALSE));
    TEST_ASSERT_TRUE(str.starts("abc", "", 8, PROTO_FALSE)); // the empty prefix always matches
    TEST_ASSERT_FALSE(str.starts("abc", "abcd", 8, PROTO_FALSE));
    TEST_ASSERT_FALSE(str.starts("abc", "b", 8, PROTO_FALSE));
    TEST_ASSERT_TRUE(str.starts("CONTENT-length: 1", "content-", 32, PROTO_TRUE));
}

// Bit 5 separates an ASCII letter from its other case, and it separates these pairs too. Folding it
// on every byte instead of on letters alone would make each of these compare equal.
//   '@' 0x40 / '`' 0x60      '[' 0x5B / '{' 0x7B      ']' 0x5D / '}' 0x7D
//   '^' 0x5E / '~' 0x7E      '_' 0x5F / DEL 0x7F      '\\' 0x5C / '|' 0x7C
void test_ci_folds_only_ascii_letters(void)
{
    static const char *const PAIRS[][2] = {
        {"@", "`"}, {"[", "{"}, {"]", "}"}, {"^", "~"}, {"_", "\x7F"}, {"\\", "|"},
    };
    for (unsigned i = 0; i < sizeof(PAIRS) / sizeof(PAIRS[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(str.eq(PAIRS[i][0], PAIRS[i][1], 4, PROTO_TRUE), PAIRS[i][0]);
        TEST_ASSERT_FALSE_MESSAGE(str.starts(PAIRS[i][0], PAIRS[i][1], 4, PROTO_TRUE), PAIRS[i][0]);
    }
    // Every letter, both cases, does fold.
    for (char c = 'a'; c <= 'z'; c++)
    {
        char lower[2] = {c, '\0'};
        char upper[2] = {(char)(c - 32), '\0'};
        TEST_ASSERT_TRUE(str.eq(lower, upper, 4, PROTO_TRUE));
        TEST_ASSERT_FALSE(str.eq(lower, upper, 4, PROTO_FALSE));
    }
    // Digits and punctuation are unchanged by the fold, so they still compare as themselves.
    TEST_ASSERT_TRUE(str.eq("0123456789", "0123456789", 16, PROTO_TRUE));
    TEST_ASSERT_FALSE(str.eq("0", "1", 4, PROTO_TRUE));
}

// A difference several words in, so the case-insensitive walk is exercised past its head lane.
void test_ci_over_a_run_longer_than_a_word(void)
{
    TEST_ASSERT_TRUE(str.eq("Sec-WebSocket-Accept", "sec-websocket-accept", 32, PROTO_TRUE));
    TEST_ASSERT_FALSE(str.eq("Sec-WebSocket-Accept", "sec-websocket-accepT!", 32, PROTO_TRUE));
    TEST_ASSERT_TRUE(str.starts("Sec-WebSocket-Accept: x", "SEC-WEBSOCKET-", 32, PROTO_TRUE));
}

// ---- searching -------------------------------------------------------------

// C11 sec 7.24.5.7: strstr returns the first occurrence, and a zero-length needle returns the start
// of the haystack.
void test_find_returns_the_first_occurrence(void)
{
    static const char HAY[] = "the quick brown fox";
    TEST_ASSERT_EQUAL_PTR(HAY + 0, str.find(HAY, sizeof(HAY), "the", 4, PROTO_FALSE));
    TEST_ASSERT_EQUAL_PTR(HAY + 4, str.find(HAY, sizeof(HAY), "quick", 6, PROTO_FALSE));
    TEST_ASSERT_EQUAL_PTR(HAY + 16, str.find(HAY, sizeof(HAY), "fox", 4, PROTO_FALSE));
    TEST_ASSERT_EQUAL_PTR(HAY + 0, str.find(HAY, sizeof(HAY), "", 1, PROTO_FALSE));
    TEST_ASSERT_NULL(str.find(HAY, sizeof(HAY), "cat", 4, PROTO_FALSE));

    // Repeated anchor bytes: the first full match wins, not the first anchor.
    static const char REP[] = "abababc";
    TEST_ASSERT_EQUAL_PTR(REP + 4, str.find(REP, sizeof(REP), "abc", 4, PROTO_FALSE));
}

// The haystack's terminator ends the search: a needle byte is never NUL, so a terminated lane fails
// the match instead of passing.
void test_find_does_not_read_past_the_terminator(void)
{
    static const char HAY[] = "abc\0def";
    TEST_ASSERT_NULL(str.find(HAY, sizeof(HAY), "def", 4, PROTO_FALSE));
    TEST_ASSERT_EQUAL_PTR(HAY + 0, str.find(HAY, sizeof(HAY), "abc", 4, PROTO_FALSE));
}

// The read_cap bounds the walk even when the haystack continues past it.
void test_find_is_bounded_by_read_cap(void)
{
    static const char HAY[] = "aaaaaaaaXY";
    TEST_ASSERT_NULL(str.find(HAY, 8, "XY", 3, PROTO_FALSE));
    TEST_ASSERT_EQUAL_PTR(HAY + 8, str.find(HAY, sizeof(HAY), "XY", 3, PROTO_FALSE));
}

// has answers the same scan, asked for less, in both case modes.
void test_has_agrees_with_find(void)
{
    static const char HAY[] = "Connection: Upgrade";
    TEST_ASSERT_TRUE(str.has(HAY, sizeof(HAY), "Upgrade", 8, PROTO_FALSE));
    TEST_ASSERT_FALSE(str.has(HAY, sizeof(HAY), "upgrade", 8, PROTO_FALSE));
    TEST_ASSERT_TRUE(str.has(HAY, sizeof(HAY), "upgrade", 8, PROTO_TRUE));
    TEST_ASSERT_FALSE(str.has(HAY, sizeof(HAY), "close", 6, PROTO_TRUE));
}

// ---- copying ---------------------------------------------------------------

// The bound belongs to the destination: the copy always terminates, writes only what it copies, and
// returns the length written.
void test_copy_always_terminates_within_the_destination(void)
{
    char dst[8];

    memset(dst, 0x7F, sizeof(dst));
    TEST_ASSERT_EQUAL_size_t(3, str.copy(dst, "abc", sizeof(dst)));
    TEST_ASSERT_EQUAL_STRING("abc", dst);
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, (uint8_t)dst[4]); // nothing past the terminator

    // An exact fit: seven bytes plus the NUL.
    memset(dst, 0x7F, sizeof(dst));
    TEST_ASSERT_EQUAL_size_t(7, str.copy(dst, "abcdefg", sizeof(dst)));
    TEST_ASSERT_EQUAL_STRING("abcdefg", dst);

    // One byte too long: truncated, still terminated, never past the capacity.
    memset(dst, 0x7F, sizeof(dst));
    TEST_ASSERT_EQUAL_size_t(7, str.copy(dst, "abcdefgh", sizeof(dst)));
    TEST_ASSERT_EQUAL_STRING("abcdefg", dst);
    TEST_ASSERT_EQUAL_CHAR('\0', dst[7]);
}

// A capacity of zero owns no bytes, so not even the terminator may be written.
void test_copy_with_zero_capacity_writes_nothing(void)
{
    char dst[2] = {0x7F, 0x7F};
    TEST_ASSERT_EQUAL_size_t(0, str.copy(dst, "abc", 0));
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, (uint8_t)dst[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, (uint8_t)dst[1]);

    // A capacity of one holds the terminator and nothing else.
    TEST_ASSERT_EQUAL_size_t(0, str.copy(dst, "abc", 1));
    TEST_ASSERT_EQUAL_CHAR('\0', dst[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, (uint8_t)dst[1]);
}

// ---- classification --------------------------------------------------------

// C11 sec 7.4.1.10: the standard white-space characters are space, form feed, new-line, carriage
// return, horizontal tab and vertical tab. Exactly those six and nothing else.
void test_ws_is_the_c11_white_space_set(void)
{
    static const char WS[6] = {' ', '\f', '\n', '\r', '\t', '\v'};
    for (unsigned i = 0; i < sizeof(WS); i++)
    {
        TEST_ASSERT_TRUE(str.ws(WS[i]));
    }
    for (int c = 0; c < 128; c++)
    {
        proto_bool want = PROTO_FALSE;
        for (unsigned i = 0; i < sizeof(WS); i++)
        {
            if ((char)c == WS[i])
            {
                want = PROTO_TRUE;
            }
        }
        TEST_ASSERT_EQUAL_INT((int)want, (int)str.ws((char)c));
    }
}

// C11 sec 7.4.1.5: isdigit is true for the ten decimal digits alone.
void test_digit_is_the_ten_decimal_digits(void)
{
    for (int c = 0; c < 128; c++)
    {
        proto_bool want = (c >= '0' && c <= '9') ? PROTO_TRUE : PROTO_FALSE;
        TEST_ASSERT_EQUAL_INT((int)want, (int)str.digit((char)c));
    }
}

// ---- the number parsers ----------------------------------------------------

// C11 sec 7.22.1.4: strtol skips leading white space, takes an optional sign, converts the digits,
// and stores a pointer to the first unconverted byte in *endptr. "If the subject sequence is empty
// ... the value of nptr is stored in *endptr", which is how a caller tells "no number" from a
// parsed zero.
void test_to_long_follows_the_strtol_endptr_contract(void)
{
    static const char S[] = "  -42xyz";
    const char *end = NULL;
    TEST_ASSERT_EQUAL_INT(-42, (int)str.to_long(S, &end));
    TEST_ASSERT_EQUAL_PTR(S + 5, end);

    TEST_ASSERT_EQUAL_INT(7, (int)str.to_long("+7", NULL)); // a null endptr is legal
    TEST_ASSERT_EQUAL_INT(0, (int)str.to_long("0", NULL));

    static const char BAD[] = "abc";
    const char *e2 = NULL;
    TEST_ASSERT_EQUAL_INT(0, (int)str.to_long(BAD, &e2));
    TEST_ASSERT_EQUAL_PTR(BAD, e2); // empty subject sequence: endptr is nptr

    // A sign with no digits after it is also an empty subject sequence.
    static const char SIGN[] = "-x";
    const char *e3 = NULL;
    TEST_ASSERT_EQUAL_INT(0, (int)str.to_long(SIGN, &e3));
    TEST_ASSERT_EQUAL_PTR(SIGN, e3);

    // Every white-space character C11 sec 7.4.1.10 names is skipped.
    TEST_ASSERT_EQUAL_INT(42, (int)str.to_long(" \f\n\r\t\v42", NULL));
}

// The unsigned form consumes a leading '+' and does not consume a '-', so "-1" has an empty subject
// sequence rather than a wrapped magnitude.
void test_to_ulong_takes_plus_and_not_minus(void)
{
    static const char S[] = "  +123abc";
    const char *end = NULL;
    TEST_ASSERT_EQUAL_UINT32(123u, (uint32_t)str.to_ulong(S, &end));
    TEST_ASSERT_EQUAL_PTR(S + 6, end);

    static const char NEG[] = "-1";
    const char *e2 = NULL;
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)str.to_ulong(NEG, &e2));
    TEST_ASSERT_EQUAL_PTR(NEG, e2);

    TEST_ASSERT_EQUAL_UINT32(9u, (uint32_t)str.to_ulong("9", NULL));
}

// C11 sec 7.22.1.3 strtod's subject sequence: digits, an optional fraction, an optional exponent
// with an optional sign. The endptr contract is the same one.
void test_to_double_parses_the_strtod_subject_sequence(void)
{
    const char *end = NULL;

    // Values whose decimal digits are each a dyadic fraction, so the parse is exact and the
    // comparison can be too: a fifth of ten is a half, and a power of ten scale is exact to 10^22.
    TEST_ASSERT_TRUE(str.to_double("0", NULL) == 0.0);
    TEST_ASSERT_TRUE(str.to_double("1", NULL) == 1.0);
    TEST_ASSERT_TRUE(str.to_double("-2.5", NULL) == -2.5);
    TEST_ASSERT_TRUE(str.to_double("+0.5", NULL) == 0.5);
    TEST_ASSERT_TRUE(str.to_double("1.5e3", &end) == 1500.0);
    TEST_ASSERT_TRUE(str.to_double("1.5E+3", NULL) == 1500.0);
    TEST_ASSERT_TRUE(str.to_double("4e-2", NULL) == 4.0 / 100.0);

    static const char TRAIL[] = "  3.5rest";
    TEST_ASSERT_TRUE(str.to_double(TRAIL, &end) == 3.5);
    TEST_ASSERT_EQUAL_PTR(TRAIL + 5, end);

    static const char BAD[] = "abc";
    const char *e2 = NULL;
    TEST_ASSERT_TRUE(str.to_double(BAD, &e2) == 0.0);
    TEST_ASSERT_EQUAL_PTR(BAD, e2);
}

// The exponent is clamped: past ten to the four hundredth a binary64 is already infinite, so a
// four-digit exponent must not wrap the scale into a small number.
void test_to_double_clamps_a_runaway_exponent(void)
{
    TEST_ASSERT_TRUE(str.to_double("1e5000", NULL) > 1e300);
    TEST_ASSERT_TRUE(str.to_double("1e-5000", NULL) < 1e-300);
}

// The float form parses at double precision first, so a sub-meter value lands on the right float.
void test_to_float_narrows_a_double_parse(void)
{
    TEST_ASSERT_TRUE(str.to_float("0.5", NULL) == 0.5f);
    TEST_ASSERT_TRUE(str.to_float("-1.5", NULL) == -1.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14f, str.to_float("  3.14", NULL));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0125f, str.to_float("1.25E-2", NULL));

    static const char BAD[] = "x";
    const char *e = NULL;
    TEST_ASSERT_TRUE(str.to_float(BAD, &e) == 0.0f);
    TEST_ASSERT_EQUAL_PTR(BAD, e);
}
