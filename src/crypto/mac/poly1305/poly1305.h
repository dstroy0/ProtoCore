// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file poly1305.h
 * @brief Poly1305 one-time authenticator (D. J. Bernstein; RFC 8439 Section 2.5).
 *
 * A one-time MAC over a message under a 32-byte key (r || s). Used by the
 * chacha20-poly1305@openssh.com cipher, where the key is the first 32 bytes of the ChaCha20
 * block-0 keystream for the packet. 130-bit modular arithmetic in 5 x 26-bit limbs (poly1305-donna
 * layout). Pure, no heap; the caller must use each key exactly once.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_POLY1305_H
#define PROTOCORE_POLY1305_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_POLY1305

PROTOCORE_BEGIN_DECLS

/** @brief Poly1305 one-time key length in bytes (r || s). */
#define PROTOCORE_POLY1305_KEY_LEN 32

/** @brief Poly1305 tag length in bytes. */
#define PROTOCORE_POLY1305_TAG_LEN 16

// PROTOCORE_POLY1305_BORROW - the bytes a tag runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The key and message a tag is taken over. */
typedef struct
{
    const uint8_t *key; ///< PROTOCORE_POLY1305_KEY_LEN bytes, used exactly once
    const uint8_t *msg; ///< the message
    size_t len;         ///< its length
    uint8_t *out;       ///< PROTOCORE_POLY1305_TAG_LEN bytes
} Poly1305MacArgs;

/**
 * @brief Poly1305 one-time authenticator (RFC 8439 Section 2.5).
 *
 * A caller sets the members the call takes, invokes it through ::Poly1305 with the bytes it runs out
 * of, and reads the outcome off the same handle. How those bytes are carved is this module's and is
 * never named here.
 *
 *   Poly1305.mac_args.key = poly_key;
 *   Poly1305.mac_args.msg = packet;
 *   Poly1305.mac_args.len = packet_len;
 *   Poly1305.mac_args.out = tag;
 *   Poly1305.mac(work);
 *
 * @var Poly1305Ns::mac_args  the key and message a tag is taken over
 * @var Poly1305Ns::ok        the call's true/false outcome
 * @var Poly1305Ns::mac       take the 16-byte tag over the whole message under the one-time key
 *
 * @c work is PROTOCORE_POLY1305_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. The borrow IS the accumulator, so a tag taken under a caller whose own borrow is still live is
 * a second borrow and the two never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref Poly1305Ns::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    Poly1305MacArgs mac_args;

    proto_bool ok;

    void (*const mac)(uint8_t *restrict work);
} Poly1305Ns;

/** @brief The one symbol this module exports. */
extern Poly1305Ns Poly1305;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_POLY1305

#endif // PROTOCORE_POLY1305_H
