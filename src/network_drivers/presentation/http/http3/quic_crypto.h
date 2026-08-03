// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file quic_crypto.h
 * @brief QUIC packet protection: Initial secrets, AEAD payload protection, header protection,
 *        and the Retry integrity tag (RFC 9001).
 *
 * This ties the HKDF key schedule (pc_hkdf) and AEAD_AES_128_GCM (aes128gcm) into the two QUIC
 * packet-protection operations of RFC 9001 sec 5:
 *
 *  - pc_quic_derive_initial_secrets() runs the sec 5.2 Initial key derivation: a fixed salt and the
 *    client's Destination Connection ID produce the client and server {key, iv, hp} triples that
 *    protect Initial packets (the only keys available before the TLS handshake yields more).
 *  - pc_quic_packet_protect() / pc_quic_packet_unprotect() perform sec 5.3 AEAD payload protection and
 *    sec 5.4 header protection together, on a whole packet in a buffer. They take a {key, iv, hp}
 *    triple and a header form, so the same code protects Initial, Handshake, and 1-RTT packets -
 *    only the secrets differ. AES-128-GCM header protection samples a 16-byte AES-ECB block.
 *  - pc_quic_retry_integrity_tag() computes the sec 5.8 Retry Integrity Tag (a fixed-key AEAD over the
 *    Retry Pseudo-Packet).
 *
 * Pure, zero heap, host-tested against RFC 9001 Appendix A (client Initial A.2, server Initial A.3,
 * Retry A.4).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_QUIC_CRYPTO_H
#define PROTOCORE_QUIC_CRYPTO_H

#include "protocore_config.h"

#if PC_ENABLE_HTTP3

#include "crypto/aead/aes128gcm.h" // pc_aes128gcm_key, PC_WORK_AES128GCM
#include "crypto/kdf/hkdf.h"       // PC_HKDF_HASH_LEN

/** @brief The client/server packet-protection secrets for one QUIC encryption level. */
typedef struct
{
    _Alignas(8) uint8_t gcm[PC_WORK_AES128GCM]; ///< keyed AEAD context, built once per key.
                                                ///< Replaces the raw key: the schedule is what the
                                                ///< AEAD needs, so no raw key stays resident.
    uint8_t iv[12];                             ///< AEAD nonce base (XOR'd with the padded packet number).
    _Alignas(8) uint8_t hp[PC_WORK_AES128];     ///< Keyed header-protection context (AES-128-ECB mask).
                                                ///< Built once: ~556 cycles per record, plus the pool
                                                ///< borrow and wipe it also removes.
} QuicPacketKeys;

/** @brief Both directions' Initial secrets derived from the client's Destination Connection ID. */
typedef struct
{
    QuicPacketKeys client; ///< Protects client-sent Initial packets (server opens with this).
    QuicPacketKeys server; ///< Protects server-sent Initial packets (server seals with this).
} QuicInitialSecrets;

/**
 * @brief Derive the Initial packet-protection secrets (RFC 9001 sec 5.2).
 *
 * initial_secret = HKDF-Extract(initial_salt, dcid); each direction's traffic secret is an
 * HKDF-Expand-Label ("client in" / "server in") of that, and key/iv/hp are expanded from the
 * traffic secret. @p dcid is the Destination Connection ID from the client's first Initial packet.
 */
void pc_quic_derive_initial_secrets(const uint8_t *dcid, size_t dcid_len, QuicInitialSecrets *out);

/**
 * @brief Expand one traffic secret into a {key, iv, hp} triple (RFC 9001 sec 5.1 labels).
 *
 * key = HKDF-Expand-Label(secret, "quic key", 16), iv = "quic iv" (12), hp = "quic hp" (16). The
 * Initial derivation uses this internally; the Handshake and 1-RTT levels call it directly on the
 * TLS-derived handshake / application traffic secrets so every level shares one code path.
 */
void pc_quic_keys_from_secret(const uint8_t secret[PC_HKDF_HASH_LEN], QuicPacketKeys *out);

/**
 * @brief Protect one QUIC packet in place: AEAD-seal the payload, then apply header protection.
 *
 * On entry @p pkt holds the unprotected header (flags with reserved bits 0, the packet number in
 * the clear) in bytes [0, pn_offset + pn_len), immediately followed by @p payload_len plaintext
 * frame bytes. On return @p pkt holds the header (now protected) followed by the ciphertext and
 * 16-byte tag. The header in [0, pn_offset + pn_len) is used as the AEAD associated data before
 * protection is applied.
 *
 * @param pkt          Buffer holding header || plaintext payload; rewritten to header || ciphertext.
 * @param cap          Capacity of @p pkt; must be >= pn_offset + pn_len + payload_len + 16.
 * @param pn_offset    Offset of the packet number within the header.
 * @param pn_len       Packet-number length in bytes (1..4).
 * @param full_pn      Full (untruncated) packet number, for the AEAD nonce.
 * @param payload_len  Plaintext payload length in bytes.
 * @param keys         The {key, iv, hp} triple for this encryption level.
 * @param is_long      True for a long header (Initial/Handshake), false for a 1-RTT short header.
 * @return total protected packet length, or 0 on a capacity/parameter error.
 */
size_t pc_quic_packet_protect(uint8_t *pkt, size_t cap, size_t pn_offset, uint8_t pn_len, uint64_t full_pn,
                              size_t payload_len, QuicPacketKeys *keys, proto_bool is_long);

/**
 * @brief Remove header protection and AEAD-open one QUIC packet in place (RFC 9001 sec 5.3/5.4).
 *
 * @p pkt holds the received packet on the wire. @p pn_offset is the offset of the (protected)
 * packet number, found by parsing the header up to the Length field. @p length is the QUIC Length
 * field value (packet-number bytes + protected payload + 16-byte tag). Header protection is removed
 * first (recovering the true packet-number length and value), the packet number is reconstructed
 * against @p largest_pn (RFC 9000 sec 17.1), and the payload is verified and decrypted.
 *
 * @param pkt          Buffer holding the protected packet (mutated: header unprotected in place).
 * @param pn_offset    Offset of the protected packet number.
 * @param length       QUIC Length field (packet-number + payload + tag bytes).
 * @param largest_pn   Largest packet number already received at this level (0 if none yet).
 * @param keys         The {key, iv, hp} triple for this encryption level.
 * @param is_long      True for a long header, false for a 1-RTT short header.
 * @param out          Output plaintext frames (>= length - pn_len - 16 bytes); may alias @p pkt payload.
 * @param out_pn       Receives the reconstructed full packet number (may be NULL).
 * @return plaintext length, or (size_t)-1 on parameter error or AEAD authentication failure.
 */
size_t pc_quic_packet_unprotect(uint8_t *pkt, size_t pn_offset, size_t length, uint64_t largest_pn,
                                QuicPacketKeys *keys, proto_bool is_long, uint8_t *out, uint64_t *out_pn);

/**
 * @brief Compute the Retry Integrity Tag (RFC 9001 sec 5.8).
 *
 * AEAD_AES_128_GCM with the version-1 fixed key and nonce over the Retry Pseudo-Packet, whose AAD
 * is the original Destination Connection ID (length-prefixed) followed by the Retry packet up to
 * but excluding the tag. Plaintext is empty, so the whole 16-byte output is the tag.
 *
 * @param odcid          Original Destination Connection ID (from the client's first Initial).
 * @param odcid_len      ODCID length in bytes.
 * @param retry          Retry packet bytes from the first byte up to (not including) the tag.
 * @param retry_len      Length of @p retry.
 * @param tag            Output 16-byte integrity tag.
 */
void pc_quic_retry_integrity_tag(const uint8_t *odcid, size_t odcid_len, const uint8_t *retry, size_t retry_len,
                                 uint8_t tag[16]);

#endif // PC_ENABLE_HTTP3
#endif // PROTOCORE_QUIC_CRYPTO_H
