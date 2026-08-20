// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ghash.h
 * @brief GHASH (the GF(2^128) universal hash under AES-GCM, NIST SP 800-38D sec 6.3), 4-bit table.
 *
 * The shared GHASH primitive for the whole library (AES-256-GCM, AES-128-GCM, DTLS 1.3). The textbook
 * GHASH is a 128-iteration bitwise GF(2^128) multiply per 16-byte block, which makes AES-GCM the
 * throughput floor of every AEAD record layer. There is no hardware GF-multiply on any die in the list,
 * so the lever is algorithmic: the 4-bit table method (Shoup) builds a 16-entry table of i*H once per
 * key, then folds four bits of the accumulator per step.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GHASH_H
#define PROTOCORE_GHASH_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_GHASH

PROTOCORE_BEGIN_DECLS

/** @brief GHASH subkey length in bytes. */
#define PROTOCORE_GHASH_KEY_LEN 16

/** @brief GHASH accumulator length in bytes. */
#define PROTOCORE_GHASH_ACC_LEN 16

// PROTOCORE_GHASH_BORROW - the bytes a bound subkey runs out of - is stated in protocore_config.h,
// which sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The subkey a table is built from. */
typedef struct
{
    const uint8_t *h; ///< PROTOCORE_GHASH_KEY_LEN bytes, H = E(K, 0^128)
} GhashKeyArgs;
/** @brief The accumulator one multiply runs in. */
typedef struct
{
    uint8_t *acc; ///< PROTOCORE_GHASH_ACC_LEN bytes, multiplied by H in place
} GhashMulArgs;
/** @brief The accumulator and the bytes a fold runs over. */
typedef struct
{
    uint8_t *acc;        ///< PROTOCORE_GHASH_ACC_LEN bytes, folded in place
    const uint8_t *data; ///< the bytes, NULL when @c len is 0
    size_t len;          ///< how many
} GhashUpdateArgs;
/**
 * @brief GHASH (NIST SP 800-38D sec 6.3), 4-bit table.
 *
 * A caller sets the members a call takes, invokes it through ::Ghash with the bytes it runs out of, and
 * reads the outcome off the same handle. How those bytes are carved is this module's and is never named
 * here.
 *
 *   Ghash.key_args.h = h;
 *   Ghash.key_init(work);
 *   Ghash.update_args.acc = acc;
 *   Ghash.update_args.data = aad;
 *   Ghash.update_args.len = aad_len;
 *   Ghash.update(work);
 *   Ghash.mul_args.acc = acc;
 *   Ghash.mul(work);
 *
 * @var GhashNs::key_args     the subkey a table is built from
 * @var GhashNs::mul_args     the accumulator one multiply runs in
 * @var GhashNs::update_args  the accumulator and the bytes a fold runs over
 * @var GhashNs::ok           a call's true/false outcome
 * @var GhashNs::key_init     build the 4-bit table for the subkey, once per key
 * @var GhashNs::mul          acc = acc * H in GF(2^128) under that table
 * @var GhashNs::update       fold the bytes into acc 16 at a time, a final short block MSB-zero-padded
 *
 * The accumulator is the CALLER's 16 bytes: both folding entries work in place on the buffer their args
 * name and hold it no longer than the call.
 *
 * @c work is PROTOCORE_GHASH_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. The
 * borrow IS the table, so two subkeys are two borrows and never collide, and the table dies with the
 * release.
 *
 * No storage member and no context: a caller sets operands and reads @ref GhashNs::ok, and that is all
 * the surface there is.
 */
typedef struct
{
    GhashKeyArgs key_args;
    GhashMulArgs mul_args;
    GhashUpdateArgs update_args;
    proto_bool ok;
} GhashVars;

/** @brief The operands and the outcome. */
extern GhashVars GhashV;

/** @brief The entries. */
typedef struct
{
    void (*const key_init)(uint8_t *restrict work);
    void (*const mul)(uint8_t *restrict work);
    void (*const update)(uint8_t *restrict work);
} GhashNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in GhashV or a region of the borrow at a fixed offset.
void protocore_ghash_key_init(uint8_t *restrict work);
void protocore_ghash_mul(uint8_t *restrict work);
void protocore_ghash_update(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Ghash.key_init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const GhashNs Ghash __attribute__((unused)) = {
    .key_init = protocore_ghash_key_init,
    .mul = protocore_ghash_mul,
    .update = protocore_ghash_update,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_GHASH

#endif // PROTOCORE_GHASH_H
