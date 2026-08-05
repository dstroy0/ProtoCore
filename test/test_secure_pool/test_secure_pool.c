// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The secure pool is the SAME mechanism as the plaintext pool instantiated a second time. What is
// under test here is only what differs - the access and control layer:
//
//   1. reclaiming WIPES, and wipes before the bytes become available again, so a secret cannot
//      survive its borrow or be handed to the next tenant
//   2. the two pools are disjoint regions, so owns() separates them by address alone - a secure
//      pointer can never be accepted where a plaintext one is required, or the reverse
//
// The allocator mechanics themselves belong to test_arena; duplicating them here would be testing
// the same code twice.

#include "mmgr/arena.h" // pc_worker_set_self()
#include "mmgr/plaintext.h"
#include "mmgr/secure.h"

#include <string.h> // memset: test/ is exempt from the no-stdlib rule, and it must be DECLARED
#include <unity.h>

void setUp(void)
{
    pc_secure_reset();
}
void tearDown(void)
{
    pc_secure_reset();
}

// --- the control that defines this pool: reclaiming wipes ---

static void test_release_wipes_the_reclaimed_region(void)
{
    uint8_t *key = NULL;
    size_t mark = pc_secure_mark();
    {
        pc_span s = pc_secure_span(32, 8);
        TEST_ASSERT_TRUE(pc_span_ok(s));
        memset(s.buf, 0xA5, s.cap);
        key = s.buf;
        TEST_ASSERT_EQUAL_UINT8(0xA5, key[0]);
        TEST_ASSERT_EQUAL_UINT8(0xA5, key[31]);
    }
    pc_secure_release(mark);

    // The bytes are zero the moment they are reclaimed - not merely marked free.
    for (size_t i = 0; i < 32; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, key[i]);
    }
}

// The regression that matters: the NEXT borrow must never see the previous tenant's key material.
static void test_a_later_borrow_never_sees_the_previous_secret(void)
{
    size_t mark = pc_secure_mark();
    pc_span first = pc_secure_span(64, 8);
    TEST_ASSERT_TRUE(pc_span_ok(first));
    memset(first.buf, 0x5C, first.cap);
    pc_secure_release(mark);

    pc_span second = pc_secure_span(64, 8);
    TEST_ASSERT_TRUE(pc_span_ok(second));
    TEST_ASSERT_EQUAL_PTR(first.buf, second.buf); // same bytes handed back out
    for (size_t i = 0; i < second.cap; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, second.buf[i]); // ...and they are clean
    }
}

static void test_reset_wipes_everything_live(void)
{
    pc_span a = pc_secure_span(48, 8);
    pc_span b = pc_secure_span(48, 8);
    TEST_ASSERT_TRUE(pc_span_ok(a));
    TEST_ASSERT_TRUE(pc_span_ok(b));
    memset(a.buf, 0x11, a.cap);
    memset(b.buf, 0x22, b.cap);
    uint8_t *pa = a.buf;
    uint8_t *pb = b.buf;

    pc_secure_reset();

    for (size_t i = 0; i < 48; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, pa[i]);
        TEST_ASSERT_EQUAL_UINT8(0, pb[i]);
    }
    TEST_ASSERT_EQUAL_size_t(0, pc_secure_used());
}

// Release wipes, and it has to happen on the early-exit paths too - the ones a hand-written wipe
// gets forgotten on. Both trips borrow, scribble key-shaped bytes, and leave by a different route;
// the region must read back zero either way.
static void test_scope_guard_wipes_on_every_exit(void)
{
    uint8_t *seen = NULL;
    for (int trip = 0; trip < 2; trip++)
    {
        size_t scope = pc_secure_mark();
        pc_span s = pc_secure_span(16, 8);
        TEST_ASSERT_TRUE(pc_span_ok(s));
        memset(s.buf, 0xEE, s.cap);
        seen = s.buf;
        if (trip == 0)
        {
            pc_secure_release(scope); // the "peer sent something malformed" shape
            continue;
        }
        pc_secure_release(scope);
    }
    for (size_t i = 0; i < 16; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, seen[i]);
    }
}

static void test_nested_scopes_release_lifo(void)
{
    size_t outer = pc_secure_mark();
    pc_span a = pc_secure_span(32, 8);
    TEST_ASSERT_TRUE(pc_span_ok(a));
    size_t after_a = pc_secure_used();
    {
        size_t inner = pc_secure_mark();
        pc_span b = pc_secure_span(32, 8);
        TEST_ASSERT_TRUE(pc_span_ok(b));
        TEST_ASSERT_TRUE(pc_secure_used() > after_a);
        pc_secure_release(inner);
    }
    TEST_ASSERT_EQUAL_size_t(after_a, pc_secure_used()); // inner reclaimed, outer intact
    TEST_ASSERT_TRUE(pc_span_ok(a));
    pc_secure_release(outer);
}

// --- the other half of the control: the pools are disjoint regions ---

static void test_a_secure_pointer_is_not_a_plaintext_one(void)
{
    pc_span s = pc_secure_span(32, 8);
    TEST_ASSERT_TRUE(pc_span_ok(s));
    TEST_ASSERT_TRUE(pc_secure_owns(s.buf));
    TEST_ASSERT_FALSE(pc_plaintext_owns(s.buf)); // cannot be mistaken for plaintext
}

static void test_a_plaintext_pointer_is_not_a_secure_one(void)
{
    size_t scope = pc_plaintext_mark();
    pc_span s = pc_plaintext_span(32, 8);
    TEST_ASSERT_TRUE(pc_span_ok(s));
    TEST_ASSERT_TRUE(pc_plaintext_owns(s.buf));
    TEST_ASSERT_FALSE(pc_secure_owns(s.buf)); // cannot be mistaken for secure
    pc_plaintext_release(scope);              // setUp resets the secure pool only, so hand this back
}

// Storage outside both pools, aligned the way every borrow is. The probe is not a stack array: a
// frame gives no alignment past 1, so testing with one would exercise a shape no pool pointer can
// have, and the address range test is what is under examination here.
static uint8_t g_outside[32] __attribute__((aligned(32)));

// A pointer from neither pool - other BSS, a code address, null - belongs to neither.
static void test_foreign_pointers_belong_to_neither_pool(void)
{
    TEST_ASSERT_FALSE(pc_secure_owns(g_outside));
    TEST_ASSERT_FALSE(pc_plaintext_owns(g_outside));
    TEST_ASSERT_EQUAL_INT(-1, pc_secure_slot_of(g_outside));
    TEST_ASSERT_EQUAL_INT(-1, pc_plaintext_slot_of(g_outside));

    // A code address is aligned by construction and cannot be in either arena.
    const void *code = (const void *)&pc_secure_reset;
    TEST_ASSERT_FALSE(pc_secure_owns(code));
    TEST_ASSERT_FALSE(pc_plaintext_owns(code));

    TEST_ASSERT_FALSE(pc_secure_owns(NULL));
    TEST_ASSERT_FALSE(pc_plaintext_owns(NULL));
    TEST_ASSERT_EQUAL_INT(-1, pc_secure_slot_of(NULL));
    TEST_ASSERT_EQUAL_INT(-1, pc_plaintext_slot_of(NULL));
}

// An address one past the end of a borrow's pool must not read as still-inside: that is what makes
// the range test a usable overrun check rather than a coincidence.
static void test_one_past_the_pool_is_not_owned(void)
{
    pc_span s = pc_secure_span(16, 8);
    TEST_ASSERT_TRUE(pc_span_ok(s));
    const uint8_t *end = s.buf + (PC_SECURE_ARENA_SIZE * PC_SEC_POOL_SLOTS);
    TEST_ASSERT_FALSE(pc_secure_owns(end));
    TEST_ASSERT_EQUAL_INT(-1, pc_secure_slot_of(end));
}

// The secure pool resolves the borrowing slot the same way the plaintext one does, and it is a
// second copy of that decision, so it is asserted here too rather than assumed to match.
//
// Which slot the caller gets is only observable at PC_WORKER_COUNT > 1 (native_pool_workers):
// below that pc_worker_self() is an inline compile-time 0 (worker.h) and every borrow is slot 0
// whatever the accessor decides.
static void test_slot_of_reports_the_borrowing_slot(void)
{
    pc_span s = pc_secure_span(16, 8);
    TEST_ASSERT_TRUE(pc_span_ok(s));
    TEST_ASSERT_EQUAL_INT(0, pc_secure_slot_of(s.buf));

#if PC_WORKER_COUNT > 1
    pc_secure_reset();
    pc_worker_set_self(1);
    pc_span own = pc_secure_span(16, 8);
    TEST_ASSERT_TRUE(pc_span_ok(own));
    TEST_ASSERT_EQUAL_INT(1, pc_secure_slot_of(own.buf)); // its own slot, not worker 0's
    pc_secure_reset();                                    // while still bound to 1: reset is per-slot

    pc_worker_set_self(PC_SEC_POOL_SLOTS); // not a server worker
    pc_span ghost = pc_secure_span(16, 8);
    TEST_ASSERT_TRUE(pc_span_ok(ghost));
    TEST_ASSERT_EQUAL_INT(PC_GHOST_WORKER_SLOT, pc_secure_slot_of(ghost.buf));
    pc_secure_reset(); // empties the ghost before the identity goes back to 0

    pc_worker_set_self(0); // restore identity for every later test
#endif
}

// --- the backward direction, same as the plaintext pool ---

static void test_high_water_reports_peak_demand(void)
{
    pc_secure_reset();
    size_t mark = pc_secure_mark();
    pc_span s = pc_secure_span(128, 8);
    TEST_ASSERT_TRUE(pc_span_ok(s));
    TEST_ASSERT_TRUE(pc_secure_high_water() >= 128);
    pc_secure_release(mark);
    TEST_ASSERT_TRUE(pc_secure_high_water() >= 128); // peak survives the reclaim
}

static void test_over_budget_fails_closed(void)
{
    pc_span s = pc_secure_span(PC_SECURE_ARENA_SIZE * 4, 8);
    TEST_ASSERT_FALSE(pc_span_ok(s));
    TEST_ASSERT_NULL(s.buf);
    TEST_ASSERT_EQUAL_UINT32(0, s.cap); // not a null with a live capacity
}

// The `secure` table names ten functions, four of them size_t(void) - used,
// mark, high_water, capacity - so a swapped pair type-checks and links, and
// only pointer identity catches it. The table is initialized in the header,
// which is what keeps --gc-sections able to reclaim the pool storage from a
// build that resets but never borrows a secret; a definition in secure.c would
// name every member and anchor alloc -> bind -> the backing bytes.
static void test_secure_table_is_wired_to_the_named_functions(void)
{
    TEST_ASSERT_EQUAL_PTR(pc_secure_alloc, secure.alloc);
    TEST_ASSERT_EQUAL_PTR(pc_secure_span, secure.span);
    TEST_ASSERT_EQUAL_PTR(pc_secure_reset, secure.reset);
    TEST_ASSERT_EQUAL_PTR(pc_secure_mark, secure.mark);
    TEST_ASSERT_EQUAL_PTR(pc_secure_release, secure.release);
    TEST_ASSERT_EQUAL_PTR(pc_secure_used, secure.used);
    TEST_ASSERT_EQUAL_PTR(pc_secure_high_water, secure.high_water);
    TEST_ASSERT_EQUAL_PTR(pc_secure_capacity, secure.capacity);
    TEST_ASSERT_EQUAL_PTR(pc_secure_owns, secure.owns);
    TEST_ASSERT_EQUAL_PTR(pc_secure_slot_of, secure.slot_of);
}

// The pool reached through the table rather than around it, and the one control
// that separates it from the plaintext side: reclaiming wipes.
static void test_secure_table_round_trip(void)
{
    secure.reset();
    TEST_ASSERT_EQUAL_size_t(0, secure.used());
    TEST_ASSERT_EQUAL_size_t(PC_SECURE_ARENA_SIZE, secure.capacity());

    const size_t mark = secure.mark();
    pc_span s = secure.span(32, 8);
    TEST_ASSERT_TRUE(pc_span_ok(s));
    TEST_ASSERT_EQUAL_size_t(32, s.cap);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t)s.buf % 8);
    TEST_ASSERT_TRUE(secure.owns(s.buf));
    TEST_ASSERT_TRUE(secure.slot_of(s.buf) >= 0 && secure.slot_of(s.buf) < PC_SEC_POOL_SLOTS);

    // A secret is never a plaintext borrow, and the reverse - disjoint regions.
    TEST_ASSERT_FALSE(pc_plaintext_owns(s.buf));
    const pc_span open = pc_plaintext_span(32, 8);
    TEST_ASSERT_TRUE(pc_span_ok(open));
    TEST_ASSERT_FALSE(secure.owns(open.buf));
    TEST_ASSERT_EQUAL_INT(-1, secure.slot_of(open.buf));
    pc_plaintext_reset();

    uint8_t *key = s.buf;
    memset(key, 0xC7, 32);
    TEST_ASSERT_EQUAL_UINT8(0xC7, key[0]);
    TEST_ASSERT_EQUAL_UINT8(0xC7, key[31]);

    secure.release(mark);
    TEST_ASSERT_EQUAL_size_t(0, secure.used());
    // Zero at the instant of reclaim, not merely marked free.
    for (size_t i = 0; i < 32; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, key[i]);
    }

    // An over-budget request fails closed and leaves the offset where it was.
    const size_t before = secure.used();
    TEST_ASSERT_NULL(secure.alloc(secure.capacity() * 4u, 8));
    TEST_ASSERT_EQUAL_size_t(before, secure.used());
    const pc_span too_big = secure.span(secure.capacity() * 4u, 8);
    TEST_ASSERT_FALSE(pc_span_ok(too_big));
    TEST_ASSERT_NULL(too_big.buf);
    TEST_ASSERT_EQUAL_size_t(0, too_big.cap);

    TEST_ASSERT_FALSE(secure.owns(NULL));
    TEST_ASSERT_TRUE(secure.high_water() >= 32);

    secure.reset();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_release_wipes_the_reclaimed_region);
    RUN_TEST(test_a_later_borrow_never_sees_the_previous_secret);
    RUN_TEST(test_reset_wipes_everything_live);
    RUN_TEST(test_scope_guard_wipes_on_every_exit);
    RUN_TEST(test_nested_scopes_release_lifo);
    RUN_TEST(test_a_secure_pointer_is_not_a_plaintext_one);
    RUN_TEST(test_a_plaintext_pointer_is_not_a_secure_one);
    RUN_TEST(test_foreign_pointers_belong_to_neither_pool);
    RUN_TEST(test_one_past_the_pool_is_not_owned);
    RUN_TEST(test_slot_of_reports_the_borrowing_slot);
    RUN_TEST(test_high_water_reports_peak_demand);
    RUN_TEST(test_over_budget_fails_closed);
    // Last: the round trip borrows, which moves the high-water mark the test above reports on.
    RUN_TEST(test_secure_table_is_wired_to_the_named_functions);
    RUN_TEST(test_secure_table_round_trip);
    return UNITY_END();
}
