// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_aes_hal.h
 * @brief Single owner of the ESP32 AES accelerator, by DIRECT register access - a self-contained HAL.
 *
 * The sibling of @ref esp_sha_hal.h, same construction and same reasons. crypto/cipher, crypto/aead
 * and crypto/mac drive ONE primitive here: encrypt one 16-byte block under the loaded key. Every mode
 * built on that block - CTR, GCM's GCTR and GHASH, CCM's CBC-MAC, CMAC's subkeys - is software on both
 * arms and stays in its own module, so this file is the block step and nothing else.
 *
 * SELF-CONTAINED - no vendor headers or symbols
 * The die is identified in its board profile, which is where PROTOCORE_AES_BASE and the clock and reset
 * bits are stated. This file includes NO `soc/` header, references NO vendor symbol (no `esp_aes_*` /
 * `aes_hal_*` / `aes_ll_*`, no `AES_*_REG`, no `SYSTEM`/`PCR`/`HP_SYS_CLKRST` struct) and tests NO
 * `CONFIG_IDF_TARGET_*`. The block register offsets below are shared by every die carrying the unified
 * AES peripheral; only the base and the clock/reset bits differ, and those come from the profile.
 *
 * EXCLUSIVITY
 * The accelerator is shared state: the key and mode registers persist between blocks, so a run of
 * blocks under one key is a critical section. This HAL owns a single PC recursive mutex (see
 * esp_aes_hal.c) taken by @ref protocore_aes_hw_acquire and dropped by @ref protocore_aes_hw_release;
 * a keyed operation holds it across its whole run of blocks, which is what keeps two keys from
 * interleaving into one key bank.
 */

#ifndef PROTOCORE_ESP_AES_HAL_H
#define PROTOCORE_ESP_AES_HAL_H

#include "protocore_config.h" // the entry point: the board profile's PROTOCORE_AES_* and the widths

#if PROTOCORE_HAS_HW_AES

#ifndef PROTOCORE_AES_BASE
#error                                                                                                                 \
    "esp_aes_hal: PROTOCORE_HAS_HW_AES is on but this die states no AES register map - add its base and clock/reset bits to its board profile"
#endif

// A host build has no peripheral window, so the accessor comes from the host arm and this file's own
// definition below is skipped - which is what lets the capability be compiled and run off-target.
#if PROTOCORE_HOST
#include "test/core_setup/hal/host/host_hw_reg.h"
#endif

#include "mmgr/rawmemcpy.h" // proto_raw_u32 - the aliasing-permitted word load at any alignment

PROTOCORE_BEGIN_DECLS

// Raw memory-mapped register access - the only primitive; no vendor REG_* macro. A read and a write
// rather than one lvalue, because a host build models the bus that carries them and a narrow or
// byte-swapped access cannot be a dereference. On silicon both are the direct volatile access. The
// SHA and RSA HALs state the same pair, so a build that pulls in several agrees on it.
#ifndef PROTOCORE_HW_RD
#define PROTOCORE_HW_RD(a) (*(volatile const uint32_t *)(uintptr_t)(a))
#endif
#ifndef PROTOCORE_HW_WR
#define PROTOCORE_HW_WR(a, v) ((*(volatile uint32_t *)(uintptr_t)(a)) = (uint32_t)(v))
#endif

// -- Block register offsets (identical on every die with the unified AES peripheral) ---------------
#define PROTOCORE_AES_KEY_MEM (PROTOCORE_AES_BASE + 0x00u)  // key, written as words, 4 to 8 of them
#define PROTOCORE_AES_TEXT_IN (PROTOCORE_AES_BASE + 0x20u)  // input block, 4 words
#define PROTOCORE_AES_TEXT_OUT (PROTOCORE_AES_BASE + 0x30u) // output block, 4 words
#define PROTOCORE_AES_MODE (PROTOCORE_AES_BASE + 0x40u)     // direction and key length, values below
#define PROTOCORE_AES_ENDIAN (PROTOCORE_AES_BASE + 0x44u)   // byte order; reset default is what we write words in
#define PROTOCORE_AES_TRIGGER (PROTOCORE_AES_BASE + 0x48u)  // write 1: transform the loaded block
#define PROTOCORE_AES_STATE (PROTOCORE_AES_BASE + 0x4Cu)    // 0 idle, 1 busy, 2 done

// -- AES_MODE values: direction base plus (key_bytes / 8 - 2) --------------------------------------
#define PROTOCORE_AES_MODE_ENCRYPT 0u
#define PROTOCORE_AES_MODE_DECRYPT 4u

/** @brief Bytes in one AES block. */
#define PROTOCORE_AES_HW_BLOCK_LEN 16u

/** @brief Iterations a busy poll takes before it gives up and the block zeroes the output. */
#ifndef PROTOCORE_AES_SPIN_MAX
#define PROTOCORE_AES_SPIN_MAX 100000u
#endif

/**
 * @brief Acquire the AES accelerator for a run of blocks (PC lock + direct-register bring-up).
 * @note  Bracket every keyed operation with acquire/release. Implemented in esp_aes_hal.c (the
 *        exclusivity mutex must be one global instance, so it cannot live in this header). Poll-only.
 */
void protocore_aes_hw_acquire(void);

/** @brief Release the AES accelerator (drop the PC lock). */
void protocore_aes_hw_release(void);

/**
 * @brief Load the encryption key and direction. Requires @ref protocore_aes_hw_acquire first.
 *
 * @param key        the key bytes, @p key_bytes of them.
 * @param key_bytes  16 for AES-128, 24 for AES-192, 32 for AES-256.
 */
static inline void protocore_aes_hw_setkey(const uint8_t *key, unsigned key_bytes)
{
    const unsigned words = key_bytes / 4u;
    for (unsigned i = 0; i < words; i++)
    {
        PROTOCORE_HW_WR(PROTOCORE_AES_KEY_MEM + 4u * i, proto_raw_u32(key + 4u * i));
    }
    PROTOCORE_HW_WR(PROTOCORE_AES_MODE, PROTOCORE_AES_MODE_ENCRYPT + (key_bytes / 8u - 2u));
}

/**
 * @brief Encrypt one block under the loaded key. Requires @ref protocore_aes_hw_setkey first.
 *
 * @param in   PROTOCORE_AES_HW_BLOCK_LEN input bytes.
 * @param out  PROTOCORE_AES_HW_BLOCK_LEN output bytes; may alias @p in.
 */
static inline void protocore_aes_hw_block(const uint8_t *in, uint8_t *out)
{
    for (unsigned i = 0; i < 4u; i++)
    {
        PROTOCORE_HW_WR(PROTOCORE_AES_TEXT_IN + 4u * i, proto_raw_u32(in + 4u * i));
    }
    PROTOCORE_HW_WR(PROTOCORE_AES_TRIGGER, 1u);
    uint32_t spins = 0u;
    while (PROTOCORE_HW_RD(PROTOCORE_AES_STATE) != 0u)
    {
        spins++;
        if (spins >= PROTOCORE_AES_SPIN_MAX)
        {
            for (unsigned i = 0; i < PROTOCORE_AES_HW_BLOCK_LEN; i++)
            {
                out[i] = 0u; // a zero block fails every downstream comparison
            }
            return;
        }
    }
    for (unsigned i = 0; i < 4u; i++)
    {
        proto_raw_put_u32(out + 4u * i, PROTOCORE_HW_RD(PROTOCORE_AES_TEXT_OUT + 4u * i));
    }
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_HAS_HW_AES

#endif // PROTOCORE_ESP_AES_HAL_H
