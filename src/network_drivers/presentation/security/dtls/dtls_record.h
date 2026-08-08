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

PROTO_BEGIN_DECLS

#if PC_ENABLE_DTLS

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
typedef enum PROTO_ENUM_PACKED
{
    DTLS_CIPHER_AES_128_GCM_SHA256 = 0
} DtlsCipher;

/**
 * @brief One direction's record-protection keys for one epoch (RFC 9147 §4).
 *
 * Derived from a TLS 1.3 traffic secret; holds the AEAD key + write IV plus the separate
 * sequence-number-encryption key. One instance per (epoch, direction).
 */
typedef struct
{
    DtlsCipher cipher;                          ///< negotiated AEAD (phase 1: AES-128-GCM)
    uint16_t epoch;                             ///< this epoch number; its low 2 bits appear in the unified header
    _Alignas(8) uint8_t gcm[PC_WORK_AES128GCM]; ///< keyed AEAD context, built once per key.
                                                ///< Replaces the raw key: the schedule is what the
                                                ///< AEAD needs, so no raw key stays resident.
    uint8_t iv[12];                             ///< AEAD write IV (per-record nonce = iv XOR sequence_number)
    _Alignas(8) uint8_t sn_key[PC_WORK_AES128]; ///< Keyed sequence-number-protection context.
                                                ///< Built once; see quic_crypto.h for the numbers.
} DtlsRecordKeys;

// ---------------------------------------------------------------------------
// DTLSPlaintext (RFC 9147 §4): unencrypted record (initial handshake flight, alerts)
// ---------------------------------------------------------------------------

/** @brief Parsed view of a DTLSPlaintext record (fields point into the caller's buffer). */
typedef struct
{
    uint8_t content_type;
    uint16_t epoch;
    uint64_t seq;            ///< 48-bit record sequence number
    const uint8_t *fragment; ///< into the input buffer
    size_t frag_len;
} DtlsPlaintext;

// ---------------------------------------------------------------------------
// DTLSCiphertext (RFC 9147 §4): AEAD-protected record with the unified header
// ---------------------------------------------------------------------------

/** @brief Result of a successful @ref pc_dtls_ciphertext_unprotect. */
typedef struct
{
    uint8_t content_type; ///< recovered inner content type (last non-zero byte of the inner plaintext)
    uint16_t epoch;       ///< epoch of @p keys (its low 2 bits matched the header)
    uint64_t seq;         ///< reconstructed full sequence number
    size_t pt_len;        ///< plaintext bytes written to @p out
} DtlsCiphertext;

// ---------------------------------------------------------------------------
// Anti-replay sliding window (RFC 9147 §4.5.1)
// ---------------------------------------------------------------------------

/** @brief 64-record sliding replay window over the highest sequence number accepted in an epoch. */
typedef struct
{
    uint64_t highest;  ///< highest accepted sequence number (bit 0 of @ref bitmap)
    uint64_t bitmap;   ///< bit i set => (highest - i) has been accepted
    proto_bool seeded; ///< false until the first record is accepted
} DtlsReplayWindow;

/**
 * @brief The record layer (RFC 9147 sec 4): the two record shapes, their keys, and the replay window.
 *
 * @var DtlsRecordNs::keys_derive    derive one direction's record keys from a 32-byte TLS 1.3 traffic secret. RFC 8446
 * sec 7.3 and RFC 9147 sec 4.2.3: key, iv and the sequence-number key are each HKDF-Expand-Label of it, under the
 * "tls13 " prefix
 * @var DtlsRecordNs::plaintext_build a DTLSPlaintext record; bytes written (13 + @p frag_len), or 0 on overflow
 * @var DtlsRecordNs::plaintext_parse the same record back, validating legacy_version and the length field; the record
 * length consumed, or 0 if malformed or truncated
 * @var DtlsRecordNs::protect        seal one record (RFC 9147 sec 4.2): the unified header, the AEAD-sealed body, and
 * the encrypted sequence number. The nonce is iv XOR seq and the associated data is the header carrying the plaintext
 * sequence number. A non-zero @p cid_len puts the peer's connection id in the header and under the AAD, and must not
 * exceed PC_DTLS_CID_MAX. Bytes written, or 0 on overflow, an unsupported cipher, or an over-long CID
 * @var DtlsRecordNs::unprotect      open one received record: decrypt the sequence number, rebuild the full one from
 *                             @p next_seq, open the AEAD, and strip the inner content type and padding.
 *                             @p keys must be the epoch whose low 2 bits match the header. A non-zero
 *                             @p expected_cid_len requires the C bit and an equal connection id; a zero one
 *                             refuses a record that carries the C bit
 * @var DtlsRecordNs::replay_init    reset a replay window to empty
 * @var DtlsRecordNs::replay_check   whether @p seq is new and inside the window, rather than a replay or older than it
 * @var DtlsRecordNs::replay_mark    record @p seq as accepted and advance the window; only after a successful unprotect
 */
typedef struct
{
    void (*keys_derive)(DtlsRecordKeys *out, DtlsCipher cipher, uint16_t epoch, const uint8_t secret[32]);
    size_t (*plaintext_build)(uint8_t content_type, uint16_t epoch, uint64_t seq, const uint8_t *fragment,
                              size_t frag_len, uint8_t *out, size_t out_cap);
    size_t (*plaintext_parse)(const uint8_t *rec, size_t rec_len, DtlsPlaintext *out);
    size_t (*protect)(DtlsRecordKeys *keys, uint64_t seq, uint8_t content_type, const uint8_t *plaintext, size_t pt_len,
                      uint8_t *out, size_t out_cap, const uint8_t *cid, size_t cid_len);
    proto_bool (*unprotect)(DtlsRecordKeys *keys, uint64_t next_seq, const uint8_t *rec, size_t rec_len, uint8_t *out,
                            size_t out_cap, DtlsCiphertext *info, const uint8_t *expected_cid, size_t expected_cid_len);
    void (*replay_init)(DtlsReplayWindow *w);
    proto_bool (*replay_check)(const DtlsReplayWindow *w, uint64_t seq);
    void (*replay_mark)(DtlsReplayWindow *w, uint64_t seq);
} DtlsRecordNs;

/** @brief The one symbol this module exports. */
extern const DtlsRecordNs DtlsRecord;

#endif // PC_ENABLE_DTLS

PROTO_END_DECLS

#endif // PROTOCORE_DTLS_RECORD_H
