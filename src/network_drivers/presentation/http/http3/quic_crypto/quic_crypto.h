// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file quic_crypto.h
 * @brief QUIC packet protection: Initial secrets, AEAD payload protection, header protection,
 *        and the Retry integrity tag (RFC 9001).
 *
 * This ties the HKDF key schedule (protocore_hkdf) and AEAD_AES_128_GCM (aes128gcm) into the two QUIC
 * packet-protection operations of RFC 9001 sec 5:
 *
 *  - QuicCrypto.derive_initial_secrets runs the sec 5.2 Initial key derivation: a fixed salt and the
 *    client's Destination Connection ID produce the client and server {key, iv, hp} triples that
 *    protect Initial packets (the only keys available before the TLS handshake yields more).
 *  - QuicCrypto.packet_protect / QuicCrypto.packet_unprotect perform sec 5.3 AEAD payload protection and
 *    sec 5.4 header protection together, on a whole packet in a buffer. They take a {key, iv, hp}
 *    triple and a header form, so the same code protects Initial, Handshake, and 1-RTT packets -
 *    only the secrets differ. AES-128-GCM header protection samples a 16-byte AES-ECB block.
 *  - QuicCrypto.retry_integrity_tag computes the sec 5.8 Retry Integrity Tag (a fixed-key AEAD over the
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

#include "crypto/kdf/hkdf/hkdf.h" // the complete type a public struct below holds by value

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP3

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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
/** @brief What derive_initial_secrets takes: keys_work, dcid, ... */
typedef struct
{
    uint8_t *keys_work;
    const uint8_t *dcid;
    size_t dcid_len;
    QuicInitialSecrets *out;
} QuicCryptoDeriveInitialSecretsArgs;
/** @brief What keys_from_secret takes: keys_work, secret, out. */
typedef struct
{
    uint8_t *keys_work;
    const uint8_t *secret; ///< PROTOCORE_HKDF_HASH_LEN bytes.
    QuicPacketKeys *out;
} QuicCryptoKeysFromSecretArgs;
/** @brief What packet_protect takes: pkt, cap, pn_offset, pn_len, ... */
typedef struct
{
    uint8_t *pkt;         ///< Buffer holding header || plaintext payload; rewritten to header || ciphertext
    size_t cap;           ///< Capacity of pkt; must be >= pn_offset + pn_len + payload_len + 16
    size_t pn_offset;     ///< Offset of the packet number within the header
    uint8_t pn_len;       ///< Packet-number length in bytes (1..4)
    uint64_t full_pn;     ///< Full (untruncated) packet number, for the AEAD nonce
    size_t payload_len;   ///< Plaintext payload length in bytes
    QuicPacketKeys *keys; ///< The {key, iv, hp} triple for this encryption level
    proto_bool is_long;   ///< True for a long header (Initial/Handshake), false for a 1-RTT short header
} QuicCryptoPacketProtectArgs;
/** @brief What packet_unprotect takes: pkt, pn_offset, length, ... */
typedef struct
{
    uint8_t *pkt;         ///< Buffer holding the protected packet (mutated: header unprotected in place)
    size_t pn_offset;     ///< Offset of the protected packet number
    size_t length;        ///< QUIC Length field (packet-number + payload + tag bytes)
    uint64_t largest_pn;  ///< Largest packet number already received at this level (0 if none yet)
    QuicPacketKeys *keys; ///< The {key, iv, hp} triple for this encryption level
    proto_bool is_long;   ///< True for a long header, false for a 1-RTT short header
    uint8_t *out;         ///< Output plaintext frames (>= length - pn_len - 16 bytes); may alias pkt payload
    uint64_t *out_pn;     ///< Receives the reconstructed full packet number (may be NULL)
} QuicCryptoPacketUnprotectArgs;
/** @brief What retry_integrity_tag takes: odcid, odcid_len, retry, ... */
typedef struct
{
    const uint8_t *odcid; ///< Original Destination Connection ID (from the client's first Initial)
    size_t odcid_len;     ///< ODCID length in bytes
    const uint8_t *retry; ///< Retry packet bytes from the first byte up to (not including) the tag
    size_t retry_len;     ///< Length of retry
    uint8_t *tag;         ///< Output 16-byte integrity tag 16 bytes.
} QuicCryptoRetryIntegrityTagArgs;
/**
 * @brief QUIC packet protection: Initial secrets, AEAD payload protection, header protection, and the Retry integrity
 * tag (RFC 9001).
 *
 * A caller sets the members a call takes, invokes it through ::QuicCrypto with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   QuicCrypto.derive_initial_secrets_args.keys_work = ...;
 *   QuicCrypto.derive_initial_secrets_args.dcid = ...;
 *   QuicCrypto.derive_initial_secrets_args.dcid_len = ...;
 *   QuicCrypto.derive_initial_secrets_args.out = ...;
 *   QuicCrypto.derive_initial_secrets(work);
 *
 * @var QuicCryptoNs::derive_initial_secrets_args  what derive_initial_secrets takes: keys_work, dcid,
 * @var QuicCryptoNs::keys_from_secret_args  what keys_from_secret takes: keys_work, secret, out
 * @var QuicCryptoNs::packet_protect_args  what packet_protect takes: pkt, cap, pn_offset, pn_len,
 * @var QuicCryptoNs::packet_unprotect_args  what packet_unprotect takes: pkt, pn_offset, length,
 * @var QuicCryptoNs::retry_integrity_tag_args  what retry_integrity_tag takes: odcid, odcid_len, retry,
 * @var QuicCryptoNs::ok  a call's true/false outcome
 * @var QuicCryptoNs::n  total protected packet length, or 0 on a capacity/parameter error
 * @var QuicCryptoNs::derive_initial_secrets  derive the Initial packet-protection secrets (RFC 9001 sec 5.2). ...
 * @var QuicCryptoNs::keys_from_secret  expand one traffic secret into a {key, iv, hp} triple (RFC 9001 sec ...
 * @var QuicCryptoNs::packet_protect  protect one QUIC packet in place: AEAD-seal the payload, then apply ...
 * @var QuicCryptoNs::packet_unprotect  remove header protection and AEAD-open one QUIC packet in place ...
 * @var QuicCryptoNs::retry_integrity_tag  compute the Retry Integrity Tag (RFC 9001 sec 5.8). ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    QuicCryptoDeriveInitialSecretsArgs derive_initial_secrets_args;
    QuicCryptoKeysFromSecretArgs keys_from_secret_args;
    QuicCryptoPacketProtectArgs packet_protect_args;
    QuicCryptoPacketUnprotectArgs packet_unprotect_args;
    QuicCryptoRetryIntegrityTagArgs retry_integrity_tag_args;
    proto_bool ok;
    size_t n;
} QuicCryptoVars;

/** @brief The operands and the outcome. */
extern QuicCryptoVars QuicCryptoV;

/** @brief The entries. */
typedef struct
{
    void (*const derive_initial_secrets)(uint8_t *restrict work);
    void (*const keys_from_secret)(uint8_t *restrict work);
    void (*const packet_protect)(uint8_t *restrict work);
    void (*const packet_unprotect)(uint8_t *restrict work);
    void (*const retry_integrity_tag)(uint8_t *restrict work);
} QuicCryptoNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in QuicCryptoV or a region of the borrow at a fixed offset.
void protocore_quic_crypto_derive_initial_secrets(uint8_t *restrict work);
void protocore_quic_crypto_keys_from_secret(uint8_t *restrict work);
void protocore_quic_crypto_packet_protect(uint8_t *restrict work);
void protocore_quic_crypto_packet_unprotect(uint8_t *restrict work);
void protocore_quic_crypto_retry_integrity_tag(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `QuicCrypto.derive_initial_secrets(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const QuicCryptoNs QuicCrypto __attribute__((unused)) = {
    .derive_initial_secrets = protocore_quic_crypto_derive_initial_secrets,
    .keys_from_secret = protocore_quic_crypto_keys_from_secret,
    .packet_protect = protocore_quic_crypto_packet_protect,
    .packet_unprotect = protocore_quic_crypto_packet_unprotect,
    .retry_integrity_tag = protocore_quic_crypto_retry_integrity_tag,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_QUIC_CRYPTO_H
