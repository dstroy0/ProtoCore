// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/wearlevel: the flash wear-leveling slot selector.

#include "services/storage/wearlevel/wearlevel.h"
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_pick_least_worn_ties_lowest_index(void)
{
    uint32_t c[4] = {5, 2, 2, 9};
    TEST_ASSERT_EQUAL_size_t(1, pc_wearlevel_pick(c, 4)); // 2 at idx1 and idx2 -> lowest index
    uint32_t all[3] = {7, 7, 7};
    TEST_ASSERT_EQUAL_size_t(0, pc_wearlevel_pick(all, 3)); // all equal -> idx0
}

void test_pick_edge(void)
{
    TEST_ASSERT_EQUAL_size_t(0, pc_wearlevel_pick(nullptr, 4));
    uint32_t c[1] = {3};
    TEST_ASSERT_EQUAL_size_t(0, pc_wearlevel_pick(c, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_wearlevel_pick(c, 1));
}

void test_pick_plus_mark_levels_the_region(void)
{
    // Repeated pick+mark must keep every slot within 1 of the others (round-robin wear).
    uint32_t c[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4000; i++)
    {
        size_t s = pc_wearlevel_pick(c, 4);
        pc_wearlevel_mark(c, 4, s);
    }
    // 4000 writes over 4 slots -> exactly 1000 each, spread 0.
    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(1000, c[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(0, pc_wearlevel_spread(c, 4));
}

void test_mark_saturates_and_bounds(void)
{
    uint32_t c[2] = {0xFFFFFFFEu, 0};
    pc_wearlevel_mark(c, 2, 0);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, c[0]);
    pc_wearlevel_mark(c, 2, 0); // saturated: stays at max, no wrap to 0
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, c[0]);
    pc_wearlevel_mark(c, 2, 5); // out of range: no-op
    TEST_ASSERT_EQUAL_UINT32(0, c[1]);
    pc_wearlevel_mark(nullptr, 2, 0); // null counts: no-op, must not crash
}

void test_spread(void)
{
    uint32_t c[3] = {10, 4, 7};
    TEST_ASSERT_EQUAL_UINT32(6, pc_wearlevel_spread(c, 3)); // 10 - 4
    TEST_ASSERT_EQUAL_UINT32(0, pc_wearlevel_spread(nullptr, 3));
    uint32_t rising[3] = {2, 5, 9};                              // strictly increasing: exercises the "new max" branch
    TEST_ASSERT_EQUAL_UINT32(7, pc_wearlevel_spread(rising, 3)); // 9 - 2
    uint32_t nonnull[1] = {42};
    TEST_ASSERT_EQUAL_UINT32(0, pc_wearlevel_spread(nonnull, 0)); // non-null counts, n == 0
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pick_least_worn_ties_lowest_index);
    RUN_TEST(test_pick_edge);
    RUN_TEST(test_pick_plus_mark_levels_the_region);
    RUN_TEST(test_mark_saturates_and_bounds);
    RUN_TEST(test_spread);
    return UNITY_END();
}
