// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// mmgr/protostr.h: the bounded-run walks. The oracle is a byte loop written here.
//
// The number parsers (to_long, to_ulong, to_float) are exercised by native_primitives against the
// numparse contract; this group covers the eleven members that had no test and asserts that every
// member of the table is the walk its name says.

#include "mmgr/protostr.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#define BIG 0xFFFFu

// Every buffer the library is handed is a pool borrow: aligned, and padded past its contents. A
// bare string literal is neither, so the subjects below live here and the literals are copied in.
static char PROTO_ALIGN(16) s_hay[64];

static const char *hay_of(const char *s)
{
    memset(s_hay, 0, sizeof(s_hay));
    memcpy(s_hay, s, strlen(s));
    return s_hay;
}

// The second operand of a compare, staged the same way. read_cap is a promise that the bytes are
// readable, so a compare over two bare literals cannot state a cap larger than the shorter one -
// and the walk steps a word at a time, so it really does run off the end.
static char PROTO_ALIGN(16) s_pat[64];

static const char *pat_of(const char *s)
{
    memset(s_pat, 0, sizeof(s_pat));
    memcpy(s_pat, s, strlen(s));
    return s_pat;
}

// The cap both staged operands can honour.
#define STAGED_CAP (sizeof(s_hay) < sizeof(s_pat) ? sizeof(s_hay) : sizeof(s_pat))

void setUp(void)
{
    memset(s_hay, 0, sizeof(s_hay));
}

void tearDown(void)
{
}

// Index of the first NUL within cap, or cap.
static size_t oracle_len(const char *s, size_t cap)
{
    size_t i = 0;
    while (i < cap && s[i] != '\0')
    {
        i++;
    }
    return i;
}

static char fold(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

// Index of the first byte that differs within cap, or cap.
static size_t oracle_diff(const char *a, const char *b, size_t cap, int ci)
{
    size_t i = 0;
    while (i < cap)
    {
        char x = a[i];
        char y = b[i];
        if (ci)
        {
            x = fold(x);
            y = fold(y);
        }
        if (x != y)
        {
            return i;
        }
        i++;
    }
    return cap;
}

// ---- len ------------------------------------------------------------------

// The bounded length, at every length through several words and at every offset within one.
void test_len_matches_the_oracle()
{
    static char buf[80];
    for (size_t off = 0; off < 8u; off++)
    {
        for (size_t n = 0; n < 48u; n++)
        {
            memset(buf, 'x', sizeof(buf));
            buf[off + n] = '\0';
            TEST_ASSERT_EQUAL_size_t(n, str.len(buf + off, BIG));
            TEST_ASSERT_EQUAL_size_t(oracle_len(buf + off, BIG), str.len(buf + off, BIG));
        }
    }
}

// The cap is how far this may look, so an unterminated run stops at it and reports it.
void test_len_stops_at_the_cap()
{
    static char buf[64];
    memset(buf, 'y', sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(0, str.len(buf, 0));
    TEST_ASSERT_EQUAL_size_t(1, str.len(buf, 1));
    TEST_ASSERT_EQUAL_size_t(17, str.len(buf, 17));
    buf[10] = '\0';
    TEST_ASSERT_EQUAL_size_t(10, str.len(buf, 17)); // the NUL arrives first
    TEST_ASSERT_EQUAL_size_t(5, str.len(buf, 5));   // the cap arrives first
}

// ---- diff -----------------------------------------------------------------

// The first differing index, at every position and every alignment, both case rules.
void test_diff_matches_the_oracle()
{
    static char a[72];
    static char b[72];
    for (size_t off = 0; off < 8u; off++)
    {
        for (size_t pos = 0; pos < 40u; pos++)
        {
            memset(a, 'm', sizeof(a));
            memset(b, 'm', sizeof(b));
            b[off + pos] = 'n';
            TEST_ASSERT_EQUAL_size_t(pos, str.diff(a + off, b + off, 48, PROTO_FALSE));
            TEST_ASSERT_EQUAL_size_t(oracle_diff(a + off, b + off, 48, 0), str.diff(a + off, b + off, 48, PROTO_FALSE));
        }
    }
}

// Agreement throughout reports the cap.
void test_diff_reports_the_cap_when_equal()
{
    static char a[64];
    static char b[64];
    memset(a, 'q', sizeof(a));
    memset(b, 'q', sizeof(b));
    TEST_ASSERT_EQUAL_size_t(40, str.diff(a, b, 40, PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(0, str.diff(a, b, 0, PROTO_FALSE));
}

// Case folding applies to the letters and to nothing else.
void test_diff_case_insensitive()
{
    TEST_ASSERT_EQUAL_size_t(8, str.diff("ABCDEFGH", "abcdefgh", 8, PROTO_TRUE));
    TEST_ASSERT_EQUAL_size_t(0, str.diff("ABCDEFGH", "abcdefgh", 8, PROTO_FALSE));
    // '_' is 0x5F and '?' is 0x3F: one apart in the bit case folding would touch.
    TEST_ASSERT_EQUAL_size_t(0, str.diff("_", "?", 1, PROTO_TRUE));
    TEST_ASSERT_EQUAL_size_t(0, str.diff("[", "{", 1, PROTO_TRUE));
}

// ---- eq and starts --------------------------------------------------------

// Equality is the whole string: a prefix never passes as the whole.
void test_eq_requires_the_whole_string()
{
    TEST_ASSERT_TRUE(str.eq(hay_of("hello"), pat_of("hello"), STAGED_CAP, PROTO_FALSE));
    TEST_ASSERT_FALSE(str.eq(hay_of("hello"), pat_of("hell"), STAGED_CAP, PROTO_FALSE));
    TEST_ASSERT_FALSE(str.eq(hay_of("hell"), pat_of("hello"), STAGED_CAP, PROTO_FALSE));
    TEST_ASSERT_TRUE(str.eq(hay_of(""), pat_of(""), STAGED_CAP, PROTO_FALSE));
    TEST_ASSERT_FALSE(str.eq(hay_of(""), pat_of("a"), STAGED_CAP, PROTO_FALSE));
    TEST_ASSERT_TRUE(str.eq(hay_of("HeLLo"), pat_of("hello"), STAGED_CAP, PROTO_TRUE));
    TEST_ASSERT_FALSE(str.eq(hay_of("HeLLo"), pat_of("hello"), STAGED_CAP, PROTO_FALSE));
}

// Equality holds at every length, including across the word boundary.
void test_eq_at_every_length()
{
    static char a[64];
    static char b[64];
    for (size_t n = 0; n < 40u; n++)
    {
        memset(a, 'z', sizeof(a));
        memset(b, 'z', sizeof(b));
        a[n] = '\0';
        b[n] = '\0';
        TEST_ASSERT_TRUE(str.eq(a, b, sizeof(a), PROTO_FALSE));
        b[n] = 'z';
        b[n + 1u] = '\0';
        TEST_ASSERT_FALSE(str.eq(a, b, sizeof(a), PROTO_FALSE)); // one byte longer is not equal
    }
}

// starts reads the tie the other way: the pattern ending is a match.
void test_starts_reads_the_tie_as_a_match()
{
    TEST_ASSERT_TRUE(str.starts(hay_of("hello world"), pat_of("hello"), STAGED_CAP, PROTO_FALSE));
    TEST_ASSERT_TRUE(str.starts(hay_of("hello"), pat_of("hello"), STAGED_CAP, PROTO_FALSE));
    // every string starts with nothing
    TEST_ASSERT_TRUE(str.starts(hay_of("hello"), pat_of(""), STAGED_CAP, PROTO_FALSE));
    TEST_ASSERT_FALSE(str.starts(hay_of("hello"), pat_of("hello world"), STAGED_CAP, PROTO_FALSE));
    TEST_ASSERT_FALSE(str.starts(hay_of("hello"), pat_of("world"), STAGED_CAP, PROTO_FALSE));
    TEST_ASSERT_TRUE(str.starts(hay_of("HTTP/1.1 200"), pat_of("http/1.1"), STAGED_CAP, PROTO_TRUE));
    TEST_ASSERT_FALSE(str.starts(hay_of("HTTP/1.1 200"), pat_of("http/1.1"), STAGED_CAP, PROTO_FALSE));
}

// The prefix is checked at every length of a long subject.
void test_starts_at_every_prefix_length()
{
    static const char subject[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static char pre[40];
    for (size_t n = 0; n < sizeof(subject) - 1u; n++)
    {
        memcpy(pre, subject, n);
        pre[n] = '\0';
        TEST_ASSERT_TRUE(str.starts(subject, pre, sizeof(pre), PROTO_FALSE));
        if (n > 0)
        {
            pre[n - 1u] = '!'; // break the last byte of the prefix
            TEST_ASSERT_FALSE(str.starts(subject, pre, sizeof(pre), PROTO_FALSE));
        }
    }
}

// ---- find and has ---------------------------------------------------------

// The first occurrence, at every position in the haystack.
void test_find_locates_the_first_occurrence()
{
    static char hay[80];
    for (size_t pos = 0; pos < 60u; pos++)
    {
        memset(hay, '.', sizeof(hay));
        hay[70] = '\0';
        memcpy(hay + pos, "key", 3);
        const char *hit = str.find(hay, sizeof(hay), "key", sizeof("key"), PROTO_FALSE);
        TEST_ASSERT_EQUAL_PTR(hay + pos, hit);
    }
}

// A needle that is not there yields NULL, and a partial match is not a match. Both caps are the
// count of READABLE bytes, so a literal's is its size: the terminator is one of them.
void test_find_absent()
{
    TEST_ASSERT_NULL(str.find(hay_of("abcdefg"), sizeof(s_hay), "xyz", sizeof("xyz"), PROTO_FALSE));
    TEST_ASSERT_NULL(str.find(hay_of("abcabd"), sizeof(s_hay), "abce", sizeof("abce"), PROTO_FALSE));
    TEST_ASSERT_NULL(str.find(hay_of(""), sizeof(s_hay), "a", sizeof("a"), PROTO_FALSE));
}

// An empty needle matches at the start.
void test_find_empty_needle_matches_at_the_start()
{
    const char *hay = hay_of("abcdef");
    TEST_ASSERT_EQUAL_PTR(hay, str.find(hay, sizeof(s_hay), "", sizeof(""), PROTO_FALSE));
}

// A haystack NUL ends the search: a needle byte is never NUL, so a terminated lane cannot match.
void test_find_stops_at_the_haystack_nul()
{
    static char hay[64];
    memset(hay, '.', sizeof(hay));
    hay[8] = '\0';
    memcpy(hay + 20, "key", 3); // past the terminator
    TEST_ASSERT_NULL(str.find(hay, sizeof(hay), "key", sizeof("key"), PROTO_FALSE));
}

// Case folding applies to the search too.
void test_find_case_insensitive()
{
    TEST_ASSERT_NOT_NULL(
        str.find(hay_of("Content-Length: 5"), sizeof(s_hay), "content-length", sizeof("content-length"), PROTO_TRUE));
    TEST_ASSERT_NULL(
        str.find(hay_of("Content-Length: 5"), sizeof(s_hay), "content-length", sizeof("content-length"), PROTO_FALSE));
}

// has answers the same scan with less.
void test_has_agrees_with_find()
{
    static const char *hays[] = {"abcdef", "", "the needle is here", "nee"};
    static const char *needles[] = {"cd", "a", "needle", "needle"};
    for (unsigned i = 0; i < 4u; i++)
    {
        size_t ncap = strlen(needles[i]) + 1u; // the terminator is a readable byte too
        const char *hay = hay_of(hays[i]);
        proto_bool want = (str.find(hay, sizeof(s_hay), needles[i], ncap, PROTO_FALSE) != NULL);
        TEST_ASSERT_EQUAL_INT(want, str.has(hay, sizeof(s_hay), needles[i], ncap, PROTO_FALSE));
    }
}

// ---- find under hard input ------------------------------------------------

static char PROTO_ALIGN(16) s_ndl[32];

// Place a literal at an offset inside the borrow, so the walk meets every alignment in turn.
static char *hay_at(size_t off, const char *s)
{
    memset(s_hay, 0, sizeof(s_hay));
    memcpy(s_hay + off, s, strlen(s));
    return s_hay + off;
}

static const char *put_needle(const char *s)
{
    memset(s_ndl, 0, sizeof(s_ndl));
    memcpy(s_ndl, s, strlen(s));
    return s_ndl;
}

// A naive search bounded by the haystack's own terminator: what find must agree with, byte for byte.
static const char *oracle_find(const char *hay, size_t cap, const char *needle, int ci)
{
    size_t hl = 0;
    while (hl < cap && hay[hl] != '\0')
    {
        hl++;
    }
    size_t nl = strlen(needle);
    if (nl == 0)
    {
        return hay;
    }
    if (nl > hl)
    {
        return NULL;
    }
    for (size_t i = 0; i + nl <= hl; i++)
    {
        size_t j = 0;
        while (j < nl)
        {
            char a = hay[i + j];
            char b = needle[j];
            if (ci)
            {
                a = fold(a);
                b = fold(b);
            }
            if (a != b)
            {
                break;
            }
            j++;
        }
        if (j == nl)
        {
            return hay + i;
        }
    }
    return NULL;
}

// Periodic and self-overlapping haystacks are what break a search that assumes a failed match can
// skip the whole needle, and a needle that is its own prefix is what breaks the anchor. Every
// combination is run at every offset within a word and under both case rules.
void test_find_agrees_with_a_naive_search_under_hard_input()
{
    static const char *hays[] = {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                 "abababababababababababababababab",
                                 "aabaabaabaabaabaabaabaabaabaabaa",
                                 "aaabaaabaaabaaabaaabaaabaaabaaab",
                                 "aaaaaaaaaaaaaaaab",
                                 "baaaaaaaaaaaaaaaa",
                                 "the quick brown fox jumps over t",
                                 "AbAbAbAbAbAbAbAbAbAbAbAbAbAbAbAb",
                                 "\x80\x81\x82\x83\x80\x81\x82\x83\x80\x81\x82\x83\x80\x81\x82\x83",
                                 "\xFF\xFE\xFF\xFE\xFF\xFE\xFF\xFE\xFF\xFE\xFF\xFE\xFF\xFE\xFF\xFE",
                                 "a",
                                 "ab"};
    static const char *ndls[] = {"a",
                                 "b",
                                 "aa",
                                 "ab",
                                 "ba",
                                 "aab",
                                 "aaa",
                                 "abab",
                                 "aabaab",
                                 "aaab",
                                 "aaaa",
                                 "aaaaa",
                                 "aaaaaaaa",
                                 "aaaaaaaaa",
                                 "fox",
                                 "the",
                                 "over t",
                                 "quick brown",
                                 "\x80\x81",
                                 "\x83\x80",
                                 "\xFF\xFE\xFF",
                                 "abababababababab"};
    char msg[192];

    for (size_t off = 0; off < 8u; off++)
    {
        for (unsigned h = 0; h < sizeof(hays) / sizeof(hays[0]); h++)
        {
            for (unsigned d = 0; d < sizeof(ndls) / sizeof(ndls[0]); d++)
            {
                for (int ci = 0; ci < 2; ci++)
                {
                    const char *hay = hay_at(off, hays[h]);
                    const char *ndl = put_needle(ndls[d]);
                    size_t cap = sizeof(s_hay) - off;
                    size_t ncap = strlen(ndls[d]) + 1u;

                    const char *want = oracle_find(hay, cap, ndl, ci);
                    const char *got = str.find(hay, cap, ndl, ncap, ci ? PROTO_TRUE : PROTO_FALSE);
                    if (want != got)
                    {
                        (void)snprintf(msg, sizeof(msg), "off=%u ci=%d hay[%u] ndl[%u]: want %+d got %+d",
                                       (unsigned)off, ci, h, d, want ? (int)(want - hay) : -1,
                                       got ? (int)(got - hay) : -1);
                        TEST_FAIL_MESSAGE(msg);
                    }
                    // has answers the same scan.
                    TEST_ASSERT_EQUAL_INT(got != NULL, str.has(hay, cap, ndl, ncap, ci ? PROTO_TRUE : PROTO_FALSE));
                }
            }
        }
    }
}

// The needle taken from every position of the haystack must be found at or before where it came
// from: an earlier occurrence is still the right answer, a later one never is.
void test_find_locates_every_substring_of_itself()
{
    static const char base[] = "abcabdabcabeabcabdabcabf";
    char msg[128];

    for (size_t off = 0; off < 8u; off++)
    {
        for (size_t pos = 0; pos < sizeof(base) - 1u; pos++)
        {
            for (size_t nl = 1; nl <= 8u && pos + nl < sizeof(base); nl++)
            {
                const char *hay = hay_at(off, base);
                memset(s_ndl, 0, sizeof(s_ndl));
                memcpy(s_ndl, base + pos, nl);

                const char *want = oracle_find(hay, sizeof(s_hay) - off, s_ndl, 0);
                const char *got = str.find(hay, sizeof(s_hay) - off, s_ndl, nl + 1u, PROTO_FALSE);
                if (want != got)
                {
                    (void)snprintf(msg, sizeof(msg), "off=%u pos=%u nl=%u: want %+d got %+d", (unsigned)off,
                                   (unsigned)pos, (unsigned)nl, want ? (int)(want - hay) : -1,
                                   got ? (int)(got - hay) : -1);
                    TEST_FAIL_MESSAGE(msg);
                }
                TEST_ASSERT_NOT_NULL(got); // it came from the haystack, so it is in there
            }
        }
    }
}

// A needle one byte off from a real occurrence, at every position and in every byte, must not match
// there. This is the near-miss the anchor plus masked compare has to reject.
void test_find_rejects_every_near_miss()
{
    static const char base[] = "abcdefghabcdefghabcdefgh";
    char msg[128];

    for (size_t pos = 0; pos + 6u < sizeof(base) - 1u; pos++)
    {
        for (size_t bit = 0; bit < 6u; bit++)
        {
            const char *hay = hay_at(0, base);
            memset(s_ndl, 0, sizeof(s_ndl));
            memcpy(s_ndl, base + pos, 6);
            s_ndl[bit] = (char)(s_ndl[bit] ^ 0x20); // a byte that is in no occurrence

            const char *want = oracle_find(hay, sizeof(s_hay), s_ndl, 0);
            const char *got = str.find(hay, sizeof(s_hay), s_ndl, 7u, PROTO_FALSE);
            if (want != got)
            {
                (void)snprintf(msg, sizeof(msg), "pos=%u bit=%u: want %+d got %+d", (unsigned)pos, (unsigned)bit,
                               want ? (int)(want - hay) : -1, got ? (int)(got - hay) : -1);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

// ---- eq, starts and diff under hard input ---------------------------------

// The difference walked to every position of a long subject, with high-bit bytes so a byte compared
// as signed would order them the wrong way.
void test_compares_under_hard_input()
{
    static char PROTO_ALIGN(16) a[64];
    static char PROTO_ALIGN(16) b[64];
    char msg[96];

    for (size_t n = 1; n < 40u; n++)
    {
        for (size_t pos = 0; pos < n; pos++)
        {
            memset(a, 0, sizeof(a));
            memset(b, 0, sizeof(b));
            for (size_t i = 0; i < n; i++)
            {
                a[i] = (char)(0x80u + (i & 0x3Fu)); // every byte has the high bit set
                b[i] = a[i];
            }
            b[pos] = (char)(a[pos] ^ 0x01);

            size_t want = oracle_diff(a, b, n, 0);
            size_t got = str.diff(a, b, n, PROTO_FALSE);
            if (want != got)
            {
                (void)snprintf(msg, sizeof(msg), "n=%u pos=%u: want %u got %u", (unsigned)n, (unsigned)pos,
                               (unsigned)want, (unsigned)got);
                TEST_FAIL_MESSAGE(msg);
            }
            TEST_ASSERT_EQUAL_size_t(pos, got);
            TEST_ASSERT_FALSE(str.eq(a, b, sizeof(a), PROTO_FALSE));
            TEST_ASSERT_EQUAL_size_t(n, str.len(a, sizeof(a)));
        }
    }
}

// A subject that agrees with the pattern for every prefix length, so starts and eq part company at
// exactly one place: the byte after the pattern ends.
void test_starts_and_eq_part_at_the_pattern_end()
{
    static char PROTO_ALIGN(16) subj[64];
    static char PROTO_ALIGN(16) pat[64];

    for (size_t n = 1; n < 40u; n++)
    {
        memset(subj, 0, sizeof(subj));
        memset(pat, 0, sizeof(pat));
        for (size_t i = 0; i < 40u; i++)
        {
            subj[i] = (char)('a' + (i % 26u));
        }
        memcpy(pat, subj, n);

        TEST_ASSERT_TRUE(str.starts(subj, pat, sizeof(subj), PROTO_FALSE));
        TEST_ASSERT_FALSE(str.eq(subj, pat, sizeof(subj), PROTO_FALSE)); // subject runs on
        TEST_ASSERT_TRUE(str.eq(pat, pat, sizeof(pat), PROTO_FALSE));
    }
}

// ---- copy -----------------------------------------------------------------

// The copy terminates, reports what it wrote, and writes only that.
void test_copy_terminates_and_reports()
{
    static char dst[32];
    memset(dst, 0x7F, sizeof(dst));
    size_t n = str.copy(dst, "hello", sizeof(dst));
    TEST_ASSERT_EQUAL_size_t(5, n);
    TEST_ASSERT_EQUAL_STRING("hello", dst);
    TEST_ASSERT_EQUAL_CHAR(0x7F, dst[6]); // nothing past the terminator
}

// The bound belongs to the destination: a source that does not fit is cut and still terminated.
void test_copy_bounds_by_the_destination()
{
    static char dst[16];
    memset(dst, 0x7F, sizeof(dst));
    size_t n = str.copy(dst, "abcdefghijklmnop", 8);
    TEST_ASSERT_EQUAL_size_t(7, n); // one byte of the eight is the terminator
    TEST_ASSERT_EQUAL_STRING("abcdefg", dst);
    TEST_ASSERT_EQUAL_CHAR(0x7F, dst[8]);
}

// A zero-capacity destination is written not at all.
void test_copy_zero_capacity_writes_nothing()
{
    static char dst[8];
    memset(dst, 0x7F, sizeof(dst));
    TEST_ASSERT_EQUAL_size_t(0, str.copy(dst, "abc", 0));
    TEST_ASSERT_EQUAL_CHAR(0x7F, dst[0]);
}

// An empty source still terminates.
void test_copy_empty_source()
{
    static char dst[8];
    memset(dst, 0x7F, sizeof(dst));
    TEST_ASSERT_EQUAL_size_t(0, str.copy(dst, "", sizeof(dst)));
    TEST_ASSERT_EQUAL_CHAR('\0', dst[0]);
}

// ---- classifiers ----------------------------------------------------------

// The six ASCII whitespace characters and nothing else.
void test_ws_classifies_the_six()
{
    static const char yes[] = {' ', '\t', '\n', '\v', '\f', '\r'};
    for (unsigned i = 0; i < sizeof(yes); i++)
    {
        TEST_ASSERT_TRUE(str.ws(yes[i]));
    }
    static const char no[] = {'a', '0', '\0', '-', 0x7F, '.'};
    for (unsigned i = 0; i < sizeof(no); i++)
    {
        TEST_ASSERT_FALSE(str.ws(no[i]));
    }
}

// The ten ASCII digits and nothing else.
void test_digit_classifies_the_ten()
{
    for (int c = 0; c < 128; c++)
    {
        proto_bool want = (c >= '0' && c <= '9');
        TEST_ASSERT_EQUAL_INT(want, str.digit((char)c));
    }
}

// ---- the step rungs -------------------------------------------------------

// One byte of the agreement test: keep going, settled yes, or settled no.
void test_step_byte_settles_or_continues()
{
    TEST_ASSERT_EQUAL_INT(PC_SWAR_GO, str.step_byte('a', 'a', PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_INT(PC_SWAR_NO, str.step_byte('a', 'b', PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_INT(PC_SWAR_YES, str.step_byte('\0', '\0', PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_INT(PC_SWAR_GO, str.step_byte('A', 'a', PROTO_TRUE, 0));
    TEST_ASSERT_EQUAL_INT(PC_SWAR_NO, str.step_byte('A', 'a', PROTO_FALSE, 0));
    // end_wins turns on the FIRST operand ending: that one is the pattern, and a prefix test wants
    // the pattern running out to be a match whatever the subject does next.
    TEST_ASSERT_EQUAL_INT(PC_SWAR_YES, str.step_byte('\0', 'x', PROTO_FALSE, 1));
    TEST_ASSERT_EQUAL_INT(PC_SWAR_NO, str.step_byte('\0', 'x', PROTO_FALSE, 0));
    // The subject running out first is a mismatch either way: only the pattern's end is a tie.
    TEST_ASSERT_EQUAL_INT(PC_SWAR_NO, str.step_byte('x', '\0', PROTO_FALSE, 1));
}

// ---- the table ------------------------------------------------------------

// Each member is the walk its name says. The table is initialized positionally, so a member added
// or reordered without moving its initializer binds the wrong walk at every call site at once.
void test_each_member_is_the_walk_it_names()
{
    static char dst[16];
    const char *endp = NULL;

    TEST_ASSERT_EQUAL_size_t(3, str.len("abc", BIG)); // nul_cap is a willingness, not a promise
    TEST_ASSERT_EQUAL_size_t(1, str.diff("ab", "aa", 2, PROTO_FALSE));
    TEST_ASSERT_TRUE(str.eq(hay_of("abc"), pat_of("abc"), STAGED_CAP, PROTO_FALSE));
    TEST_ASSERT_TRUE(str.starts(hay_of("abcdef"), pat_of("abc"), STAGED_CAP, PROTO_FALSE));
    TEST_ASSERT_NOT_NULL(str.find(hay_of("abcdef"), sizeof(s_hay), "cd", sizeof("cd"), PROTO_FALSE));
    TEST_ASSERT_TRUE(str.has(hay_of("abcdef"), sizeof(s_hay), "cd", sizeof("cd"), PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(3, str.copy(dst, "abc", sizeof(dst)));
    TEST_ASSERT_EQUAL_INT(PC_SWAR_GO, str.step_byte('a', 'a', PROTO_FALSE, 0));
    TEST_ASSERT_TRUE(str.ws(' '));
    TEST_ASSERT_TRUE(str.digit('7'));
    TEST_ASSERT_EQUAL_INT(-42, str.to_long("-42", &endp));
    TEST_ASSERT_EQUAL_UINT32(42u, (uint32_t)str.to_ulong("42", &endp));
    TEST_ASSERT_TRUE(str.to_double("1.5", &endp) > 1.49 && str.to_double("1.5", &endp) < 1.51);
    TEST_ASSERT_TRUE(str.to_float("2.5", &endp) > 2.49f && str.to_float("2.5", &endp) < 2.51f);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_len_matches_the_oracle);
    RUN_TEST(test_len_stops_at_the_cap);
    RUN_TEST(test_diff_matches_the_oracle);
    RUN_TEST(test_diff_reports_the_cap_when_equal);
    RUN_TEST(test_diff_case_insensitive);
    RUN_TEST(test_eq_requires_the_whole_string);
    RUN_TEST(test_eq_at_every_length);
    RUN_TEST(test_starts_reads_the_tie_as_a_match);
    RUN_TEST(test_starts_at_every_prefix_length);
    RUN_TEST(test_find_locates_the_first_occurrence);
    RUN_TEST(test_find_absent);
    RUN_TEST(test_find_empty_needle_matches_at_the_start);
    RUN_TEST(test_find_stops_at_the_haystack_nul);
    RUN_TEST(test_find_case_insensitive);
    RUN_TEST(test_has_agrees_with_find);
    RUN_TEST(test_find_agrees_with_a_naive_search_under_hard_input);
    RUN_TEST(test_find_locates_every_substring_of_itself);
    RUN_TEST(test_find_rejects_every_near_miss);
    RUN_TEST(test_compares_under_hard_input);
    RUN_TEST(test_starts_and_eq_part_at_the_pattern_end);
    RUN_TEST(test_copy_terminates_and_reports);
    RUN_TEST(test_copy_bounds_by_the_destination);
    RUN_TEST(test_copy_zero_capacity_writes_nothing);
    RUN_TEST(test_copy_empty_source);
    RUN_TEST(test_ws_classifies_the_six);
    RUN_TEST(test_digit_classifies_the_ten);
    RUN_TEST(test_step_byte_settles_or_continues);
    RUN_TEST(test_each_member_is_the_walk_it_names);
    return UNITY_END();
}
