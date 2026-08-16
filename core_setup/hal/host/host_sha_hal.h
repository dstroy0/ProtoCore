// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_sha_hal.h
 * @brief The host arm of the SHA accelerator: the same functions esp_sha_hal.h states, in software.
 *
 * A part carries a hashing peripheral and esp_sha_hal.h drives it by register. A host has no such
 * window, so the accelerated arm of every hash-based module would be uncompilable and untestable off
 * target. This states the SAME function set against a software compression, so PROTOCORE_HAS_HW_SHA
 * is a real capability on a native build: the arm compiles, runs, and answers with the same bytes.
 *
 * What this is and is not. It is the peripheral's CONTRACT - compress one message block into a
 * running state, with the standard IV on the first block of a digest - answered in software. It is
 * not the peripheral's TIMING or its register sequence; those are esp_sha_hal.h's and are checked on
 * the part. Padding, block buffering and the finalize-into-a-copy stay in the modules either way, so
 * a native run exercises the arm's own logic: the state save and restore, the block loop, the split.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HOST_SHA_HAL_H
#define PROTOCORE_HOST_SHA_HAL_H

#include "protocore_config.h" // the entry point: PROTOCORE_HOST and the widths

#if PROTOCORE_HOST && PROTOCORE_HAS_HW_SHA

PROTOCORE_BEGIN_DECLS

// -- SHA_MODE values, the same numbering the peripheral uses --------------------------------------
#ifndef PROTOCORE_SHA_MODE_1
#define PROTOCORE_SHA_MODE_1 0u
#define PROTOCORE_SHA_MODE_224 1u
#define PROTOCORE_SHA_MODE_256 2u
#define PROTOCORE_SHA_MODE_384 3u
#define PROTOCORE_SHA_MODE_512 4u
#endif

/**
 * @brief Acquire the accelerator. The host stand-in has no clock or reset to bring up and no other
 *        user to exclude, so this is the bracket's shape and nothing else.
 */
void protocore_sha_hw_acquire(void);

/** @brief Release the accelerator. */
void protocore_sha_hw_release(void);

/**
 * @brief Compress one message block into @p h. Requires @ref protocore_sha_hw_acquire first.
 *
 * @param mode    one of the PROTOCORE_SHA_MODE_* values.
 * @param h       running state, @p hwords words; read back on return.
 * @param hwords  state words: 5 for SHA-1, 8 for SHA-256, 16 for SHA-512 (two words per lane).
 * @param blk     one message block, @p bwords words, the message bytes in memory order.
 * @param bwords  block words: 16 for SHA-1 and SHA-256, 32 for SHA-512.
 * @param first   true for the first block of a digest (the standard IV is supplied), false to
 *                compress onto the state @p h carries.
 */
void protocore_sha_hw_block(uint32_t mode, uint32_t *h, unsigned hwords, const uint32_t *blk, unsigned bwords,
                            proto_bool first);

PROTOCORE_END_DECLS

#endif // PROTOCORE_HOST && PROTOCORE_HAS_HW_SHA

#endif // PROTOCORE_HOST_SHA_HAL_H
