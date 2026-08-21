// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_POLY1305_H
#define PROTOCORE_POLY1305_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file poly1305.h
 * @brief Poly1305 one-time authenticator (D. J. Bernstein; RFC 8439 Section 2.5).
 *
 * A one-time MAC over a message under a 32-byte key (r || s). Used by the
 * chacha20-poly1305@openssh.com cipher, where the key is the first 32 bytes of the ChaCha20
 * block-0 keystream for the packet. 130-bit modular arithmetic in 5 x 26-bit limbs (poly1305-donna
 * layout). Pure, no heap; the caller must use each key exactly once.
 *
 * @c work is PROTOCORE_POLY1305_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. The borrow IS the accumulator, so a tag taken under a caller whose own borrow is still live is
 * a second borrow and the two never collide.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief Poly1305 one-time key length in bytes (r || s). */
#define PROTOCORE_POLY1305_KEY_LEN 32

/** @brief Poly1305 tag length in bytes. */
#define PROTOCORE_POLY1305_TAG_LEN 16

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*mac)(uint8_t *restrict, const uint8_t *, const uint8_t *, size_t, uint8_t *);
} Poly1305Ns;
PROTOCORE_NS_LAYOUT(Poly1305Ns, mac);

/**
 * @brief Take the 16-byte tag over the whole message under the one-time key.
 * @param work PROTOCORE_POLY1305_BORROW bytes the caller took. Not held past the call.
 * @param key PROTOCORE_POLY1305_KEY_LEN bytes, used exactly once
 * @param msg the message
 * @param len its length
 * @param out PROTOCORE_POLY1305_TAG_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_poly1305_mac(uint8_t *restrict work, const uint8_t *key, const uint8_t *msg, size_t len,
                                  uint8_t *out);

/** @brief Module namespace. */
PROTOCORE_NS Poly1305Ns Poly1305 PROTOCORE_UNUSED = {.mac = protocore_poly1305_mac};

PROTOCORE_END_DECLS

#endif // PROTOCORE_POLY1305_H
