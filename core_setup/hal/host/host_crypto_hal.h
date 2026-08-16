// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_crypto_hal.h
 * @brief The host arm of the RSA/MPI accelerator: the same functions esp_crypto_hal.h states, in
 *        software.
 *
 * A part carries an RSA/MPI peripheral with a single-shot MODMULT and esp_crypto_hal.h drives it by
 * register. A host has no such window, so every arm built on that multiply - the canonical 25519
 * field layer, the P-256 field layer, and the modexp above them - would be uncompilable and
 * untestable off target. This states the SAME function against a software wide multiply and
 * reduction, so PROTOCORE_RSA_MODMUL_HW is a real capability on a native build.
 *
 * What this is and is not. It is the peripheral's CONTRACT - z = x*y mod m over @c words limbs,
 * canonical on the way out - answered in software. It is not its TIMING and not its register
 * sequence; those are esp_crypto_hal.h's and are checked on the part. The Montgomery constants the
 * peripheral needs to produce a plain residue are part of that register contract, so they are
 * accepted and unused here: an arm that passes the wrong ones is wrong on silicon and this will not
 * say so.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HOST_CRYPTO_HAL_H
#define PROTOCORE_HOST_CRYPTO_HAL_H

#include "protocore_config.h" // the entry point: PROTOCORE_HOST and the widths

#if PROTOCORE_HOST && PROTOCORE_RSA_MODMUL_HW

PROTOCORE_BEGIN_DECLS

/** @brief Limbs the widest operand this stand-in accepts: 2048-bit at 32 bits a limb. */
#define PROTOCORE_RSA_HOST_MAX_WORDS 64

/**
 * @brief Acquire the accelerator for a run of multiplies. The host stand-in has no clock, no reset
 *        and no other user, so this is the bracket's shape and nothing else.
 */
void protocore_rsa_hw_acquire(void);

/** @brief Release the accelerator. */
void protocore_rsa_hw_release(void);

/**
 * @brief `z = x*y mod m` over @p words little-endian limbs. Requires @ref protocore_rsa_hw_acquire.
 *
 * @param z      result, @p words limbs; safe to alias @p x or @p y.
 * @param x,y    operands, canonical (< m).
 * @param m      the modulus, canonical @p words limbs.
 * @param mprime Montgomery m' = -m^-1 mod 2^32. Part of the register contract; unused here.
 * @param rinv   R^2 mod m. Part of the register contract; unused here.
 * @param words  limbs per operand, at most PROTOCORE_RSA_HOST_MAX_WORDS.
 */
void protocore_rsa_modmul(uint32_t *z, const uint32_t *x, const uint32_t *y, const uint32_t *m, uint32_t mprime,
                          const uint32_t *rinv, unsigned words);

PROTOCORE_END_DECLS

#endif // PROTOCORE_HOST && PROTOCORE_RSA_MODMUL_HW

#endif // PROTOCORE_HOST_CRYPTO_HAL_H
