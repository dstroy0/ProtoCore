// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the plaintext pool accessor (mmgr/plaintext.h).
//
// No published standard governs a per-dispatch scratch pool, so every expectation here is a
// PROPERTY of the model the header states: a borrow is aligned and inside the pool, the reset
// empties it in one step and the same base comes back, a mark restores usage exactly, an
// over-budget request returns NULL without moving the cursor, and a nested mark unwinds LIFO.
//
// test_the_two_pools_are_disjoint_regions is the load-bearing one, and it is a security property
// rather than an allocator one. Ownership is answered by an address range, and the plaintext and
// secret pools are separate regions: that is what stops a key-material borrow from being accepted
// where a plaintext one is required, and the reverse. An ownership test written as bookkeeping
// instead of as an address bound would let one pass for the other.

#include "mmgr/arena/arena.h" // protocore_worker_set_self - the slot identity a borrow resolves through
#include "mmgr/plaintext/plaintext.h"
#include "mmgr/secure/secure.h" // the other pool, for the disjointness property

#include <unity.h>

void setUp(void)
{
    protocore_plaintext_reset(); // every case starts from an empty arena
}

void tearDown(void)
{
}

// ---- the high-water mark, before anything has borrowed ----------------------

// Must be the first case in this file: the runner is generated in source order, the peak starts
// BSS-zeroed, and no reset ever lowers it. This is the only point in the process where it can be
// observed at zero.
void test_the_high_water_mark_starts_at_zero(void)
{
    TEST_ASSERT_EQUAL_size_t(0, plain.high_water());
}

// ---- borrowing -------------------------------------------------------------

// A borrow is non-null while there is room, and it advances the usage report.
void test_a_borrow_advances_the_usage_report(void)
{
    TEST_ASSERT_EQUAL_size_t(0, plain.used());
    void *p = plain.alloc(16, 1);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(plain.used() >= 16);
}

// Two borrows are distinct and do not overlap. The DIRECTION is not part of the contract: this end
// of the arena bumps down, so asserting an ascending order would assert an accident.
void test_two_borrows_never_overlap(void)
{
    uint8_t *a = (uint8_t *)plain.alloc(8, 1);
    uint8_t *b = (uint8_t *)plain.alloc(8, 1);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    const uint8_t *lo = (a < b) ? a : b;
    const uint8_t *hi = (a < b) ? b : a;
    TEST_ASSERT_TRUE(hi >= lo + 8);
    TEST_ASSERT_TRUE(plain.used() >= 16);
}

// The requested alignment is honored across the range the arena underneath supports, 0 selects the
// platform default, and a request above PROTOCORE_ARENA_MAX_ALIGN is clamped to it rather than
// refused - the region base only guarantees that much.
void test_the_requested_alignment_is_honored(void)
{
    plain.alloc(1, 1); // bump to an odd offset first, so alignment has work to do
    void *p8 = plain.alloc(8, 8);
    void *p16 = plain.alloc(8, PROTOCORE_ARENA_MAX_ALIGN);
    void *over = plain.alloc(8, PROTOCORE_ARENA_MAX_ALIGN * 2u);
    TEST_ASSERT_NOT_NULL(p8);
    TEST_ASSERT_NOT_NULL(p16);
    TEST_ASSERT_NOT_NULL(over);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)p8 % 8u);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)p16 % PROTOCORE_ARENA_MAX_ALIGN);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)over % PROTOCORE_ARENA_MAX_ALIGN);

    void *def = plain.alloc(16, 0);
    TEST_ASSERT_NOT_NULL(def);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)def % PROTOCORE_ARENA_ALIGN);
}

// A zero-size request still yields a usable pointer while there is room.
void test_a_zero_size_borrow_is_not_a_failure(void)
{
    TEST_ASSERT_NOT_NULL(plain.alloc(0, 1));
}

// ---- the reset -------------------------------------------------------------

// The reset empties the arena and the same base comes back, which is what makes it O(1) and what
// makes a borrow valid only until the next dispatch.
void test_the_reset_empties_the_arena_and_reuses_the_base(void)
{
    void *first = plain.alloc(32, 1);
    TEST_ASSERT_NOT_NULL(first);
    plain.reset();
    TEST_ASSERT_EQUAL_size_t(0, plain.used());
    TEST_ASSERT_EQUAL_PTR(first, plain.alloc(32, 1));
}

// ---- exhaustion ------------------------------------------------------------

// A request for exactly the capacity succeeds; one byte more fails closed and leaves the cursor
// where it was. The arena is usable again after a reset.
void test_exhaustion_fails_closed_without_moving_the_cursor(void)
{
    size_t cap = plain.capacity();
    TEST_ASSERT_NOT_NULL(plain.alloc(cap, 1));
    TEST_ASSERT_EQUAL_size_t(cap, plain.used());

    TEST_ASSERT_NULL(plain.alloc(1, 1));
    TEST_ASSERT_EQUAL_size_t(cap, plain.used());

    plain.reset();
    TEST_ASSERT_NOT_NULL(plain.alloc(1, 1));
}

// A request wider than the whole arena is refused without taking anything.
void test_a_request_wider_than_the_arena_is_refused(void)
{
    TEST_ASSERT_NULL(plain.alloc(plain.capacity() + 1u, 1));
    TEST_ASSERT_EQUAL_size_t(0, plain.used());
}

// Alignment padding is inside the bound: rounding a base up must not push a borrow past the end.
void test_alignment_padding_cannot_run_past_the_end(void)
{
    TEST_ASSERT_NOT_NULL(plain.alloc(plain.capacity() - 1u, 1));
    TEST_ASSERT_NULL(plain.alloc(1, 64));
}

// The peak is bounded below by the current usage and above by the arena.
void test_the_high_water_mark_is_bounded_by_the_arena(void)
{
    plain.alloc(50, 1);
    TEST_ASSERT_TRUE(plain.high_water() >= plain.used());
    TEST_ASSERT_TRUE(plain.high_water() <= plain.capacity());
}

// ---- marks -----------------------------------------------------------------

// A release restores usage to exactly what it was when the mark was taken, and the same space is
// handed out again.
void test_a_release_restores_the_usage_at_the_mark(void)
{
    plain.alloc(100, 1);
    const size_t at_mark = plain.used();
    const size_t mark = plain.mark();
    void *inner = plain.alloc(200, 1);
    TEST_ASSERT_NOT_NULL(inner);
    TEST_ASSERT_TRUE(plain.used() >= at_mark + 200);

    plain.release(mark);
    TEST_ASSERT_EQUAL_size_t(at_mark, plain.used());
    TEST_ASSERT_EQUAL_PTR(inner, plain.alloc(200, 1)); // the same space, reused
}

// Nested marks unwind innermost first, and the outer one is untouched until it is released.
void test_nested_marks_unwind_innermost_first(void)
{
    const size_t outer = plain.mark();
    plain.alloc(100, 1);
    const size_t after_outer = plain.used();

    const size_t inner = plain.mark();
    plain.alloc(100, 1);
    TEST_ASSERT_TRUE(plain.used() > after_outer);
    plain.release(inner);
    TEST_ASSERT_EQUAL_size_t(after_outer, plain.used());

    plain.release(outer);
    TEST_ASSERT_EQUAL_size_t(0, plain.used());
}

// Borrow-then-release in a loop keeps the peak at one borrow however many iterations run, which is
// what stops a busy connection from creeping the arena to exhaustion.
void test_repeated_scopes_do_not_accumulate(void)
{
    for (int k = 0; k < 100; k++)
    {
        const size_t it = plain.mark();
        void *p = plain.alloc(2048, 16);
        TEST_ASSERT_NOT_NULL(p);
        plain.release(it);
    }
    TEST_ASSERT_EQUAL_size_t(0, plain.used());
}

// ---- ownership -------------------------------------------------------------

// The plaintext and secret pools are separate regions, so each answers only for its own bytes. A
// borrow from one is never claimed by the other, and NULL is claimed by neither.
void test_the_two_pools_are_disjoint_regions(void)
{
    void *mine = plain.alloc(64, 8);
    TEST_ASSERT_NOT_NULL(mine);
    TEST_ASSERT_TRUE(plain.owns(mine));
    TEST_ASSERT_FALSE(protocore_secure_owns(mine));
    TEST_ASSERT_EQUAL_INT(-1, protocore_secure_slot_of(mine));

    const protocore_span secret = protocore_secure_span(32, 8);
    TEST_ASSERT_TRUE(span.ok(secret));
    TEST_ASSERT_FALSE(plain.owns(secret.buf));
    TEST_ASSERT_EQUAL_INT(-1, plain.slot_of(secret.buf));
    TEST_ASSERT_TRUE(protocore_secure_owns(secret.buf));
    protocore_secure_reset();

    TEST_ASSERT_FALSE(plain.owns(NULL));
    TEST_ASSERT_EQUAL_INT(-1, plain.slot_of(NULL));
}

// A borrow comes from the caller's own slot, which is what makes the pool lock-free without a lock.
void test_a_borrow_comes_from_the_callers_slot(void)
{
    void *own = plain.alloc(8, 1);
    TEST_ASSERT_NOT_NULL(own);
    TEST_ASSERT_EQUAL_INT(protocore_worker_self(), plain.slot_of(own));
    TEST_ASSERT_TRUE(plain.slot_of(own) >= 0);
    TEST_ASSERT_TRUE(plain.slot_of(own) < PROTOCORE_REG_POOL_SLOTS);
}

// ---- the span form ---------------------------------------------------------

// One argument sets both the pointer and the capacity, so the run length is stated once and cannot
// drift from the allocation.
void test_the_span_form_binds_the_length_to_the_borrow(void)
{
    protocore_span s = plain.span(48, 8);
    TEST_ASSERT_TRUE(span.ok(s));
    TEST_ASSERT_EQUAL_size_t(48, s.cap);
    TEST_ASSERT_EQUAL_size_t(0, s.pos);
    TEST_ASSERT_TRUE(plain.owns(s.buf));
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)s.buf % 8u);
}

// An over-budget span is {NULL, 0}, never a null pointer carrying a live capacity: a caller that
// skips the check then writes nothing instead of dereferencing null.
void test_an_over_budget_span_is_empty_not_null_with_capacity(void)
{
    const protocore_span too_big = plain.span(plain.capacity() * 4u, 8);
    TEST_ASSERT_FALSE(span.ok(too_big));
    TEST_ASSERT_NULL(too_big.buf);
    TEST_ASSERT_EQUAL_size_t(0, too_big.cap);
}

// ---- the persistent end ----------------------------------------------------

// A persistent borrow grows from the other end of the same arena, so the per-dispatch reset does
// not reach it, and it comes back zeroed.
void test_a_persistent_borrow_survives_the_reset(void)
{
    protocore_span keep = plain.persist(64);
    TEST_ASSERT_TRUE(span.ok(keep));
    TEST_ASSERT_EQUAL_size_t(64, keep.cap);
    for (unsigned i = 0; i < 64u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00u, keep.buf[i]);
    }
    keep.buf[0] = 0x5A;

    plain.reset();
    TEST_ASSERT_EQUAL_size_t(0, plain.used()); // the transient end is empty
    TEST_ASSERT_EQUAL_HEX8(0x5Au, keep.buf[0]);
    TEST_ASSERT_TRUE(plain.owns(keep.buf));

    // A transient borrow after the reset does not land on the persistent one.
    uint8_t *t = (uint8_t *)plain.alloc(64, 8);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_TRUE(t >= keep.buf + 64 || t + 64 <= keep.buf);
}

// ---- the table -------------------------------------------------------------

// Four members are size_t(void), so a swapped pair type-checks and links; only identity catches it.
// The table is initialized in the header, which is what keeps --gc-sections able to reclaim the
// pool storage from a build that resets but never borrows.
void test_the_table_names_the_functions_it_claims_to(void)
{
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_alloc, plain.alloc);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_span, plain.span);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_persist_span, plain.persist);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_reset, plain.reset);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_mark, plain.mark);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_release, plain.release);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_used, plain.used);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_high_water, plain.high_water);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_capacity, plain.capacity);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_owns, plain.owns);
    TEST_ASSERT_EQUAL_PTR(protocore_plaintext_slot_of, plain.slot_of);
    TEST_ASSERT_EQUAL_PTR(&protocore_plaintext_internal, plain.internal);
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PLAINTEXT_ARENA_SIZE, plain.capacity());
}
