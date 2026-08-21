// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_DTLS_RECORD_H
#define PROTOCORE_DTLS_RECORD_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file dtls_record.h
 * @brief DTLS 1.3 record layer (RFC 9147 §4).
 *
 * The datagram counterpart to the TLS 1.3 record layer: it protects and unprotects individual
 * UDP-carried records. This is the transport-specific half of DTLS 1.3; the handshake it carries
 * reuses the TLS 1.3 crypto that already backs HTTP/3 (protocore_tls13_*, protocore_hkdf, aes128gcm).
 *
 * Two record shapes (RFC 9147 §4):
 * - **DTLSPlaintext** - the classic 13-byte header (type, legacy_version, epoch, 48-bit sequence
 * number, length, fragment). Used unencrypted for the first handshake flight and for alerts
 * sent in epoch 0.
 * - **DTLSCiphertext** - the compact "unified header" plus an AEAD-sealed body, used once record
 * keys exist. The record's sequence number is itself encrypted (RFC 9147 §4.2.3), and the AEAD
 * nonce is the TLS 1.3 construction over the full 64-bit sequence number (§4.2.2, epoch excluded).
 *
 * ─ Reuse ─
 * AEAD (AEAD_AES_128_GCM) and the AES-128 block used for sequence-number encryption come from
 * aes128gcm; key/iv/sn derivation from protocore_hkdf (HKDF-Expand-Label). Phase 1 supports the one
 * cipher suite the whole hand-rolled TLS 1.3 stack uses: TLS_AES_128_GCM_SHA256.
 *
 * Pure, zero heap, host-tested. Not the mbedTLS TCP-TLS engine (network_drivers/tls) - this is the
 * self-contained datagram record layer.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_DTLS_RECORD_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @name Record content types (RFC 8446 §5 / RFC 9147 §4).
 *  Shared by the DTLSPlaintext `type` field and the DTLSInnerPlaintext trailing content type. */
///@{
#define PROTOCORE_DTLS_CT_CHANGE_CIPHER_SPEC 20
#define PROTOCORE_DTLS_CT_ALERT 21
#define PROTOCORE_DTLS_CT_HANDSHAKE 22
#define PROTOCORE_DTLS_CT_APPLICATION_DATA 23
#define PROTOCORE_DTLS_CT_ACK 26 ///< DTLS 1.3 acknowledgement (RFC 9147 §7)
///@}

/** @brief DTLSPlaintext legacy_version on the wire: DTLS 1.2 (RFC 9147 §4). */
#define PROTOCORE_DTLS_LEGACY_VERSION 0xFEFD

/** @brief DTLSPlaintext header length: type(1) + version(2) + epoch(2) + seq(6) + length(2). */
#define PROTOCORE_DTLS_PLAINTEXT_HDR_LEN 13

/** @brief AEAD tag length (all supported suites: 16 bytes). */
#define PROTOCORE_DTLS_TAG_LEN 16

/** @brief Largest connection id carried in a DTLSCiphertext header (RFC 9146 / RFC 9147 §9). The CID
 *         is not length-prefixed on the wire, so the receiver must know its length from negotiation; 8
 *         bytes is ample routing entropy and bounds the fixed header-scratch buffers. */
#define PROTOCORE_DTLS_CID_MAX 8

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
    DtlsCipher cipher;                       ///< negotiated AEAD (phase 1: AES-128-GCM)
    uint16_t epoch;                          ///< this epoch number; its low 2 bits appear in the unified header
    uint8_t gcm[PROTOCORE_AES128GCM_BORROW]; ///< this epoch's AEAD borrow. Carries both keyed
                                             ///< contexts: the record AEAD and the
                                             ///< sequence-number-protection block. Replaces the raw
                                             ///< keys, so neither stays resident.
    uint8_t iv[12];                          ///< AEAD write IV (per-record nonce = iv XOR sequence_number)
} DtlsRecordKeys;

/** @brief Parsed view of a DTLSPlaintext record (fields point into the caller's buffer). */
typedef struct
{
    uint8_t content_type;
    uint16_t epoch;
    uint64_t seq;            ///< 48-bit record sequence number
    const uint8_t *fragment; ///< into the input buffer
    size_t frag_len;
} DtlsPlaintext;

/** @brief Result of a successful @ref protocore_dtls_ciphertext_unprotect. */
typedef struct
{
    uint8_t content_type; ///< recovered inner content type (last non-zero byte of the inner plaintext)
    uint16_t epoch;       ///< epoch of @p keys (its low 2 bits matched the header)
    uint64_t seq;         ///< reconstructed full sequence number
    size_t pt_len;        ///< plaintext bytes written to @p out
} DtlsCiphertext;

/** @brief 64-record sliding replay window over the highest sequence number accepted in an epoch. */
typedef struct
{
    uint64_t highest;  ///< highest accepted sequence number (bit 0 of @ref bitmap)
    uint64_t bitmap;   ///< bit i set => (highest - i) has been accepted
    proto_bool seeded; ///< false until the first record is accepted
} DtlsReplayWindow;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    void (*keys_derive)(uint8_t *, DtlsRecordKeys *, DtlsCipher, uint16_t, const uint8_t *);
    size_t (*plaintext_build)(uint8_t *, uint8_t, uint16_t, uint64_t, const uint8_t *, size_t, uint8_t *, size_t);
    size_t (*plaintext_parse)(uint8_t *, const uint8_t *, size_t, DtlsPlaintext *);
    size_t (*protect)(uint8_t *, DtlsRecordKeys *, uint64_t, uint8_t, const uint8_t *, size_t, uint8_t *, size_t,
                      const uint8_t *, size_t);
    proto_bool (*unprotect)(uint8_t *, DtlsRecordKeys *, uint64_t, const uint8_t *, size_t, uint8_t *, size_t,
                            DtlsCiphertext *, const uint8_t *, size_t);
    void (*replay_init)(uint8_t *, DtlsReplayWindow *);
    proto_bool (*replay_check)(uint8_t *, const DtlsReplayWindow *, uint64_t);
    void (*replay_mark)(uint8_t *, DtlsReplayWindow *, uint64_t);
} DtlsRecordNs;
PROTOCORE_NS_LAYOUT(DtlsRecordNs, keys_derive, plaintext_build, plaintext_parse, protect, unprotect, replay_init,
                    replay_check, replay_mark);

/**
 * @brief Derive one direction's record keys from a 32-byte TLS 1.3 traffic .
 * @param work PROTOCORE_DTLS_RECORD_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cipher Cipher
 * @param epoch Epoch
 * @param secret 32 bytes
 */
void protocore_dtls_record_keys_derive(uint8_t *work, DtlsRecordKeys *out, DtlsCipher cipher, uint16_t epoch,
                                       const uint8_t *secret);
/**
 * @brief A DTLSPlaintext record; bytes written (13 + frag_len), or 0 on .
 * @param work PROTOCORE_DTLS_RECORD_BORROW bytes the caller took. Not held past the call.
 * @param content_type Content type
 * @param epoch Epoch
 * @param seq Seq
 * @param fragment Fragment
 * @param frag_len Frag len
 * @param out Out
 * @param out_cap Out cap
 * @return The size_t.
 */
size_t protocore_dtls_record_plaintext_build(uint8_t *work, uint8_t content_type, uint16_t epoch, uint64_t seq,
                                             const uint8_t *fragment, size_t frag_len, uint8_t *out, size_t out_cap);
/**
 * @brief The same record back, validating legacy_version and the length .
 * @param work PROTOCORE_DTLS_RECORD_BORROW bytes the caller took. Not held past the call.
 * @param rec Rec
 * @param rec_len Rec len
 * @param out Out
 * @return The size_t.
 */
size_t protocore_dtls_record_plaintext_parse(uint8_t *work, const uint8_t *rec, size_t rec_len, DtlsPlaintext *out);
/**
 * @brief Seal one record (RFC 9147 sec 4.2): the unified header, the .
 * @param work PROTOCORE_DTLS_RECORD_BORROW bytes the caller took. Not held past the call.
 * @param keys Keys
 * @param seq Seq
 * @param content_type Content type
 * @param plaintext Plaintext
 * @param pt_len Pt len
 * @param out Out
 * @param out_cap Out cap
 * @param cid Cid
 * @param cid_len Cid len
 * @return The size_t.
 */
size_t protocore_dtls_record_protect(uint8_t *work, DtlsRecordKeys *keys, uint64_t seq, uint8_t content_type,
                                     const uint8_t *plaintext, size_t pt_len, uint8_t *out, size_t out_cap,
                                     const uint8_t *cid, size_t cid_len);
/**
 * @brief Open one received record: decrypt the sequence number, rebuild the .
 * @param work PROTOCORE_DTLS_RECORD_BORROW bytes the caller took. Not held past the call.
 * @param keys Keys
 * @param next_seq Next seq
 * @param rec Rec
 * @param rec_len Rec len
 * @param out Out
 * @param out_cap Out cap
 * @param info Info
 * @param expected_cid Expected cid
 * @param expected_cid_len Expected cid len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_dtls_record_unprotect(uint8_t *work, DtlsRecordKeys *keys, uint64_t next_seq, const uint8_t *rec,
                                           size_t rec_len, uint8_t *out, size_t out_cap, DtlsCiphertext *info,
                                           const uint8_t *expected_cid, size_t expected_cid_len);
/**
 * @brief Reset a replay window to empty.
 * @param work PROTOCORE_DTLS_RECORD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 */
void protocore_dtls_record_replay_init(uint8_t *work, DtlsReplayWindow *w);
/**
 * @brief Whether seq is new and inside the window, rather than a replay or .
 * @param work PROTOCORE_DTLS_RECORD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param seq Seq
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_dtls_record_replay_check(uint8_t *work, const DtlsReplayWindow *w, uint64_t seq);
/**
 * @brief Record seq as accepted and advance the window; only after a .
 * @param work PROTOCORE_DTLS_RECORD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param seq Seq
 */
void protocore_dtls_record_replay_mark(uint8_t *work, DtlsReplayWindow *w, uint64_t seq);

/** @brief Module namespace. */
PROTOCORE_NS DtlsRecordNs DtlsRecord PROTOCORE_UNUSED = {.keys_derive = protocore_dtls_record_keys_derive,
                                                         .plaintext_build = protocore_dtls_record_plaintext_build,
                                                         .plaintext_parse = protocore_dtls_record_plaintext_parse,
                                                         .protect = protocore_dtls_record_protect,
                                                         .unprotect = protocore_dtls_record_unprotect,
                                                         .replay_init = protocore_dtls_record_replay_init,
                                                         .replay_check = protocore_dtls_record_replay_check,
                                                         .replay_mark = protocore_dtls_record_replay_mark};

PROTOCORE_END_DECLS

#endif // PROTOCORE_DTLS_RECORD_H
