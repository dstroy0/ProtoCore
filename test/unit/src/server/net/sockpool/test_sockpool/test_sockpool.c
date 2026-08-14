// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the LRU connection-slot pool (server/net/sockpool/sockpool.h).
//
// No standard governs a slot pool, so every expectation here is PROPERTIES. The load-bearing case
// is test_saturated_pool_evicts_in_last_used_order: it drives a full pool through a scripted
// sequence of touches and acquires and, at each acquire, computes the least-recently-used slot from
// the table itself and requires that to be the one recycled. A pool that recycles a live slot hands
// a new peer a socket another connection is still using.

#include "server/net/sockpool/sockpool.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

#define POOL_N 4

static SockSlot g_slots[POOL_N];
static SockPool g_pool;

static void fresh(void)
{
    protocore_sockpool_init(&g_pool, g_slots, POOL_N);
}

// Fill every slot, one connection per slot, at increasing ticks.
static void fill(uint32_t first_id, uint32_t first_tick)
{
    for (size_t i = 0; i < POOL_N; i++)
    {
        size_t idx = (size_t)-1;
        TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FREE, protocore_sockpool_acquire(&g_pool, first_id + (uint32_t)i,
                                                                        first_tick + (uint32_t)i, &idx, NULL));
    }
}

void test_init_leaves_every_slot_free(void)
{
    for (size_t i = 0; i < POOL_N; i++)
    {
        g_slots[i].in_use = PROTO_TRUE;
        g_slots[i].id = 0xDEADBEEFu;
        g_slots[i].last_used = 99u;
    }
    fresh();
    TEST_ASSERT_EQUAL_size_t(0u, protocore_sockpool_in_use(&g_pool));
    for (size_t i = 0; i < POOL_N; i++)
    {
        TEST_ASSERT_FALSE(g_slots[i].in_use);
        TEST_ASSERT_EQUAL_UINT32(0u, g_slots[i].id);
        TEST_ASSERT_EQUAL_UINT32(0u, g_slots[i].last_used);
    }
}

// A free slot is preferred over a recycle, and the slots fill in index order.
void test_acquire_takes_free_slots_first(void)
{
    fresh();
    for (size_t i = 0; i < POOL_N; i++)
    {
        size_t idx = (size_t)-1;
        uint32_t evicted = 0xFFFFFFFFu;
        TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FREE,
                              protocore_sockpool_acquire(&g_pool, 100u + (uint32_t)i, (uint32_t)i, &idx, &evicted));
        TEST_ASSERT_EQUAL_size_t(i, idx);
        TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, evicted); // untouched: nothing was evicted
        TEST_ASSERT_EQUAL_size_t(i + 1u, protocore_sockpool_in_use(&g_pool));
    }
}

// The slot with the smallest last_used is the one recycled, and its id is reported so the caller
// can close that socket. Recomputed from the table at every step rather than assumed.
void test_saturated_pool_evicts_in_last_used_order(void)
{
    fresh();
    fill(100u, 10u); // ids 100..103 at ticks 10..13

    // A scripted mix of refreshes and new connections, driving the LRU choice around the table.
    static const struct
    {
        uint32_t touch_idx; // POOL_N means "no touch this round"
        uint32_t tick;
        uint32_t new_id;
    } STEP[] = {
        {POOL_N, 20u, 200u}, {2u, 21u, 201u}, {POOL_N, 22u, 202u}, {0u, 23u, 203u},
        {3u, 24u, 204u},     {3u, 25u, 205u}, {POOL_N, 26u, 206u},
    };
    // Every step leaves exactly one oldest slot, so the expectation never rests on a tie-break rule.

    for (size_t s = 0; s < sizeof(STEP) / sizeof(STEP[0]); s++)
    {
        if (STEP[s].touch_idx < POOL_N)
        {
            protocore_sockpool_touch(&g_pool, (size_t)STEP[s].touch_idx, STEP[s].tick);
        }

        // The least-recently-used slot, read straight out of the table.
        size_t want = 0;
        for (size_t i = 1; i < POOL_N; i++)
        {
            if (g_slots[i].last_used < g_slots[want].last_used)
            {
                want = i;
            }
        }
        uint32_t want_id = g_slots[want].id;

        size_t idx = (size_t)-1;
        uint32_t evicted = 0u;
        TEST_ASSERT_EQUAL_INT(SOCK_ACQ_RECYCLED,
                              protocore_sockpool_acquire(&g_pool, STEP[s].new_id, STEP[s].tick + 1u, &idx, &evicted));
        TEST_ASSERT_EQUAL_size_t(want, idx);
        TEST_ASSERT_EQUAL_UINT32(want_id, evicted);
        TEST_ASSERT_EQUAL_UINT32(STEP[s].new_id, g_slots[idx].id);
        TEST_ASSERT_EQUAL_UINT32(STEP[s].tick + 1u, g_slots[idx].last_used);
        TEST_ASSERT_EQUAL_size_t(POOL_N, protocore_sockpool_in_use(&g_pool)); // a recycle never grows the pool
    }
}

// Touching a slot moves it out of the eviction path: the previously oldest slot survives and the
// next-oldest goes instead.
void test_touch_refreshes_the_lru_position(void)
{
    fresh();
    fill(100u, 10u); // slot 0 is the oldest at tick 10

    protocore_sockpool_touch(&g_pool, 0u, 50u);

    size_t idx = (size_t)-1;
    uint32_t evicted = 0u;
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_RECYCLED, protocore_sockpool_acquire(&g_pool, 900u, 51u, &idx, &evicted));
    TEST_ASSERT_EQUAL_size_t(1u, idx); // slot 1, tick 11, is now the oldest
    TEST_ASSERT_EQUAL_UINT32(101u, evicted);
}

// A free slot has no LRU position to refresh, so touching it does not make it look live.
void test_touch_of_a_free_slot_is_ignored(void)
{
    fresh();
    protocore_sockpool_touch(&g_pool, 0u, 77u);
    TEST_ASSERT_EQUAL_UINT32(0u, g_slots[0].last_used);
    TEST_ASSERT_FALSE(g_slots[0].in_use);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_sockpool_in_use(&g_pool));
}

// A released slot is the free slot the next acquire takes, ahead of any recycle.
void test_release_returns_a_slot_to_the_free_list(void)
{
    fresh();
    fill(100u, 10u);
    TEST_ASSERT_TRUE(protocore_sockpool_release(&g_pool, 2u));
    TEST_ASSERT_EQUAL_size_t(POOL_N - 1u, protocore_sockpool_in_use(&g_pool));

    size_t idx = (size_t)-1;
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FREE, protocore_sockpool_acquire(&g_pool, 500u, 60u, &idx, NULL));
    TEST_ASSERT_EQUAL_size_t(2u, idx);
    TEST_ASSERT_EQUAL_size_t(POOL_N, protocore_sockpool_in_use(&g_pool));
}

// Releasing twice, or out of range, reports false rather than corrupting the count.
void test_release_refuses_a_free_or_out_of_range_slot(void)
{
    fresh();
    fill(100u, 10u);
    TEST_ASSERT_TRUE(protocore_sockpool_release(&g_pool, 1u));
    TEST_ASSERT_FALSE(protocore_sockpool_release(&g_pool, 1u));
    TEST_ASSERT_FALSE(protocore_sockpool_release(&g_pool, POOL_N));
    TEST_ASSERT_FALSE(protocore_sockpool_release(NULL, 0u));
    TEST_ASSERT_EQUAL_size_t(POOL_N - 1u, protocore_sockpool_in_use(&g_pool));
}

// find locates a live connection by id and stops locating it once the slot is released.
void test_find_tracks_the_live_ids(void)
{
    fresh();
    fill(100u, 10u);
    for (uint32_t i = 0; i < POOL_N; i++)
    {
        size_t idx = (size_t)-1;
        TEST_ASSERT_TRUE(protocore_sockpool_find(&g_pool, 100u + i, &idx));
        TEST_ASSERT_EQUAL_size_t((size_t)i, idx);
    }
    TEST_ASSERT_FALSE(protocore_sockpool_find(&g_pool, 999u, NULL));

    TEST_ASSERT_TRUE(protocore_sockpool_release(&g_pool, 3u));
    TEST_ASSERT_FALSE(protocore_sockpool_find(&g_pool, 103u, NULL));
    TEST_ASSERT_TRUE(protocore_sockpool_find(&g_pool, 102u, NULL)); // the others are unaffected
}

// A recycled connection is found at the slot it took over, and the evicted id is gone.
void test_find_follows_a_recycle(void)
{
    fresh();
    fill(100u, 10u);
    size_t idx = (size_t)-1;
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_RECYCLED, protocore_sockpool_acquire(&g_pool, 777u, 40u, &idx, NULL));
    size_t found = (size_t)-1;
    TEST_ASSERT_TRUE(protocore_sockpool_find(&g_pool, 777u, &found));
    TEST_ASSERT_EQUAL_size_t(idx, found);
    TEST_ASSERT_FALSE(protocore_sockpool_find(&g_pool, 100u, NULL));
}

// Both out-parameters are optional on the free path and on the recycle path.
void test_out_parameters_are_optional(void)
{
    fresh();
    fill(200u, 5u);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_RECYCLED, protocore_sockpool_acquire(&g_pool, 2u, 9u, NULL, NULL));
    TEST_ASSERT_TRUE(protocore_sockpool_find(&g_pool, 2u, NULL));
}

// A pool with no storage cannot serve anyone, and says so rather than writing through a null.
void test_a_pool_with_no_slots_fails_closed(void)
{
    SockPool empty;
    protocore_sockpool_init(&empty, NULL, 8u);
    TEST_ASSERT_EQUAL_size_t(0u, empty.n);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FAIL, protocore_sockpool_acquire(&empty, 1u, 1u, NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_sockpool_in_use(&empty));
    TEST_ASSERT_FALSE(protocore_sockpool_find(&empty, 1u, NULL));

    protocore_sockpool_init(&empty, g_slots, 0u);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FAIL, protocore_sockpool_acquire(&empty, 1u, 1u, NULL, NULL));

    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FAIL, protocore_sockpool_acquire(NULL, 1u, 1u, NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_sockpool_in_use(NULL));
    TEST_ASSERT_FALSE(protocore_sockpool_find(NULL, 1u, NULL));
    protocore_sockpool_init(NULL, g_slots, POOL_N); // must not fault
    protocore_sockpool_touch(NULL, 0u, 1u);
}

// The number of live slots never exceeds the table, whatever the mix of operations.
void test_in_use_never_exceeds_the_table(void)
{
    fresh();
    for (uint32_t k = 0; k < 200u; k++)
    {
        if ((k & 3u) == 3u)
        {
            (void)protocore_sockpool_release(&g_pool, (size_t)(k % POOL_N));
        }
        else
        {
            (void)protocore_sockpool_acquire(&g_pool, k, k, NULL, NULL);
        }
        TEST_ASSERT_TRUE(protocore_sockpool_in_use(&g_pool) <= (size_t)POOL_N);
    }
}
