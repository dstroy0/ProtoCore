// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the flash wear-levelling slot selector (server/storage/wearlevel.h).
//
// No standard publishes a wear-levelling policy, so every expectation here is category 3: a
// property the selector must hold whatever the implementation. The load-bearing one is
// test_pick_then_mark_levels_the_region_exactly - the module exists so a repeatedly-written record
// does not burn one block out early, and the only proof of that is that k*n writes driven by
// pick+mark leave every one of n slots at exactly k, with an imbalance of zero. A selector that is
// merely "usually fair" still wears one slot out first.

#include "server/storage/wearlevel/wearlevel.h"

#include <unity.h>

static uint8_t wearlevel_work[16]; // the borrow an entry takes; Wearlevel never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static size_t pick(const uint32_t *counts, size_t n)
{
    WearlevelV.args.counts = counts;
    WearlevelV.args.n = n;
    Wearlevel.pick(wearlevel_work);
    return WearlevelV.n_out;
}

static void mark(uint32_t *counts, size_t n, size_t idx)
{
    WearlevelV.args.counts_rw = counts;
    WearlevelV.args.n = n;
    WearlevelV.args.idx = idx;
    Wearlevel.mark(wearlevel_work);
}

static uint32_t imbalance(const uint32_t *counts, size_t n)
{
    WearlevelV.args.counts = counts;
    WearlevelV.args.n = n;
    Wearlevel.imbalance(wearlevel_work);
    return WearlevelV.spread;
}

// The whole point of the policy: writing where pick says, then recording it, spreads the wear
// perfectly. After 4000 writes over 8 slots every slot has taken exactly 500 and the imbalance is
// zero, because a slot can only be chosen again once every other slot has caught up with it.
void test_pick_then_mark_levels_the_region_exactly(void)
{
    uint32_t counts[8] = {0};
    for (int i = 0; i < 4000; i++)
    {
        mark(counts, 8, pick(counts, 8));
    }
    for (int i = 0; i < 8; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(500u, counts[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, imbalance(counts, 8));
}

// The same holds from an uneven start: the policy fills the laggards first, so the region converges
// to level rather than preserving the head start. 3 + 7 + 1 = 11 slots' worth of wear plus 13
// writes is 24 over 3 slots, which is 8 each.
void test_an_uneven_region_converges_to_level(void)
{
    uint32_t counts[3] = {3, 7, 1};
    for (int i = 0; i < 13; i++)
    {
        mark(counts, 3, pick(counts, 3));
    }
    TEST_ASSERT_EQUAL_UINT32(8u, counts[0]);
    TEST_ASSERT_EQUAL_UINT32(8u, counts[1]);
    TEST_ASSERT_EQUAL_UINT32(8u, counts[2]);
    TEST_ASSERT_EQUAL_UINT32(0u, imbalance(counts, 3));
}

// The chosen slot is the least worn, and a tie goes to the lowest index so the choice is the same
// on every boot from the same table.
void test_pick_is_the_least_worn_slot_and_ties_go_low(void)
{
    static const uint32_t only_one[1] = {99};
    TEST_ASSERT_EQUAL_size_t(0u, pick(only_one, 1));

    static const uint32_t last_is_lowest[4] = {5, 5, 5, 1};
    TEST_ASSERT_EQUAL_size_t(3u, pick(last_is_lowest, 4));

    static const uint32_t first_is_lowest[4] = {1, 5, 5, 5};
    TEST_ASSERT_EQUAL_size_t(0u, pick(first_is_lowest, 4));

    static const uint32_t middle_is_lowest[5] = {5, 5, 2, 5, 5};
    TEST_ASSERT_EQUAL_size_t(2u, pick(middle_is_lowest, 5));

    static const uint32_t all_tied[4] = {7, 7, 7, 7};
    TEST_ASSERT_EQUAL_size_t(0u, pick(all_tied, 4));

    static const uint32_t two_tied_low[4] = {9, 2, 2, 9};
    TEST_ASSERT_EQUAL_size_t(1u, pick(two_tied_low, 4));
}

// pick reads the table without changing it, so asking twice gives the same slot and the counts are
// untouched.
void test_pick_does_not_change_the_table(void)
{
    uint32_t counts[3] = {4, 1, 9};
    TEST_ASSERT_EQUAL_size_t(1u, pick(counts, 3));
    TEST_ASSERT_EQUAL_size_t(1u, pick(counts, 3));
    TEST_ASSERT_EQUAL_UINT32(4u, counts[0]);
    TEST_ASSERT_EQUAL_UINT32(1u, counts[1]);
    TEST_ASSERT_EQUAL_UINT32(9u, counts[2]);
}

// mark bumps one slot by one and leaves the rest alone.
void test_mark_bumps_exactly_one_slot(void)
{
    uint32_t counts[3] = {0, 0, 0};
    mark(counts, 3, 1);
    TEST_ASSERT_EQUAL_UINT32(0u, counts[0]);
    TEST_ASSERT_EQUAL_UINT32(1u, counts[1]);
    TEST_ASSERT_EQUAL_UINT32(0u, counts[2]);

    mark(counts, 3, 1);
    TEST_ASSERT_EQUAL_UINT32(2u, counts[1]);
}

// A count that has reached the 32-bit maximum stays there. Wrapping it to 0 would make the most
// worn slot look like the least worn one, and the policy would then write to it forever.
void test_a_saturated_count_never_wraps(void)
{
    uint32_t counts[2] = {0xFFFFFFFEu, 0u};
    mark(counts, 2, 0);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, counts[0]);
    mark(counts, 2, 0);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, counts[0]);
    mark(counts, 2, 0);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, counts[0]);

    // The saturated slot is still the most worn, so the pick stays on the other one.
    TEST_ASSERT_EQUAL_size_t(1u, pick(counts, 2));
}

// A slot index past the end of the table names nothing, so nothing is bumped.
void test_a_mark_past_the_table_bumps_nothing(void)
{
    uint32_t counts[2] = {0, 0};
    mark(counts, 2, 2);
    mark(counts, 2, (size_t)-1);
    TEST_ASSERT_EQUAL_UINT32(0u, counts[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, counts[1]);
}

// The imbalance is the highest count less the lowest, so a level region reports 0 and the metric
// grows by exactly what an off-policy write adds.
void test_imbalance_is_the_high_water_mark_less_the_low(void)
{
    uint32_t counts[4] = {10, 10, 10, 10};
    TEST_ASSERT_EQUAL_UINT32(0u, imbalance(counts, 4));

    counts[2] = 13;
    TEST_ASSERT_EQUAL_UINT32(3u, imbalance(counts, 4));

    counts[0] = 4;
    TEST_ASSERT_EQUAL_UINT32(9u, imbalance(counts, 4));

    // A single slot has nothing to be out of balance with.
    TEST_ASSERT_EQUAL_UINT32(0u, imbalance(counts, 1));

    // The widest possible spread does not overflow into a small number.
    static const uint32_t extremes[2] = {0u, 0xFFFFFFFFu};
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, imbalance(extremes, 2));
}

// A region with no slots, or no table at all, picks slot 0 and reports no imbalance rather than
// following a null pointer.
void test_an_absent_table_is_refused(void)
{
    static const uint32_t counts[2] = {5, 1};
    TEST_ASSERT_EQUAL_size_t(0u, pick(NULL, 4));
    TEST_ASSERT_EQUAL_size_t(0u, pick(counts, 0));
    TEST_ASSERT_EQUAL_UINT32(0u, imbalance(NULL, 4));
    TEST_ASSERT_EQUAL_UINT32(0u, imbalance(counts, 0));

    mark(NULL, 4, 0); // no table to bump
}
