// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_aes_hal.h
 * @brief The host arm of the AES accelerator: the same functions esp_aes_hal.h states, in software.
 *
 * A part carries an AES peripheral and esp_aes_hal.h drives it by register. A host has no such
 * window, so the accelerated arm of every AES-based module would be uncompilable and untestable off
 * target. This states the SAME function set against the software block, so PROTOCORE_HAS_HW_AES is a
 * real capability on a native build: the arm compiles, runs, and answers with the same bytes.
 *
 * What this is and is not. It is the peripheral's CONTRACT - key loading, one 16-byte ECB encrypt
 * under that key, and the exclusivity bracket around a run of blocks - answered in software. It is
 * not the peripheral's TIMING or its register sequence; those are esp_aes_hal.h's and are checked on
 * the part. A module's accelerated arm is otherwise the same code either way, so a native run
 * exercises the arm's own logic: the key staging, the block loop, the borrow carve.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HOST_AES_HAL_H
#define PROTOCORE_HOST_AES_HAL_H

#include "protocore_config.h" // the entry point: PROTOCORE_HOST and the widths

#if PROTOCORE_HOST && PROTOCORE_HAS_HW_AES

PROTOCORE_BEGIN_DECLS

/** @brief Bytes in one AES block. */
#ifndef PROTOCORE_AES_HW_BLOCK_LEN
#define PROTOCORE_AES_HW_BLOCK_LEN 16u
#endif

/** @brief Words the widest key schedule this stand-in holds: AES-256 is 15 round keys of 4. */
#define PROTOCORE_AES_HOST_RK_WORDS 60

/**
 * @brief Acquire the accelerator. The host stand-in has no clock or reset to bring up and no other
 *        user to exclude, so this is the bracket's shape and nothing else.
 */
void protocore_aes_hw_acquire(void);

/** @brief Release the accelerator. */
void protocore_aes_hw_release(void);

/**
 * @brief Load the encryption key. Requires @ref protocore_aes_hw_acquire first.
 *
 * @param key        the key bytes, @p key_bytes of them.
 * @param key_bytes  16 for AES-128, 24 for AES-192, 32 for AES-256.
 */
void protocore_aes_hw_setkey(const uint8_t *key, unsigned key_bytes);

/**
 * @brief Encrypt one block under the loaded key. Requires @ref protocore_aes_hw_setkey first.
 *
 * @param in   PROTOCORE_AES_HW_BLOCK_LEN input bytes.
 * @param out  PROTOCORE_AES_HW_BLOCK_LEN output bytes; may alias @p in.
 */
void protocore_aes_hw_block(const uint8_t *in, uint8_t *out);

PROTOCORE_END_DECLS

#endif // PROTOCORE_HOST && PROTOCORE_HAS_HW_AES

#endif // PROTOCORE_HOST_AES_HAL_H
