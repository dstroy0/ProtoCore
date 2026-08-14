// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the raw access module (mmgr/rawmemcpy.h).
//
// The governing text is ISO/IEC 9899:2011 sec 6.2.6.1: an object's value is its bytes, and reading
// those bytes as a wider type reconstructs it in the order this machine stores them. C publishes no
// vector for that, so every expectation is PROPERTIES.
//
// test_scalar_rungs_match_a_byte_loop is the load-bearing case: each rung is diffed against a byte
// loop the test assembles itself, in an order taken from a runtime probe of this machine rather
// than from a build macro, at every offset within a word. A wrong shift or a byte order copied from
// a config knob cannot agree with a value built from the bytes themselves.

#include "mmgr/rawmemcpy.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// 16-byte aligned so an index into them selects the offset under test rather than inheriting one.
static uint8_t g_src[64] __attribute__((aligned(16)));
static uint8_t g_dst[64] __attribute__((aligned(16)));

// Which end of a two-byte object holds the low octet, asked of the machine and not of a macro.
static int little_endian(void)
{
    const uint16_t one = 1u;
    unsigned char probe[sizeof one];
    memcpy(probe, &one, sizeof probe);
    return probe[0] == 1u;
}

// The n bytes at p as one integer, assembled a byte at a time in this machine's order.
static uint64_t assemble(const uint8_t *p, size_t n)
{
    uint64_t v = 0;
    if (little_endian())
    {
        for (size_t i = n; i-- > 0;)
        {
            v = (v << 8) | p[i];
        }
        return v;
    }
    for (size_t i = 0; i < n; i++)
    {
        v = (v << 8) | p[i];
    }
    return v;
}

static void fill_pattern(void)
{
    for (size_t i = 0; i < sizeof(g_src); i++)
    {
        g_src[i] = (uint8_t)(0x10u + i);
    }
}

// The mover's rung follows PROTO_WORD_BITS, which the die declares, and the carrier type follows the
// rung. Both relations hold at any width, so neither can drift from the other.
void test_word_rung_follows_the_declared_register_width(void)
{
    TEST_ASSERT_EQUAL_UINT(PROTO_RAW_WORD * 8u, (unsigned)PROTO_MV_BITS);
    TEST_ASSERT_EQUAL_UINT(PROTO_RAW_WORD, sizeof(proto_mv_word));
    // One of the three rungs the file is built from, and never wider than the declared register.
    TEST_ASSERT_TRUE(PROTO_RAW_WORD == 2 || PROTO_RAW_WORD == 4 || PROTO_RAW_WORD == 8);
    TEST_ASSERT_TRUE((size_t)PROTO_RAW_WORD * 8u <= (size_t)PROTO_WORD_BITS);
}

// Each rung against a byte loop, at every offset within a word, so an alignment the compiler would
// otherwise assume is exercised rather than avoided.
void test_scalar_rungs_match_a_byte_loop(void)
{
    fill_pattern();
    for (size_t off = 0; off < 8; off++)
    {
        TEST_ASSERT_EQUAL_HEX16((uint16_t)assemble(&g_src[off], 2), raw.u16(&g_src[off]));
        TEST_ASSERT_EQUAL_HEX32((uint32_t)assemble(&g_src[off], 4), raw.u32(&g_src[off]));
        TEST_ASSERT_EQUAL_HEX64(assemble(&g_src[off], 8), raw.u64(&g_src[off]));
    }
}

// load selects the rung by width; the octet case is the plain dereference. A width the file has no
// rung for reads nothing and yields 0, so a caller cannot get a partial value it would treat as whole.
void test_load_selects_a_rung_and_refuses_every_other_width(void)
{
    fill_pattern();
    for (size_t off = 0; off < 8; off++)
    {
        TEST_ASSERT_EQUAL_HEX64((uint64_t)g_src[off], raw.load(&g_src[off], 1));
        TEST_ASSERT_EQUAL_HEX64(assemble(&g_src[off], 2), raw.load(&g_src[off], 2));
        TEST_ASSERT_EQUAL_HEX64(assemble(&g_src[off], 4), raw.load(&g_src[off], 4));
        TEST_ASSERT_EQUAL_HEX64(assemble(&g_src[off], 8), raw.load(&g_src[off], 8));
    }
    static const size_t NOT_A_RUNG[] = {0, 3, 5, 6, 7, 9, 16};
    for (size_t i = 0; i < sizeof(NOT_A_RUNG) / sizeof(NOT_A_RUNG[0]); i++)
    {
        TEST_ASSERT_EQUAL_HEX64(0u, raw.load(g_src, NOT_A_RUNG[i]));
    }
}

// A store followed by the matching load returns the value, and touches nothing outside its width.
void test_put_round_trips_and_stays_inside_its_width(void)
{
    static const uint64_t V = 0x0123456789ABCDEFull;
    for (size_t off = 0; off < 8; off++)
    {
        memset(g_dst, 0xA5, sizeof(g_dst));
        raw.put_u16(&g_dst[off], (uint16_t)V);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)V, raw.u16(&g_dst[off]));
        TEST_ASSERT_EQUAL_HEX8(0xA5, g_dst[off + 2]);

        memset(g_dst, 0xA5, sizeof(g_dst));
        raw.put_u32(&g_dst[off], (uint32_t)V);
        TEST_ASSERT_EQUAL_HEX32((uint32_t)V, raw.u32(&g_dst[off]));
        TEST_ASSERT_EQUAL_HEX8(0xA5, g_dst[off + 4]);

        memset(g_dst, 0xA5, sizeof(g_dst));
        raw.put_u64(&g_dst[off], V);
        TEST_ASSERT_EQUAL_HEX64(V, raw.u64(&g_dst[off]));
        TEST_ASSERT_EQUAL_HEX8(0xA5, g_dst[off + 8]);

        // Nothing below the store either.
        if (off > 0)
        {
            TEST_ASSERT_EQUAL_HEX8(0xA5, g_dst[off - 1]);
        }
    }
}

// The aligned pair drops the alignment disclaimer and nothing else: at an address that carries the
// alignment, it reads and writes exactly what the unaligned pair does.
void test_aligned_rungs_agree_with_the_unaligned_ones(void)
{
    fill_pattern();
    for (size_t off = 0; off < 16; off += 8) // g_src is 16-byte aligned, so both are 8-byte aligned
    {
        TEST_ASSERT_EQUAL_HEX64((uint64_t)g_src[off], raw.al_load(&g_src[off], 1));
        TEST_ASSERT_EQUAL_HEX64(raw.load(&g_src[off], 2), raw.al_load(&g_src[off], 2));
        TEST_ASSERT_EQUAL_HEX64(raw.load(&g_src[off], 4), raw.al_load(&g_src[off], 4));
        TEST_ASSERT_EQUAL_HEX64(raw.load(&g_src[off], 8), raw.al_load(&g_src[off], 8));
        TEST_ASSERT_EQUAL_HEX64(0u, raw.al_load(&g_src[off], 3));
    }

    memset(g_dst, 0xA5, sizeof(g_dst));
    raw.al_put_u16(&g_dst[0], 0xBEEFu);
    raw.al_put_u32(&g_dst[4], 0xDEADBEEFu);
    raw.al_put_u64(&g_dst[8], 0x0123456789ABCDEFull);
    TEST_ASSERT_EQUAL_HEX16(0xBEEFu, raw.u16(&g_dst[0]));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, raw.u32(&g_dst[4]));
    TEST_ASSERT_EQUAL_HEX64(0x0123456789ABCDEFull, raw.u64(&g_dst[8]));

    // The mover's own rung is the aligned load and store at PROTO_RAW_WORD.
    memset(g_dst, 0xA5, sizeof(g_dst));
    raw.mv_put(&g_dst[0], (proto_mv_word)0x0123456789ABCDEFull);
    TEST_ASSERT_EQUAL_HEX64((uint64_t)(proto_mv_word)0x0123456789ABCDEFull, (uint64_t)raw.mv_load(&g_dst[0]));
    TEST_ASSERT_EQUAL_HEX8(0xA5, g_dst[PROTO_RAW_WORD]);
}

// The ladder over the cross product of both offsets and every length through two full words: the
// head aligns the destination, so the SOURCE offset is what selects the co-aligned branch or the
// funnel that joins two adjacent words. Poison outside the span is checked on every case, so a rung
// that writes past its width fails here rather than in whatever borrowed the next bytes.
void test_read_moves_every_offset_pair_and_length(void)
{
    const size_t maxlen = 2u * (size_t)PROTO_RAW_WORD + 1u;
    for (size_t so = 0; so < 8; so++)
    {
        for (size_t dof = 0; dof < 8; dof++)
        {
            for (size_t len = 0; len <= maxlen; len++)
            {
                fill_pattern();
                memset(g_dst, 0xA5, sizeof(g_dst));
                raw.read(&g_dst[dof], &g_src[so], len);
                for (size_t i = 0; i < len; i++)
                {
                    TEST_ASSERT_EQUAL_HEX8(g_src[so + i], g_dst[dof + i]);
                }
                for (size_t i = 0; i < dof; i++)
                {
                    TEST_ASSERT_EQUAL_HEX8(0xA5, g_dst[i]);
                }
                for (size_t i = dof + len; i < sizeof(g_dst); i++)
                {
                    TEST_ASSERT_EQUAL_HEX8(0xA5, g_dst[i]);
                }
            }
        }
    }
}

// A struct overlaid on a byte stream is the case the two attributes exist for: it lands at whatever
// offset the stream put it, and its members read back once it has been moved to storage of its own.
void test_read_carries_an_overlaid_header_struct(void)
{
    typedef struct PROTO_RAW
    {
        uint32_t id;
        uint16_t len;
    } wire_hdr;

    fill_pattern();
    for (size_t off = 0; off < 8; off++)
    {
        wire_hdr h;
        memset(&h, 0, sizeof h);
        raw.read(&h, &g_src[off], sizeof h);
        TEST_ASSERT_EQUAL_HEX32((uint32_t)assemble(&g_src[off], 4), h.id);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)assemble(&g_src[off + 4], 2), h.len);
    }
}

// The names the module exports resolve to the rungs the header declares. Six of the members share
// the void(*)(void *, uint64_t)-shaped family, so a swapped pair type-checks and links.
void test_the_table_is_wired_to_the_named_rungs(void)
{
    TEST_ASSERT_EQUAL_PTR(proto_raw_u16, raw.u16);
    TEST_ASSERT_EQUAL_PTR(proto_raw_u32, raw.u32);
    TEST_ASSERT_EQUAL_PTR(proto_raw_u64, raw.u64);
    TEST_ASSERT_EQUAL_PTR(proto_raw_load, raw.load);
    TEST_ASSERT_EQUAL_PTR(proto_raw_put_u16, raw.put_u16);
    TEST_ASSERT_EQUAL_PTR(proto_raw_put_u32, raw.put_u32);
    TEST_ASSERT_EQUAL_PTR(proto_raw_put_u64, raw.put_u64);
    TEST_ASSERT_EQUAL_PTR(proto_al_load, raw.al_load);
    TEST_ASSERT_EQUAL_PTR(proto_al_put_u16, raw.al_put_u16);
    TEST_ASSERT_EQUAL_PTR(proto_al_put_u32, raw.al_put_u32);
    TEST_ASSERT_EQUAL_PTR(proto_al_put_u64, raw.al_put_u64);
    TEST_ASSERT_EQUAL_PTR(proto_mv_load, raw.mv_load);
    TEST_ASSERT_EQUAL_PTR(proto_mv_put, raw.mv_put);
    TEST_ASSERT_EQUAL_PTR(proto_raw_read, raw.read);
}
