// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file psram_pool.h
 * @brief Buffer placement policy (DRAM vs PSRAM) + SPI DMA ping-pong index manager
 *        (PC_ENABLE_PSRAM_POOL).
 *
 * An ESP32 with PSRAM has two heaps: fast internal DRAM (scarce, DMA-capable) and large external PSRAM
 * (roomy, but not DMA-capable for most peripherals and slower). Serving big web assets / net buffers well
 * means putting the large, cold buffers in PSRAM and keeping the small, hot, DMA buffers in DRAM, while
 * always leaving an internal-DRAM reserve so the stack does not starve. That placement choice is a pure
 * policy; the actual `heap_caps_calloc(..., MALLOC_CAP_SPIRAM / MALLOC_CAP_DMA)` is the app's.
 *
 * This module is that policy (`pc_psram_place`) plus the classic SPI DMA **ping-pong** double-buffer
 * bookkeeping (`pc_pingpong_*`): while DMA drains one buffer, the CPU fills the other, and a swap
 * exchanges their roles. Pure, no heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_PSRAM_POOL_H
#define PROTOCORE_PSRAM_POOL_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_PSRAM_POOL

/** @brief Placement verdict (the sole return of pc_psram_place). */
typedef enum PROTO_ENUM_PACKED
{
    PLACE_DRAM = 0,  ///< allocate in internal DRAM.
    PLACE_PSRAM = 1, ///< allocate in external PSRAM.
    PLACE_FAIL = 2   ///< neither heap can satisfy the request.
} pc_place;

/**
 * @brief Decide where a buffer should live.
 *
 * Rules: a zero-size request fails; a DMA-required buffer must go in DRAM (or fail); a buffer at or above
 * @p psram_threshold prefers PSRAM (falling back to DRAM); a smaller buffer prefers DRAM (falling back to
 * PSRAM). A DRAM placement must still leave @p dram_reserve bytes free in DRAM.
 *
 * @param size           requested bytes.
 * @param dma_required   true if the buffer must be DMA-capable (DRAM only).
 * @param free_dram      currently free internal DRAM.
 * @param free_psram     currently free PSRAM (0 if no PSRAM).
 * @param psram_threshold size at/above which PSRAM is preferred.
 * @param dram_reserve   internal DRAM to keep free after a DRAM placement.
 * @return PLACE_DRAM / PLACE_PSRAM / PLACE_FAIL.
 */
pc_place pc_psram_place(size_t size, proto_bool dma_required, size_t free_dram, size_t free_psram,
                        size_t psram_threshold, size_t dram_reserve);

/** @brief SPI DMA ping-pong double-buffer state. */
typedef struct
{
    uint8_t fill_idx; ///< buffer the CPU is filling (DMA drains the other).
} PingPong;

/** @brief Initialize: CPU fills buffer 0, DMA drains buffer 1. */
void pc_pingpong_init(PingPong *pp);

/** @brief The buffer index the CPU should fill. */
uint8_t pc_pingpong_fill_index(const PingPong *pp);

/** @brief The buffer index DMA should drain (the other one). */
uint8_t pc_pingpong_drain_index(const PingPong *pp);

/** @brief Swap roles (a filled buffer is handed to DMA; the drained one is now filled). @return new fill index. */
uint8_t pc_pingpong_swap(PingPong *pp);

#endif // PC_ENABLE_PSRAM_POOL

PROTO_END_DECLS

#endif // PROTOCORE_PSRAM_POOL_H
