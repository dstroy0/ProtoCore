// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_QUIC_CRYPTO_H
#define PROTOCORE_QUIC_CRYPTO_H

#include "crypto/kdf/hkdf/hkdf.h" // the complete type a public struct below holds by value
#include "protocore_config.h"     // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file quic_crypto.h
 * @brief QUIC packet protection: Initial secrets, AEAD payload protection, header protection,
and the Retry integrity tag (RFC 9001).
 *
 * This ties the HKDF key schedule (protocore_hkdf) and AEAD_AES_128_GCM (aes128gcm) into the two QUIC
 * packet-protection operations of RFC 9001 sec 5:
 *
 * - QuicCrypto.derive_initial_secrets runs the sec 5.2 Initial key derivation: a fixed salt and the
 * client's Destination Connection ID produce the client and server {key, iv, hp} triples that
 * protect Initial packets (the only keys available before the TLS handshake yields more).
 * - QuicCrypto.packet_protect / QuicCrypto.packet_unprotect perform sec 5.3 AEAD payload protection and
 * sec 5.4 header protection together, on a whole packet in a buffer. They take a {key, iv, hp}
 * triple and a header form, so the same code protects Initial, Handshake, and 1-RTT packets -
 * only the secrets differ. AES-128-GCM header protection samples a 16-byte AES-ECB block.
 * - QuicCrypto.retry_integrity_tag computes the sec 5.8 Retry Integrity Tag (a fixed-key AEAD over the
 * Retry Pseudo-Packet).
 *
 * Pure, zero heap, host-tested against RFC 9001 Appendix A (client Initial A.2, server Initial A.3,
 * Retry A.4).
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_QUIC_CRYPTO_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief The client/server packet-protection secrets for one QUIC encryption level. */
typedef struct
{
    uint8_t gcm[PROTOCORE_AES128GCM_BORROW]; ///< this direction's AEAD borrow. Carries both keyed
                                             ///< contexts: the record AEAD and the header-protection
                                             ///< block. Replaces the raw keys, so neither stays
                                             ///< resident, and both are keyed once.
    uint8_t iv[12];                          ///< AEAD nonce base (XOR'd with the padded packet number).
} QuicPacketKeys;

/** @brief Both directions' Initial secrets derived from the client's Destination Connection ID. */
typedef struct
{
    QuicPacketKeys client; ///< Protects client-sent Initial packets (server opens with this).
    QuicPacketKeys server; ///< Protects server-sent Initial packets (server seals with this).
} QuicInitialSecrets;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    void (*derive_initial_secrets)(uint8_t *restrict, uint8_t *, const uint8_t *, size_t, QuicInitialSecrets *);
    void (*keys_from_secret)(uint8_t *restrict, uint8_t *, const uint8_t *, QuicPacketKeys *);
    size_t (*packet_protect)(uint8_t *restrict, uint8_t *, size_t, size_t, uint8_t, uint64_t, size_t, QuicPacketKeys *,
                             proto_bool);
    size_t (*packet_unprotect)(uint8_t *restrict, uint8_t *, size_t, size_t, uint64_t, QuicPacketKeys *, proto_bool,
                               uint8_t *, uint64_t *);
    void (*retry_integrity_tag)(uint8_t *restrict, const uint8_t *, size_t, const uint8_t *, size_t, uint8_t *);
} QuicCryptoNs;
PROTOCORE_NS_LAYOUT(QuicCryptoNs, derive_initial_secrets, keys_from_secret, packet_protect, packet_unprotect,
                    retry_integrity_tag);

/**
 * @brief Derive the Initial packet-protection secrets (RFC 9001 sec 5.2). .
 * @param work PROTOCORE_QUIC_CRYPTO_BORROW bytes the caller took. Not held past the call.
 * @param keys_work Keys work
 * @param dcid Dcid
 * @param dcid_len Dcid len
 * @param out Out
 */
void protocore_quic_crypto_derive_initial_secrets(uint8_t *restrict work, uint8_t *keys_work, const uint8_t *dcid,
                                                  size_t dcid_len, QuicInitialSecrets *out);
/**
 * @brief Expand one traffic secret into a {key, iv, hp} triple (RFC 9001 sec .
 * @param work PROTOCORE_QUIC_CRYPTO_BORROW bytes the caller took. Not held past the call.
 * @param keys_work Keys work
 * @param secret PROTOCORE_HKDF_HASH_LEN bytes
 * @param out Out
 */
void protocore_quic_crypto_keys_from_secret(uint8_t *restrict work, uint8_t *keys_work, const uint8_t *secret,
                                            QuicPacketKeys *out);
/**
 * @brief Protect one QUIC packet in place: AEAD-seal the payload, then apply .
 * @param work PROTOCORE_QUIC_CRYPTO_BORROW bytes the caller took. Not held past the call.
 * @param pkt Buffer holding header || plaintext payload; rewritten to header || ciphertext
 * @param cap Capacity of pkt; must be >= pn_offset + pn_len + payload_len + 16
 * @param pn_offset Offset of the packet number within the header
 * @param pn_len Packet-number length in bytes (1..4)
 * @param full_pn Full (untruncated) packet number, for the AEAD nonce
 * @param payload_len Plaintext payload length in bytes
 * @param keys The {key, iv, hp} triple for this encryption level
 * @param is_long True for a long header (Initial/Handshake), false for a 1-RTT short header
 * @return The size_t.
 */
size_t protocore_quic_crypto_packet_protect(uint8_t *restrict work, uint8_t *pkt, size_t cap, size_t pn_offset,
                                            uint8_t pn_len, uint64_t full_pn, size_t payload_len, QuicPacketKeys *keys,
                                            proto_bool is_long);
/**
 * @brief Remove header protection and AEAD-open one QUIC packet in place .
 * @param work PROTOCORE_QUIC_CRYPTO_BORROW bytes the caller took. Not held past the call.
 * @param pkt Buffer holding the protected packet (mutated: header unprotected in place)
 * @param pn_offset Offset of the protected packet number
 * @param length QUIC Length field (packet-number + payload + tag bytes)
 * @param largest_pn Largest packet number already received at this level (0 if none yet)
 * @param keys The {key, iv, hp} triple for this encryption level
 * @param is_long True for a long header, false for a 1-RTT short header
 * @param out Output plaintext frames (>= length - pn_len - 16 bytes); may alias pkt payload
 * @param out_pn Receives the reconstructed full packet number (may be NULL)
 * @return The size_t.
 */
size_t protocore_quic_crypto_packet_unprotect(uint8_t *restrict work, uint8_t *pkt, size_t pn_offset, size_t length,
                                              uint64_t largest_pn, QuicPacketKeys *keys, proto_bool is_long,
                                              uint8_t *out, uint64_t *out_pn);
/**
 * @brief Compute the Retry Integrity Tag (RFC 9001 sec 5.8). .
 * @param work PROTOCORE_QUIC_CRYPTO_BORROW bytes the caller took. Not held past the call.
 * @param odcid Original Destination Connection ID (from the client's first Initial)
 * @param odcid_len ODCID length in bytes
 * @param retry Retry packet bytes from the first byte up to (not including) the tag
 * @param retry_len Length of retry
 * @param tag Output 16-byte integrity tag 16 bytes
 */
void protocore_quic_crypto_retry_integrity_tag(uint8_t *restrict work, const uint8_t *odcid, size_t odcid_len,
                                               const uint8_t *retry, size_t retry_len, uint8_t *tag);

/** @brief Module namespace. */
PROTOCORE_NS QuicCryptoNs QuicCrypto PROTOCORE_UNUSED = {
    .derive_initial_secrets = protocore_quic_crypto_derive_initial_secrets,
    .keys_from_secret = protocore_quic_crypto_keys_from_secret,
    .packet_protect = protocore_quic_crypto_packet_protect,
    .packet_unprotect = protocore_quic_crypto_packet_unprotect,
    .retry_integrity_tag = protocore_quic_crypto_retry_integrity_tag};

PROTOCORE_END_DECLS

#endif // PROTOCORE_QUIC_CRYPTO_H
