// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_dtls_record.h
 * @brief DTLS 1.3 record layer (RFC 9147 §4).
 *
 * The datagram counterpart to the TLS 1.3 record layer: it protects and unprotects individual
 * UDP-carried records. This is the transport-specific half of DTLS 1.3; the handshake it carries
 * reuses the TLS 1.3 crypto that already backs HTTP/3 (protocore_tls13_*, protocore_hkdf, aes128gcm).
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
 *   aes128gcm; key/iv/sn derivation from protocore_hkdf (HKDF-Expand-Label). Phase 1 supports the one
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_DTLS

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @name Record content types (RFC 8446 §5 / RFC 9147 §4).
 *  Shared by the DTLSPlaintext `type` field and the DTLSInnerPlaintext trailing content type. */
///@{
#define PROTOCORE_DTLS_CT_CHANGE_CIPHER_SPEC 20
#define PROTOCORE_DTLS_CT_ALERT 21
#define PROTOCORE_DTLS_CT_HANDSHAKE 22
#define PROTOCORE_DTLS_CT_APPLICATION_DATA 23
#define PROTOCORE_DTLS_CT_ACK 26 ///< DTLS 1.3 acknowledgement (RFC 9147 §7)

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
/** @brief What keys_derive takes: out, cipher, epoch, secret. */
typedef struct
{
    DtlsRecordKeys *out;
    DtlsCipher cipher;
    uint16_t epoch;
    const uint8_t *secret; ///< 32 bytes.
} DtlsRecordKeysDeriveArgs;
/** @brief What plaintext_build takes: content_type, epoch, seq, ... */
typedef struct
{
    uint8_t content_type;
    uint16_t epoch;
    uint64_t seq;
    const uint8_t *fragment;
    size_t frag_len;
    uint8_t *out;
    size_t out_cap;
} DtlsRecordPlaintextBuildArgs;
/** @brief What plaintext_parse takes: rec, rec_len, out. */
typedef struct
{
    const uint8_t *rec;
    size_t rec_len;
    DtlsPlaintext *out;
} DtlsRecordPlaintextParseArgs;
/** @brief What protect takes: keys, seq, content_type, plaintext, ... */
typedef struct
{
    DtlsRecordKeys *keys;
    uint64_t seq;
    uint8_t content_type;
    const uint8_t *plaintext;
    size_t pt_len;
    uint8_t *out;
    size_t out_cap;
    const uint8_t *cid;
    size_t cid_len;
} DtlsRecordProtectArgs;
/** @brief What unprotect takes: keys, next_seq, rec, rec_len, out, ... */
typedef struct
{
    DtlsRecordKeys *keys;
    uint64_t next_seq;
    const uint8_t *rec;
    size_t rec_len;
    uint8_t *out;
    size_t out_cap;
    DtlsCiphertext *info;
    const uint8_t *expected_cid;
    size_t expected_cid_len;
} DtlsRecordUnprotectArgs;
/** @brief What replay_init takes: w. */
typedef struct
{
    DtlsReplayWindow *w;
} DtlsRecordReplayInitArgs;
/** @brief What replay_check takes: w, seq. */
typedef struct
{
    const DtlsReplayWindow *w;
    uint64_t seq;
} DtlsRecordReplayCheckArgs;
/** @brief What replay_mark takes: w, seq. */
typedef struct
{
    DtlsReplayWindow *w;
    uint64_t seq;
} DtlsRecordReplayMarkArgs;
/**
 * @brief DTLS 1.3 record layer (RFC 9147 §4).
 *
 * A caller sets the members a call takes, invokes it through ::DtlsRecord with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   DtlsRecord.keys_derive_args.out = ...;
 *   DtlsRecord.keys_derive_args.cipher = ...;
 *   DtlsRecord.keys_derive_args.epoch = ...;
 *   DtlsRecord.keys_derive_args.secret = ...;
 *   DtlsRecord.keys_derive(work);
 *
 * @var DtlsRecordNs::keys_derive_args  what keys_derive takes: out, cipher, epoch, secret
 * @var DtlsRecordNs::plaintext_build_args  what plaintext_build takes: content_type, epoch, seq,
 * @var DtlsRecordNs::plaintext_parse_args  what plaintext_parse takes: rec, rec_len, out
 * @var DtlsRecordNs::protect_args  what protect takes: keys, seq, content_type, plaintext,
 * @var DtlsRecordNs::unprotect_args  what unprotect takes: keys, next_seq, rec, rec_len, out,
 * @var DtlsRecordNs::replay_init_args  what replay_init takes: w
 * @var DtlsRecordNs::replay_check_args  what replay_check takes: w, seq
 * @var DtlsRecordNs::replay_mark_args  what replay_mark takes: w, seq
 * @var DtlsRecordNs::ok  a call's true/false outcome
 * @var DtlsRecordNs::n  the count a call reports
 * @var DtlsRecordNs::keys_derive  derive one direction's record keys from a 32-byte TLS 1.3 traffic ...
 * @var DtlsRecordNs::plaintext_build  a DTLSPlaintext record; bytes written (13 + frag_len), or 0 on ...
 * @var DtlsRecordNs::plaintext_parse  the same record back, validating legacy_version and the length ...
 * @var DtlsRecordNs::protect  seal one record (RFC 9147 sec 4.2): the unified header, the ...
 * @var DtlsRecordNs::unprotect  open one received record: decrypt the sequence number, rebuild the ...
 * @var DtlsRecordNs::replay_init  reset a replay window to empty
 * @var DtlsRecordNs::replay_check  whether seq is new and inside the window, rather than a replay or ...
 * @var DtlsRecordNs::replay_mark  record seq as accepted and advance the window; only after a ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    DtlsRecordKeysDeriveArgs keys_derive_args;
    DtlsRecordPlaintextBuildArgs plaintext_build_args;
    DtlsRecordPlaintextParseArgs plaintext_parse_args;
    DtlsRecordProtectArgs protect_args;
    DtlsRecordUnprotectArgs unprotect_args;
    DtlsRecordReplayInitArgs replay_init_args;
    DtlsRecordReplayCheckArgs replay_check_args;
    DtlsRecordReplayMarkArgs replay_mark_args;
    proto_bool ok;
    size_t n;
} DtlsRecordVars;

/** @brief The operands and the outcome. */
extern DtlsRecordVars DtlsRecordV;

/** @brief The entries. */
typedef struct
{
    void (*const keys_derive)(uint8_t *restrict work);
    void (*const plaintext_build)(uint8_t *restrict work);
    void (*const plaintext_parse)(uint8_t *restrict work);
    void (*const protect)(uint8_t *restrict work);
    void (*const unprotect)(uint8_t *restrict work);
    void (*const replay_init)(uint8_t *restrict work);
    void (*const replay_check)(uint8_t *restrict work);
    void (*const replay_mark)(uint8_t *restrict work);
} DtlsRecordNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in DtlsRecordV or a region of the borrow at a fixed offset.
void protocore_dtls_record_keys_derive(uint8_t *restrict work);
void protocore_dtls_record_plaintext_build(uint8_t *restrict work);
void protocore_dtls_record_plaintext_parse(uint8_t *restrict work);
void protocore_dtls_record_protect(uint8_t *restrict work);
void protocore_dtls_record_unprotect(uint8_t *restrict work);
void protocore_dtls_record_replay_init(uint8_t *restrict work);
void protocore_dtls_record_replay_check(uint8_t *restrict work);
void protocore_dtls_record_replay_mark(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `DtlsRecord.keys_derive(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const DtlsRecordNs DtlsRecord __attribute__((unused)) = {
    .keys_derive = protocore_dtls_record_keys_derive,
    .plaintext_build = protocore_dtls_record_plaintext_build,
    .plaintext_parse = protocore_dtls_record_plaintext_parse,
    .protect = protocore_dtls_record_protect,
    .unprotect = protocore_dtls_record_unprotect,
    .replay_init = protocore_dtls_record_replay_init,
    .replay_check = protocore_dtls_record_replay_check,
    .replay_mark = protocore_dtls_record_replay_mark,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DTLS

#endif // PROTOCORE_DTLS_RECORD_H
