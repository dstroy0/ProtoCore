// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha1.h
 * @brief SHA-1 (FIPS 180-4) - one-shot digest.
 *
 * The shared SHA-1 primitive. On Arduino (ESP32) delegates to the hardware SHA accelerator; on native
 * builds a portable software implementation is used. Used for the WebSocket opening handshake
 * (RFC 6455 §4.2.2) and other legacy digest needs. Output is always 20 bytes.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SHA1_H
#define PROTOCORE_SHA1_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SHA1

PROTOCORE_BEGIN_DECLS

/** @brief SHA-1 digest length in bytes. */
#define PROTOCORE_SHA1_DIGEST_LEN 20

// PROTOCORE_SHA1_BORROW - the bytes a digest runs out of - is stated in protocore_config.h, which sums
// it into the secure arena. A caller takes them once with protocore_secure_persist_span() and passes
// the pointer to every call.

/** @brief The message a digest is taken over, and where it lands. */
typedef struct
{
    const uint8_t *data; ///< the bytes
    size_t len;          ///< how many
    uint8_t *out;        ///< PROTOCORE_SHA1_DIGEST_LEN bytes
} Sha1HashArgs;

/**
 * @brief SHA-1 (FIPS 180-4).
 *
 * A caller sets the members a call takes, invokes it through ::Sha1 with the bytes it runs out of, and
 * reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Sha1.hash_args.data = accept_key;
 *   Sha1.hash_args.len = accept_key_len;
 *   Sha1.hash_args.out = digest;
 *   Sha1.hash(work);
 *
 * @var Sha1Ns::hash_args  the message a digest is taken over, and where it lands
 * @var Sha1Ns::ok         a call's true/false outcome
 * @var Sha1Ns::hash       digest the whole message in one call
 *
 * @c work is PROTOCORE_SHA1_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it.
 *
 * No storage member and no context: a caller sets operands and reads @ref Sha1Ns::ok, and that is all
 * the surface there is.
 */
typedef struct
{
    Sha1HashArgs hash_args;
    proto_bool ok;
} Sha1Vars;

/** @brief The operands and the outcome. */
extern Sha1Vars Sha1V;

/** @brief The entries. */
typedef struct
{
    void (*const hash)(uint8_t *restrict work);
} Sha1Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Sha1V or a region of the borrow at a fixed offset.
void protocore_sha1_hash(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Sha1.hash(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Sha1Ns Sha1 __attribute__((unused)) = {
    .hash = protocore_sha1_hash,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SHA1

#endif // PROTOCORE_SHA1_H
