// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file chacha20.h
 * @brief ChaCha20 stream cipher (D. J. Bernstein; RFC 8439).
 *
 * The 20-round ARX permutation used by the chacha20-poly1305@openssh.com cipher. Two views of the
 * same core are exposed:
 *
 *   - pc_chacha20_xor(): the original ChaCha layout OpenSSH uses - a 64-bit little-endian block
 *     counter (state words 12-13) and a 64-bit nonce/IV (words 14-15). This is what the SSH AEAD
 *     drives; the counter increments per 64-byte block.
 *   - pc_chacha20_block_ietf(): the RFC 8439 layout (32-bit counter in word 12, 96-bit nonce in
 *     words 13-15), exposed so the core can be checked against the published RFC 8439 Section 2.3.2
 *     block test vector.
 *
 * Pure ARX (add-rotate-xor): naturally constant-time, no tables, ~64-byte state. No heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CHACHA20_H
#define PROTOCORE_CHACHA20_H

#include <stddef.h>
#include <stdint.h>

#define PC_CHACHA20_KEY_LEN 32
#define PC_CHACHA20_BLOCK_LEN 64

/**
 * @brief XOR @p len bytes of @p in with the ChaCha20 keystream into @p out (OpenSSH layout).
 * @param key       32-byte key.
 * @param iv        8-byte nonce (OpenSSH uses the packet sequence number, big-endian).
 * @param counter   initial 64-bit block counter (0 for the Poly1305-key / length blocks, 1 for
 *                  the packet payload); it increments per 64-byte block.
 * @param in        plaintext (or ciphertext when decrypting); may be nullptr to emit raw keystream.
 * @param out       output buffer (@p len bytes); may alias @p in.
 */
void pc_chacha20_xor(const uint8_t key[PC_CHACHA20_KEY_LEN], const uint8_t iv[8], uint64_t counter, const uint8_t *in,
                     uint8_t *out, size_t len);

/**
 * @brief One 64-byte ChaCha20 keystream block in the RFC 8439 layout (for the KAT).
 * @param key     32-byte key.
 * @param counter 32-bit block counter (word 12).
 * @param nonce   12-byte nonce (words 13-15).
 * @param out     64-byte keystream block.
 */
void pc_chacha20_block_ietf(const uint8_t key[PC_CHACHA20_KEY_LEN], uint32_t counter, const uint8_t nonce[12],
                            uint8_t out[PC_CHACHA20_BLOCK_LEN]);

#endif // PROTOCORE_CHACHA20_H
