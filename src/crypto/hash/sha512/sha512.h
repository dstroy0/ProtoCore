// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha512.h
 * @brief SHA-512 (FIPS 180-4) - streaming and one-shot digest.
 *
 * The shared SHA-512 primitive for the whole library (SSH Ed25519 / kex hashing, PQC, SMB 3.1.1
 * preauth integrity). The entries below are one surface over both arms: a part with a hashing
 * peripheral compresses on it, a part without runs the FIPS 180-4 rounds. Mirrors the sha256
 * structure.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SHA512_H
#define PROTOCORE_SHA512_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SHA512

PROTOCORE_BEGIN_DECLS

/** @brief SHA-512 digest length in bytes. */
#define PROTOCORE_SHA512_DIGEST_LEN 64

/** @brief SHA-512 block size in bytes. */
#define PROTOCORE_SHA512_BLOCK_LEN 128

// PROTOCORE_SHA512_BORROW - the bytes a digest runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief One chunk fed to a running digest. */
typedef struct
{
    const uint8_t *data; ///< the bytes
    size_t len;          ///< how many
} Sha512UpdateArgs;

/** @brief Where a finished digest lands. */
typedef struct
{
    uint8_t *out; ///< PROTOCORE_SHA512_DIGEST_LEN bytes
} Sha512FinalArgs;

/** @brief The message a one-shot digest is taken over. */
typedef struct
{
    const uint8_t *data; ///< the message
    size_t len;          ///< its length
    uint8_t *out;        ///< PROTOCORE_SHA512_DIGEST_LEN bytes
} Sha512HashArgs;

/**
 * @brief SHA-512 (FIPS 180-4).
 *
 * A caller sets the members a call takes, invokes it through ::Sha512 with the bytes it runs out of,
 * and reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Sha512.init(work);
 *   Sha512.update_args.data = msg;
 *   Sha512.update_args.len = msg_len;
 *   Sha512.update(work);
 *   Sha512.final_args.out = digest;
 *   Sha512.final(work);
 *
 * @var Sha512Ns::update_args  one chunk fed to a running digest
 * @var Sha512Ns::final_args   where a finished digest lands
 * @var Sha512Ns::hash_args    the message a one-shot digest is taken over
 * @var Sha512Ns::ok           a call's true/false outcome
 * @var Sha512Ns::init         start a digest
 * @var Sha512Ns::update       feed the running digest a chunk
 * @var Sha512Ns::final        pad, compress the last block, write the 64 bytes out
 * @var Sha512Ns::hash         init, update and final in one call, for a message already whole
 *
 * @ref Sha512Ns::final leaves the running digest where it was: the padded blocks compress into a copy
 * of the state, so the hash keeps taking data afterwards. That is what lets SSH read the exchange
 * hash at every stage the key exchange asks for without snapshotting anything.
 *
 * @c work is PROTOCORE_SHA512_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. The
 * borrow IS the digest, so two running hashes are two borrows and never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref Sha512Ns::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    Sha512UpdateArgs update_args;
    Sha512FinalArgs final_args;
    Sha512HashArgs hash_args;
    proto_bool ok;
} Sha512Vars;

/** @brief The operands and the outcome. */
extern Sha512Vars Sha512V;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const update)(uint8_t *restrict work);
    void (*const final)(uint8_t *restrict work);
    void (*const hash)(uint8_t *restrict work);
} Sha512Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Sha512V or a region of the borrow at a fixed offset.
void protocore_sha512_init(uint8_t *restrict work);
void protocore_sha512_update(uint8_t *restrict work);
void protocore_sha512_final(uint8_t *restrict work);
void protocore_sha512_hash(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Sha512.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Sha512Ns Sha512 __attribute__((unused)) = {
    .init = protocore_sha512_init,
    .update = protocore_sha512_update,
    .final = protocore_sha512_final,
    .hash = protocore_sha512_hash,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SHA512

#endif // PROTOCORE_SHA512_H
