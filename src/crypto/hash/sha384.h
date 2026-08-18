// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha384.h
 * @brief SHA-384 (FIPS 180-4) - streaming and one-shot digest.
 *
 * The SHA-384 primitive: the TLS 1.3 SHA-384 cipher suites hash their transcript and run their key
 * schedule on it. The entries below are one surface over both arms: a part with a hashing peripheral
 * compresses on it, a part without runs the FIPS 180-4 rounds. Mirrors the sha512 structure at the
 * SHA-384 seed and digest length.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SHA384_H
#define PROTOCORE_SHA384_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SHA384

PROTOCORE_BEGIN_DECLS

/** @brief SHA-384 digest length in bytes. */
#define PROTOCORE_SHA384_DIGEST_LEN 48

/** @brief SHA-384 block size in bytes. */
#define PROTOCORE_SHA384_BLOCK_LEN 128

// PROTOCORE_SHA384_BORROW - the bytes a digest runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief One chunk fed to a running digest. */
typedef struct
{
    const uint8_t *data; ///< the bytes
    size_t len;          ///< how many
} Sha384UpdateArgs;

/** @brief Where a finished digest lands. */
typedef struct
{
    uint8_t *out; ///< PROTOCORE_SHA384_DIGEST_LEN bytes
} Sha384FinalArgs;

/** @brief The message a one-shot digest is taken over. */
typedef struct
{
    const uint8_t *data; ///< the message
    size_t len;          ///< its length
    uint8_t *out;        ///< PROTOCORE_SHA384_DIGEST_LEN bytes
} Sha384HashArgs;

/**
 * @brief SHA-384 (FIPS 180-4).
 *
 * A caller sets the members a call takes, invokes it through ::Sha384 with the bytes it runs out of,
 * and reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Sha384.init(work);
 *   Sha384.update_args.data = msg;
 *   Sha384.update_args.len = msg_len;
 *   Sha384.update(work);
 *   Sha384.final_args.out = digest;
 *   Sha384.final(work);
 *
 * @var Sha384Ns::update_args  one chunk fed to a running digest
 * @var Sha384Ns::final_args   where a finished digest lands
 * @var Sha384Ns::hash_args    the message a one-shot digest is taken over
 * @var Sha384Ns::ok           a call's true/false outcome
 * @var Sha384Ns::init         start a digest
 * @var Sha384Ns::update       feed the running digest a chunk
 * @var Sha384Ns::final        pad, compress the last block, write the 48 bytes out
 * @var Sha384Ns::hash         init, update and final in one call, for a message already whole
 *
 * @ref Sha384Ns::final leaves the running digest where it was: the padded blocks compress into a copy
 * of the state, so the hash keeps taking data afterwards. That is what lets a TLS transcript be read
 * at every stage the handshake asks for without snapshotting anything.
 *
 * @c work is PROTOCORE_SHA384_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. The
 * borrow IS the digest, so two running hashes are two borrows and never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref Sha384Ns::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    Sha384UpdateArgs update_args;
    Sha384FinalArgs final_args;
    Sha384HashArgs hash_args;

    proto_bool ok;

    void (*const init)(uint8_t *restrict work);
    void (*const update)(uint8_t *restrict work);
    void (*const final)(uint8_t *restrict work);
    void (*const hash)(uint8_t *restrict work);
} Sha384Ns;

/** @brief The one symbol this module exports. */
extern Sha384Ns Sha384;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SHA384

#endif // PROTOCORE_SHA384_H
