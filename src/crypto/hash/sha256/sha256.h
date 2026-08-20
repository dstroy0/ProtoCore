// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha256.h
 * @brief SHA-256 (FIPS 180-4) - streaming and one-shot digest.
 *
 * The shared SHA-256 primitive for the whole library (SSH per-packet HMAC and KEX exchange-hash, TLS
 * 1.3 / QUIC / DTLS key schedules, SNMPv3, JWT, CSRF, SMB 2.x message signing). The entries below are
 * one surface over both arms: a part with a hashing peripheral compresses on it, a part without runs
 * the FIPS 180-4 rounds. Mirrors the sha512 structure.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SHA256_H
#define PROTOCORE_SHA256_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SHA256

PROTOCORE_BEGIN_DECLS

/** @brief SHA-256 digest length in bytes. */
#define PROTOCORE_SHA256_DIGEST_LEN 32

/** @brief SHA-256 block size in bytes. */
#define PROTOCORE_SHA256_BLOCK_LEN 64

// PROTOCORE_SHA256_BORROW - the bytes a digest runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief One chunk fed to a running digest. */
typedef struct
{
    const uint8_t *data; ///< the bytes
    size_t len;          ///< how many
} Sha256UpdateArgs;
/** @brief Where a finished digest lands. */
typedef struct
{
    uint8_t *out; ///< PROTOCORE_SHA256_DIGEST_LEN bytes
} Sha256FinalArgs;
/** @brief The message a one-shot digest is taken over. */
typedef struct
{
    const uint8_t *data; ///< the message
    size_t len;          ///< its length
    uint8_t *out;        ///< PROTOCORE_SHA256_DIGEST_LEN bytes
} Sha256HashArgs;
/**
 * @brief SHA-256 (FIPS 180-4).
 *
 * A caller sets the members a call takes, invokes it through ::Sha256 with the bytes it runs out of,
 * and reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Sha256.init(work);
 *   Sha256.update_args.data = msg;
 *   Sha256.update_args.len = msg_len;
 *   Sha256.update(work);
 *   Sha256.final_args.out = digest;
 *   Sha256.final(work);
 *
 * @var Sha256Ns::update_args  one chunk fed to a running digest
 * @var Sha256Ns::final_args   where a finished digest lands
 * @var Sha256Ns::hash_args    the message a one-shot digest is taken over
 * @var Sha256Ns::ok           a call's true/false outcome
 * @var Sha256Ns::init         start a digest
 * @var Sha256Ns::update       feed the running digest a chunk
 * @var Sha256Ns::final        pad, compress the last block, write the 32 bytes out
 * @var Sha256Ns::hash         init, update and final in one call, for a message already whole
 *
 * @ref Sha256Ns::final leaves the running digest where it was: the padded blocks compress into a copy
 * of the state, so the hash keeps taking data afterwards. That is what lets TLS 1.3 read
 * Transcript-Hash at every stage the key schedule asks for without snapshotting anything.
 *
 * @c work is PROTOCORE_SHA256_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. The
 * borrow IS the digest, so two running hashes are two borrows and never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref Sha256Ns::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    Sha256UpdateArgs update_args;
    Sha256FinalArgs final_args;
    Sha256HashArgs hash_args;
    proto_bool ok;
} Sha256Vars;

/** @brief The operands and the outcome. */
extern Sha256Vars Sha256V;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const update)(uint8_t *restrict work);
    void (*const final)(uint8_t *restrict work);
    void (*const hash)(uint8_t *restrict work);
} Sha256Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Sha256V or a region of the borrow at a fixed offset.
void protocore_sha256_init(uint8_t *restrict work);
void protocore_sha256_update(uint8_t *restrict work);
void protocore_sha256_final(uint8_t *restrict work);
void protocore_sha256_hash(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Sha256.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Sha256Ns Sha256 __attribute__((unused)) = {
    .init = protocore_sha256_init,
    .update = protocore_sha256_update,
    .final = protocore_sha256_final,
    .hash = protocore_sha256_hash,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SHA256

#endif // PROTOCORE_SHA256_H
