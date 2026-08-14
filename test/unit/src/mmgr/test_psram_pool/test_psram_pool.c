// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the buffer placement policy and the SPI DMA ping-pong index (mmgr/psram_pool.h).
//
// No standards body publishes where a buffer belongs, so every expectation here is PROPERTIES: the
// rules psram_pool.h states, plus what must hold whatever the implementation.
//
// test_dram_reserve_is_never_spent is the load-bearing case. The reserve is the memory the stack
// lives in, so a DRAM placement is legal only while size + dram_reserve <= free_dram. Written that
// way the sum overflows for a large size and the refusal inverts into an acceptance, which is the
// one failure mode that hands out the reserve.

#include "mmgr/psram_pool.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// A DRAM placement must leave dram_reserve bytes free: legal exactly while size <= free_dram - reserve.
//   free_dram 1100, reserve 1000  ->  1100 - 1000 = 100 placeable bytes
//   size 100 is the largest that fits; 101 is the first that does not.
// The last case is the same refusal with no room for a sum: size + reserve exceeds what a size_t
// names, so a policy that adds before comparing wraps to a small number and answers DRAM.
void test_dram_reserve_is_never_spent(void)
{
    TEST_ASSERT_EQUAL_INT(PLACE_DRAM, protocore_psram_place(100, PROTO_FALSE, 1100, 0, 4096, 1000));
    TEST_ASSERT_EQUAL_INT(PLACE_FAIL, protocore_psram_place(101, PROTO_FALSE, 1100, 0, 4096, 1000));
    TEST_ASSERT_EQUAL_INT(PLACE_FAIL, protocore_psram_place(SIZE_MAX, PROTO_FALSE, 1100, 0, 4096, 1000));
    TEST_ASSERT_EQUAL_INT(PLACE_FAIL, protocore_psram_place(SIZE_MAX, PROTO_TRUE, 1100, 0, 4096, 1000));
    // A reserve of 0 places the whole free heap and nothing past it.
    TEST_ASSERT_EQUAL_INT(PLACE_DRAM, protocore_psram_place(1100, PROTO_FALSE, 1100, 0, 4096, 0));
    TEST_ASSERT_EQUAL_INT(PLACE_FAIL, protocore_psram_place(1101, PROTO_FALSE, 1100, 0, 4096, 0));
}

// A zero-byte buffer is refused before either heap is consulted, so no caller receives a pointer it
// may not write to.
void test_a_zero_size_request_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(PLACE_FAIL, protocore_psram_place(0, PROTO_FALSE, 120000, 2000000, 4096, 32768));
    TEST_ASSERT_EQUAL_INT(PLACE_FAIL, protocore_psram_place(0, PROTO_TRUE, 120000, 2000000, 4096, 32768));
    TEST_ASSERT_EQUAL_INT(PLACE_FAIL, protocore_psram_place(0, PROTO_FALSE, 0, 0, 0, 0));
}

// PSRAM is not DMA-capable, so a DMA buffer is DRAM or nothing - never the PSRAM fallback the same
// size would take without the requirement.
void test_dma_required_never_leaves_dram(void)
{
    // 8192 is at/above the threshold, which without the DMA requirement would prefer PSRAM.
    TEST_ASSERT_EQUAL_INT(PLACE_PSRAM, protocore_psram_place(8192, PROTO_FALSE, 120000, 2000000, 4096, 32768));
    TEST_ASSERT_EQUAL_INT(PLACE_DRAM, protocore_psram_place(8192, PROTO_TRUE, 120000, 2000000, 4096, 32768));
    // DRAM cannot hold it while keeping the reserve, and the roomy PSRAM does not substitute.
    TEST_ASSERT_EQUAL_INT(PLACE_FAIL, protocore_psram_place(8192, PROTO_TRUE, 40000, 2000000, 4096, 32768));
}

// At or above the threshold the buffer is large and cold: PSRAM first, DRAM only as the fallback.
// The boundary is inclusive, so threshold-1 and threshold land in different heaps with everything
// else held equal.
void test_at_or_above_the_threshold_prefers_psram(void)
{
    TEST_ASSERT_EQUAL_INT(PLACE_DRAM, protocore_psram_place(4095, PROTO_FALSE, 120000, 2000000, 4096, 32768));
    TEST_ASSERT_EQUAL_INT(PLACE_PSRAM, protocore_psram_place(4096, PROTO_FALSE, 120000, 2000000, 4096, 32768));
    // No PSRAM: the large buffer falls back to DRAM while the reserve still fits.
    TEST_ASSERT_EQUAL_INT(PLACE_DRAM, protocore_psram_place(65536, PROTO_FALSE, 120000, 0, 4096, 32768));
    // Neither heap can take it.
    TEST_ASSERT_EQUAL_INT(PLACE_FAIL, protocore_psram_place(65536, PROTO_FALSE, 80000, 0, 4096, 32768));
}

// Below the threshold the buffer is small and hot: DRAM first, PSRAM as the fallback, FAIL when
// neither has room.
void test_below_the_threshold_prefers_dram(void)
{
    TEST_ASSERT_EQUAL_INT(PLACE_DRAM, protocore_psram_place(512, PROTO_FALSE, 120000, 2000000, 4096, 32768));
    // 512 + 32768 > 33000, so the reserve rules DRAM out and PSRAM takes it.
    TEST_ASSERT_EQUAL_INT(PLACE_PSRAM, protocore_psram_place(512, PROTO_FALSE, 33000, 2000000, 4096, 32768));
    // Same DRAM refusal with a PSRAM too small to hold 512 bytes.
    TEST_ASSERT_EQUAL_INT(PLACE_FAIL, protocore_psram_place(512, PROTO_FALSE, 33000, 100, 4096, 32768));
}

// The two indices name different buffers at every point in the cycle: the CPU never fills the
// buffer DMA is draining. Two swaps return to the start, so the state has period two.
void test_pingpong_roles_are_always_opposite(void)
{
    PingPong pp;
    protocore_pingpong_init(&pp);
    TEST_ASSERT_EQUAL_UINT8(0, protocore_pingpong_fill_index(&pp));
    TEST_ASSERT_EQUAL_UINT8(1, protocore_pingpong_drain_index(&pp));

    for (int i = 0; i < 4; i++)
    {
        const uint8_t fill = protocore_pingpong_fill_index(&pp);
        const uint8_t drain = protocore_pingpong_drain_index(&pp);
        TEST_ASSERT_TRUE(fill < 2u);
        TEST_ASSERT_TRUE(drain < 2u);
        TEST_ASSERT_TRUE(fill != drain);
        // The swap reports the new fill index, and it is the buffer DMA was draining.
        TEST_ASSERT_EQUAL_UINT8(drain, protocore_pingpong_swap(&pp));
        TEST_ASSERT_EQUAL_UINT8(drain, protocore_pingpong_fill_index(&pp));
        TEST_ASSERT_EQUAL_UINT8(fill, protocore_pingpong_drain_index(&pp));
    }
    // Four swaps is two full periods: back to the initial roles.
    TEST_ASSERT_EQUAL_UINT8(0, protocore_pingpong_fill_index(&pp));
}

// Every accessor guards a null handle and answers the initial roles rather than dereferencing.
void test_pingpong_accessors_refuse_a_null_handle(void)
{
    protocore_pingpong_init(NULL);
    TEST_ASSERT_EQUAL_UINT8(0, protocore_pingpong_fill_index(NULL));
    TEST_ASSERT_EQUAL_UINT8(1, protocore_pingpong_drain_index(NULL));
    TEST_ASSERT_EQUAL_UINT8(0, protocore_pingpong_swap(NULL));
}
