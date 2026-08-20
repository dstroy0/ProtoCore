// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rng.h
 * @brief The seed a worker holds, and the draw over it.
 *
 * One draw: give me @p len bytes, and that is what comes back. The generator keeps its own schedule -
 * it redraws from the platform once its budget is spent, so no caller sets the pace and no entropy
 * source is drained by whichever module asks most often.
 *
 * ## What belongs here and what does not
 *
 * An algorithm whose specification dictates how it expands its randomness keeps that expansion and
 * does not call this: ECDSA derives its nonce with the RFC 6979 HMAC-SHA256 DRBG (ecdsa.c), and
 * ML-KEM is the FIPS 203 derandomized form, so its caller passes (d, z) and m in as values. This is
 * the draw for the cases no specification pins down - a Diffie-Hellman private, SSH packet padding,
 * a GUID, a WebSocket mask, a nonce - which would otherwise be one copy of the same expansion per
 * caller.
 *
 * ## The expansion
 *
 * ChaCha20 keystream (RFC 8439) under the seed, the counter incrementing per 64-byte block. A stream
 * cipher is what a keystream-from-a-seed is, it is already in the tree, and it costs one ARX
 * permutation per 64 bytes rather than one HMAC per 32.
 *
 * After every draw the seed is replaced with 32 fresh keystream bytes, so the state that produced a
 * value is gone before the value is returned and a later disclosure does not recover an earlier
 * draw. Independently of that ratchet, the seed is redrawn from the platform once the draw budget
 * @ref PROTOCORE_RAND_RESEED_BYTES is spent.
 *
 * The seed lives in the caller's borrow, one span per worker, so two workers never share a generator
 * and the draw path takes no lock.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RNG_H
#define PROTOCORE_RNG_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_RNG

PROTOCORE_BEGIN_DECLS

/** @brief The seed: a ChaCha20 key. */
#define PROTOCORE_RAND_SEED_LEN 32

// PROTOCORE_RNG_BORROW - the bytes a generator runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. A caller takes them once, for the life of the program, and passes
// the pointer to every call.

/** @brief Where a draw lands. */
typedef struct
{
    uint8_t *out; ///< destination
    size_t len;   ///< how many; any length, the block counter carries across 64-byte boundaries
} RngFillArgs;

/**
 * @brief The general-purpose draw (ChaCha20 keystream, RFC 8439).
 *
 * A caller sets the members a call takes, invokes it through ::Rng with the bytes it runs out of, and
 * reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Rng.fill_args.out = nonce;
 *   Rng.fill_args.len = nonce_len;
 *   Rng.fill(protocore_rng_span());
 *
 * @var RngNs::fill_args  where a draw lands
 * @var RngNs::ok         a call's true/false outcome
 * @var RngNs::fill       write @c len keystream bytes out, then ratchet the seed
 * @var RngNs::reseed     redraw the seed and its nonce from the platform, and start the budget over
 *
 * A caller with no reason to hold a generator of its own passes @ref protocore_rng_span, the one the
 * whole program shares. A caller that took its own PROTOCORE_RNG_BORROW span passes that instead and
 * is a separate generator.
 *
 * @ref RngNs::fill draws the seed from the platform itself on the first call over a borrow, and again
 * once @ref PROTOCORE_RAND_RESEED_BYTES is spent. @ref RngNs::reseed forces that redraw at a moment
 * the caller picks.
 *
 * @c work is PROTOCORE_RNG_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The seed is in those bytes
 * rather than in this module, so a caller takes them once for the life of the program and every draw
 * runs out of the same span. The caller releases it, and the pool wipes on release; this module
 * neither takes it, holds it, nor releases it, and the only bytes of it this module erases are the
 * ratchet's replacement copy, after every draw. The borrow IS the generator, so two workers are two
 * borrows and never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref RngNs::ok, and that is all
 * the surface there is.
 */
typedef struct
{
    RngFillArgs fill_args;

    proto_bool ok;

    void (*const fill)(uint8_t *restrict work);
    void (*const reseed)(uint8_t *restrict work);
} RngNs;

/** @brief The one symbol this module exports. */
extern RngNs Rng;

/**
 * @brief The PROTOCORE_RNG_BORROW bytes the whole program's generator runs out of.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where that
 * borrow comes from. Taken once from the end of the secure pool, which no mark and no release walks,
 * so the seed and the ratchet last the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_rng_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RNG

#endif // PROTOCORE_RNG_H
