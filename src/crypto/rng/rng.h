// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rng.h
 * @brief The seed a worker inherits, and the only thing a caller may ask of it.
 *
 * One call: give me @p len bytes, and that is what comes back. A caller cannot seed it, reseed it,
 * or read its pace, because a caller that could would set the pace, and every caller setting its own
 * is how an entropy source gets drained by whichever module asks most often. The generator keeps its
 * own schedule.
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
 * @ref PC_RAND_RESEED_BYTES is spent.
 *
 * The seed is per worker: a persistent borrow from that worker's secure pool, so two workers never
 * share a generator and the draw path takes no lock.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RNG_H
#define PROTOCORE_RNG_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

/** @brief The seed: a ChaCha20 key. */
#define PC_RAND_SEED_LEN 32

/**
 * @brief Write @p len bytes to @p out.
 *
 * Binds and seeds itself on first use, ratchets after every draw, and redraws from the platform on
 * its own schedule. There is nothing else to call.
 *
 * @param out  destination; untouched when @p out is NULL or @p len is 0.
 * @param len  bytes to write. Any length: the block counter carries across 64-byte boundaries.
 */
void pc_rand_fill(uint8_t *out, size_t len);

PROTO_END_DECLS

#endif // PROTOCORE_RNG_H
