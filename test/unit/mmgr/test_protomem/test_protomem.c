// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// mmgr/protomem.h: the byte-span operations. The oracle is a byte loop written here.
//
// cpy and set store whole words and mask the tail, so the lanes between n and the end of the word
// n stops in go to zero. Those lanes are the padding of the borrow being written, which is why the
// store stays a dumb word. Every assertion below is written against that boundary rather than
// against n, and the destinations are aligned because a borrow is.

#include "mmgr/protomem.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#define BODY 96u
#define GUARD 32u
#define POISON 0xAAu

static uint8_t PROTO_ALIGN(16) s_src[BODY + 32u];
static uint8_t PROTO_ALIGN(16) s_dst[BODY + GUARD];

void setUp(void)
{
    for (size_t i = 0; i < sizeof(s_src); i++)
    {
        s_src[i] = (uint8_t)(i * 7u + 3u);
    }
    memset(s_dst, POISON, sizeof(s_dst));
}

void tearDown(void)
{
}

// n rounded up to the word the span's last byte falls in.
static size_t lane_end(size_t n)
{
    return (n + (size_t)PROTO_RAW_WORD - 1u) & ~((size_t)PROTO_RAW_WORD - 1u);
}

// The bytes the span asked for match, the lanes to the end of that word are zero, and nothing past
// that word moved.
static void assert_span_and_padding(const uint8_t *want, size_t n)
{
    char msg[80];
    for (size_t i = 0; i < n; i++)
    {
        if (s_dst[i] != want[i])
        {
            (void)snprintf(msg, sizeof(msg), "byte %u of %u differs", (unsigned)i, (unsigned)n);
            TEST_FAIL_MESSAGE(msg);
        }
    }
    for (size_t i = n; i < lane_end(n); i++)
    {
        if (s_dst[i] != 0u)
        {
            (void)snprintf(msg, sizeof(msg), "pad byte %u not zeroed (n=%u)", (unsigned)i, (unsigned)n);
            TEST_FAIL_MESSAGE(msg);
        }
    }
    for (size_t i = lane_end(n); i < sizeof(s_dst); i++)
    {
        if (s_dst[i] != POISON)
        {
            (void)snprintf(msg, sizeof(msg), "wrote past the lane at %u (n=%u)", (unsigned)i, (unsigned)n);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

// ---- cpy ------------------------------------------------------------------

// Every length through two full words, from a co-aligned source.
void test_cpy_every_length_aligned_source()
{
    for (size_t n = 0; n <= 2u * PROTO_RAW_WORD + 3u; n++)
    {
        memset(s_dst, POISON, sizeof(s_dst));
        mem.cpy(s_dst, s_src, n);
        assert_span_and_padding(s_src, n);
    }
}

// A source at every offset within a word: not co-aligned with the destination is the funnel, which
// assembles the wanted word from the two aligned words holding it.
void test_cpy_every_source_offset()
{
    for (size_t off = 0; off < PROTO_RAW_WORD; off++)
    {
        for (size_t n = 1; n <= 2u * PROTO_RAW_WORD + 1u; n++)
        {
            memset(s_dst, POISON, sizeof(s_dst));
            mem.cpy(s_dst, s_src + off, n);
            assert_span_and_padding(s_src + off, n);
        }
    }
}

// A span longer than the word loop's first pass is still byte exact.
void test_cpy_long_span()
{
    mem.cpy(s_dst, s_src, BODY);
    assert_span_and_padding(s_src, BODY);
}

// A zero-length copy writes nothing at all.
void test_cpy_zero_writes_nothing()
{
    mem.cpy(s_dst, s_src, 0);
    for (size_t i = 0; i < sizeof(s_dst); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(POISON, s_dst[i]);
    }
}

// ---- move -----------------------------------------------------------------

// Destination below the source inside it: reading before the store reaches each byte.
void test_move_overlap_down()
{
    static uint8_t PROTO_ALIGN(16) buf[64];
    uint8_t want[64];
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)(i + 1u);
    }
    memcpy(want, buf + PROTO_RAW_WORD, 32);
    mem.move(buf, buf + PROTO_RAW_WORD, 32);
    TEST_ASSERT_EQUAL_MEMORY(want, buf, 32);
}

// Destination ahead of the source and inside it: the walk goes down instead.
void test_move_overlap_up()
{
    static uint8_t PROTO_ALIGN(16) buf[64];
    uint8_t want[64];
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)(i + 1u);
    }
    memcpy(want, buf, 32);
    mem.move(buf + PROTO_RAW_WORD, buf, 32);
    TEST_ASSERT_EQUAL_MEMORY(want, buf + PROTO_RAW_WORD, 32);
}

// Disjoint regions go through the copy path and are byte exact.
void test_move_disjoint()
{
    mem.move(s_dst, s_src, 40);
    TEST_ASSERT_EQUAL_MEMORY(s_src, s_dst, 40);
}

// A move onto itself, and a zero-length move, both do nothing.
void test_move_same_and_zero()
{
    static uint8_t PROTO_ALIGN(16) buf[32];
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)(i + 1u);
    }
    mem.move(buf, buf, sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(i + 1u), buf[i]);
    }
    mem.move(buf, buf + 1, 0);
    TEST_ASSERT_EQUAL_HEX8(1, buf[0]);
}

// ---- cmp ------------------------------------------------------------------

// Equal spans order equal at every length, including zero.
void test_cmp_equal()
{
    static uint8_t a[64];
    static uint8_t b[64];
    for (size_t i = 0; i < sizeof(a); i++)
    {
        a[i] = (uint8_t)(i * 3u + 1u);
        b[i] = a[i];
    }
    for (size_t n = 0; n <= sizeof(a); n++)
    {
        TEST_ASSERT_EQUAL_INT(0, mem.cmp(a, b, n));
    }
}

// The sign comes from the first byte that differs, at every position.
void test_cmp_first_difference_at_every_position()
{
    static uint8_t a[64];
    static uint8_t b[64];
    for (size_t pos = 0; pos < sizeof(a); pos++)
    {
        for (size_t i = 0; i < sizeof(a); i++)
        {
            a[i] = 0x40u;
            b[i] = 0x40u;
        }
        b[pos] = 0x41u;
        TEST_ASSERT_TRUE(mem.cmp(a, b, sizeof(a)) < 0);
        TEST_ASSERT_TRUE(mem.cmp(b, a, sizeof(a)) > 0);
        // A bound that stops before the difference sees none of it.
        TEST_ASSERT_EQUAL_INT(0, mem.cmp(a, b, pos));
    }
}

// Bytes order as unsigned: 0x80 is above 0x7F, not below it.
void test_cmp_is_unsigned()
{
    static const uint8_t hi[] = {0x80, 0, 0, 0, 0, 0, 0, 0};
    static const uint8_t lo[] = {0x7F, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(mem.cmp(hi, lo, sizeof(hi)) > 0);
    TEST_ASSERT_TRUE(mem.cmp(lo, hi, sizeof(hi)) < 0);
}

// A NUL inside the span does not end the comparison: this orders spans, not strings.
void test_cmp_does_not_stop_at_a_nul()
{
    static const uint8_t a[] = {'a', 0, 'b', 0, 0, 0, 0, 0};
    static const uint8_t b[] = {'a', 0, 'c', 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(mem.cmp(a, b, sizeof(a)) < 0);
    TEST_ASSERT_EQUAL_INT(0, mem.cmp(a, b, 2));
}

// ---- chr ------------------------------------------------------------------

// The first match is the one returned, at every start offset and every position.
void test_chr_finds_the_first_match()
{
    static uint8_t PROTO_ALIGN(16) buf[80];
    for (size_t off = 0; off < 8u; off++)
    {
        for (size_t pos = 0; pos < 48u; pos++)
        {
            memset(buf, 0x11, sizeof(buf));
            buf[off + pos] = 0x99u;
            buf[off + pos + 1u] = 0x99u; // a second match must not be the answer
            const void *hit = mem.chr(buf + off, 64, 0x99u);
            TEST_ASSERT_EQUAL_PTR(buf + off + pos, hit);
        }
    }
}

// A byte that is not there yields NULL, and the bound is the whole stop.
void test_chr_absent_and_bounded()
{
    static uint8_t PROTO_ALIGN(16) buf[64];
    memset(buf, 0x11, sizeof(buf));
    TEST_ASSERT_NULL(mem.chr(buf, sizeof(buf), 0x99u));

    buf[40] = 0x99u;
    TEST_ASSERT_NULL(mem.chr(buf, 40, 0x99u)); // the bound stops before it
    TEST_ASSERT_EQUAL_PTR(buf + 40, mem.chr(buf, 41, 0x99u));
    TEST_ASSERT_NULL(mem.chr(buf, 0, 0x11u));
}

// A span may carry NULs, so a zero byte is findable and does not end the search.
void test_chr_finds_a_nul_and_searches_past_one()
{
    static uint8_t PROTO_ALIGN(16) buf[64];
    memset(buf, 0x11, sizeof(buf));
    buf[3] = 0u;
    buf[20] = 0x77u;
    TEST_ASSERT_EQUAL_PTR(buf + 3, mem.chr(buf, sizeof(buf), 0u));
    TEST_ASSERT_EQUAL_PTR(buf + 20, mem.chr(buf, sizeof(buf), 0x77u)); // past the NUL
}

// ---- set and zero ---------------------------------------------------------

// Every byte of the span takes the value, and the lanes to the end of the word go to zero.
void test_set_fills_the_span()
{
    uint8_t want[BODY];
    for (size_t n = 0; n <= 2u * PROTO_RAW_WORD + 3u; n++)
    {
        memset(s_dst, POISON, sizeof(s_dst));
        memset(want, 0x5Au, sizeof(want));
        mem.set(s_dst, 0x5Au, n);
        assert_span_and_padding(want, n);
    }
}

// Zero is the same walk with a zero value.
void test_zero_clears_the_span()
{
    uint8_t want[BODY];
    for (size_t n = 0; n <= 2u * PROTO_RAW_WORD + 3u; n++)
    {
        memset(s_dst, POISON, sizeof(s_dst));
        memset(want, 0, sizeof(want));
        mem.zero(s_dst, n);
        assert_span_and_padding(want, n);
    }
}

// A fill value with the high bit set reaches every lane, not just the low one.
void test_set_splats_every_lane()
{
    mem.set(s_dst, 0xFFu, 2u * PROTO_RAW_WORD);
    for (size_t i = 0; i < 2u * PROTO_RAW_WORD; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xFFu, s_dst[i]);
    }
}

// ---- the table ------------------------------------------------------------

// Each member holds the walk its name says. The table is initialized positionally, so a member
// added or reordered without moving its initializer binds the wrong walk, and every call site
// inherits it silently.
void test_each_member_is_the_walk_it_names()
{
    static uint8_t PROTO_ALIGN(16) a[32];
    static uint8_t PROTO_ALIGN(16) b[32];

    memset(a, 0x11, sizeof(a));
    memset(b, 0x22, sizeof(b));

    mem.cpy(a, b, 16);
    TEST_ASSERT_EQUAL_HEX8(0x22, a[0]); // cpy moved bytes

    mem.set(a, 0x33, 16);
    TEST_ASSERT_EQUAL_HEX8(0x33, a[0]); // set wrote the value

    mem.zero(a, 16);
    TEST_ASSERT_EQUAL_HEX8(0x00, a[0]); // zero cleared

    TEST_ASSERT_TRUE(mem.cmp(a, b, 16) < 0); // cmp ordered
    TEST_ASSERT_EQUAL_PTR(b + 0, mem.chr(b, 16, 0x22u));

    // move over OVERLAPPING operands, which is the only thing that separates it from cpy: a
    // distinct pattern shifted forward, so a cpy bound here instead would read bytes it has
    // already overwritten and the tail would not match.
    for (size_t i = 0; i < sizeof(a); i++)
    {
        a[i] = (uint8_t)(i + 1u);
    }
    uint8_t want[16];
    memcpy(want, a, 16);
    mem.move(a + PROTO_RAW_WORD, a, 16);
    TEST_ASSERT_EQUAL_MEMORY(want, a + PROTO_RAW_WORD, 16);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cpy_every_length_aligned_source);
    RUN_TEST(test_cpy_every_source_offset);
    RUN_TEST(test_cpy_long_span);
    RUN_TEST(test_cpy_zero_writes_nothing);
    RUN_TEST(test_move_overlap_down);
    RUN_TEST(test_move_overlap_up);
    RUN_TEST(test_move_disjoint);
    RUN_TEST(test_move_same_and_zero);
    RUN_TEST(test_cmp_equal);
    RUN_TEST(test_cmp_first_difference_at_every_position);
    RUN_TEST(test_cmp_is_unsigned);
    RUN_TEST(test_cmp_does_not_stop_at_a_nul);
    RUN_TEST(test_chr_finds_the_first_match);
    RUN_TEST(test_chr_absent_and_bounded);
    RUN_TEST(test_chr_finds_a_nul_and_searches_past_one);
    RUN_TEST(test_set_fills_the_span);
    RUN_TEST(test_zero_clears_the_span);
    RUN_TEST(test_set_splats_every_lane);
    RUN_TEST(test_each_member_is_the_walk_it_names);
    return UNITY_END();
}
