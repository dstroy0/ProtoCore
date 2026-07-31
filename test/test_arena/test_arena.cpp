// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the unified double-ended arena (network_drivers/session/pc_arena):
// first-fit persistent end (bottom) + bump scratch end (top) sharing a floating middle.

#include "server/mmgr/arena.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

static uint8_t g_buf[4096];
static pc_arena a;

void setUp()
{
    memset(g_buf, 0xAA, sizeof(g_buf)); // poison so we can see zeroing
    pc_arena_init(&a, g_buf, sizeof(g_buf));
}
void tearDown()
{
}

static bool aligned8(const void *p)
{
    return ((uintptr_t)p & 7u) == 0;
}
static bool in_region(const pc_arena *ar, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    return b >= ar->base && b + n <= ar->base + ar->size;
}

// ---- persistent end -------------------------------------------------------

void test_persist_basic_alloc()
{
    void *p = pc_arena_persist_alloc(&a, 100);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(aligned8(p));
    TEST_ASSERT_TRUE(in_region(&a, p, 100));
    void *q = pc_arena_persist_alloc(&a, 100);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_TRUE(p != q);
    TEST_ASSERT_TRUE((uintptr_t)q >= (uintptr_t)p + 100); // grows up, no overlap
}

void test_persist_zeroed()
{
    uint8_t *p = (uint8_t *)pc_arena_persist_alloc(&a, 64);
    for (int i = 0; i < 64; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, p[i]); // zeroed despite 0xAA poison
    }
    memset(p, 0xFF, 64);
    pc_arena_persist_free(&a, p);
    uint8_t *q = (uint8_t *)pc_arena_persist_alloc(&a, 64); // reuses the slot
    TEST_ASSERT_EQUAL_PTR(p, q);
    for (int i = 0; i < 64; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, q[i]); // re-zeroed
    }
}

void test_persist_first_fit_reuse()
{
    void *A = pc_arena_persist_alloc(&a, 64);
    void *B = pc_arena_persist_alloc(&a, 64);
    void *C = pc_arena_persist_alloc(&a, 64);
    (void)A;
    (void)C;
    pc_arena_persist_free(&a, B);
    void *D = pc_arena_persist_alloc(&a, 64); // first-fit reuses B's hole
    TEST_ASSERT_EQUAL_PTR(B, D);
}

void test_persist_coalesce()
{
    void *A = pc_arena_persist_alloc(&a, 64);
    void *B = pc_arena_persist_alloc(&a, 64);
    void *C = pc_arena_persist_alloc(&a, 64);
    (void)A;
    pc_arena_persist_free(&a, B);
    pc_arena_persist_free(&a, C); // B+C coalesce into one hole
    void *big = pc_arena_persist_alloc(&a, 150);
    TEST_ASSERT_NOT_NULL(big);
    TEST_ASSERT_EQUAL_PTR(B, big); // the merged B+C region holds 150
}

void test_persist_free_shrinks_boundary()
{
    size_t free0 = pc_arena_free_bytes(&a);
    void *p = pc_arena_persist_alloc(&a, 1024);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(pc_arena_free_bytes(&a) < free0);
    pc_arena_persist_free(&a, p); // top block freed -> returns to middle
    TEST_ASSERT_EQUAL_size_t(free0, pc_arena_free_bytes(&a));
    TEST_ASSERT_EQUAL_size_t(0, pc_arena_persist_used(&a));
}

void test_persist_init_zero_size()
{
    // size == 0 must take the ternary's false arm (size > adj is false) rather than underflow.
    pc_arena z;
    pc_arena_init(&z, g_buf, 0);
    TEST_ASSERT_EQUAL_size_t(0, z.size);
    TEST_ASSERT_NULL(pc_arena_persist_alloc(&z, 16));
    TEST_ASSERT_NULL(pc_arena_scratch_alloc(&z, 16));
}

void test_persist_first_fit_skips_too_small_free_block()
{
    // A free hole too small for the request must be skipped, not "reused" anyway.
    void *A = pc_arena_persist_alloc(&a, 16);  // small block -> becomes an undersized hole
    void *B = pc_arena_persist_alloc(&a, 500); // used block, sits right after A
    (void)A;
    pc_arena_persist_free(&a, A);
    void *C = pc_arena_persist_alloc(&a, 100); // too big for A's hole; B is in use -> extends past B
    TEST_ASSERT_NOT_NULL(C);
    TEST_ASSERT_TRUE((uintptr_t)C > (uintptr_t)B);
}

void test_persist_alloc_overflow_guard()
{
    // First carve a block so persist_end > 239, then request a size so large that
    // (AHDR + n) wraps size_t; persist_end + need must not wrap back into apparent success.
    void *first = pc_arena_persist_alloc(&a, 250);
    TEST_ASSERT_NOT_NULL(first);
    size_t huge = (size_t)0 - 256; // SIZE_MAX-255: already 8-aligned, survives align_up unchanged
    void *p = pc_arena_persist_alloc(&a, huge);
    TEST_ASSERT_NULL(p); // overflow guard must fail closed, not wrap into a bogus success
}

void test_persist_double_free_and_empty_chain_free()
{
    // Freeing an already-free block (and freeing into an already-empty chain) must be a
    // safe no-op, not a double-decrement or an out-of-bounds boundary scan.
    void *p = pc_arena_persist_alloc(&a, 64);
    TEST_ASSERT_NOT_NULL(p);
    pc_arena_persist_free(&a, p); // block was the only one -> chain empties, persist_end -> 0
    TEST_ASSERT_EQUAL_size_t(0, pc_arena_persist_used(&a));
    pc_arena_persist_free(&a, p); // double free: b->used already false; persist_end already 0
    TEST_ASSERT_EQUAL_size_t(0, pc_arena_persist_used(&a));
}

void test_free_bytes_when_exactly_full()
{
    // Consuming exactly the reported free middle brings persist_end up to meet scratch_top
    // exactly (not past it); free_bytes() must then report 0 via its "no middle left" arm.
    size_t free0 = pc_arena_free_bytes(&a);
    TEST_ASSERT_TRUE(free0 > 0);
    void *p = pc_arena_persist_alloc(&a, free0);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(0, pc_arena_free_bytes(&a));
}

// ---- scratch end ----------------------------------------------------------

void test_scratch_bump_and_reset()
{
    TEST_ASSERT_EQUAL_size_t(0, pc_arena_scratch_used(&a));
    void *p = pc_arena_scratch_alloc(&a, 100);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(aligned8(p));
    TEST_ASSERT_TRUE(pc_arena_scratch_used(&a) >= 100);
    void *q = pc_arena_scratch_alloc(&a, 100);
    TEST_ASSERT_TRUE(p != q);
    TEST_ASSERT_TRUE((uintptr_t)q < (uintptr_t)p); // grows down
    pc_arena_scratch_reset(&a);
    TEST_ASSERT_EQUAL_size_t(0, pc_arena_scratch_used(&a));
}

void test_scratch_mark_release()
{
    pc_arena_scratch_alloc(&a, 100);
    size_t used1 = pc_arena_scratch_used(&a);
    size_t mk = pc_arena_scratch_mark(&a);
    pc_arena_scratch_alloc(&a, 200);
    pc_arena_scratch_alloc(&a, 50);
    TEST_ASSERT_TRUE(pc_arena_scratch_used(&a) > used1);
    pc_arena_scratch_release(&a, mk); // frees everything after the mark
    TEST_ASSERT_EQUAL_size_t(used1, pc_arena_scratch_used(&a));
}

void test_scratch_high_water_mark_not_regressed()
{
    // A later, smaller allocation must not appear to raise usage past an earlier peak.
    void *big = pc_arena_scratch_alloc(&a, 1000);
    TEST_ASSERT_NOT_NULL(big);
    pc_arena_scratch_reset(&a); // usage back to 0; the internal high-water mark stays at the peak
    void *small = pc_arena_scratch_alloc(&a, 10);
    TEST_ASSERT_NOT_NULL(small);
    TEST_ASSERT_TRUE(pc_arena_scratch_used(&a) < 1000);
}

void test_scratch_release_rejects_invalid_marks()
{
    // A mark below the current top (would grow usage) or beyond the arena (a foreign/corrupt
    // mark) must both be rejected, leaving scratch_top untouched.
    pc_arena_scratch_alloc(&a, 200);
    size_t used_before = pc_arena_scratch_used(&a);
    pc_arena_scratch_release(&a, 0); // 0 < current scratch_top -> rejected
    TEST_ASSERT_EQUAL_size_t(used_before, pc_arena_scratch_used(&a));
    pc_arena_scratch_release(&a, a.size + 1000); // beyond the arena -> rejected
    TEST_ASSERT_EQUAL_size_t(used_before, pc_arena_scratch_used(&a));
}

// ---- both ends / floating boundary ----------------------------------------

void test_persist_and_scratch_no_overlap()
{
    uint8_t *p = (uint8_t *)pc_arena_persist_alloc(&a, 500);
    uint8_t *s = (uint8_t *)pc_arena_scratch_alloc(&a, 500);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NOT_NULL(s);
    // persistent is below scratch, no overlap
    TEST_ASSERT_TRUE(p + 500 <= s);
    memset(p, 0x11, 500);
    memset(s, 0x22, 500);
    TEST_ASSERT_EQUAL_UINT8(0x11, p[499]);
    TEST_ASSERT_EQUAL_UINT8(0x22, s[0]);
}

void test_boundary_collision_fail_closed()
{
    // Take most of the arena from the bottom, then from the top, until they nearly meet.
    void *p = pc_arena_persist_alloc(&a, 1800);
    void *s = pc_arena_scratch_alloc(&a, 1800);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NOT_NULL(s);
    // The middle is now tiny; a big alloc from either end must fail (not cross).
    TEST_ASSERT_NULL(pc_arena_persist_alloc(&a, 1000));
    TEST_ASSERT_NULL(pc_arena_scratch_alloc(&a, 1000));
    // A failed alloc must not have moved either boundary / corrupted the survivors.
    TEST_ASSERT_TRUE((uint8_t *)p + 1800 <= (uint8_t *)s);
}

void test_scratch_reset_frees_middle_for_persist()
{
    pc_arena_scratch_alloc(&a, 3000); // hog the top
    TEST_ASSERT_NULL(pc_arena_persist_alloc(&a, 2000));
    pc_arena_scratch_reset(&a); // give it back
    TEST_ASSERT_NOT_NULL(pc_arena_persist_alloc(&a, 2000));
}

void test_alignment_various_sizes()
{
    size_t sizes[] = {1, 3, 7, 8, 9, 17, 33, 100, 255};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        void *p = pc_arena_persist_alloc(&a, sizes[i]);
        void *s = pc_arena_scratch_alloc(&a, sizes[i]);
        TEST_ASSERT_TRUE(aligned8(p));
        TEST_ASSERT_TRUE(aligned8(s));
    }
}

void test_scratch_alignment_16()
{
    // The real scratch callers (SSH, WS deflate) ask for 16-byte alignment.
    for (int i = 0; i < 20; i++)
    {
        void *p = pc_arena_scratch_alloc_aligned(&a, 17 + i, 16);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_TRUE(((uintptr_t)p & 15u) == 0); // 16-aligned
    }
    void *q = pc_arena_scratch_alloc_aligned(&a, 1, 1); // align clamps up to 8, never 0
    TEST_ASSERT_TRUE(aligned8(q));
}

void test_zero_size_and_null_free()
{
    void *p = pc_arena_persist_alloc(&a, 0); // rounds up to alignment, non-null
    TEST_ASSERT_NOT_NULL(p);
    pc_arena_persist_free(&a, nullptr); // no-op, no crash
    void *s = pc_arena_scratch_alloc(&a, 0);
    TEST_ASSERT_NOT_NULL(s);
}

// ---- multi-region set (DRAM base + PSRAM extension) -----------------------

static uint8_t g_r0[512];  // stand-in for internal DRAM (preferred)
static uint8_t g_r1[2048]; // stand-in for the PSRAM extension

static bool in_buf(const void *p, const uint8_t *buf, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    return b >= buf && b < buf + n;
}

void test_set_add_limits()
{
    pc_arena_set s;
    pc_arena_set_init(&s);
    TEST_ASSERT_TRUE(pc_arena_set_add(&s, g_r0, sizeof(g_r0)));
    TEST_ASSERT_TRUE(pc_arena_set_add(&s, g_r1, sizeof(g_r1)));
    static uint8_t extra[256];
    TEST_ASSERT_FALSE(pc_arena_set_add(&s, extra, sizeof(extra))); // full (max 2)
    pc_arena_set t;
    pc_arena_set_init(&t);
    static uint8_t tiny[4];
    TEST_ASSERT_FALSE(pc_arena_set_add(&t, tiny, sizeof(tiny))); // too small
}

void test_set_persist_overflow_and_prefer()
{
    pc_arena_set s;
    pc_arena_set_init(&s);
    pc_arena_set_add(&s, g_r0, sizeof(g_r0));
    pc_arena_set_add(&s, g_r1, sizeof(g_r1));
    void *big = pc_arena_set_persist_alloc(&s, 700); // too big for r0 -> spills to r1
    TEST_ASSERT_NOT_NULL(big);
    TEST_ASSERT_TRUE(in_buf(big, g_r1, sizeof(g_r1)));
    void *small = pc_arena_set_persist_alloc(&s, 32); // prefers r0
    TEST_ASSERT_NOT_NULL(small);
    TEST_ASSERT_TRUE(in_buf(small, g_r0, sizeof(g_r0)));
    TEST_ASSERT_TRUE(pc_arena_set_persist_used(&s) >= 732); // >= requested (alignment rounds up)
}

void test_set_persist_free_routes_by_address()
{
    pc_arena_set s;
    pc_arena_set_init(&s);
    pc_arena_set_add(&s, g_r0, sizeof(g_r0));
    pc_arena_set_add(&s, g_r1, sizeof(g_r1));
    void *inR1 = pc_arena_set_persist_alloc(&s, 700);
    TEST_ASSERT_TRUE(in_buf(inR1, g_r1, sizeof(g_r1)));
    pc_arena_set_persist_free(&s, inR1); // must route to r1, not r0
    TEST_ASSERT_EQUAL_size_t(0, pc_arena_set_persist_used(&s));
    pc_arena_set_persist_free(&s, nullptr); // no-op
}

void test_set_persist_free_unmatched_pointer_is_noop()
{
    // A pointer that belongs to neither region must let the routing loop run to completion
    // (no region claims it) and simply return, rather than misroute or crash.
    pc_arena_set s;
    pc_arena_set_init(&s);
    pc_arena_set_add(&s, g_r0, sizeof(g_r0));
    pc_arena_set_add(&s, g_r1, sizeof(g_r1));
    uint8_t stray[8]; // not part of either region's backing buffer
    pc_arena_set_persist_free(&s, stray);
    TEST_ASSERT_EQUAL_size_t(0, pc_arena_set_persist_used(&s));

    // Exercise both address-compare arms of the routing test regardless of link layout: a pointer below
    // every region (b >= base is false) and one past every region (b >= base true, b < base+size false).
    // Neither claims the pointer, so both are safe no-ops.
    uint8_t *b0 = s.region[0].base;
    uint8_t *b1 = s.region[1].base;
    uint8_t *below = (b0 < b1 ? b0 : b1) - 16;
    uint8_t *e0 = b0 + s.region[0].size;
    uint8_t *e1 = b1 + s.region[1].size;
    uint8_t *above = (e0 > e1 ? e0 : e1) + 16;
    pc_arena_set_persist_free(&s, below);
    pc_arena_set_persist_free(&s, above);
    TEST_ASSERT_EQUAL_size_t(0, pc_arena_set_persist_used(&s));
}

void test_set_scratch_overflow_and_unwind()
{
    pc_arena_set s;
    pc_arena_set_init(&s);
    pc_arena_set_add(&s, g_r0, sizeof(g_r0));
    pc_arena_set_add(&s, g_r1, sizeof(g_r1));
    void *a0 = pc_arena_set_scratch_alloc(&s, 400); // fits r0
    TEST_ASSERT_TRUE(in_buf(a0, g_r0, sizeof(g_r0)));
    pc_arena_mark mk = pc_arena_set_scratch_mark(&s);
    void *a1 = pc_arena_set_scratch_alloc(&s, 900); // r0 too full -> spills to r1
    TEST_ASSERT_TRUE(in_buf(a1, g_r1, sizeof(g_r1)));
    TEST_ASSERT_TRUE(pc_arena_set_scratch_used(&s) >= 1300);
    pc_arena_set_scratch_release(&s, &mk); // unwinds the r1 spill too
    TEST_ASSERT_TRUE(pc_arena_set_scratch_used(&s) >= 400);
    TEST_ASSERT_TRUE(pc_arena_set_scratch_used(&s) < 900);
    pc_arena_set_scratch_reset(&s);
    TEST_ASSERT_EQUAL_size_t(0, pc_arena_set_scratch_used(&s));
}

void test_persist_split_and_max_align()
{
    // A small alloc into a large non-terminal hole splits the hole (leaves a free remainder).
    void *A = pc_arena_persist_alloc(&a, 64);
    void *B = pc_arena_persist_alloc(&a, 200);
    void *C = pc_arena_persist_alloc(&a, 64);
    (void)A;
    pc_arena_persist_free(&a, B); // a 200-byte hole between A and C (not the last block)
    void *D = pc_arena_persist_alloc(&a, 16);
    TEST_ASSERT_EQUAL_PTR(B, D);              // reuses B's hole
    void *E = pc_arena_persist_alloc(&a, 16); // the split remainder is itself reusable
    TEST_ASSERT_NOT_NULL(E);
    TEST_ASSERT_TRUE((uintptr_t)E > (uintptr_t)D && (uintptr_t)E < (uintptr_t)C);

    // Requesting more alignment than the base guarantees clamps to PC_ARENA_MAX_ALIGN.
    void *p = pc_arena_scratch_alloc_aligned(&a, 32, 32); // 32 > 16 -> clamps to 16
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(((uintptr_t)p & 15u) == 0);
}

void test_set_scratch_release_partial_mark_count()
{
    // A mark captured with fewer regions than the set currently has (a region was added after
    // the mark) must restore only the regions the mark actually covers (m->count < s->count).
    pc_arena_set s;
    pc_arena_set_init(&s);
    pc_arena_set_add(&s, g_r0, sizeof(g_r0)); // count == 1 when the mark is captured
    pc_arena_mark mk = pc_arena_set_scratch_mark(&s);
    pc_arena_set_add(&s, g_r1, sizeof(g_r1)); // count grows to 2 after the mark
    void *p = pc_arena_set_scratch_alloc(&s, 100);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(in_buf(p, g_r0, sizeof(g_r0))); // r0 is tried first and has room
    pc_arena_set_scratch_release(&s, &mk);           // mk.count(1) < s.count(2)
    TEST_ASSERT_EQUAL_size_t(0, pc_arena_set_scratch_used(&s));
}

void test_set_exhaustion_and_free_bytes()
{
    pc_arena_set s;
    pc_arena_set_init(&s);
    pc_arena_set_add(&s, g_r0, sizeof(g_r0)); // 512
    pc_arena_set_add(&s, g_r1, sizeof(g_r1)); // 2048
    // A request larger than any single region fails closed across the whole set.
    TEST_ASSERT_NULL(pc_arena_set_persist_alloc(&s, 100000));
    TEST_ASSERT_NULL(pc_arena_set_scratch_alloc(&s, 100000));
    // free_bytes sums the free middle of every region.
    size_t before = pc_arena_set_free_bytes(&s);
    TEST_ASSERT_TRUE(before > 0);
    void *p = pc_arena_set_persist_alloc(&s, 128);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(pc_arena_set_free_bytes(&s) < before); // shrank by the allocation
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_persist_basic_alloc);
    RUN_TEST(test_persist_zeroed);
    RUN_TEST(test_persist_first_fit_reuse);
    RUN_TEST(test_persist_coalesce);
    RUN_TEST(test_persist_free_shrinks_boundary);
    RUN_TEST(test_persist_init_zero_size);
    RUN_TEST(test_persist_first_fit_skips_too_small_free_block);
    RUN_TEST(test_persist_alloc_overflow_guard);
    RUN_TEST(test_persist_double_free_and_empty_chain_free);
    RUN_TEST(test_free_bytes_when_exactly_full);
    RUN_TEST(test_scratch_bump_and_reset);
    RUN_TEST(test_scratch_mark_release);
    RUN_TEST(test_scratch_high_water_mark_not_regressed);
    RUN_TEST(test_scratch_release_rejects_invalid_marks);
    RUN_TEST(test_persist_and_scratch_no_overlap);
    RUN_TEST(test_boundary_collision_fail_closed);
    RUN_TEST(test_scratch_reset_frees_middle_for_persist);
    RUN_TEST(test_alignment_various_sizes);
    RUN_TEST(test_scratch_alignment_16);
    RUN_TEST(test_zero_size_and_null_free);
    RUN_TEST(test_set_add_limits);
    RUN_TEST(test_set_persist_overflow_and_prefer);
    RUN_TEST(test_set_persist_free_routes_by_address);
    RUN_TEST(test_set_persist_free_unmatched_pointer_is_noop);
    RUN_TEST(test_set_scratch_overflow_and_unwind);
    RUN_TEST(test_set_scratch_release_partial_mark_count);
    RUN_TEST(test_persist_split_and_max_align);
    RUN_TEST(test_set_exhaustion_and_free_bytes);
    return UNITY_END();
}
