// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the byte-span operations (mmgr/protomem.h).
//
// ISO C11 sec 7.24 defines each of these operations, and this module reimplements them word at a
// time with no stdlib. sec 7.24.1 p1 is the clause that decides correctness: "the characters of
// each object are interpreted as if they had the type unsigned char".
//
// test_c11_cmp_orders_bytes_as_unsigned is therefore load-bearing. A comparison written over plain
// char is signed on every target in this library's list, so 0x80 would order BELOW 0x7F and a
// length check or a tag match on high-bit bytes would silently invert. Everything else here is a
// property: only n bytes move, the move is correct under overlap in both directions, and a search
// spans the whole bound rather than stopping at a NUL.

#include "mmgr/protomem.h"
#include <string.h>

#include <unity.h>

#define GUARD 16u
#define BODY 64u
#define POISON 0xAAu

static uint8_t buf[GUARD + BODY + GUARD];

void setUp(void)
{
    memset(buf, POISON, sizeof(buf));
}

void tearDown(void)
{
}

// The body byte at @p off, with a poisoned guard on either side.
static uint8_t *at(size_t off)
{
    return buf + GUARD + off;
}

// Every byte outside [off, off + n) still reads POISON.
static void assert_only_the_span_changed(size_t off, size_t n)
{
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        if (i >= GUARD + off && i < GUARD + off + n)
        {
            continue;
        }
        TEST_ASSERT_EQUAL_HEX8(POISON, buf[i]);
    }
}

// ---- ordering --------------------------------------------------------------

// C11 sec 7.24.1 p1: the bytes are compared as unsigned char, so 0x80 is above 0x7F, not below it.
// sec 7.24.4.1 fixes the sign of the result by the first differing byte.
void test_c11_cmp_orders_bytes_as_unsigned(void)
{
    static const uint8_t HIGH[1] = {0x80};
    static const uint8_t LOW[1] = {0x7F};
    TEST_ASSERT_TRUE(mem.cmp(HIGH, LOW, 1) > 0);
    TEST_ASSERT_TRUE(mem.cmp(LOW, HIGH, 1) < 0);

    static const uint8_t A[4] = {0x00, 0x01, 0xFF, 0x03};
    static const uint8_t B[4] = {0x00, 0x01, 0x02, 0x03};
    TEST_ASSERT_TRUE(mem.cmp(A, B, 4) > 0); // 0xFF above 0x02
    TEST_ASSERT_TRUE(mem.cmp(B, A, 4) < 0);
    TEST_ASSERT_EQUAL_INT(0, mem.cmp(A, B, 2)); // the first two agree
}

// Equal spans compare zero, and a zero-length comparison is zero whatever the bytes hold.
void test_cmp_of_equal_and_of_zero_length(void)
{
    static const uint8_t A[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t B[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_INT(0, mem.cmp(A, B, 8));
    TEST_ASSERT_EQUAL_INT(0, mem.cmp(A, B, 0));

    static const uint8_t C[8] = {1, 2, 3, 4, 5, 6, 7, 9};
    TEST_ASSERT_EQUAL_INT(0, mem.cmp(A, C, 7)); // the difference is past the bound
    TEST_ASSERT_TRUE(mem.cmp(A, C, 8) < 0);
}

// A span comparison has no terminator: a NUL inside the bound does not stop it.
void test_cmp_does_not_stop_at_a_nul(void)
{
    static const uint8_t A[6] = {'a', 0x00, 'b', 'c', 'd', 'e'};
    static const uint8_t B[6] = {'a', 0x00, 'b', 'c', 'd', 'f'};
    TEST_ASSERT_EQUAL_INT(0, mem.cmp(A, B, 5));
    TEST_ASSERT_TRUE(mem.cmp(A, B, 6) < 0);
}

// ---- copying ---------------------------------------------------------------

// C11 sec 7.24.2.1: memcpy copies n characters and nothing else.
void test_cpy_moves_exactly_n_bytes(void)
{
    static const uint8_t SRC[8] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    mem.cpy(at(4), SRC, sizeof(SRC));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SRC, at(4), sizeof(SRC));
    assert_only_the_span_changed(4, sizeof(SRC));
}

// A source that is not co-aligned with the destination is funnelled through shifts, so the copy has
// to be right at every combination of offsets and lengths, not just the aligned ones.
void test_cpy_at_every_offset_pair(void)
{
    static uint8_t src[BODY];
    for (unsigned i = 0; i < BODY; i++)
    {
        src[i] = (uint8_t)(i * 7u + 1u);
    }
    for (size_t so = 0; so < 9u; so++)
    {
        for (size_t d = 0; d < 9u; d++)
        {
            for (size_t n = 0; n <= 17u; n++)
            {
                memset(buf, POISON, sizeof(buf));
                mem.cpy(at(d), src + so, n);
                for (size_t i = 0; i < n; i++)
                {
                    TEST_ASSERT_EQUAL_HEX8(src[so + i], at(d)[i]);
                }
                assert_only_the_span_changed(d, n);
            }
        }
    }
}

// A zero-length copy writes nothing.
void test_cpy_of_zero_bytes_writes_nothing(void)
{
    static const uint8_t SRC[4] = {1, 2, 3, 4};
    mem.cpy(at(0), SRC, 0);
    assert_only_the_span_changed(0, 0);
}

// C11 sec 7.24.2.2: memmove behaves as if the source were copied to a temporary first, so both
// overlap directions come out as the original bytes shifted, never as a smeared repeat.
void test_move_is_correct_under_overlap_in_both_directions(void)
{
    static const uint8_t PATTERN[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    for (size_t shift = 1; shift <= 9u; shift++)
    {
        // destination above the source
        memset(buf, POISON, sizeof(buf));
        mem.cpy(at(0), PATTERN, sizeof(PATTERN));
        mem.move(at(shift), at(0), sizeof(PATTERN));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(PATTERN, at(shift), sizeof(PATTERN));

        // destination below the source
        memset(buf, POISON, sizeof(buf));
        mem.cpy(at(shift), PATTERN, sizeof(PATTERN));
        mem.move(at(0), at(shift), sizeof(PATTERN));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(PATTERN, at(0), sizeof(PATTERN));
    }
}

// Moving a span onto itself leaves it unchanged.
void test_move_onto_itself_changes_nothing(void)
{
    static const uint8_t PATTERN[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    mem.cpy(at(2), PATTERN, sizeof(PATTERN));
    mem.move(at(2), at(2), sizeof(PATTERN));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PATTERN, at(2), sizeof(PATTERN));
    assert_only_the_span_changed(2, sizeof(PATTERN));
}

// Non-overlapping operands go through the same call and must still be exact.
void test_move_without_overlap(void)
{
    static const uint8_t PATTERN[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    mem.cpy(at(0), PATTERN, sizeof(PATTERN));
    mem.move(at(32), at(0), sizeof(PATTERN));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PATTERN, at(32), sizeof(PATTERN));
}

// ---- searching -------------------------------------------------------------

// C11 sec 7.24.5.1: memchr locates the FIRST occurrence in the initial n characters.
void test_chr_finds_the_first_occurrence(void)
{
    static const uint8_t P[8] = {'a', 'b', 'c', 'b', 'a', 'b', 'c', 'd'};
    TEST_ASSERT_EQUAL_PTR(P + 1, mem.chr(P, sizeof(P), 'b'));
    TEST_ASSERT_EQUAL_PTR(P + 0, mem.chr(P, sizeof(P), 'a'));
    TEST_ASSERT_EQUAL_PTR(P + 7, mem.chr(P, sizeof(P), 'd'));
    TEST_ASSERT_NULL(mem.chr(P, sizeof(P), 'z'));
    TEST_ASSERT_NULL(mem.chr(P, 0, 'a'));
    TEST_ASSERT_NULL(mem.chr(P, 1, 'b')); // past the bound, so not found
}

// n is the whole bound. A span has no terminator, so an embedded NUL neither stops the search nor
// hides a byte behind it - which is the difference from a string search.
void test_chr_does_not_stop_at_a_nul(void)
{
    static const uint8_t P[6] = {'a', 0x00, 'b', 0x00, 'c', 0x00};
    TEST_ASSERT_EQUAL_PTR(P + 4, mem.chr(P, sizeof(P), 'c'));
    TEST_ASSERT_EQUAL_PTR(P + 1, mem.chr(P, sizeof(P), 0x00));
}

// The high half of the byte range is searchable: the value is compared as unsigned char.
void test_chr_finds_a_high_byte(void)
{
    static const uint8_t P[4] = {0x00, 0x7F, 0x80, 0xFF};
    TEST_ASSERT_EQUAL_PTR(P + 2, mem.chr(P, sizeof(P), 0x80));
    TEST_ASSERT_EQUAL_PTR(P + 3, mem.chr(P, sizeof(P), 0xFF));
}

// ---- filling ---------------------------------------------------------------

// C11 sec 7.24.6.1: memset writes the value into exactly n characters.
void test_set_fills_exactly_n_bytes(void)
{
    for (size_t d = 0; d < 9u; d++)
    {
        for (size_t n = 0; n <= 17u; n++)
        {
            memset(buf, POISON, sizeof(buf));
            mem.set(at(d), 0x5A, n);
            for (size_t i = 0; i < n; i++)
            {
                TEST_ASSERT_EQUAL_HEX8(0x5Au, at(d)[i]);
            }
            assert_only_the_span_changed(d, n);
        }
    }
}

// The fill value is a byte: only the low eight bits reach the span.
void test_set_writes_a_byte_not_a_word(void)
{
    mem.set(at(0), 0xFF, 4);
    for (unsigned i = 0; i < 4u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xFFu, at(0)[i]);
    }
    assert_only_the_span_changed(0, 4);
}

// Zeroing is the same walk with the value fixed at zero.
void test_zero_clears_exactly_n_bytes(void)
{
    for (size_t d = 0; d < 9u; d++)
    {
        for (size_t n = 0; n <= 17u; n++)
        {
            memset(buf, POISON, sizeof(buf));
            mem.zero(at(d), n);
            for (size_t i = 0; i < n; i++)
            {
                TEST_ASSERT_EQUAL_HEX8(0x00u, at(d)[i]);
            }
            assert_only_the_span_changed(d, n);
        }
    }
}
