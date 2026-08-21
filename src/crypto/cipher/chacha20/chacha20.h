// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_CHACHA20_H
#define PROTOCORE_CHACHA20_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file chacha20.h
 * @brief ChaCha20 stream cipher (D. J. Bernstein; RFC 8439).
 *
 * The 20-round ARX permutation used by the chacha20-poly1305@openssh.com cipher. Two views of the
 * same core are exposed:
 *
 * - @ref Chacha20Ns::xor_ : the original ChaCha layout OpenSSH uses - a 64-bit little-endian block
 * counter (state words 12-13) and a 64-bit nonce/IV (words 14-15). This is what the SSH AEAD
 * drives; the counter increments per 64-byte block.
 * - @ref Chacha20Ns::block_ietf : the RFC 8439 layout (32-bit counter in word 12, 96-bit nonce in
 * words 13-15), exposed so the core can be checked against the published RFC 8439 Section 2.3.2
 * block test vector.
 *
 * Pure ARX (add-rotate-xor): naturally constant-time, no tables, ~64-byte state. No heap.
 *
 * The member is spelled `xor_` because `xor` is an alternative token in C++, and this header reaches
 * the C++ translation units.
 *
 * @c work is PROTOCORE_CHACHA20_BORROW secure bytes the CALLER took, at an address it knows. It is not held past the
 * call, so nothing here aliases it. The caller releases it, and the pool wipes on release; this module neither takes
 * it, holds it, releases it, nor wipes it. The borrow IS the working state, so two keystreams in flight are two borrows
 * and never collide.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief ChaCha20 key length in bytes. */
#define PROTOCORE_CHACHA20_KEY_LEN 32

/** @brief ChaCha20 keystream block length in bytes. */
#define PROTOCORE_CHACHA20_BLOCK_LEN 64

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*xor_)(uint8_t *, const uint8_t *, const uint8_t *, uint64_t, const uint8_t *, uint8_t *, size_t);
    proto_bool (*block_ietf)(uint8_t *, const uint8_t *, uint32_t, const uint8_t *, uint8_t *);
} Chacha20Ns;
PROTOCORE_NS_LAYOUT(Chacha20Ns, xor_, block_ietf);

/**
 * @brief XOR the OpenSSH-layout keystream over in into out.
 * @param work PROTOCORE_CHACHA20_BORROW bytes the caller took. Not held past the call.
 * @param key PROTOCORE_CHACHA20_KEY_LEN bytes
 * @param iv 8-byte nonce; OpenSSH uses the packet sequence number, big-endian
 * @param counter initial 64-bit block counter, stepping once per 64-byte block
 * @param in plaintext, or ciphertext when decrypting; nullptr emits raw keystream
 * @param out len bytes; may alias in
 * @param len how many
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_chacha20_xor_(uint8_t *work, const uint8_t *key, const uint8_t *iv, uint64_t counter,
                                   const uint8_t *in, uint8_t *out, size_t len);
/**
 * @brief One 64-byte keystream block in the RFC 8439 layout.
 * @param work PROTOCORE_CHACHA20_BORROW bytes the caller took. Not held past the call.
 * @param key PROTOCORE_CHACHA20_KEY_LEN bytes
 * @param counter 32-bit block counter, state word 12
 * @param nonce 12-byte nonce, state words 13-15
 * @param out PROTOCORE_CHACHA20_BLOCK_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_chacha20_block_ietf(uint8_t *work, const uint8_t *key, uint32_t counter, const uint8_t *nonce,
                                         uint8_t *out);

/** @brief Module namespace. */
PROTOCORE_NS Chacha20Ns Chacha20 PROTOCORE_UNUSED = {.xor_ = protocore_chacha20_xor_,
                                                     .block_ietf = protocore_chacha20_block_ietf};

PROTOCORE_END_DECLS

#endif // PROTOCORE_CHACHA20_H
