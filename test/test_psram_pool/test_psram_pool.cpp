// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/psram_pool: DRAM/PSRAM placement policy + DMA ping-pong bookkeeping.

#include "services/storage/psram_pool/psram_pool.h"
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_place_large_prefers_psram(void)
{
    // 64KB asset, threshold 4KB, plenty of both heaps, 32KB DRAM reserve.
    TEST_ASSERT_EQUAL_INT(pc_place::PLACE_PSRAM, pc_psram_place(65536, false, 120000, 2000000, 4096, 32768));
    // No PSRAM available -> falls back to DRAM (fits with reserve).
    TEST_ASSERT_EQUAL_INT(pc_place::PLACE_DRAM, pc_psram_place(65536, false, 120000, 0, 4096, 32768));
    // No PSRAM and DRAM can't hold it while keeping the reserve -> FAIL.
    TEST_ASSERT_EQUAL_INT(pc_place::PLACE_FAIL, pc_psram_place(65536, false, 80000, 0, 4096, 32768));
}

void test_place_small_prefers_dram(void)
{
    // 512B hot buffer, threshold 4KB -> DRAM.
    TEST_ASSERT_EQUAL_INT(pc_place::PLACE_DRAM, pc_psram_place(512, false, 120000, 2000000, 4096, 32768));
    // DRAM too tight (reserve dominates) -> PSRAM fallback.
    TEST_ASSERT_EQUAL_INT(pc_place::PLACE_PSRAM, pc_psram_place(512, false, 33000, 2000000, 4096, 32768));
}

void test_place_dma_forces_dram(void)
{
    // DMA-required buffer must be DRAM even if large.
    TEST_ASSERT_EQUAL_INT(pc_place::PLACE_DRAM, pc_psram_place(8192, true, 120000, 2000000, 4096, 32768));
    // DMA required but DRAM can't fit with reserve -> FAIL (PSRAM is not DMA-capable).
    TEST_ASSERT_EQUAL_INT(pc_place::PLACE_FAIL, pc_psram_place(8192, true, 40000, 2000000, 4096, 32768));
}

void test_place_edges(void)
{
    TEST_ASSERT_EQUAL_INT(pc_place::PLACE_FAIL, pc_psram_place(0, false, 120000, 2000000, 4096, 32768));
    // At exactly the threshold -> treated as large (>=), prefers PSRAM.
    TEST_ASSERT_EQUAL_INT(pc_place::PLACE_PSRAM, pc_psram_place(4096, false, 120000, 2000000, 4096, 0));
    // Exact DRAM fit: size + reserve == free_dram is allowed.
    TEST_ASSERT_EQUAL_INT(pc_place::PLACE_DRAM, pc_psram_place(100, false, 1100, 0, 4096, 1000));
}

void test_place_small_neither_fits(void)
{
    // small / hot buffer: DRAM too tight (reserve dominates) AND PSRAM too small -> FAIL.
    TEST_ASSERT_EQUAL_INT(pc_place::PLACE_FAIL, pc_psram_place(512, false, 33000, 100, 4096, 32768));
}

void test_pingpong(void)
{
    PingPong pp;
    pc_pingpong_init(&pp);
    TEST_ASSERT_EQUAL_UINT8(0, pc_pingpong_fill_index(&pp));
    TEST_ASSERT_EQUAL_UINT8(1, pc_pingpong_drain_index(&pp));
    // Swap: roles flip.
    TEST_ASSERT_EQUAL_UINT8(1, pc_pingpong_swap(&pp));
    TEST_ASSERT_EQUAL_UINT8(1, pc_pingpong_fill_index(&pp));
    TEST_ASSERT_EQUAL_UINT8(0, pc_pingpong_drain_index(&pp));
    // Swap back.
    TEST_ASSERT_EQUAL_UINT8(0, pc_pingpong_swap(&pp));
    TEST_ASSERT_EQUAL_UINT8(0, pc_pingpong_fill_index(&pp));
}

void test_pingpong_null_safety(void)
{
    // Every pc_pingpong_* accessor guards against a null PingPong* and returns a fixed fallback.
    pc_pingpong_init(NULL); // must not crash; nothing to assert (no state to touch).
    TEST_ASSERT_EQUAL_UINT8(0, pc_pingpong_fill_index(NULL));
    TEST_ASSERT_EQUAL_UINT8(1, pc_pingpong_drain_index(NULL));
    TEST_ASSERT_EQUAL_UINT8(0, pc_pingpong_swap(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_place_large_prefers_psram);
    RUN_TEST(test_place_small_prefers_dram);
    RUN_TEST(test_place_dma_forces_dram);
    RUN_TEST(test_place_edges);
    RUN_TEST(test_place_small_neither_fits);
    RUN_TEST(test_pingpong);
    RUN_TEST(test_pingpong_null_safety);
    return UNITY_END();
}
