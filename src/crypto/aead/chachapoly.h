// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file chachapoly.h
 * @brief chacha20-poly1305@openssh.com AEAD cipher (OpenSSH PROTOCOL.chacha20poly1305).
 *
 * OpenSSH's authenticated cipher for the SSH binary packet. The 512-bit key is split into two
 * 256-bit ChaCha20 keys: K_main = key[0..32] encrypts the packet payload, K_header = key[32..64]
 * encrypts the 4-byte packet-length field separately (so a receiver can size the packet before it
 * has the whole thing). The nonce for both is the packet sequence number as a big-endian uint64.
 *
 *   - Poly1305 key = first 32 bytes of ChaCha20(K_main, seqnr, counter 0)
 *   - encrypted length  = ChaCha20(K_header, seqnr, counter 0) XOR length
 *   - encrypted payload = ChaCha20(K_main,   seqnr, counter 1) XOR payload
 *   - tag = Poly1305(encrypted_length || encrypted_payload)  (16 bytes, appended)
 *
 * On decrypt the tag is verified (constant-time) before any plaintext is produced. Pure, no heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CHACHAPOLY_H
#define PROTOCORE_CHACHAPOLY_H

#include "protocore_config.h" // the entry point: protocore_types.h for proto_bool and the widths

#if PROTOCORE_ENABLE_CHACHAPOLY

PROTOCORE_BEGIN_DECLS

#define PROTOCORE_CHACHAPOLY_KEY_LEN 64 ///< two 256-bit ChaCha20 keys
#define PROTOCORE_CHACHAPOLY_TAG_LEN 16 ///< Poly1305 tag
#define PROTOCORE_CHACHAPOLY_AAD_LEN 4  ///< the encrypted packet-length field

/**
 * @brief Decrypt just the 4-byte length field to learn the packet length before reading the body.
 * @return the SSH packet_length (bytes of the packet after the length field, excluding the tag).
 */
uint32_t protocore_chachapoly_get_length(const uint8_t key[PROTOCORE_CHACHAPOLY_KEY_LEN], uint32_t seqnr,
                                         const uint8_t enc_len[PROTOCORE_CHACHAPOLY_AAD_LEN]);

/**
 * @brief Encrypt+authenticate one packet.
 * @param src   plaintext: 4-byte packet length (big-endian) || @p payload_len payload bytes.
 * @param dest  output: encrypted length (4) || encrypted payload (@p payload_len) || tag (16).
 *              May alias @p src. dest must hold 4 + payload_len + 16 bytes.
 */
void protocore_chachapoly_encrypt(const uint8_t key[PROTOCORE_CHACHAPOLY_KEY_LEN], uint32_t seqnr, uint8_t *dest,
                                  const uint8_t *src, uint32_t payload_len);

/**
 * @brief Verify+decrypt one packet.
 * @param src   ciphertext: encrypted length (4) || encrypted payload (@p payload_len) || tag (16).
 * @param dest  output: plaintext length (4) || plaintext payload (@p payload_len). May alias @p src.
 * @return true if the Poly1305 tag verified; false (and no usable plaintext) otherwise.
 */
proto_bool protocore_chachapoly_decrypt(const uint8_t key[PROTOCORE_CHACHAPOLY_KEY_LEN], uint32_t seqnr, uint8_t *dest,
                                        const uint8_t *src, uint32_t payload_len);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CHACHAPOLY

#endif // PROTOCORE_CHACHAPOLY_H
