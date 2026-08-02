// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes256ctr.h
 * @brief AES-256-CTR stream cipher (aes256-ctr, RFC 4344 §4) - stateless one-shot API.
 *
 * AES-256-CTR is the mandatory cipher for this SSH implementation. CTR mode turns AES into a stream
 * cipher: each 16-byte counter block is AES-ECB encrypted to produce a keystream block, and data is
 * XOR'd with the keystream. Encrypt and decrypt are the identical operation.
 *
 * STATELESS API (mirrors chachapoly.h)
 * ────────────────────────────────────
 * There is no context object. The caller keeps the two things that must persist across packets - the
 * 32-byte key and the 16-byte counter - as plain bytes (SshKeyMat holds them, exactly as it holds the
 * raw chacha20-poly1305 key), and passes both in on every call. The AES key schedule is rebuilt per call
 * in the secure pool and wiped when released, so expanded key material never lives in
 * BSS or on the stack - every crypto secret is funneled through the one hardened, wiped scratch region
 * (mmgr/secure.h). @p counter is advanced in place by ceil(@p len / 16) blocks so successive calls
 * continue the stream.
 *
 * COUNTER FORMAT (RFC 4344 §4)
 * The 16-byte counter increments as a big-endian 128-bit integer after each 16-byte keystream block.
 * The initial counter is the IV from the key exchange (RFC 4253 §7.2, labels 'A'/'B').
 *
 * @note The SSH binary packet is always a whole number of cipher blocks, so every call is block-aligned
 *       and the counter alone is sufficient state. A non-block-aligned @p len is permitted only as the
 *       final call of a stream (any leftover keystream in the last block is discarded, not carried).
 *
 * On Arduino (ESP32) the AES block is mbedtls, routed to the hardware AES accelerator; on native host
 * builds a compact software AES-256 is used so the cipher is unit-testable off-target.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AES256CTR_H
#define PROTOCORE_AES256CTR_H

#include <stddef.h>
#include <stdint.h>

/** @brief AES-256-CTR key length (bytes). */
#define PC_AES256CTR_KEY_LEN 32
/** @brief AES-256-CTR counter/IV block length (bytes). */
#define PC_AES256CTR_CTR_LEN 16

/**
 * @brief Encrypt or decrypt @p len bytes with AES-256-CTR (the two are identical).
 *
 * Rebuilds the key schedule from @p key on the stack, produces the CTR keystream starting at @p counter,
 * and XORs it into @p out. @p counter is advanced in place by ceil(@p len / 16) blocks so the next call
 * continues the same stream. @p in and @p out may alias (in-place is the mode ssh_packet.cpp uses).
 *
 * @param key      32-byte AES-256 key.
 * @param counter  16-byte counter block (big-endian); updated in place to the next unused block.
 * @param in       Input bytes.
 * @param out      Output bytes (may equal @p in).
 * @param len      Number of bytes to process.
 */
void pc_aes256ctr_crypt(const uint8_t key[PC_AES256CTR_KEY_LEN], uint8_t counter[PC_AES256CTR_CTR_LEN],
                        const uint8_t *in, uint8_t *out, size_t len);

/**
 * @brief Decrypt only the 4-byte SSH packet_length prefix WITHOUT advancing @p counter.
 *
 * Mirrors pc_chachapoly_get_length: lets the receiver learn a packet's length (and thus how many bytes
 * to wait for) before the whole packet has arrived, without consuming counter state. The key schedule
 * and keystream block live in the shared crypto scratch; no cipher state touches the stack.
 *
 * @param key      32-byte AES-256 key.
 * @param counter  current 16-byte counter block (read only; not advanced).
 * @param enc4     the 4 encrypted length bytes (start of the packet).
 * @return the decrypted big-endian SSH packet_length.
 */
uint32_t pc_aes256ctr_get_length(const uint8_t key[PC_AES256CTR_KEY_LEN], const uint8_t counter[PC_AES256CTR_CTR_LEN],
                                 const uint8_t enc4[4]);

#endif // PROTOCORE_AES256CTR_H
