// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t sockpool_work[16]; // the borrow an entry takes; Sockpool never reads it

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
    Sockpool.init_args.p = &g_pool;
    Sockpool.init_args.slots = g_slots;
    Sockpool.init_args.n = POOL_N;
    Sockpool.init(sockpool_work);
}

// Fill every slot, one connection per slot, at increasing ticks.
static void fill(uint32_t first_id, uint32_t first_tick)
{
    for (size_t i = 0; i < POOL_N; i++)
    {
        size_t idx = (size_t)-1;
        Sockpool.acquire_args.p = &g_pool;
        Sockpool.acquire_args.id = first_id + (uint32_t)i;
        Sockpool.acquire_args.now = first_tick + (uint32_t)i;
        Sockpool.acquire_args.idx = &idx;
        Sockpool.acquire_args.evicted_id = NULL;
        Sockpool.acquire(sockpool_work);
        TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FREE, Sockpool.acq);
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
    Sockpool.in_use_args.p = &g_pool;
    Sockpool.in_use(sockpool_work);
    TEST_ASSERT_EQUAL_size_t(0u, Sockpool.n);
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
        Sockpool.acquire_args.p = &g_pool;
        Sockpool.acquire_args.id = 100u + (uint32_t)i;
        Sockpool.acquire_args.now = (uint32_t)i;
        Sockpool.acquire_args.idx = &idx;
        Sockpool.acquire_args.evicted_id = &evicted;
        Sockpool.acquire(sockpool_work);
        TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FREE, Sockpool.acq);
        TEST_ASSERT_EQUAL_size_t(i, idx);
        TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, evicted); // untouched: nothing was evicted
        Sockpool.in_use_args.p = &g_pool;
        Sockpool.in_use(sockpool_work);
        TEST_ASSERT_EQUAL_size_t(i + 1u, Sockpool.n);
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
            Sockpool.touch_args.p = &g_pool;
            Sockpool.touch_args.idx = (size_t)STEP[s].touch_idx;
            Sockpool.touch_args.now = STEP[s].tick;
            Sockpool.touch(sockpool_work);
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
        Sockpool.acquire_args.p = &g_pool;
        Sockpool.acquire_args.id = STEP[s].new_id;
        Sockpool.acquire_args.now = STEP[s].tick + 1u;
        Sockpool.acquire_args.idx = &idx;
        Sockpool.acquire_args.evicted_id = &evicted;
        Sockpool.acquire(sockpool_work);
        TEST_ASSERT_EQUAL_INT(SOCK_ACQ_RECYCLED, Sockpool.acq);
        TEST_ASSERT_EQUAL_size_t(want, idx);
        TEST_ASSERT_EQUAL_UINT32(want_id, evicted);
        TEST_ASSERT_EQUAL_UINT32(STEP[s].new_id, g_slots[idx].id);
        TEST_ASSERT_EQUAL_UINT32(STEP[s].tick + 1u, g_slots[idx].last_used);
        Sockpool.in_use_args.p = &g_pool;
        Sockpool.in_use(sockpool_work);
        TEST_ASSERT_EQUAL_size_t(POOL_N, Sockpool.n); // a recycle never grows the pool
    }
}

// Touching a slot moves it out of the eviction path: the previously oldest slot survives and the
// next-oldest goes instead.
void test_touch_refreshes_the_lru_position(void)
{
    fresh();
    fill(100u, 10u); // slot 0 is the oldest at tick 10

    Sockpool.touch_args.p = &g_pool;
    Sockpool.touch_args.idx = 0u;
    Sockpool.touch_args.now = 50u;
    Sockpool.touch(sockpool_work);

    size_t idx = (size_t)-1;
    uint32_t evicted = 0u;
    Sockpool.acquire_args.p = &g_pool;
    Sockpool.acquire_args.id = 900u;
    Sockpool.acquire_args.now = 51u;
    Sockpool.acquire_args.idx = &idx;
    Sockpool.acquire_args.evicted_id = &evicted;
    Sockpool.acquire(sockpool_work);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_RECYCLED, Sockpool.acq);
    TEST_ASSERT_EQUAL_size_t(1u, idx); // slot 1, tick 11, is now the oldest
    TEST_ASSERT_EQUAL_UINT32(101u, evicted);
}

// A free slot has no LRU position to refresh, so touching it does not make it look live.
void test_touch_of_a_free_slot_is_ignored(void)
{
    fresh();
    Sockpool.touch_args.p = &g_pool;
    Sockpool.touch_args.idx = 0u;
    Sockpool.touch_args.now = 77u;
    Sockpool.touch(sockpool_work);
    TEST_ASSERT_EQUAL_UINT32(0u, g_slots[0].last_used);
    TEST_ASSERT_FALSE(g_slots[0].in_use);
    Sockpool.in_use_args.p = &g_pool;
    Sockpool.in_use(sockpool_work);
    TEST_ASSERT_EQUAL_size_t(0u, Sockpool.n);
}

// A released slot is the free slot the next acquire takes, ahead of any recycle.
void test_release_returns_a_slot_to_the_free_list(void)
{
    fresh();
    fill(100u, 10u);
    Sockpool.release_args.p = &g_pool;
    Sockpool.release_args.idx = 2u;
    Sockpool.release(sockpool_work);
    TEST_ASSERT_TRUE(Sockpool.ok);
    Sockpool.in_use_args.p = &g_pool;
    Sockpool.in_use(sockpool_work);
    TEST_ASSERT_EQUAL_size_t(POOL_N - 1u, Sockpool.n);

    size_t idx = (size_t)-1;
    Sockpool.acquire_args.p = &g_pool;
    Sockpool.acquire_args.id = 500u;
    Sockpool.acquire_args.now = 60u;
    Sockpool.acquire_args.idx = &idx;
    Sockpool.acquire_args.evicted_id = NULL;
    Sockpool.acquire(sockpool_work);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FREE, Sockpool.acq);
    TEST_ASSERT_EQUAL_size_t(2u, idx);
    Sockpool.in_use_args.p = &g_pool;
    Sockpool.in_use(sockpool_work);
    TEST_ASSERT_EQUAL_size_t(POOL_N, Sockpool.n);
}

// Releasing twice, or out of range, reports false rather than corrupting the count.
void test_release_refuses_a_free_or_out_of_range_slot(void)
{
    fresh();
    fill(100u, 10u);
    Sockpool.release_args.p = &g_pool;
    Sockpool.release_args.idx = 1u;
    Sockpool.release(sockpool_work);
    TEST_ASSERT_TRUE(Sockpool.ok);
    Sockpool.release_args.p = &g_pool;
    Sockpool.release_args.idx = 1u;
    Sockpool.release(sockpool_work);
    TEST_ASSERT_FALSE(Sockpool.ok);
    Sockpool.release_args.p = &g_pool;
    Sockpool.release_args.idx = POOL_N;
    Sockpool.release(sockpool_work);
    TEST_ASSERT_FALSE(Sockpool.ok);
    Sockpool.release_args.p = NULL;
    Sockpool.release_args.idx = 0u;
    Sockpool.release(sockpool_work);
    TEST_ASSERT_FALSE(Sockpool.ok);
    Sockpool.in_use_args.p = &g_pool;
    Sockpool.in_use(sockpool_work);
    TEST_ASSERT_EQUAL_size_t(POOL_N - 1u, Sockpool.n);
}

// find locates a live connection by id and stops locating it once the slot is released.
void test_find_tracks_the_live_ids(void)
{
    fresh();
    fill(100u, 10u);
    for (uint32_t i = 0; i < POOL_N; i++)
    {
        size_t idx = (size_t)-1;
        Sockpool.find_args.p = &g_pool;
        Sockpool.find_args.id = 100u + i;
        Sockpool.find_args.idx = &idx;
        Sockpool.find(sockpool_work);
        TEST_ASSERT_TRUE(Sockpool.ok);
        TEST_ASSERT_EQUAL_size_t((size_t)i, idx);
    }
    Sockpool.find_args.p = &g_pool;
    Sockpool.find_args.id = 999u;
    Sockpool.find_args.idx = NULL;
    Sockpool.find(sockpool_work);
    TEST_ASSERT_FALSE(Sockpool.ok);

    Sockpool.release_args.p = &g_pool;
    Sockpool.release_args.idx = 3u;
    Sockpool.release(sockpool_work);
    TEST_ASSERT_TRUE(Sockpool.ok);
    Sockpool.find_args.p = &g_pool;
    Sockpool.find_args.id = 103u;
    Sockpool.find_args.idx = NULL;
    Sockpool.find(sockpool_work);
    TEST_ASSERT_FALSE(Sockpool.ok);
    Sockpool.find_args.p = &g_pool;
    Sockpool.find_args.id = 102u;
    Sockpool.find_args.idx = NULL;
    Sockpool.find(sockpool_work);
    TEST_ASSERT_TRUE(Sockpool.ok); // the others are unaffected
}

// A recycled connection is found at the slot it took over, and the evicted id is gone.
void test_find_follows_a_recycle(void)
{
    fresh();
    fill(100u, 10u);
    size_t idx = (size_t)-1;
    Sockpool.acquire_args.p = &g_pool;
    Sockpool.acquire_args.id = 777u;
    Sockpool.acquire_args.now = 40u;
    Sockpool.acquire_args.idx = &idx;
    Sockpool.acquire_args.evicted_id = NULL;
    Sockpool.acquire(sockpool_work);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_RECYCLED, Sockpool.acq);
    size_t found = (size_t)-1;
    Sockpool.find_args.p = &g_pool;
    Sockpool.find_args.id = 777u;
    Sockpool.find_args.idx = &found;
    Sockpool.find(sockpool_work);
    TEST_ASSERT_TRUE(Sockpool.ok);
    TEST_ASSERT_EQUAL_size_t(idx, found);
    Sockpool.find_args.p = &g_pool;
    Sockpool.find_args.id = 100u;
    Sockpool.find_args.idx = NULL;
    Sockpool.find(sockpool_work);
    TEST_ASSERT_FALSE(Sockpool.ok);
}

// Both out-parameters are optional on the free path and on the recycle path.
void test_out_parameters_are_optional(void)
{
    fresh();
    fill(200u, 5u);
    Sockpool.acquire_args.p = &g_pool;
    Sockpool.acquire_args.id = 2u;
    Sockpool.acquire_args.now = 9u;
    Sockpool.acquire_args.idx = NULL;
    Sockpool.acquire_args.evicted_id = NULL;
    Sockpool.acquire(sockpool_work);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_RECYCLED, Sockpool.acq);
    Sockpool.find_args.p = &g_pool;
    Sockpool.find_args.id = 2u;
    Sockpool.find_args.idx = NULL;
    Sockpool.find(sockpool_work);
    TEST_ASSERT_TRUE(Sockpool.ok);
}

// A pool with no storage cannot serve anyone, and says so rather than writing through a null.
void test_a_pool_with_no_slots_fails_closed(void)
{
    SockPool empty;
    Sockpool.init_args.p = &empty;
    Sockpool.init_args.slots = NULL;
    Sockpool.init_args.n = 8u;
    Sockpool.init(sockpool_work);
    TEST_ASSERT_EQUAL_size_t(0u, empty.n);
    Sockpool.acquire_args.p = &empty;
    Sockpool.acquire_args.id = 1u;
    Sockpool.acquire_args.now = 1u;
    Sockpool.acquire_args.idx = NULL;
    Sockpool.acquire_args.evicted_id = NULL;
    Sockpool.acquire(sockpool_work);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FAIL, Sockpool.acq);
    Sockpool.in_use_args.p = &empty;
    Sockpool.in_use(sockpool_work);
    TEST_ASSERT_EQUAL_size_t(0u, Sockpool.n);
    Sockpool.find_args.p = &empty;
    Sockpool.find_args.id = 1u;
    Sockpool.find_args.idx = NULL;
    Sockpool.find(sockpool_work);
    TEST_ASSERT_FALSE(Sockpool.ok);

    Sockpool.init_args.p = &empty;
    Sockpool.init_args.slots = g_slots;
    Sockpool.init_args.n = 0u;
    Sockpool.init(sockpool_work);
    Sockpool.acquire_args.p = &empty;
    Sockpool.acquire_args.id = 1u;
    Sockpool.acquire_args.now = 1u;
    Sockpool.acquire_args.idx = NULL;
    Sockpool.acquire_args.evicted_id = NULL;
    Sockpool.acquire(sockpool_work);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FAIL, Sockpool.acq);

    Sockpool.acquire_args.p = NULL;
    Sockpool.acquire_args.id = 1u;
    Sockpool.acquire_args.now = 1u;
    Sockpool.acquire_args.idx = NULL;
    Sockpool.acquire_args.evicted_id = NULL;
    Sockpool.acquire(sockpool_work);
    TEST_ASSERT_EQUAL_INT(SOCK_ACQ_FAIL, Sockpool.acq);
    Sockpool.in_use_args.p = NULL;
    Sockpool.in_use(sockpool_work);
    TEST_ASSERT_EQUAL_size_t(0u, Sockpool.n);
    Sockpool.find_args.p = NULL;
    Sockpool.find_args.id = 1u;
    Sockpool.find_args.idx = NULL;
    Sockpool.find(sockpool_work);
    TEST_ASSERT_FALSE(Sockpool.ok);
    Sockpool.init_args.p = NULL;
    Sockpool.init_args.slots = g_slots;
    Sockpool.init_args.n = POOL_N;
    Sockpool.init(sockpool_work); // must not fault
    Sockpool.touch_args.p = NULL;
    Sockpool.touch_args.idx = 0u;
    Sockpool.touch_args.now = 1u;
    Sockpool.touch(sockpool_work);
}

// The number of live slots never exceeds the table, whatever the mix of operations.
void test_in_use_never_exceeds_the_table(void)
{
    fresh();
    for (uint32_t k = 0; k < 200u; k++)
    {
        if ((k & 3u) == 3u)
        {
            Sockpool.release_args.p = &g_pool;
            Sockpool.release_args.idx = (size_t)(k % POOL_N);
            Sockpool.release(sockpool_work);
            (void)Sockpool.ok;
        }
        else
        {
            Sockpool.acquire_args.p = &g_pool;
            Sockpool.acquire_args.id = k;
            Sockpool.acquire_args.now = k;
            Sockpool.acquire_args.idx = NULL;
            Sockpool.acquire_args.evicted_id = NULL;
            Sockpool.acquire(sockpool_work);
            (void)Sockpool.acq;
        }
        Sockpool.in_use_args.p = &g_pool;
        Sockpool.in_use(sockpool_work);
        TEST_ASSERT_TRUE(Sockpool.n <= (size_t)POOL_N);
    }
}
