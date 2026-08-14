// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the double-ended arena (mmgr/arena.h).
//
// No published standard governs an allocator's layout, so every expectation here is a PROPERTY
// that must hold whatever the implementation does: a borrow is aligned, zeroed and inside the
// region; the two ends never overlap; a request that would cross the boundary returns NULL rather
// than a pointer into the neighbour; a freed hole is reusable; and a mark restores exactly what it
// captured.
//
// test_a_borrow_owns_its_alignment_pad is the load-bearing one. This library reads a span a whole
// register word at a time and truncates the surplus, so the last word of an n-byte borrow is read
// in full. That is only sound if the borrow owns every byte up to the next PROTOCORE_ARENA_ALIGN
// boundary. The case asserts exactly that, from both ends.

#include "mmgr/arena.h"
#include <string.h>

#include <unity.h>

static uint8_t g_buf[4096];
static protocore_arena a;

void setUp(void)
{
    memset(g_buf, 0xAA, sizeof(g_buf)); // poison, so a zeroing claim is not vacuous
    protocore_arena_init(&a, g_buf, sizeof(g_buf));
}

void tearDown(void)
{
}

// protocore_arena_init() aligns the region base up to PROTOCORE_ARENA_MAX_ALIGN, so an offset from
// that base aligned to n means the pointer is too.
static proto_bool aligned(const protocore_arena *ar, const void *p, size_t n)
{
    return ((size_t)((const uint8_t *)p - ar->base) & (n - 1u)) == 0u;
}

// Round n up the way PROTOCORE_ARENA_ALIGN requires, spelled here rather than called from the header.
static size_t padded(size_t n)
{
    return (n + (PROTOCORE_ARENA_ALIGN - 1u)) & ~(size_t)(PROTOCORE_ARENA_ALIGN - 1u);
}

static proto_bool inside(const void *p, const uint8_t *buf, size_t n)
{
    const uint8_t *q = (const uint8_t *)p;
    return q >= buf && q < buf + n;
}

// ---- the persistent end ----------------------------------------------------

// A borrow is aligned, lands inside the region, and does not overlap the one before it.
void test_persist_alloc_is_aligned_and_inside_the_region(void)
{
    uint8_t *p = (uint8_t *)protocore_arena_persist_alloc(&a, 100);
    uint8_t *q = (uint8_t *)protocore_arena_persist_alloc(&a, 100);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_TRUE(aligned(&a, p, PROTOCORE_ARENA_ALIGN));
    TEST_ASSERT_TRUE(aligned(&a, q, PROTOCORE_ARENA_ALIGN));
    TEST_ASSERT_TRUE(inside(p, g_buf, sizeof(g_buf)));
    TEST_ASSERT_TRUE(p + 100 <= q); // grows up, no overlap
}

// The persistent end hands back zeroed storage, both on a fresh carve and on a reused hole, so a
// caller never reads a previous tenant's bytes.
void test_persist_alloc_is_zeroed_on_carve_and_on_reuse(void)
{
    uint8_t *p = (uint8_t *)protocore_arena_persist_alloc(&a, 64);
    TEST_ASSERT_NOT_NULL(p);
    for (unsigned i = 0; i < 64u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00u, p[i]);
    }

    memset(p, 0xFF, 64);
    protocore_arena_persist_free(&a, p);
    uint8_t *q = (uint8_t *)protocore_arena_persist_alloc(&a, 64);
    TEST_ASSERT_EQUAL_PTR(p, q);
    for (unsigned i = 0; i < 64u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00u, q[i]);
    }
}

// A freed hole is reused by the next request that fits it, ahead of untouched space.
void test_persist_reuses_a_freed_hole(void)
{
    void *A = protocore_arena_persist_alloc(&a, 64);
    void *B = protocore_arena_persist_alloc(&a, 64);
    void *C = protocore_arena_persist_alloc(&a, 64);
    TEST_ASSERT_NOT_NULL(A);
    TEST_ASSERT_NOT_NULL(C);
    protocore_arena_persist_free(&a, B);
    TEST_ASSERT_EQUAL_PTR(B, protocore_arena_persist_alloc(&a, 64));
}

// Two adjacent holes merge, so a request neither could serve alone is served by the pair.
void test_persist_free_coalesces_adjacent_holes(void)
{
    void *A = protocore_arena_persist_alloc(&a, 64);
    void *B = protocore_arena_persist_alloc(&a, 64);
    void *C = protocore_arena_persist_alloc(&a, 64);
    TEST_ASSERT_NOT_NULL(A);
    protocore_arena_persist_free(&a, B);
    protocore_arena_persist_free(&a, C);
    void *big = protocore_arena_persist_alloc(&a, 150); // > 64, so only the merged pair holds it
    TEST_ASSERT_EQUAL_PTR(B, big);
}

// A hole too small for the request is skipped rather than handed out anyway.
void test_persist_skips_a_hole_that_is_too_small(void)
{
    void *A = protocore_arena_persist_alloc(&a, 16);
    void *B = protocore_arena_persist_alloc(&a, 500);
    TEST_ASSERT_NOT_NULL(B);
    protocore_arena_persist_free(&a, A);
    uint8_t *C = (uint8_t *)protocore_arena_persist_alloc(&a, 100);
    TEST_ASSERT_NOT_NULL(C);
    TEST_ASSERT_TRUE(C > (uint8_t *)B); // past B, not into A's undersized hole
}

// Freeing the top block gives its space back to the shared middle.
void test_persist_free_of_the_top_block_returns_the_middle(void)
{
    size_t free0 = protocore_arena_free_bytes(&a);
    void *p = protocore_arena_persist_alloc(&a, 1024);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(protocore_arena_free_bytes(&a) < free0);
    protocore_arena_persist_free(&a, p);
    TEST_ASSERT_EQUAL_size_t(free0, protocore_arena_free_bytes(&a));
    TEST_ASSERT_EQUAL_size_t(0, protocore_arena_persist_used(&a));
}

// A size whose header-plus-payload wraps size_t must fail closed rather than wrap into an
// apparent success.
void test_persist_size_overflow_fails_closed(void)
{
    TEST_ASSERT_NOT_NULL(protocore_arena_persist_alloc(&a, 250)); // move persist_end off zero first
    size_t huge = (size_t)0 - 256u;                               // already aligned, survives the round up
    TEST_ASSERT_NULL(protocore_arena_persist_alloc(&a, huge));
}

// Freeing twice, and freeing NULL, are no-ops rather than a second decrement.
void test_persist_double_free_and_null_free_are_noops(void)
{
    void *p = protocore_arena_persist_alloc(&a, 64);
    TEST_ASSERT_NOT_NULL(p);
    protocore_arena_persist_free(&a, p);
    TEST_ASSERT_EQUAL_size_t(0, protocore_arena_persist_used(&a));
    protocore_arena_persist_free(&a, p);
    TEST_ASSERT_EQUAL_size_t(0, protocore_arena_persist_used(&a));
    protocore_arena_persist_free(&a, NULL);
    TEST_ASSERT_EQUAL_size_t(0, protocore_arena_persist_used(&a));
}

// Consuming exactly the reported free middle brings the two ends together without crossing, and
// the report then reads zero.
void test_free_bytes_reaches_zero_without_crossing(void)
{
    size_t free0 = protocore_arena_free_bytes(&a);
    TEST_ASSERT_TRUE(free0 > 0);
    TEST_ASSERT_NOT_NULL(protocore_arena_persist_alloc(&a, free0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_arena_free_bytes(&a));
}

// ---- the scratch end -------------------------------------------------------

// Scratch bumps downward and is emptied in one step.
void test_scratch_bumps_down_and_resets(void)
{
    TEST_ASSERT_EQUAL_size_t(0, protocore_arena_scratch_used(&a));
    uint8_t *p = (uint8_t *)protocore_arena_scratch_alloc(&a, 100);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(aligned(&a, p, PROTOCORE_ARENA_ALIGN));
    TEST_ASSERT_TRUE(protocore_arena_scratch_used(&a) >= 100);
    uint8_t *q = (uint8_t *)protocore_arena_scratch_alloc(&a, 100);
    TEST_ASSERT_TRUE(q < p);
    protocore_arena_scratch_reset(&a);
    TEST_ASSERT_EQUAL_size_t(0, protocore_arena_scratch_used(&a));
}

// A mark restores exactly the position it captured, whatever was taken after it.
void test_scratch_mark_and_release(void)
{
    TEST_ASSERT_NOT_NULL(protocore_arena_scratch_alloc(&a, 100));
    size_t used = protocore_arena_scratch_used(&a);
    size_t mk = protocore_arena_scratch_mark(&a);
    TEST_ASSERT_NOT_NULL(protocore_arena_scratch_alloc(&a, 200));
    TEST_ASSERT_NOT_NULL(protocore_arena_scratch_alloc(&a, 50));
    TEST_ASSERT_TRUE(protocore_arena_scratch_used(&a) > used);
    protocore_arena_scratch_release(&a, mk);
    TEST_ASSERT_EQUAL_size_t(used, protocore_arena_scratch_used(&a));
}

// A mark below the current top would hand back memory that is in use, and one past the region
// belongs to another arena: both are rejected, leaving the top where it was.
void test_scratch_release_rejects_a_mark_outside_the_region(void)
{
    TEST_ASSERT_NOT_NULL(protocore_arena_scratch_alloc(&a, 200));
    size_t used = protocore_arena_scratch_used(&a);
    protocore_arena_scratch_release(&a, 0);
    TEST_ASSERT_EQUAL_size_t(used, protocore_arena_scratch_used(&a));
    protocore_arena_scratch_release(&a, sizeof(g_buf) + 1000u);
    TEST_ASSERT_EQUAL_size_t(used, protocore_arena_scratch_used(&a));
}

// An alignment request is clamped into [PROTOCORE_ARENA_ALIGN, PROTOCORE_ARENA_MAX_ALIGN]: below
// the baseline it rises to it, above the region's own guarantee it falls back to it.
void test_scratch_alignment_is_clamped_to_what_the_region_guarantees(void)
{
    for (unsigned i = 0; i < 20u; i++)
    {
        void *p = protocore_arena_scratch_alloc_aligned(&a, 17u + i, PROTOCORE_ARENA_MAX_ALIGN);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_TRUE(aligned(&a, p, PROTOCORE_ARENA_MAX_ALIGN));
    }
    void *low = protocore_arena_scratch_alloc_aligned(&a, 1, 1);
    TEST_ASSERT_NOT_NULL(low);
    TEST_ASSERT_TRUE(aligned(&a, low, PROTOCORE_ARENA_ALIGN));

    void *high = protocore_arena_scratch_alloc_aligned(&a, 32, PROTOCORE_ARENA_MAX_ALIGN * 2u);
    TEST_ASSERT_NOT_NULL(high);
    TEST_ASSERT_TRUE(aligned(&a, high, PROTOCORE_ARENA_MAX_ALIGN));
}

// ---- the shared middle -----------------------------------------------------

// The two ends occupy disjoint address ranges, and writing one leaves the other intact.
void test_the_two_ends_never_overlap(void)
{
    uint8_t *p = (uint8_t *)protocore_arena_persist_alloc(&a, 500);
    uint8_t *s = (uint8_t *)protocore_arena_scratch_alloc(&a, 500);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(p + 500 <= s);
    memset(p, 0x11, 500);
    memset(s, 0x22, 500);
    TEST_ASSERT_EQUAL_HEX8(0x11u, p[499]);
    TEST_ASSERT_EQUAL_HEX8(0x22u, s[0]);
}

// With the middle nearly gone, a request from either end returns NULL rather than crossing, and
// neither boundary moves.
void test_a_request_that_would_cross_fails_closed(void)
{
    uint8_t *p = (uint8_t *)protocore_arena_persist_alloc(&a, 1800);
    uint8_t *s = (uint8_t *)protocore_arena_scratch_alloc(&a, 1800);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_NULL(protocore_arena_persist_alloc(&a, 1000));
    TEST_ASSERT_NULL(protocore_arena_scratch_alloc(&a, 1000));
    TEST_ASSERT_TRUE(p + 1800 <= s);
}

// Whichever end needs the room takes it: emptying scratch lets the persistent end grow into what
// scratch was holding.
void test_the_middle_floats_between_the_ends(void)
{
    TEST_ASSERT_NOT_NULL(protocore_arena_scratch_alloc(&a, 3000));
    TEST_ASSERT_NULL(protocore_arena_persist_alloc(&a, 2000));
    protocore_arena_scratch_reset(&a);
    TEST_ASSERT_NOT_NULL(protocore_arena_persist_alloc(&a, 2000));
}

// A borrow of n bytes owns every byte up to the next alignment boundary, from both ends, and that
// pad stays inside the backing store. A word-at-a-time read of the tail therefore reaches no
// neighbour and no memory outside the region.
void test_a_borrow_owns_its_alignment_pad(void)
{
    static const size_t SIZES[] = {1, 2, 3, 5, 7, 9, 15, 17, 31, 33, 255};
    for (unsigned i = 0; i < sizeof(SIZES) / sizeof(SIZES[0]); i++)
    {
        protocore_arena_init(&a, g_buf, sizeof(g_buf));

        uint8_t *p = (uint8_t *)protocore_arena_persist_alloc(&a, SIZES[i]);
        uint8_t *next = (uint8_t *)protocore_arena_persist_alloc(&a, 8);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_NOT_NULL(next);
        TEST_ASSERT_TRUE(aligned(&a, p, PROTOCORE_ARENA_ALIGN));
        TEST_ASSERT_TRUE(next >= p + padded(SIZES[i]));
        TEST_ASSERT_TRUE(p + padded(SIZES[i]) <= g_buf + sizeof(g_buf));

        uint8_t *first = (uint8_t *)protocore_arena_scratch_alloc(&a, 8);
        uint8_t *s = (uint8_t *)protocore_arena_scratch_alloc(&a, SIZES[i]);
        TEST_ASSERT_NOT_NULL(first);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(aligned(&a, s, PROTOCORE_ARENA_ALIGN));
        TEST_ASSERT_TRUE(s + padded(SIZES[i]) <= first);
        TEST_ASSERT_TRUE(s >= g_buf);
    }
}

// A word-wide store over a borrow's padded extent disturbs no neighbour.
void test_a_write_over_the_pad_hits_no_neighbour(void)
{
    uint8_t *p = (uint8_t *)protocore_arena_persist_alloc(&a, 13);
    uint8_t *q = (uint8_t *)protocore_arena_persist_alloc(&a, 16);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NOT_NULL(q);
    memset(q, 0x5A, 16);
    memset(p, 0xC3, padded(13));
    for (unsigned i = 0; i < 16u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x5Au, q[i]);
    }
}

// Ownership is an address-range test: a pointer inside the region is claimed, one outside is not.
void test_owns_is_an_address_range_test(void)
{
    TEST_ASSERT_TRUE(protocore_arena_owns(&a, a.base));
    TEST_ASSERT_TRUE(protocore_arena_owns(&a, a.base + a.size - 1u));
    TEST_ASSERT_FALSE(protocore_arena_owns(&a, a.base + a.size));
    static uint8_t elsewhere[8];
    TEST_ASSERT_FALSE(protocore_arena_owns(&a, elsewhere));
}

// A zero-length region refuses every request instead of underflowing its own size arithmetic.
void test_a_zero_length_region_refuses_everything(void)
{
    protocore_arena z;
    protocore_arena_init(&z, g_buf, 0);
    TEST_ASSERT_EQUAL_size_t(0, protocore_arena_free_bytes(&z));
    TEST_ASSERT_NULL(protocore_arena_persist_alloc(&z, 16));
    TEST_ASSERT_NULL(protocore_arena_scratch_alloc(&z, 16));
}

// A zero-size request still yields a usable, distinct pointer rather than NULL.
void test_a_zero_size_request_still_yields_a_pointer(void)
{
    void *p = protocore_arena_persist_alloc(&a, 0);
    void *s = protocore_arena_scratch_alloc(&a, 0);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(p != s);
}

// ---- the multi-region set --------------------------------------------------

static uint8_t g_r0[512];  // the preferred region
static uint8_t g_r1[2048]; // the extension

static void set_of_two(protocore_arena_set *s)
{
    protocore_arena_set_init(s);
    TEST_ASSERT_TRUE(protocore_arena_set_add(s, g_r0, sizeof(g_r0)));
    TEST_ASSERT_TRUE(protocore_arena_set_add(s, g_r1, sizeof(g_r1)));
}

// The set holds PROTOCORE_ARENA_MAX_REGIONS regions and refuses a region too small to hold a block.
void test_set_add_limits(void)
{
    protocore_arena_set s;
    set_of_two(&s);
    static uint8_t extra[256];
    TEST_ASSERT_FALSE(protocore_arena_set_add(&s, extra, sizeof(extra)));

    protocore_arena_set t;
    protocore_arena_set_init(&t);
    static uint8_t tiny[4];
    TEST_ASSERT_FALSE(protocore_arena_set_add(&t, tiny, sizeof(tiny)));
}

// Regions are searched in the order they were added, so a request that fits the first lands there
// and only the overflow spills into the second.
void test_set_prefers_the_first_region_and_spills_to_the_second(void)
{
    protocore_arena_set s;
    set_of_two(&s);
    void *big = protocore_arena_set_persist_alloc(&s, 700); // wider than r0
    TEST_ASSERT_NOT_NULL(big);
    TEST_ASSERT_TRUE(inside(big, g_r1, sizeof(g_r1)));
    void *small = protocore_arena_set_persist_alloc(&s, 32);
    TEST_ASSERT_NOT_NULL(small);
    TEST_ASSERT_TRUE(inside(small, g_r0, sizeof(g_r0)));
    TEST_ASSERT_TRUE(protocore_arena_set_persist_used(&s) >= 732);
}

// A free is routed to the region that owns the address; a pointer belonging to neither is ignored.
void test_set_free_routes_by_address(void)
{
    protocore_arena_set s;
    set_of_two(&s);
    void *in_r1 = protocore_arena_set_persist_alloc(&s, 700);
    TEST_ASSERT_TRUE(inside(in_r1, g_r1, sizeof(g_r1)));
    protocore_arena_set_persist_free(&s, in_r1);
    TEST_ASSERT_EQUAL_size_t(0, protocore_arena_set_persist_used(&s));

    protocore_arena_set_persist_free(&s, NULL);
    static uint8_t stray[8];
    protocore_arena_set_persist_free(&s, stray);
    TEST_ASSERT_EQUAL_size_t(0, protocore_arena_set_persist_used(&s));
}

// One mark covers every region, so releasing it unwinds a spill into the second region too.
void test_set_mark_release_spans_every_region(void)
{
    protocore_arena_set s;
    set_of_two(&s);
    void *a0 = protocore_arena_set_scratch_alloc(&s, 400);
    TEST_ASSERT_TRUE(inside(a0, g_r0, sizeof(g_r0)));
    protocore_arena_mark mk = protocore_arena_set_scratch_mark(&s);
    void *a1 = protocore_arena_set_scratch_alloc(&s, 900); // r0 is full, so this spills
    TEST_ASSERT_TRUE(inside(a1, g_r1, sizeof(g_r1)));
    TEST_ASSERT_TRUE(protocore_arena_set_scratch_used(&s) >= 1300);

    protocore_arena_set_scratch_release(&s, &mk);
    TEST_ASSERT_TRUE(protocore_arena_set_scratch_used(&s) >= 400);
    TEST_ASSERT_TRUE(protocore_arena_set_scratch_used(&s) < 900);
    protocore_arena_set_scratch_reset(&s);
    TEST_ASSERT_EQUAL_size_t(0, protocore_arena_set_scratch_used(&s));
}

// A mark captured before a region joined covers only the regions it saw, and restores those.
void test_set_release_of_a_mark_taken_before_a_region_joined(void)
{
    protocore_arena_set s;
    protocore_arena_set_init(&s);
    TEST_ASSERT_TRUE(protocore_arena_set_add(&s, g_r0, sizeof(g_r0)));
    protocore_arena_mark mk = protocore_arena_set_scratch_mark(&s);
    TEST_ASSERT_TRUE(protocore_arena_set_add(&s, g_r1, sizeof(g_r1)));
    void *p = protocore_arena_set_scratch_alloc(&s, 100);
    TEST_ASSERT_TRUE(inside(p, g_r0, sizeof(g_r0)));
    protocore_arena_set_scratch_release(&s, &mk);
    TEST_ASSERT_EQUAL_size_t(0, protocore_arena_set_scratch_used(&s));
}

// A request larger than any single region fails across the whole set, and the free report shrinks
// by what a successful one took.
void test_set_exhaustion_and_free_bytes(void)
{
    protocore_arena_set s;
    set_of_two(&s);
    TEST_ASSERT_NULL(protocore_arena_set_persist_alloc(&s, 100000));
    TEST_ASSERT_NULL(protocore_arena_set_scratch_alloc(&s, 100000));

    size_t before = protocore_arena_set_free_bytes(&s);
    TEST_ASSERT_TRUE(before > 0);
    TEST_ASSERT_NOT_NULL(protocore_arena_set_persist_alloc(&s, 128));
    TEST_ASSERT_TRUE(protocore_arena_set_free_bytes(&s) < before);
}
