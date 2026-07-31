// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file psram_pool.cpp
 * @brief Buffer placement policy + SPI DMA ping-pong index manager (see psram_pool.h).
 */

#include "services/storage/psram_pool/psram_pool.h"

#if PC_ENABLE_PSRAM_POOL

namespace
{
// Does `size` fit in DRAM while still leaving `reserve` free? Overflow-safe (64-bit sum).
bool dram_fits(size_t size, size_t free_dram, size_t reserve)
{
    return (uint64_t)size + reserve <= (uint64_t)free_dram;
}
} // namespace

pc_place pc_psram_place(size_t size, bool dma_required, size_t free_dram, size_t free_psram, size_t psram_threshold,
                        size_t dram_reserve)
{
    if (size == 0)
    {
        return pc_place::PLACE_FAIL;
    }

    bool d_fits = dram_fits(size, free_dram, dram_reserve);
    bool p_fits = size <= free_psram;

    if (dma_required) // PSRAM is not DMA-capable: DRAM or bust.
    {
        return d_fits ? pc_place::PLACE_DRAM : pc_place::PLACE_FAIL;
    }

    if (size >= psram_threshold) // large / cold: prefer PSRAM.
    {
        if (p_fits)
        {
            return pc_place::PLACE_PSRAM;
        }
        if (d_fits)
        {
            return pc_place::PLACE_DRAM;
        }
        return pc_place::PLACE_FAIL;
    }

    // small / hot: prefer DRAM.
    if (d_fits)
    {
        return pc_place::PLACE_DRAM;
    }
    if (p_fits)
    {
        return pc_place::PLACE_PSRAM;
    }
    return pc_place::PLACE_FAIL;
}

void pc_pingpong_init(PingPong *pp)
{
    if (pp)
    {
        pp->fill_idx = 0;
    }
}

uint8_t pc_pingpong_fill_index(const PingPong *pp)
{
    return pp ? pp->fill_idx : 0;
}

uint8_t pc_pingpong_drain_index(const PingPong *pp)
{
    return pp ? (uint8_t)(pp->fill_idx ^ 1u) : 1;
}

uint8_t pc_pingpong_swap(PingPong *pp)
{
    if (!pp)
    {
        return 0;
    }
    pp->fill_idx ^= 1u;
    return pp->fill_idx;
}

#endif // PC_ENABLE_PSRAM_POOL
