// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/sockpool: the LRU connection-slot recycling pool.

#include "services/net/sockpool/sockpool.h"
#include <unity.h>

static SockSlot g_slots[3];
static SockPool g_pool;

void setUp(void)
{
    pc_sockpool_init(&g_pool, g_slots, 3);
}
void tearDown(void)
{
}

void test_acquire_free(void)
{
    size_t idx = 99;
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FREE, pc_sockpool_acquire(&g_pool, 100, 10, &idx, NULL));
    TEST_ASSERT_EQUAL_size_t(0, idx);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FREE, pc_sockpool_acquire(&g_pool, 101, 11, &idx, NULL));
    TEST_ASSERT_EQUAL_size_t(1, idx);
    TEST_ASSERT_EQUAL_size_t(2, pc_sockpool_in_use(&g_pool));
    // Find.
    size_t found = 99;
    TEST_ASSERT_TRUE(pc_sockpool_find(&g_pool, 101, &found));
    TEST_ASSERT_EQUAL_size_t(1, found);
    TEST_ASSERT_FALSE(pc_sockpool_find(&g_pool, 999, &found));
}

void test_lru_recycle(void)
{
    // Fill: id 100@t10, 101@t20, 102@t30.
    pc_sockpool_acquire(&g_pool, 100, 10, NULL, NULL);
    pc_sockpool_acquire(&g_pool, 101, 20, NULL, NULL);
    pc_sockpool_acquire(&g_pool, 102, 30, NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(3, pc_sockpool_in_use(&g_pool));
    // Acquire a 4th: the LRU (id 100, oldest tick) is recycled.
    size_t idx = 99;
    uint32_t evicted = 0;
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_RECYCLED, pc_sockpool_acquire(&g_pool, 103, 40, &idx, &evicted));
    TEST_ASSERT_EQUAL_UINT32(100, evicted);
    TEST_ASSERT_EQUAL_size_t(0, idx);                         // slot 0 held id 100
    TEST_ASSERT_FALSE(pc_sockpool_find(&g_pool, 100, NULL));  // gone
    TEST_ASSERT_TRUE(pc_sockpool_find(&g_pool, 103, NULL));   // now present
    TEST_ASSERT_EQUAL_size_t(3, pc_sockpool_in_use(&g_pool)); // still full
}

void test_touch_changes_lru(void)
{
    pc_sockpool_acquire(&g_pool, 100, 10, NULL, NULL);
    pc_sockpool_acquire(&g_pool, 101, 20, NULL, NULL);
    pc_sockpool_acquire(&g_pool, 102, 30, NULL, NULL);
    // Touch id 100 so it is no longer the LRU; now id 101 is oldest.
    size_t i100 = 99;
    pc_sockpool_find(&g_pool, 100, &i100);
    pc_sockpool_touch(&g_pool, i100, 50);
    uint32_t evicted = 0;
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_RECYCLED, pc_sockpool_acquire(&g_pool, 103, 60, NULL, &evicted));
    TEST_ASSERT_EQUAL_UINT32(101, evicted);
}

void test_release_reopens_free(void)
{
    pc_sockpool_acquire(&g_pool, 100, 10, NULL, NULL);
    size_t idx = 99;
    pc_sockpool_find(&g_pool, 100, &idx);
    TEST_ASSERT_TRUE(pc_sockpool_release(&g_pool, idx));
    TEST_ASSERT_EQUAL_size_t(0, pc_sockpool_in_use(&g_pool));
    // Double release is a no-op false.
    TEST_ASSERT_FALSE(pc_sockpool_release(&g_pool, idx));
    // A subsequent acquire reuses a free slot (not a recycle).
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FREE, pc_sockpool_acquire(&g_pool, 200, 20, NULL, NULL));
}

void test_empty_pool_fails(void)
{
    SockPool empty;
    pc_sockpool_init(&empty, NULL, 0);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FAIL, pc_sockpool_acquire(&empty, 1, 1, NULL, NULL));
}

void test_null_guard_subconditions()
{
    pc_sockpool_init(NULL, NULL, 0); // null pool -> no-op
    pc_sockpool_touch(NULL, 0, 0);   // null pool -> no-op
    size_t idx = 0;
    TEST_ASSERT_FALSE(pc_sockpool_find(NULL, 1, &idx));    // null pool -> false
    TEST_ASSERT_EQUAL_size_t(0, pc_sockpool_in_use(NULL)); // null pool -> 0
}

void test_acquire_null_pool_and_nonnull_slots_zero_n(void)
{
    // Null pool pointer -> FAIL (the acquire-specific null-pool branch; not exercised elsewhere).
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FAIL, pc_sockpool_acquire(NULL, 1, 1, NULL, NULL));

    // Non-null slots pointer but n == 0: exercises the `p->n == 0` branch reached only when
    // p->slots is non-null (test_empty_pool_fails' pool has slots == NULL, so it never gets here).
    SockSlot dummy_slot[1];
    SockPool zero_n_pool;
    pc_sockpool_init(&zero_n_pool, dummy_slot, 0);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FAIL, pc_sockpool_acquire(&zero_n_pool, 1, 1, NULL, NULL));
}

void test_acquire_recycle_with_null_evicted_id(void)
{
    // Fill the pool, then force a recycle while passing evicted_id == NULL, exercising the
    // false branch of `if (evicted_id)` (every other recycle test passes a non-null pointer).
    pc_sockpool_acquire(&g_pool, 100, 10, NULL, NULL);
    pc_sockpool_acquire(&g_pool, 101, 20, NULL, NULL);
    pc_sockpool_acquire(&g_pool, 102, 30, NULL, NULL);
    size_t idx = 99;
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_RECYCLED, pc_sockpool_acquire(&g_pool, 103, 40, &idx, NULL));
    TEST_ASSERT_EQUAL_size_t(0, idx);
    TEST_ASSERT_TRUE(pc_sockpool_find(&g_pool, 103, NULL));
}

void test_touch_guard_subconditions(void)
{
    // Valid pool pointer but null slots array -> no-op (p->slots branch).
    SockPool empty;
    pc_sockpool_init(&empty, NULL, 0);
    pc_sockpool_touch(&empty, 0, 5);

    // Valid pool/slots but idx out of range -> no-op (idx >= p->n branch).
    pc_sockpool_touch(&g_pool, 99, 5);

    // Valid idx, but that slot is not in_use -> no-op (the `in_use` false branch).
    size_t idx = 99;
    pc_sockpool_acquire(&g_pool, 100, 10, &idx, NULL);
    TEST_ASSERT_TRUE(pc_sockpool_release(&g_pool, idx));
    pc_sockpool_touch(&g_pool, idx, 999); // slot idx is free; must not mark it used
    TEST_ASSERT_EQUAL_size_t(0, pc_sockpool_in_use(&g_pool));
}

void test_release_guard_subconditions(void)
{
    // Null pool pointer -> false (release-specific null-pool branch; not exercised elsewhere).
    TEST_ASSERT_FALSE(pc_sockpool_release(NULL, 0));

    // Valid pool pointer but null slots array -> false (p->slots branch).
    SockPool empty;
    pc_sockpool_init(&empty, NULL, 0);
    TEST_ASSERT_FALSE(pc_sockpool_release(&empty, 0));

    // Valid pool/slots but idx out of range -> false (idx >= p->n branch).
    TEST_ASSERT_FALSE(pc_sockpool_release(&g_pool, 99));
}

void test_find_and_in_use_with_null_slots(void)
{
    // Valid pool pointer but null slots array -> exercises the p->slots branch in both
    // pc_sockpool_find and pc_sockpool_in_use (only the null-pool branch was tested before).
    SockPool empty;
    pc_sockpool_init(&empty, NULL, 0);
    size_t idx = 99;
    TEST_ASSERT_FALSE(pc_sockpool_find(&empty, 1, &idx));
    TEST_ASSERT_EQUAL_size_t(0, pc_sockpool_in_use(&empty));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_acquire_free);
    RUN_TEST(test_lru_recycle);
    RUN_TEST(test_touch_changes_lru);
    RUN_TEST(test_release_reopens_free);
    RUN_TEST(test_empty_pool_fails);
    RUN_TEST(test_null_guard_subconditions);
    RUN_TEST(test_acquire_null_pool_and_nonnull_slots_zero_n);
    RUN_TEST(test_acquire_recycle_with_null_evicted_id);
    RUN_TEST(test_touch_guard_subconditions);
    RUN_TEST(test_release_guard_subconditions);
    RUN_TEST(test_find_and_in_use_with_null_slots);
    return UNITY_END();
}
