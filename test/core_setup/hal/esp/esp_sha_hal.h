// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_sha_hal.h
 * @brief Single owner of the ESP32 SHA accelerator, by DIRECT register access - a self-contained HAL.
 *
 * The sibling of @ref esp_crypto_hal.h, same construction and same reasons. crypto/hash drives ONE
 * primitive here: compress one message block into a running hash state. Padding, block buffering and
 * the finalize-into-a-copy that keeps a context running are software on both arms and stay in
 * sha256.c / sha512.c, so this file is the compression step and nothing else.
 *
 * SELF-CONTAINED - no vendor headers or symbols
 * The die is identified in its board profile, which is where PROTOCORE_SHA_BASE and the clock, reset
 * and hold-clear bits are stated. This file includes NO `soc/` header, references NO vendor symbol
 * (no `esp_sha_*` / `sha_hal_*` / `sha_ll_*`, no `SHA_*_REG`, no `SYSTEM`/`PCR`/`HP_SYS_CLKRST`
 * struct) and tests NO `CONFIG_IDF_TARGET_*`. The block register offsets below are shared by every
 * die carrying the unified SHA peripheral; only the base and the clock/reset bits differ, and those
 * come from the profile.
 *
 * EXCLUSIVITY
 * The accelerator is shared state, and a running hash lives in its H registers between blocks. This
 * HAL owns a single PC recursive mutex (see esp_sha_hal.c) taken by @ref protocore_sha_hw_acquire and
 * dropped by @ref protocore_sha_hw_release; a streaming digest holds it across its whole run of
 * blocks, which is what keeps two contexts from interleaving into one H bank.
 */

#ifndef PROTOCORE_ESP_SHA_HAL_H
#define PROTOCORE_ESP_SHA_HAL_H

#include "protocore_config.h" // the entry point: the board profile's PROTOCORE_SHA_* and the widths

#if PROTOCORE_HAS_HW_SHA

#ifndef PROTOCORE_SHA_BASE
#error                                                                                                                 \
    "esp_sha_hal: PROTOCORE_HAS_HW_SHA is on but this die states no SHA register map - add its base and clock/reset bits to its board profile"
#endif

// A host build has no peripheral window, so the accessor comes from the host arm and this file's own
// definition below is skipped - which is what lets the capability be compiled and run off-target.
#if PROTOCORE_HOST
#include "test/core_setup/hal/host/host_hw_reg.h"
#endif

PROTOCORE_BEGIN_DECLS

// Raw memory-mapped register access - the only primitive; no vendor REG_* macro. A read and a write
// rather than one lvalue, because a host build models the bus that carries them and a narrow or
// byte-swapped access cannot be a dereference. On silicon both are the direct volatile access. The
// AES and RSA HALs state the same pair, so a build that pulls in several agrees on it.
#ifndef PROTOCORE_HW_RD
#define PROTOCORE_HW_RD(a) (*(volatile const uint32_t *)(uintptr_t)(a))
#endif
#ifndef PROTOCORE_HW_WR
#define PROTOCORE_HW_WR(a, v) ((*(volatile uint32_t *)(uintptr_t)(a)) = (uint32_t)(v))
#endif

// ── Block register offsets (identical on every die with the unified SHA peripheral) ──────────────
#define PROTOCORE_SHA_MODE (PROTOCORE_SHA_BASE + 0x00u)         // hash mode, the values below
#define PROTOCORE_SHA_T_STRING (PROTOCORE_SHA_BASE + 0x04u)     // SHA-512/t only
#define PROTOCORE_SHA_T_LENGTH (PROTOCORE_SHA_BASE + 0x08u)     // SHA-512/t only
#define PROTOCORE_SHA_BLOCK_NUM (PROTOCORE_SHA_BASE + 0x0Cu)    // DMA operation only
#define PROTOCORE_SHA_START (PROTOCORE_SHA_BASE + 0x10u)        // write 1: compress with the standard IV
#define PROTOCORE_SHA_CONTINUE (PROTOCORE_SHA_BASE + 0x14u)     // write 1: compress onto what H holds
#define PROTOCORE_SHA_BUSY (PROTOCORE_SHA_BASE + 0x18u)         // reads 1 while the core is running
#define PROTOCORE_SHA_DMA_START (PROTOCORE_SHA_BASE + 0x1Cu)    // DMA operation only
#define PROTOCORE_SHA_DMA_CONTINUE (PROTOCORE_SHA_BASE + 0x20u) // DMA operation only
#define PROTOCORE_SHA_CLEAR_IRQ (PROTOCORE_SHA_BASE + 0x24u)    // write 1 to clear the completion flag
#define PROTOCORE_SHA_INT_ENA (PROTOCORE_SHA_BASE + 0x28u)      // completion-interrupt enable (we poll: keep 0)
#define PROTOCORE_SHA_H_MEM (PROTOCORE_SHA_BASE + 0x40u)        // digest state, read and written as words
#define PROTOCORE_SHA_M_MEM (PROTOCORE_SHA_BASE + 0x80u)        // message block, written as words

// ── SHA_MODE values ──────────────────────────────────────────────────────────────────────────────
#define PROTOCORE_SHA_MODE_1 0u
#define PROTOCORE_SHA_MODE_224 1u
#define PROTOCORE_SHA_MODE_256 2u
#define PROTOCORE_SHA_MODE_384 3u
#define PROTOCORE_SHA_MODE_512 4u

/** @brief Iterations a busy poll takes before it gives up and the block zeroes the state. */
#ifndef PROTOCORE_SHA_SPIN_MAX
#define PROTOCORE_SHA_SPIN_MAX 100000u
#endif

/**
 * @brief Acquire the SHA accelerator for a run of blocks (PC lock + direct-register bring-up).
 * @note  Bracket every streaming digest with acquire/release. Implemented in esp_sha_hal.c (the
 *        exclusivity mutex must be one global instance, so it cannot live in this header). Poll-only.
 */
void protocore_sha_hw_acquire(void);

/** @brief Release the SHA accelerator (drop the PC lock). */
void protocore_sha_hw_release(void);

/**
 * @brief Compress one message block into @p h. Requires @ref protocore_sha_hw_acquire first.
 *
 * @param mode    one of the PROTOCORE_SHA_MODE_* values.
 * @param h       running state, @p hwords words; read back from the accelerator on return.
 * @param hwords  state words: 8 for SHA-256, 16 for SHA-512.
 * @param blk     one message block, @p bwords words.
 * @param bwords  block words: 16 for SHA-256, 32 for SHA-512.
 * @param first   true for the first block of a digest (the accelerator supplies the standard IV),
 *                false to compress onto the state written back from @p h.
 */
static inline void protocore_sha_hw_block(uint32_t mode, uint32_t *h, unsigned hwords, const uint32_t *blk,
                                          unsigned bwords, proto_bool first)
{
    PROTOCORE_HW_WR(PROTOCORE_SHA_MODE, mode);
    if (!first)
    {
        for (unsigned i = 0; i < hwords; i++)
        {
            // this context's running state, so two digests do not interleave in one bank
            PROTOCORE_HW_WR(PROTOCORE_SHA_H_MEM + 4u * i, h[i]);
        }
    }
    for (unsigned i = 0; i < bwords; i++)
    {
        PROTOCORE_HW_WR(PROTOCORE_SHA_M_MEM + 4u * i, blk[i]);
    }
    PROTOCORE_HW_WR(PROTOCORE_SHA_CLEAR_IRQ, 1u); // clear any stale completion flag before starting
    PROTOCORE_HW_WR(first ? PROTOCORE_SHA_START : PROTOCORE_SHA_CONTINUE, 1u);
    uint32_t spins = 0u;
    while (PROTOCORE_HW_RD(PROTOCORE_SHA_BUSY) != 0u)
    {
        spins++;
        if (spins >= PROTOCORE_SHA_SPIN_MAX)
        {
            for (unsigned i = 0; i < hwords; i++)
            {
                h[i] = 0u; // a zero state fails every downstream comparison
            }
            return;
        }
    }
    for (unsigned i = 0; i < hwords; i++)
    {
        h[i] = PROTOCORE_HW_RD(PROTOCORE_SHA_H_MEM + 4u * i);
    }
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_HAS_HW_SHA

#endif // PROTOCORE_ESP_SHA_HAL_H
