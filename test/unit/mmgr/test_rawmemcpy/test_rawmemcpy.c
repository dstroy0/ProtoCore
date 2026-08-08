// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// mmgr/rawmemcpy.h: the scalar rungs, the aligned rungs, and the ladder proto_raw_read steps down.
// The oracle is a byte loop written here; no other mmgr call is used to check one.

#include "mmgr/rawmemcpy.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#define ALIGN_MAX 16u
#define BODY 64u
#define GUARD 16u
#define POISON 0xAAu

#define DST_WINDOW (ALIGN_MAX + BODY)

static uint8_t PROTO_ALIGN(16) s_src[ALIGN_MAX + BODY + ALIGN_MAX];
static uint8_t PROTO_ALIGN(16) s_dst[GUARD + DST_WINDOW + GUARD];

void setUp(void)
{
    for (size_t i = 0; i < sizeof(s_src); i++)
    {
        s_src[i] = (uint8_t)(i * 7u + 1u);
    }
    memset(s_dst, POISON, sizeof(s_dst));
}

void tearDown(void)
{
}

// True when the low byte of a 1 sits at the lowest address.
static int host_is_le(void)
{
    uint32_t v = 1u;
    uint8_t b[4];
    memcpy(b, &v, 4);
    return b[0] == 1u;
}

// Assemble n bytes at p into an integer the way this machine stores one.
static uint64_t oracle_load(const uint8_t *p, size_t n)
{
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++)
    {
        size_t shift = host_is_le() ? i : (n - 1u - i);
        v |= (uint64_t)p[i] << (shift * 8u);
    }
    return v;
}

// Spread v over n bytes at p in the same order.
static void oracle_store(uint8_t *p, uint64_t v, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        size_t shift = host_is_le() ? i : (n - 1u - i);
        p[i] = (uint8_t)(v >> (shift * 8u));
    }
}

// Every byte of s_dst outside [off, off + len) still reads POISON.
static void assert_only_span_written(size_t off, size_t len)
{
    for (size_t i = 0; i < sizeof(s_dst); i++)
    {
        if (i >= off && i < off + len)
        {
            continue;
        }
        if (s_dst[i] != POISON)
        {
            char msg[64];
            (void)snprintf(msg, sizeof(msg), "wrote outside span at %u", (unsigned)i);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

// ---- the declared rung ----------------------------------------------------

// PROTO_RAW_WORD follows PROTO_WORD_BITS, which the die states, not the build machine's pointer.
void test_word_rung_follows_the_declared_width()
{
#if PROTO_WORD_BITS >= 64
    TEST_ASSERT_EQUAL_UINT(8, PROTO_RAW_WORD);
#elif PROTO_WORD_BITS >= 32
    TEST_ASSERT_EQUAL_UINT(4, PROTO_RAW_WORD);
#else
    TEST_ASSERT_EQUAL_UINT(2, PROTO_RAW_WORD);
#endif
    TEST_ASSERT_EQUAL_UINT(PROTO_RAW_WORD * 8u, PROTO_MV_BITS);
    TEST_ASSERT_EQUAL_UINT(PROTO_RAW_WORD, sizeof(proto_mv_word));
    TEST_ASSERT_EQUAL_UINT(0, PROTO_RAW_WORD & (PROTO_RAW_WORD - 1u)); // a mask, not a divide
}

// PROTO_ALIGN states an alignment and the storage carries it.
void test_align_declares_alignment()
{
    static uint8_t PROTO_ALIGN(16) a16[32];
    static uint8_t PROTO_ALIGN(8) a8[32];
    static uint8_t PROTO_ALIGN(4) a4[32];
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)((uintptr_t)a16 & 15u));
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)((uintptr_t)a8 & 7u));
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)((uintptr_t)a4 & 3u));
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)((uintptr_t)s_src & 15u));
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)((uintptr_t)s_dst & 15u));
}

// ---- scalar rungs ---------------------------------------------------------

// Each width reads the machine's own order at any offset.
void test_raw_scalar_loads_match_the_oracle()
{
    for (size_t off = 0; off < ALIGN_MAX; off++)
    {
        const uint8_t *p = s_src + off;
        TEST_ASSERT_EQUAL_HEX16((uint16_t)oracle_load(p, 2), proto_raw_u16(p));
        TEST_ASSERT_EQUAL_HEX32((uint32_t)oracle_load(p, 4), proto_raw_u32(p));
        TEST_ASSERT_EQUAL_HEX64(oracle_load(p, 8), proto_raw_u64(p));
    }
}

// Each store lays down the machine's own order at any offset, and nothing beyond its width.
void test_raw_scalar_stores_match_the_oracle()
{
    static const uint64_t v = 0x0123456789ABCDEFull;
    uint8_t want[8];

    for (size_t off = 0; off < ALIGN_MAX; off++)
    {
        uint8_t *p = s_dst + GUARD + off;

        memset(s_dst, POISON, sizeof(s_dst));
        proto_raw_put_u16(p, (uint16_t)v);
        oracle_store(want, v, 2);
        TEST_ASSERT_EQUAL_MEMORY(want, p, 2);
        assert_only_span_written(GUARD + off, 2);

        memset(s_dst, POISON, sizeof(s_dst));
        proto_raw_put_u32(p, (uint32_t)v);
        oracle_store(want, v, 4);
        TEST_ASSERT_EQUAL_MEMORY(want, p, 4);
        assert_only_span_written(GUARD + off, 4);

        memset(s_dst, POISON, sizeof(s_dst));
        proto_raw_put_u64(p, v);
        oracle_store(want, v, 8);
        TEST_ASSERT_EQUAL_MEMORY(want, p, 8);
        assert_only_span_written(GUARD + off, 8);
    }
}

// proto_raw_load dispatches 1, 2, 4 and 8, and yields 0 for anything else.
void test_raw_load_widths_and_the_default()
{
    for (size_t off = 0; off < ALIGN_MAX; off++)
    {
        const uint8_t *p = s_src + off;
        TEST_ASSERT_EQUAL_HEX64(oracle_load(p, 1), proto_raw_load(p, 1));
        TEST_ASSERT_EQUAL_HEX64(oracle_load(p, 2), proto_raw_load(p, 2));
        TEST_ASSERT_EQUAL_HEX64(oracle_load(p, 4), proto_raw_load(p, 4));
        TEST_ASSERT_EQUAL_HEX64(oracle_load(p, 8), proto_raw_load(p, 8));
    }
    TEST_ASSERT_EQUAL_HEX64(0, proto_raw_load(s_src, 0));
    TEST_ASSERT_EQUAL_HEX64(0, proto_raw_load(s_src, 3));
    TEST_ASSERT_EQUAL_HEX64(0, proto_raw_load(s_src, 5));
    TEST_ASSERT_EQUAL_HEX64(0, proto_raw_load(s_src, 16));
}

// ---- aligned rungs --------------------------------------------------------

// At a natural boundary the aligned rung reads what the raw rung reads.
void test_aligned_loads_agree_with_raw()
{
    for (size_t off = 0; off < ALIGN_MAX; off += 8u)
    {
        const uint8_t *p = s_src + off;
        TEST_ASSERT_EQUAL_HEX64(proto_raw_load(p, 1), proto_al_load(p, 1));
        TEST_ASSERT_EQUAL_HEX64(proto_raw_u16(p), proto_al_load(p, 2));
        TEST_ASSERT_EQUAL_HEX64(proto_raw_u32(p), proto_al_load(p, 4));
        TEST_ASSERT_EQUAL_HEX64(proto_raw_u64(p), proto_al_load(p, 8));
    }
    TEST_ASSERT_EQUAL_HEX64(0, proto_al_load(s_src, 3));
    TEST_ASSERT_EQUAL_HEX64(0, proto_al_load(s_src, 16));
}

// The aligned stores lay down what the raw stores lay down.
void test_aligned_stores_agree_with_raw()
{
    static const uint64_t v = 0xFEEDFACECAFEBEEFull;
    uint8_t want[8];
    uint8_t *p = s_dst + GUARD; // GUARD and s_dst are both 16-aligned

    TEST_ASSERT_EQUAL_UINT(0, (unsigned)((uintptr_t)p & 7u));

    proto_al_put_u16(p, (uint16_t)v);
    oracle_store(want, v, 2);
    TEST_ASSERT_EQUAL_MEMORY(want, p, 2);

    proto_al_put_u32(p, (uint32_t)v);
    oracle_store(want, v, 4);
    TEST_ASSERT_EQUAL_MEMORY(want, p, 4);

    proto_al_put_u64(p, v);
    oracle_store(want, v, 8);
    TEST_ASSERT_EQUAL_MEMORY(want, p, 8);
    assert_only_span_written(GUARD, 8);
}

// The mover's rung round-trips at its own width.
void test_mover_rung_round_trips()
{
    uint8_t *p = s_dst + GUARD;
    proto_mv_word v = proto_mv_load(s_src + ALIGN_MAX);
    proto_mv_put(p, v);
    TEST_ASSERT_EQUAL_MEMORY(s_src + ALIGN_MAX, p, PROTO_RAW_WORD);
    TEST_ASSERT_EQUAL_HEX64((uint64_t)v, oracle_load(s_src + ALIGN_MAX, PROTO_RAW_WORD));
    assert_only_span_written(GUARD, PROTO_RAW_WORD);
}

// ---- the ladder -----------------------------------------------------------

// Every source offset against every destination offset, at every length through two full words and
// past the mover's loop. The head loop aligns the destination, so the source offset decides whether
// the word loop takes the co-aligned branch or the funnel.
void test_read_cross_product_of_offsets_and_lengths()
{
    static const size_t lens[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 23, 24, 25, 31, 32, 33, 47, 48, 63, 64};

    for (size_t d_off = 0; d_off < ALIGN_MAX; d_off++)
    {
        for (size_t s_off = 0; s_off < ALIGN_MAX; s_off++)
        {
            for (size_t li = 0; li < sizeof(lens) / sizeof(lens[0]); li++)
            {
                const size_t len = lens[li];
                if (d_off + len > DST_WINDOW || s_off + len > sizeof(s_src))
                {
                    continue;
                }
                memset(s_dst, POISON, sizeof(s_dst));
                proto_raw_read(s_dst + GUARD + d_off, s_src + s_off, len);
                if (len > 0)
                {
                    TEST_ASSERT_EQUAL_MEMORY(s_src + s_off, s_dst + GUARD + d_off, len);
                }
                assert_only_span_written(GUARD + d_off, len);
            }
        }
    }
}

// A zero-length move touches nothing.
void test_read_zero_length_writes_nothing()
{
    for (size_t s_off = 0; s_off < ALIGN_MAX; s_off++)
    {
        proto_raw_read(s_dst + GUARD, s_src + s_off, 0);
        assert_only_span_written(0, 0);
    }
}

// A move byte for byte over a span longer than the word loop's first pass.
void test_read_long_span_is_byte_exact()
{
    proto_raw_read(s_dst + GUARD, s_src, BODY);
    for (size_t i = 0; i < BODY; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(s_src[i], s_dst[GUARD + i]);
    }
    assert_only_span_written(GUARD, BODY);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_word_rung_follows_the_declared_width);
    RUN_TEST(test_align_declares_alignment);
    RUN_TEST(test_raw_scalar_loads_match_the_oracle);
    RUN_TEST(test_raw_scalar_stores_match_the_oracle);
    RUN_TEST(test_raw_load_widths_and_the_default);
    RUN_TEST(test_aligned_loads_agree_with_raw);
    RUN_TEST(test_aligned_stores_agree_with_raw);
    RUN_TEST(test_mover_rung_round_trips);
    RUN_TEST(test_read_cross_product_of_offsets_and_lengths);
    RUN_TEST(test_read_zero_length_writes_nothing);
    RUN_TEST(test_read_long_span_is_byte_exact);
    return UNITY_END();
}
