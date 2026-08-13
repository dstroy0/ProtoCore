// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the plaintext pool accessor (mmgr/plaintext): bump
// allocation, alignment, the reset contract, and fail-closed exhaustion. Pure
// host tests - no sockets, no FreeRTOS (the owner-task tripwire is ESP32-only).

#include "mmgr/arena.h" // protocore_worker_set_self()
#include "mmgr/plaintext.h"
#include "mmgr/secure.h"
#include "server/core/worker.h"
#include <stdint.h>
#include <unity.h>

void setUp()
{
    protocore_plaintext_reset(); // every test starts from an empty arena
}
void tearDown()
{
}

// ---------------------------------------------------------------------------

// Must run before any other test in this binary (see its RUN_TEST placement in
// main()): protocore_plaintext_high_water()'s internal peak starts BSS-zeroed and is never
// reset by protocore_plaintext_reset(), so this is the only point in the process where the
// "not a new peak" branch of its per-slot loop (peak stays 0) can be observed -
// every later test has already pushed a worker's high-water mark above zero.
void test_high_water_starts_at_zero()
{
    TEST_ASSERT_EQUAL_size_t(0, protocore_plaintext_high_water());
}

void test_alloc_returns_nonnull_and_advances_used()
{
    TEST_ASSERT_EQUAL_size_t(0, protocore_plaintext_used());
    void *p = protocore_plaintext_alloc(16, 1);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(16, protocore_plaintext_used());
}

void test_sequential_allocs_are_distinct_and_non_overlapping()
{
    uint8_t *a = (uint8_t *)protocore_plaintext_alloc(8, 1);
    uint8_t *b = (uint8_t *)protocore_plaintext_alloc(8, 1);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    // Distinct and non-overlapping is the guarantee; the DIRECTION is not. The pool's scratch end
    // grows down (it descends to meet the persistent end ascending), so b < a here - asserting
    // ascending order would be asserting an accident of the allocator this replaced.
    const uint8_t *lo = (a < b) ? a : b;
    const uint8_t *hi = (a < b) ? b : a;
    TEST_ASSERT_TRUE(hi >= lo + 8);
    // Usage is at least what was asked for; the pool rounds each request up to its granularity.
    TEST_ASSERT_TRUE(protocore_plaintext_used() >= 16);
}

void test_reset_frees_all_and_reuses_base()
{
    void *first = protocore_plaintext_alloc(32, 1);
    protocore_plaintext_reset();
    TEST_ASSERT_EQUAL_size_t(0, protocore_plaintext_used());
    void *again = protocore_plaintext_alloc(32, 1);
    TEST_ASSERT_EQUAL_PTR(first, again); // same base reused after reset
}

void test_alignment_is_honored()
{
    protocore_plaintext_alloc(1, 1); // bump to an odd offset first
    uint8_t *p16 = (uint8_t *)protocore_plaintext_alloc(8, 16);
    TEST_ASSERT_NOT_NULL(p16);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)p16 % 16);
    uint8_t *p32 = (uint8_t *)protocore_plaintext_alloc(8, 32);
    TEST_ASSERT_NOT_NULL(p32);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)p32 % 32);
}

void test_exhaustion_returns_null_without_corrupting_arena()
{
    size_t cap = protocore_plaintext_capacity();
    void *whole = protocore_plaintext_alloc(cap, 1);
    TEST_ASSERT_NOT_NULL(whole); // an exactly-full request succeeds
    TEST_ASSERT_EQUAL_size_t(cap, protocore_plaintext_used());
    void *over = protocore_plaintext_alloc(1, 1);
    TEST_ASSERT_NULL(over);                             // one more byte fails closed
    TEST_ASSERT_EQUAL_size_t(cap, protocore_plaintext_used()); // the failed alloc did not advance
    protocore_plaintext_reset();
    TEST_ASSERT_NOT_NULL(protocore_plaintext_alloc(1, 1)); // arena usable again after reset
}

void test_alloc_larger_than_capacity_returns_null()
{
    TEST_ASSERT_NULL(protocore_plaintext_alloc(protocore_plaintext_capacity() + 1, 1));
    TEST_ASSERT_EQUAL_size_t(0, protocore_plaintext_used());
}

void test_alignment_padding_cannot_overflow_arena()
{
    // Fill to one byte below capacity, then a large-alignment request whose
    // rounding would push the base past the end must fail closed (not wrap).
    void *bulk = protocore_plaintext_alloc(protocore_plaintext_capacity() - 1, 1);
    TEST_ASSERT_NOT_NULL(bulk);
    TEST_ASSERT_NULL(protocore_plaintext_alloc(1, 64));
}

void test_high_water_bounds()
{
    protocore_plaintext_alloc(50, 1);
    TEST_ASSERT_TRUE(protocore_plaintext_high_water() >= protocore_plaintext_used());     // peak >= current
    TEST_ASSERT_TRUE(protocore_plaintext_high_water() <= protocore_plaintext_capacity()); // peak never exceeds the arena
}

void test_zero_size_alloc_returns_nonnull_when_space()
{
    TEST_ASSERT_NOT_NULL(protocore_plaintext_alloc(0, 1));
}

void test_mark_release_reclaims()
{
    protocore_plaintext_alloc(100, 1);
    const size_t after_first = protocore_plaintext_used(); // >= 100; the pool rounds to its granularity
    size_t mark = protocore_plaintext_mark();
    protocore_plaintext_alloc(200, 1);
    TEST_ASSERT_TRUE(protocore_plaintext_used() >= after_first + 200);
    protocore_plaintext_release(mark);
    // The guarantee: release restores usage to exactly where the mark was taken.
    TEST_ASSERT_EQUAL_size_t(after_first, protocore_plaintext_used());
}

void test_release_allows_reuse_of_same_region()
{
    size_t mark = protocore_plaintext_mark();
    void *a = protocore_plaintext_alloc(64, 1);
    protocore_plaintext_release(mark);
    void *b = protocore_plaintext_alloc(64, 1);
    TEST_ASSERT_EQUAL_PTR(a, b); // same space reused after release
}

void test_plaintext_scope_releases_on_scope_exit()
{
    protocore_plaintext_alloc(100, 1);
    const size_t outside = protocore_plaintext_used();
    {
        size_t inner = protocore_plaintext_mark();
        protocore_plaintext_alloc(500, 1);
        TEST_ASSERT_TRUE(protocore_plaintext_used() >= outside + 500);
        protocore_plaintext_release(inner);
    }
    TEST_ASSERT_EQUAL_size_t(outside, protocore_plaintext_used());
}

void test_nested_scopes_reclaim_lifo()
{
    size_t outer = protocore_plaintext_mark();
    protocore_plaintext_alloc(100, 1);
    const size_t after_outer = protocore_plaintext_used();
    {
        size_t inner = protocore_plaintext_mark();
        protocore_plaintext_alloc(100, 1);
        TEST_ASSERT_TRUE(protocore_plaintext_used() > after_outer);
        protocore_plaintext_release(inner);
    }
    TEST_ASSERT_EQUAL_size_t(after_outer, protocore_plaintext_used()); // inner reclaimed, outer intact
    protocore_plaintext_release(outer);
    TEST_ASSERT_EQUAL_size_t(0, protocore_plaintext_used());
}

void test_sequential_scopes_do_not_accumulate()
{
    // Mirrors ssh_pkt_recv's multi-packet loop: each iteration borrows then
    // releases, so the peak stays at one borrow regardless of iteration count -
    // the property that keeps a busy connection from exhausting the arena.
    for (int k = 0; k < 100; k++)
    {
        size_t it = protocore_plaintext_mark();
        void *p = protocore_plaintext_alloc(2048, 16);
        TEST_ASSERT_NOT_NULL(p);
        protocore_plaintext_release(it);
    }
    TEST_ASSERT_EQUAL_size_t(0, protocore_plaintext_used());
}

// A borrow comes from the caller's OWN slot, which is what makes it lock-free, and a caller that is
// not a server worker gets the ghost rather than colliding with worker 0.
//
// The out-of-range half is only observable at PROTOCORE_WORKER_COUNT > 1 (native_pool_workers). Below
// that, protocore_worker_self() is an inline compile-time 0 (worker.h) that protocore_worker_set_self() cannot
// reach, so every borrow lands on slot 0 no matter what the accessor decides - asserting anything
// about a stray id in a single-worker build asserts the constant, not the code.
void test_borrow_comes_from_the_callers_slot()
{
    protocore_plaintext_reset();
    void *own = protocore_plaintext_alloc(8, 1);
    TEST_ASSERT_NOT_NULL(own);
    TEST_ASSERT_EQUAL_INT(0, protocore_plaintext_slot_of(own));
    protocore_plaintext_reset();

#if PROTOCORE_WORKER_COUNT > 1
    protocore_worker_set_self(1);
    void *w1 = protocore_plaintext_alloc(8, 1);
    TEST_ASSERT_EQUAL_INT(1, protocore_plaintext_slot_of(w1)); // its own slot, not worker 0's
    protocore_plaintext_reset();                               // while still bound to 1: reset is per-slot

    protocore_worker_set_self(-1); // the w >= 0 half of the range test fails
    TEST_ASSERT_EQUAL_size_t(0, protocore_plaintext_used());
    void *neg = protocore_plaintext_alloc(8, 1);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_GHOST_WORKER_SLOT, protocore_plaintext_slot_of(neg));
    protocore_plaintext_reset();

    protocore_worker_set_self(PROTOCORE_REG_POOL_SLOTS); // the w < PROTOCORE_REG_POOL_SLOTS half fails
    TEST_ASSERT_EQUAL_size_t(0, protocore_plaintext_used());
    void *big = protocore_plaintext_alloc(8, 1);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_GHOST_WORKER_SLOT, protocore_plaintext_slot_of(big));
    protocore_plaintext_reset(); // empties the ghost before the identity goes back to 0

    protocore_worker_set_self(0); // restore identity for every later test
    protocore_plaintext_reset();
#endif
}

// align == 0 falls back to the default alignment.
void test_zero_align_uses_default()
{
    protocore_plaintext_reset();
    void *p = protocore_plaintext_alloc(16, 0);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)p % 8); // default alignment is at least 8
}

// ---------------------------------------------------------------------------
// The `plain` table names ten functions, and four of them (used, mark,
// high_water, capacity) are size_t(void) - so a swapped pair type-checks and
// links, and only identity catches it. The table is initialized in the header,
// which is what keeps --gc-sections able to reclaim the pool storage from a
// build that resets but never borrows; a definition in the .c would name every
// member and anchor alloc -> bind -> the backing bytes.
void test_plain_table_is_wired_to_the_named_functions()
{
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_alloc, plain.alloc);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_span, plain.span);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_reset, plain.reset);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_mark, plain.mark);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_release, plain.release);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_used, plain.used);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_high_water, plain.high_water);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_capacity, plain.capacity);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_owns, plain.owns);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_slot_of, plain.slot_of);
}

// The pool reached through the table rather than around it: borrow, own, mark,
// release, and fail closed on an over-budget request.
void test_plain_table_round_trip()
{
    plain.reset();
    TEST_ASSERT_EQUAL_size_t(0, plain.used());
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PLAINTEXT_ARENA_SIZE, plain.capacity());

    void *a = plain.alloc(64, 8);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)a % 8);
    TEST_ASSERT_TRUE(plain.owns(a));
    TEST_ASSERT_TRUE(plain.slot_of(a) >= 0 && plain.slot_of(a) < PROTOCORE_REG_POOL_SLOTS);

    // The foreign pointer is a secure-pool borrow, not a stack address: it is aligned and padded
    // like every borrow, and the two pools are disjoint regions by construction, which is the
    // property being asserted. A stack object would test an accident of the frame instead.
    const protocore_span secret = protocore_secure_span(32, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(secret));
    TEST_ASSERT_FALSE(plain.owns(secret.buf));
    TEST_ASSERT_EQUAL_INT(-1, plain.slot_of(secret.buf));
    TEST_ASSERT_FALSE(plain.owns(NULL));
    protocore_secure_reset();

    // mark() is the arena top and used() is size minus it, so the contract is
    // that release puts used() back, not that the two numbers agree.
    const size_t used_at_mark = plain.used();
    const size_t mark = plain.mark();
    void *b = plain.alloc(32, 8);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(plain.used() >= used_at_mark + 32);
    plain.release(mark);
    TEST_ASSERT_EQUAL_size_t(used_at_mark, plain.used());
    TEST_ASSERT_EQUAL_PTR(b, plain.alloc(32, 8));

    plain.reset();
    protocore_span s = plain.span(48, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(s));
    TEST_ASSERT_EQUAL_size_t(48, s.cap);
    TEST_ASSERT_TRUE(plain.owns(s.buf));

    const protocore_span too_big = plain.span(plain.capacity() * 4u, 8);
    TEST_ASSERT_FALSE(protocore_span_ok(too_big));
    TEST_ASSERT_NULL(too_big.buf);
    TEST_ASSERT_EQUAL_size_t(0, too_big.cap);

    plain.reset();
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
    RUN_TEST(test_plaintext_scope_releases_on_scope_exit);
    RUN_TEST(test_nested_scopes_reclaim_lifo);
    RUN_TEST(test_sequential_scopes_do_not_accumulate);
    RUN_TEST(test_borrow_comes_from_the_callers_slot);
    // Last: the round trip borrows, which moves the high-water mark the tests above bound.
    RUN_TEST(test_plain_table_is_wired_to_the_named_functions);
    RUN_TEST(test_plain_table_round_trip);
    return UNITY_END();
}
