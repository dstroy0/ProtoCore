// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_dtls_record.h
 * @brief DTLS 1.3 record layer (RFC 9147 §4).
 *
 * The datagram counterpart to the TLS 1.3 record layer: it protects and unprotects individual
 * UDP-carried records. This is the transport-specific half of DTLS 1.3; the handshake it carries
 * reuses the TLS 1.3 crypto that already backs HTTP/3 (pc_tls13_*, pc_hkdf, aes128gcm).
 *
 * Two record shapes (RFC 9147 §4):
 *   - **DTLSPlaintext** - the classic 13-byte header (type, legacy_version, epoch, 48-bit sequence
 *     number, length, fragment). Used unencrypted for the first handshake flight and for alerts
 *     sent in epoch 0.
 *   - **DTLSCiphertext** - the compact "unified header" plus an AEAD-sealed body, used once record
 *     keys exist. The record's sequence number is itself encrypted (RFC 9147 §4.2.3), and the AEAD
 *     nonce is the TLS 1.3 construction over the full 64-bit sequence number (§4.2.2, epoch excluded).
 *
 * ─ Reuse ─
 *   AEAD (AEAD_AES_128_GCM) and the AES-128 block used for sequence-number encryption come from
 *   aes128gcm; key/iv/sn derivation from pc_hkdf (HKDF-Expand-Label). Phase 1 supports the one
 *   cipher suite the whole hand-rolled TLS 1.3 stack uses: TLS_AES_128_GCM_SHA256.
 *
 * Pure, zero heap, host-tested. Not the mbedTLS TCP-TLS engine (network_drivers/tls) - this is the
 * self-contained datagram record layer.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DTLS_RECORD_H
#define PROTOCORE_DTLS_RECORD_H

#include "crypto/aead/aes128gcm.h" // pc_aes128gcm_key, PC_WORK_AES128GCM
#include "protocore_config.h"

#if PC_ENABLE_DTLS

#include <stddef.h>
#include <stdint.h>

/** @name Record content types (RFC 8446 §5 / RFC 9147 §4).
 *  Shared by the DTLSPlaintext `type` field and the DTLSInnerPlaintext trailing content type. */
///@{
#define PC_DTLS_CT_CHANGE_CIPHER_SPEC 20
#define PC_DTLS_CT_ALERT 21
#define PC_DTLS_CT_HANDSHAKE 22
#define PC_DTLS_CT_APPLICATION_DATA 23
#define PC_DTLS_CT_ACK 26 ///< DTLS 1.3 acknowledgement (RFC 9147 §7)
///@}

/** @brief DTLSPlaintext legacy_version on the wire: DTLS 1.2 (RFC 9147 §4). */
#define PC_DTLS_LEGACY_VERSION 0xFEFD

/** @brief DTLSPlaintext header length: type(1) + version(2) + epoch(2) + seq(6) + length(2). */
#define PC_DTLS_PLAINTEXT_HDR_LEN 13

/** @brief AEAD tag length (all supported suites: 16 bytes). */
#define PC_DTLS_TAG_LEN 16

/** @brief Largest connection id carried in a DTLSCiphertext header (RFC 9146 / RFC 9147 §9). The CID
 *         is not length-prefixed on the wire, so the receiver must know its length from negotiation; 8
 *         bytes is ample routing entropy and bounds the fixed header-scratch buffers. */
#define PC_DTLS_CID_MAX 8

/** @brief Record-layer AEAD suites (phase 1: AEAD_AES_128_GCM with SHA-256). */
enum class DtlsCipher : uint8_t
{
    AES_128_GCM_SHA256 = 0
};

/**
 * @brief One direction's record-protection keys for one epoch (RFC 9147 §4).
 *
 * Derived from a TLS 1.3 traffic secret; holds the AEAD key + write IV plus the separate
 * sequence-number-encryption key. One instance per (epoch, direction).
 */
struct DtlsRecordKeys
{
    DtlsCipher cipher;                         ///< negotiated AEAD (phase 1: AES-128-GCM)
    uint16_t epoch;                            ///< this epoch number; its low 2 bits appear in the unified header
    alignas(8) uint8_t gcm[PC_WORK_AES128GCM]; ///< keyed AEAD context, built once per key.
                                               ///< Replaces the raw key: the schedule is what the
                                               ///< AEAD needs, so no raw key stays resident.
    uint8_t iv[12];                            ///< AEAD write IV (per-record nonce = iv XOR sequence_number)
    alignas(8) uint8_t sn_key[PC_WORK_AES128]; ///< Keyed sequence-number-protection context.
                                               ///< Built once; see quic_crypto.h for the numbers.
};

/**
 * @brief Derive the directional record keys from a 32-byte TLS 1.3 traffic secret.
 *
 * RFC 8446 §7.3 + RFC 9147 §4.2.3: key = HKDF-Expand-Label(secret,"key",""), iv = "iv", and the
 * sequence-number key = HKDF-Expand-Label(secret,"sn","") (all with the "tls13 " prefix).
 */
void pc_dtls_record_keys_derive(DtlsRecordKeys *out, DtlsCipher cipher, uint16_t epoch, const uint8_t secret[32]);

// ---------------------------------------------------------------------------
// DTLSPlaintext (RFC 9147 §4): unencrypted record (initial handshake flight, alerts)
// ---------------------------------------------------------------------------

/**
 * @brief Build a DTLSPlaintext record.
 * @return total bytes written (PC_DTLS_PLAINTEXT_HDR_LEN + @p frag_len), or 0 on overflow.
 */
size_t pc_dtls_plaintext_build(uint8_t content_type, uint16_t epoch, uint64_t seq, const uint8_t *fragment,
                               size_t frag_len, uint8_t *out, size_t out_cap);

/** @brief Parsed view of a DTLSPlaintext record (fields point into the caller's buffer). */
struct DtlsPlaintext
{
    uint8_t content_type;
    uint16_t epoch;
    uint64_t seq;            ///< 48-bit record sequence number
    const uint8_t *fragment; ///< into the input buffer
    size_t frag_len;
};

/**
 * @brief Parse a DTLSPlaintext record, validating legacy_version and the length field.
 * @return total record length consumed (13 + length), or 0 if malformed / truncated.
 */
size_t pc_dtls_plaintext_parse(const uint8_t *rec, size_t rec_len, DtlsPlaintext *out);

// ---------------------------------------------------------------------------
// DTLSCiphertext (RFC 9147 §4): AEAD-protected record with the unified header
// ---------------------------------------------------------------------------

/**
 * @brief Protect one record (RFC 9147 §4.2).
 *
 * Seals @p plaintext (whose inner content type is @p content_type) under @p keys at record sequence
 * number @p seq, writing a complete DTLSCiphertext to @p out: the unified header (S=1 16-bit sequence
 * number, L=1 length present, low 2 epoch bits), the AEAD-sealed body, and the encrypted sequence
 * number. The AEAD nonce is iv XOR seq; the associated data is the unified header carrying the
 * *plaintext* sequence number (before §4.2.3 encryption).
 *
 * When @p cid_len is non-zero the peer's connection id (RFC 9146 / RFC 9147 §9) is placed in the
 * header (C bit set, @p cid bytes immediately after the first byte) and is covered by the AEAD AAD;
 * @p cid_len must be <= @ref PC_DTLS_CID_MAX. With @p cid_len 0 the header carries no CID (C=0), the
 * original behavior.
 *
 * @return bytes written, or 0 on overflow / unsupported cipher / an over-long CID.
 */
size_t pc_dtls_ciphertext_protect(DtlsRecordKeys &keys, uint64_t seq, uint8_t content_type, const uint8_t *plaintext,
                                  size_t pt_len, uint8_t *out, size_t out_cap, const uint8_t *cid = nullptr,
                                  size_t cid_len = 0);

/** @brief Result of a successful @ref pc_dtls_ciphertext_unprotect. */
struct DtlsCiphertext
{
    uint8_t content_type; ///< recovered inner content type (last non-zero byte of the inner plaintext)
    uint16_t epoch;       ///< epoch of @p keys (its low 2 bits matched the header)
    uint64_t seq;         ///< reconstructed full sequence number
    size_t pt_len;        ///< plaintext bytes written to @p out
};

/**
 * @brief Unprotect one received DTLSCiphertext record (RFC 9147 §4.2).
 *
 * Decrypts the sequence number, reconstructs the full sequence number from @p next_seq (the expected
 * next value for this epoch, RFC 9147 §4.2.2 / RFC 9000 Appendix A.3), opens the AEAD, and strips the
 * inner content type and any zero padding. @p keys must be the epoch whose low 2 bits match the
 * record header (verified).
 *
 * Connection ids (RFC 9146 / RFC 9147 §9): when @p expected_cid_len is non-zero a CID was negotiated
 * for this direction, so the record must carry the C bit and a connection id equal to @p expected_cid
 * (the CID is not length-prefixed on the wire - its length is known only from negotiation); the CID is
 * covered by the AEAD AAD. When @p expected_cid_len is 0 a record with the C bit set is rejected (a CID
 * was not negotiated).
 *
 * @return true on success (@p out / @p info filled); false on a malformed header, an epoch-bit
 *         mismatch, an unexpected / mismatched connection id, a failed AEAD tag, or an output overflow.
 */
bool pc_dtls_ciphertext_unprotect(DtlsRecordKeys &keys, uint64_t next_seq, const uint8_t *rec, size_t rec_len,
                                  uint8_t *out, size_t out_cap, DtlsCiphertext *info,
                                  const uint8_t *expected_cid = nullptr, size_t expected_cid_len = 0);

// ---------------------------------------------------------------------------
// Anti-replay sliding window (RFC 9147 §4.5.1)
// ---------------------------------------------------------------------------

/** @brief 64-record sliding replay window over the highest sequence number accepted in an epoch. */
struct DtlsReplayWindow
{
    uint64_t highest; ///< highest accepted sequence number (bit 0 of @ref bitmap)
    uint64_t bitmap;  ///< bit i set => (highest - i) has been accepted
    bool seeded;      ///< false until the first record is accepted
};

/** @brief Reset a replay window to empty. */
void pc_dtls_replay_init(DtlsReplayWindow *w);

/**
 * @brief Test whether @p seq may be accepted (new and within the window).
 * @return true if @p seq is new and in-window; false if it is a replay or older than the window.
 */
bool pc_dtls_replay_check(const DtlsReplayWindow *w, uint64_t seq);

/** @brief Record @p seq as accepted, advancing the window. Call only after a successful deprotect. */
void pc_dtls_replay_mark(DtlsReplayWindow *w, uint64_t seq);

#endif // PC_ENABLE_DTLS
#endif // PROTOCORE_DTLS_RECORD_H
