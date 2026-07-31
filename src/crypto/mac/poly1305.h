// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include <stddef.h>
#include <stdint.h>

#define PC_POLY1305_KEY_LEN 32
#define PC_POLY1305_TAG_LEN 16

/** @brief Compute the 16-byte Poly1305 tag over @p msg under the 32-byte one-time @p key. */
void pc_poly1305(uint8_t tag[PC_POLY1305_TAG_LEN], const uint8_t *msg, size_t len,
                 const uint8_t key[PC_POLY1305_KEY_LEN]);

#endif // PROTOCORE_POLY1305_H
