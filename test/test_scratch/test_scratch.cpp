// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the shared per-dispatch scratch arena (session/scratch): bump
// allocation, alignment, the reset contract, and fail-closed exhaustion. Pure
// host tests - no sockets, no FreeRTOS (the owner-task tripwire is ESP32-only).

#include "network_drivers/session/worker.h"
#include "server/mmgr/scratch.h"
#include <stdint.h>
#include <unity.h>

void setUp()
{
    scratch_reset(); // every test starts from an empty arena
}
void tearDown()
{
}

// ---------------------------------------------------------------------------

// Must run before any other test in this binary (see its RUN_TEST placement in
// main()): scratch_high_water()'s internal peak starts BSS-zeroed and is never
// reset by scratch_reset(), so this is the only point in the process where the
// "not a new peak" branch of its per-slot loop (peak stays 0) can be observed -
// every later test has already pushed a worker's high-water mark above zero.
void test_high_water_starts_at_zero()
{
    TEST_ASSERT_EQUAL_size_t(0, scratch_high_water());
}

void test_alloc_returns_nonnull_and_advances_used()
{
    TEST_ASSERT_EQUAL_size_t(0, scratch_used());
    void *p = scratch_alloc(16, 1);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(16, scratch_used());
}

void test_sequential_allocs_are_distinct_and_non_overlapping()
{
    uint8_t *a = (uint8_t *)scratch_alloc(8, 1);
    uint8_t *b = (uint8_t *)scratch_alloc(8, 1);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    // Distinct and non-overlapping is the guarantee; the DIRECTION is not. The pool's scratch end
    // grows down (it descends to meet the persistent end ascending), so b < a here - asserting
    // ascending order would be asserting an accident of the allocator this replaced.
    const uint8_t *lo = (a < b) ? a : b;
    const uint8_t *hi = (a < b) ? b : a;
    TEST_ASSERT_TRUE(hi >= lo + 8);
    // Usage is at least what was asked for; the pool rounds each request up to its granularity.
    TEST_ASSERT_TRUE(scratch_used() >= 16);
}

void test_reset_frees_all_and_reuses_base()
{
    void *first = scratch_alloc(32, 1);
    scratch_reset();
    TEST_ASSERT_EQUAL_size_t(0, scratch_used());
    void *again = scratch_alloc(32, 1);
    TEST_ASSERT_EQUAL_PTR(first, again); // same base reused after reset
}

void test_alignment_is_honored()
{
    scratch_alloc(1, 1); // bump to an odd offset first
    uint8_t *p16 = (uint8_t *)scratch_alloc(8, 16);
    TEST_ASSERT_NOT_NULL(p16);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)p16 % 16);
    uint8_t *p32 = (uint8_t *)scratch_alloc(8, 32);
    TEST_ASSERT_NOT_NULL(p32);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)p32 % 32);
}

void test_exhaustion_returns_null_without_corrupting_arena()
{
    size_t cap = scratch_capacity();
    void *whole = scratch_alloc(cap, 1);
    TEST_ASSERT_NOT_NULL(whole); // an exactly-full request succeeds
    TEST_ASSERT_EQUAL_size_t(cap, scratch_used());
    void *over = scratch_alloc(1, 1);
    TEST_ASSERT_NULL(over);                        // one more byte fails closed
    TEST_ASSERT_EQUAL_size_t(cap, scratch_used()); // the failed alloc did not advance
    scratch_reset();
    TEST_ASSERT_NOT_NULL(scratch_alloc(1, 1)); // arena usable again after reset
}

void test_alloc_larger_than_capacity_returns_null()
{
    TEST_ASSERT_NULL(scratch_alloc(scratch_capacity() + 1, 1));
    TEST_ASSERT_EQUAL_size_t(0, scratch_used());
}

void test_alignment_padding_cannot_overflow_arena()
{
    // Fill to one byte below capacity, then a large-alignment request whose
    // rounding would push the base past the end must fail closed (not wrap).
    void *bulk = scratch_alloc(scratch_capacity() - 1, 1);
    TEST_ASSERT_NOT_NULL(bulk);
    TEST_ASSERT_NULL(scratch_alloc(1, 64));
}

void test_high_water_bounds()
{
    scratch_alloc(50, 1);
    TEST_ASSERT_TRUE(scratch_high_water() >= scratch_used());     // peak >= current
    TEST_ASSERT_TRUE(scratch_high_water() <= scratch_capacity()); // peak never exceeds the arena
}

void test_zero_size_alloc_returns_nonnull_when_space()
{
    TEST_ASSERT_NOT_NULL(scratch_alloc(0, 1));
}

void test_mark_release_reclaims()
{
    scratch_alloc(100, 1);
    const size_t after_first = scratch_used(); // >= 100; the pool rounds to its granularity
    size_t mark = scratch_mark();
    scratch_alloc(200, 1);
    TEST_ASSERT_TRUE(scratch_used() >= after_first + 200);
    scratch_release(mark);
    // The guarantee: release restores usage to exactly where the mark was taken.
    TEST_ASSERT_EQUAL_size_t(after_first, scratch_used());
}

void test_release_allows_reuse_of_same_region()
{
    size_t mark = scratch_mark();
    void *a = scratch_alloc(64, 1);
    scratch_release(mark);
    void *b = scratch_alloc(64, 1);
    TEST_ASSERT_EQUAL_PTR(a, b); // same space reused after release
}

void test_scratch_scope_releases_on_scope_exit()
{
    scratch_alloc(100, 1);
    const size_t outside = scratch_used();
    {
        ScratchScope ss;
        scratch_alloc(500, 1);
        TEST_ASSERT_TRUE(scratch_used() >= outside + 500);
    } // ss destructs -> release
    TEST_ASSERT_EQUAL_size_t(outside, scratch_used());
}

void test_nested_scopes_reclaim_lifo()
{
    ScratchScope outer;
    scratch_alloc(100, 1);
    const size_t after_outer = scratch_used();
    {
        ScratchScope inner;
        scratch_alloc(100, 1);
        TEST_ASSERT_TRUE(scratch_used() > after_outer);
    }
    TEST_ASSERT_EQUAL_size_t(after_outer, scratch_used());
}

void test_sequential_scopes_do_not_accumulate()
{
    // Mirrors ssh_pkt_recv's multi-packet loop: each iteration borrows then
    // releases, so the peak stays at one borrow regardless of iteration count -
    // the property that keeps a busy connection from exhausting the arena.
    for (int k = 0; k < 100; k++)
    {
        ScratchScope ss;
        void *p = scratch_alloc(2048, 16);
        TEST_ASSERT_NOT_NULL(p);
    }
    TEST_ASSERT_EQUAL_size_t(0, scratch_used());
}

// cur_worker() clamps an out-of-range worker id back to slot 0 (in this env
// PC_SCRATCH_SLOTS == PC_WORKER_COUNT == 1, so 0 is the only valid id) so a
// stray id can never index the arena out of bounds - exercise both the
// negative and the too-large side of that clamp.
void test_cur_worker_clamps_out_of_range_ids()
{
    scratch_reset();
    void *base = scratch_alloc(8, 1); // record worker 0's base while self == 0
    TEST_ASSERT_NOT_NULL(base);
    scratch_reset();

    pc_worker_set_self(-1); // w >= 0 half of the clamp condition fails
    TEST_ASSERT_EQUAL_size_t(0, scratch_used());
    void *neg = scratch_alloc(8, 1);
    TEST_ASSERT_EQUAL_PTR(base, neg); // still resolves to arena 0
    scratch_reset();

    pc_worker_set_self(PC_SCRATCH_SLOTS); // w < PC_SCRATCH_SLOTS half fails
    TEST_ASSERT_EQUAL_size_t(0, scratch_used());
    void *big = scratch_alloc(8, 1);
    TEST_ASSERT_EQUAL_PTR(base, big); // still resolves to arena 0

    pc_worker_set_self(0); // restore identity for every later test
    scratch_reset();
}

// align == 0 falls back to the default alignment.
void test_zero_align_uses_default()
{
    scratch_reset();
    void *p = scratch_alloc(16, 0);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)p % 8); // default alignment is at least 8
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_high_water_starts_at_zero); // must run first - see comment above the test
    RUN_TEST(test_zero_align_uses_default);
    RUN_TEST(test_alloc_returns_nonnull_and_advances_used);
    RUN_TEST(test_sequential_allocs_are_distinct_and_non_overlapping);
    RUN_TEST(test_reset_frees_all_and_reuses_base);
    RUN_TEST(test_alignment_is_honored);
    RUN_TEST(test_exhaustion_returns_null_without_corrupting_arena);
    RUN_TEST(test_alloc_larger_than_capacity_returns_null);
    RUN_TEST(test_alignment_padding_cannot_overflow_arena);
    RUN_TEST(test_high_water_bounds);
    RUN_TEST(test_zero_size_alloc_returns_nonnull_when_space);
    RUN_TEST(test_mark_release_reclaims);
    RUN_TEST(test_release_allows_reuse_of_same_region);
    RUN_TEST(test_scratch_scope_releases_on_scope_exit);
    RUN_TEST(test_nested_scopes_reclaim_lifo);
    RUN_TEST(test_sequential_scopes_do_not_accumulate);
    RUN_TEST(test_cur_worker_clamps_out_of_range_ids);
    return UNITY_END();
}
