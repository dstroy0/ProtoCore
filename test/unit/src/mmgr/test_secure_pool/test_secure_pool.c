// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the secure pool accessor (mmgr/secure.h).
//
// The secure pool is the plaintext pool's mechanism instantiated a second time, so the allocator
// itself belongs to test_arena. What is covered here is only the access and control layer, and no
// standard governs it: every expectation is PROPERTIES.
//
// test_release_wipes_before_the_bytes_are_available_again is the load-bearing case. The wipe has to
// happen BEFORE the position moves, so the same borrow handed out again reads zero. Wiping after
// leaves a window in which the next tenant is given memory still holding the previous one's key
// material, and that window is invisible to a test that only checks the bytes are eventually clean.

#include "mmgr/plaintext/plaintext.h"
#include "mmgr/secure/secure.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
    protocore_secure_reset();
}
void tearDown(void)
{
    protocore_secure_reset();
}

// The reclaimed region reads zero at the instant it is reclaimed, and the next borrow of the same
// bytes is handed them clean rather than merely marked free.
void test_release_wipes_before_the_bytes_are_available_again(void)
{
    const size_t mark = protocore_secure_mark();
    protocore_span first = protocore_secure_span(64, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(first));
    memset(first.buf, 0x5C, first.cap);
    uint8_t *key = first.buf;

    protocore_secure_release(mark);
    for (size_t i = 0; i < 64; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0, key[i]);
    }

    protocore_span second = protocore_secure_span(64, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(second));
    TEST_ASSERT_EQUAL_PTR(key, second.buf); // the same bytes come back out
    for (size_t i = 0; i < second.cap; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0, second.buf[i]);
    }
}

// The bulk reclaim wipes every live borrow, not only the most recent one.
void test_reset_wipes_every_live_borrow(void)
{
    protocore_span a = protocore_secure_span(48, 8);
    protocore_span b = protocore_secure_span(48, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(a));
    TEST_ASSERT_TRUE(protocore_span_ok(b));
    memset(a.buf, 0x11, a.cap);
    memset(b.buf, 0x22, b.cap);
    uint8_t *pa = a.buf;
    uint8_t *pb = b.buf;

    protocore_secure_reset();

    for (size_t i = 0; i < 48; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0, pa[i]);
        TEST_ASSERT_EQUAL_HEX8(0, pb[i]);
    }
    TEST_ASSERT_EQUAL_size_t(0, protocore_secure_used());
}

// Both exits from a borrow reach the same release, so the early return a malformed peer message
// takes wipes exactly as the normal one does.
void test_a_scope_guard_wipes_on_every_exit_path(void)
{
    uint8_t *seen[2] = {NULL, NULL};
    for (int trip = 0; trip < 2; trip++)
    {
        const size_t scope = protocore_secure_mark();
        protocore_span s = protocore_secure_span(16, 8);
        TEST_ASSERT_TRUE(protocore_span_ok(s));
        memset(s.buf, 0xEE, s.cap);
        seen[trip] = s.buf;
        if (trip == 0)
        {
            protocore_secure_release(scope); // the "peer sent something malformed" shape
            continue;
        }
        protocore_secure_release(scope);
    }
    for (int trip = 0; trip < 2; trip++)
    {
        for (size_t i = 0; i < 16; i++)
        {
            TEST_ASSERT_EQUAL_HEX8(0, seen[trip][i]);
        }
    }
}

// A nested scope reclaims back to its own mark and no further: the outer borrow is still live and
// still holds its bytes.
void test_nested_scopes_reclaim_lifo(void)
{
    const size_t outer = protocore_secure_mark();
    protocore_span a = protocore_secure_span(32, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(a));
    memset(a.buf, 0x77, a.cap);
    const size_t after_a = protocore_secure_used();

    const size_t inner = protocore_secure_mark();
    protocore_span b = protocore_secure_span(32, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(b));
    TEST_ASSERT_TRUE(protocore_secure_used() > after_a);
    protocore_secure_release(inner);

    TEST_ASSERT_EQUAL_size_t(after_a, protocore_secure_used());
    for (size_t i = 0; i < 32; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x77, a.buf[i]); // the outer borrow was not touched
    }
    protocore_secure_release(outer);
}

// The pools occupy disjoint regions, so ownership is decided by address alone: a secret can never be
// accepted where plaintext is expected, or the reverse, with no per-allocation metadata.
void test_the_two_pools_are_disjoint_regions(void)
{
    protocore_span s = protocore_secure_span(32, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(s));
    TEST_ASSERT_TRUE(protocore_secure_owns(s.buf));
    TEST_ASSERT_FALSE(protocore_plaintext_owns(s.buf));
    TEST_ASSERT_EQUAL_INT(-1, protocore_plaintext_slot_of(s.buf));

    const size_t scope = protocore_plaintext_mark();
    protocore_span open = protocore_plaintext_span(32, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(open));
    TEST_ASSERT_TRUE(protocore_plaintext_owns(open.buf));
    TEST_ASSERT_FALSE(protocore_secure_owns(open.buf));
    TEST_ASSERT_EQUAL_INT(-1, protocore_secure_slot_of(open.buf));
    protocore_plaintext_release(scope); // setUp resets the secure pool only

    // The borrowing slot is the calling worker's own.
    TEST_ASSERT_EQUAL_INT(0, protocore_secure_slot_of(s.buf));
}

// Storage outside both pools, aligned the way every borrow is: a stack array gives no alignment past
// 1, so it would exercise a shape no pool pointer can have.
static uint8_t g_outside[32] __attribute__((aligned(32)));

// A pointer from neither pool - other BSS, a code address, null - belongs to neither.
void test_a_pointer_from_neither_pool_belongs_to_neither(void)
{
    TEST_ASSERT_FALSE(protocore_secure_owns(g_outside));
    TEST_ASSERT_FALSE(protocore_plaintext_owns(g_outside));
    TEST_ASSERT_EQUAL_INT(-1, protocore_secure_slot_of(g_outside));
    TEST_ASSERT_EQUAL_INT(-1, protocore_plaintext_slot_of(g_outside));

    const void *code = (const void *)&protocore_secure_reset;
    TEST_ASSERT_FALSE(protocore_secure_owns(code));
    TEST_ASSERT_FALSE(protocore_plaintext_owns(code));

    TEST_ASSERT_FALSE(protocore_secure_owns(NULL));
    TEST_ASSERT_FALSE(protocore_plaintext_owns(NULL));
    TEST_ASSERT_EQUAL_INT(-1, protocore_secure_slot_of(NULL));
}

// One past the pool's last byte is outside it. That is what makes the range test a usable overrun
// check rather than a coincidence.
void test_one_past_the_pool_is_not_owned(void)
{
    protocore_span s = protocore_secure_span(16, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(s));
    const uint8_t *end = s.buf + (PROTOCORE_SECURE_ARENA_SIZE * PROTOCORE_SEC_POOL_SLOTS);
    TEST_ASSERT_FALSE(protocore_secure_owns(end));
    TEST_ASSERT_EQUAL_INT(-1, protocore_secure_slot_of(end));
}

// The persistent end grows up from the base while a borrow bumps down from the top, so no mark
// walks it: a table taken there survives a release and the whole-slot reset, and arrives zeroed.
void test_a_persistent_borrow_outlives_every_release(void)
{
    protocore_span table = protocore_secure_persist_span(64);
    TEST_ASSERT_TRUE(protocore_span_ok(table));
    for (size_t i = 0; i < table.cap; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0, table.buf[i]); // handed back zeroed
    }
    memset(table.buf, 0x3B, table.cap);

    const size_t mark = protocore_secure_mark();
    protocore_span scratch = protocore_secure_span(32, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(scratch));
    TEST_ASSERT_TRUE(protocore_secure_owns(scratch.buf));
    protocore_secure_release(mark);
    protocore_secure_reset();

    for (size_t i = 0; i < table.cap; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x3B, table.buf[i]);
    }
    TEST_ASSERT_TRUE(protocore_secure_owns(table.buf));
}

// The peak survives the reclaim, which is what makes it usable for sizing the arena.
void test_high_water_records_peak_demand(void)
{
    const size_t mark = protocore_secure_mark();
    protocore_span s = protocore_secure_span(128, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(s));
    TEST_ASSERT_TRUE(protocore_secure_high_water() >= 128);
    protocore_secure_release(mark);
    TEST_ASSERT_TRUE(protocore_secure_high_water() >= 128);
    TEST_ASSERT_EQUAL_size_t(0, protocore_secure_used());
}

// An over-budget request yields a null with a zero capacity, never a null with a live one, and
// leaves the position where it was.
void test_an_over_budget_borrow_fails_closed(void)
{
    const size_t before = protocore_secure_used();
    TEST_ASSERT_NULL(protocore_secure_alloc(protocore_secure_capacity() * 4u, 8));
    TEST_ASSERT_EQUAL_size_t(before, protocore_secure_used());

    protocore_span s = protocore_secure_span(protocore_secure_capacity() * 4u, 8);
    TEST_ASSERT_FALSE(protocore_span_ok(s));
    TEST_ASSERT_NULL(s.buf);
    TEST_ASSERT_EQUAL_size_t(0, s.cap);
    TEST_ASSERT_EQUAL_size_t(before, protocore_secure_used());

    TEST_ASSERT_EQUAL_size_t(PROTOCORE_SECURE_ARENA_SIZE, protocore_secure_capacity());
}

// The table names eleven functions and four of them are size_t(void) - used, mark, high_water,
// capacity - so a swapped pair type-checks and links, and only pointer identity catches it.
void test_the_table_is_wired_to_the_named_functions(void)
{
    TEST_ASSERT_EQUAL_PTR(protocore_secure_alloc, secure.alloc);
    TEST_ASSERT_EQUAL_PTR(protocore_secure_span, secure.span);
    TEST_ASSERT_EQUAL_PTR(protocore_secure_persist_span, secure.persist_span);
    TEST_ASSERT_EQUAL_PTR(protocore_secure_reset, secure.reset);
    TEST_ASSERT_EQUAL_PTR(protocore_secure_mark, secure.mark);
    TEST_ASSERT_EQUAL_PTR(protocore_secure_release, secure.release);
    TEST_ASSERT_EQUAL_PTR(protocore_secure_used, secure.used);
    TEST_ASSERT_EQUAL_PTR(protocore_secure_high_water, secure.high_water);
    TEST_ASSERT_EQUAL_PTR(protocore_secure_capacity, secure.capacity);
    TEST_ASSERT_EQUAL_PTR(protocore_secure_owns, secure.owns);
    TEST_ASSERT_EQUAL_PTR(protocore_secure_slot_of, secure.slot_of);
    TEST_ASSERT_EQUAL_PTR(&protocore_secure_state, secure.internal);
}

// The pool reached through the table rather than around it, with the one control that separates it
// from the plaintext side: reclaiming wipes.
void test_the_pool_works_through_the_table(void)
{
    secure.reset();
    TEST_ASSERT_EQUAL_size_t(0, secure.used());

    const size_t mark = secure.mark();
    protocore_span s = secure.span(32, 8);
    TEST_ASSERT_TRUE(protocore_span_ok(s));
    TEST_ASSERT_EQUAL_size_t(32, s.cap);
    TEST_ASSERT_EQUAL_size_t(0, (size_t)((uintptr_t)s.buf % 8u));
    TEST_ASSERT_TRUE(secure.owns(s.buf));
    TEST_ASSERT_TRUE(secure.slot_of(s.buf) >= 0);
    TEST_ASSERT_TRUE(secure.slot_of(s.buf) < PROTOCORE_SEC_POOL_SLOTS);

    uint8_t *key = s.buf;
    memset(key, 0xC7, 32);
    secure.release(mark);
    TEST_ASSERT_EQUAL_size_t(0, secure.used());
    for (size_t i = 0; i < 32; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0, key[i]);
    }
    TEST_ASSERT_FALSE(secure.owns(NULL));
    secure.reset();
}
