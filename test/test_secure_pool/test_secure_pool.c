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
#include <string.h>
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

// The scope guard exists because the wipe must happen on the early-return paths too - the ones a
// hand-written wipe gets forgotten on.
static void test_scope_guard_wipes_on_every_exit(void)
{
    size_t scope = pc_secure_mark();
    uint8_t *seen = NULL;
    for (int trip = 0; trip < 2; trip++)
    {
        pc_span s = pc_secure_span(16, 8);
        TEST_ASSERT_TRUE(pc_span_ok(s));
        memset(s.buf, 0xEE, s.cap);
        seen = s.buf;
        if (trip == 0)
        {
            continue; // the "peer sent something malformed" shape
        }
    }
    for (size_t i = 0; i < 16; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, seen[i]);
    }
}

static void test_nested_scopes_release_lifo(void)
{
    size_t outer = pc_secure_mark();
    SecureScope outer;
    pc_span a = pc_secure_span(32, 8);
    TEST_ASSERT_TRUE(pc_span_ok(a));
    size_t after_a = pc_secure_used();
    {
        pc_span b = pc_secure_span(32, 8);
        TEST_ASSERT_TRUE(pc_span_ok(b));
        TEST_ASSERT_TRUE(pc_secure_used() > after_a);
    }
    TEST_ASSERT_EQUAL_size_t(after_a, pc_secure_used()); // inner reclaimed, outer intact
    TEST_ASSERT_TRUE(pc_span_ok(a));
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
}

// A pointer from neither pool - a stack address, a literal, null - belongs to neither.
static void test_foreign_pointers_belong_to_neither_pool(void)
{
    uint8_t on_stack[8];
    TEST_ASSERT_FALSE(pc_secure_owns(on_stack));
    TEST_ASSERT_FALSE(pc_plaintext_owns(on_stack));
    TEST_ASSERT_FALSE(pc_secure_owns(NULL));
    TEST_ASSERT_FALSE(pc_plaintext_owns(NULL));
    TEST_ASSERT_EQUAL_INT(-1, pc_secure_slot_of(on_stack));
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

int main(int, char **)
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
    return UNITY_END();
}
