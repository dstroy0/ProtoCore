// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls_record.h
 * @brief TLS 1.3 record layer over a reliable stream (RFC 8446 sec 5).
 *
 * The TCP counterpart to pc_dtls_record: it protects and unprotects individual records on a byte
 * stream. TCP delivers them in order, exactly once, so this half of the protocol is the smaller
 * one - there is no epoch, no sequence number on the wire, no anti-replay window and no fragment
 * reassembly. What remains is the header, the AEAD, and a record counter each side keeps itself.
 *
 * Two record shapes (RFC 8446 sec 5.1, 5.2):
 *   - **TLSPlaintext** - the 5-byte header (type, legacy_record_version, length) followed by the
 *     fragment, sent unencrypted for the first handshake flight and for alerts before keys exist.
 *   - **TLSCiphertext** - the same header with opaque_type = application_data(23) over an AEAD-sealed
 *     body. The sealed plaintext is `content || real_type`, so the true content type travels inside
 *     the encryption; the header's type is a constant that reveals nothing.
 *
 * The record sequence number is never transmitted. It starts at zero when a key is installed and
 * counts records under that key (sec 5.3), so both ends derive the same nonce from their own count.
 *
 * -- Reuse --
 *   AEAD (AEAD_AES_128_GCM) and the key/iv derivation come from pc_hkdf via pc_tls13_kdf, under the
 *   "tls13 " label prefix (@ref TLS13_KDF). One cipher suite, the one the whole hand-rolled TLS 1.3
 *   stack uses: TLS_AES_128_GCM_SHA256.
 *
 * Pure, zero heap, host-tested. This is the portable record layer; a build whose vendor ships a TLS
 * stack (PC_HAS_VENDOR_TLS) compiles that instead and none of this.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TLS_RECORD_H
#define PROTOCORE_TLS_RECORD_H

#include "crypto/aead/aes128gcm.h" // pc_aes128gcm_key, PC_WORK_AES128GCM
#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_TLS_SOFTWARE

/** @name Record content types (RFC 8446 sec 5).
 *  Shared by the TLSPlaintext `type` field and the TLSInnerPlaintext trailing content type. */
///@{
#define PC_TLS_CT_CHANGE_CIPHER_SPEC 20
#define PC_TLS_CT_ALERT 21
#define PC_TLS_CT_HANDSHAKE 22
#define PC_TLS_CT_APPLICATION_DATA 23
///@}

/** @brief TLSPlaintext legacy_record_version on the wire: TLS 1.2 (RFC 8446 sec 5.1). */
#define PC_TLS_LEGACY_VERSION 0x0303

/** @brief TLSPlaintext header length: type(1) + legacy_record_version(2) + length(2). */
#define PC_TLS_PLAINTEXT_HDR_LEN 5

/** @brief AEAD tag length (all supported suites: 16 bytes). */
#define PC_TLS_TAG_LEN 16

/** @brief Largest plaintext fragment one record carries: 2^14 (RFC 8446 sec 5.1). */
#define PC_TLS_MAX_PLAINTEXT 16384

/**
 * @brief Largest protected record body: the fragment, its inner content type, and the tag.
 *
 * RFC 8446 sec 5.2 allows up to 256 further bytes of padding. Nothing here pads, so a record this
 * side builds never reaches that; a peer's may, and @ref TlsRecordNs::unprotect strips it.
 */
#define PC_TLS_MAX_CIPHERTEXT (PC_TLS_MAX_PLAINTEXT + 1 + PC_TLS_TAG_LEN)

/** @brief Record-layer AEAD suites (phase 1: AEAD_AES_128_GCM with SHA-256). */
typedef enum PROTO_ENUM_PACKED
{
    TLS_CIPHER_AES_128_GCM_SHA256 = 0
} TlsCipher;

/**
 * @brief One direction's record-protection state (RFC 8446 sec 5.3).
 *
 * Derived from a TLS 1.3 traffic secret; holds the AEAD key plus write IV and the record counter the
 * nonce is built from. One instance per (direction, key generation).
 */
typedef struct
{
    TlsCipher cipher;                           ///< negotiated AEAD (phase 1: AES-128-GCM)
    _Alignas(8) uint8_t gcm[PC_WORK_AES128GCM]; ///< keyed AEAD context, built once per key
                                                ///< Replaces the raw key: the schedule is what the
                                                ///< AEAD needs, so no raw key stays resident.
    uint8_t iv[PC_AES128GCM_IV_LEN];            ///< AEAD write IV (per-record nonce = iv XOR seq)
    uint8_t nonce[PC_AES128GCM_IV_LEN];         ///< this record's nonce, rebuilt from iv and seq
    uint64_t seq;                               ///< records sealed/opened under this key, never sent
    proto_bool ready;                           ///< the AEAD context holds a key
} TlsRecordKeys;

/** @brief Parsed view of a TLSPlaintext record (fields point into the caller's buffer). */
typedef struct
{
    uint8_t content_type;
    const uint8_t *fragment; ///< into the input buffer
    size_t frag_len;
} TlsPlaintext;

/** @brief Result of a successful @ref TlsRecordNs::unprotect. */
typedef struct
{
    uint8_t content_type; ///< recovered inner content type (last non-zero byte of the inner plaintext)
    size_t pt_len;        ///< plaintext bytes written to @p out
} TlsCiphertext;

/**
 * @brief The record layer (RFC 8446 sec 5): the two record shapes and their keys.
 *
 * @var TlsRecordNs::keys_derive  derive one direction's record keys from a 32-byte TLS 1.3 traffic
 *      secret. RFC 8446 sec 7.3: key and iv are each HKDF-Expand-Label of it under the "tls13 "
 *      prefix. Sets seq to zero, which is what starting a key generation means (sec 5.3).
 * @var TlsRecordNs::plaintext_build  build a TLSPlaintext record; bytes written, or 0 on overflow
 * @var TlsRecordNs::plaintext_parse  parse one back, validating the length field; the record length
 *      consumed, or 0 if malformed or truncated
 * @var TlsRecordNs::protect  seal one record (sec 5.2): the inner plaintext is @p pt with the real
 *      content type appended, the header carries application_data(23) and the sealed length, and the
 *      nonce is iv XOR seq with the header as the associated data. Advances seq. Bytes written, or 0
 *      on overflow or an over-long fragment.
 * @var TlsRecordNs::unprotect  open a received record: verify and decrypt into @p out, then scan back
 *      past the zero padding for the real content type. Advances seq only on success. A record whose
 *      inner plaintext is all zeros has no content type and is refused (sec 5.4).
 * @var TlsRecordNs::keys_wipe  wipe the AEAD schedule and the IV; the storage stays the caller's
 */
typedef struct
{
    void (*keys_derive)(TlsRecordKeys *out, TlsCipher cipher, const uint8_t secret[32]);
    size_t (*plaintext_build)(uint8_t content_type, const uint8_t *fragment, size_t frag_len, uint8_t *out,
                              size_t out_cap);
    size_t (*plaintext_parse)(const uint8_t *rec, size_t rec_len, TlsPlaintext *out);
    size_t (*protect)(TlsRecordKeys *keys, uint8_t content_type, const uint8_t *pt, size_t pt_len, uint8_t *out,
                      size_t out_cap);
    proto_bool (*unprotect)(TlsRecordKeys *keys, const uint8_t *rec, size_t rec_len, uint8_t *out, size_t out_cap,
                            TlsCiphertext *out_info);
    void (*keys_wipe)(TlsRecordKeys *keys);
} TlsRecordNs;

/** @brief The one symbol this module exports. */
extern const TlsRecordNs TlsRecord;

#endif // PC_TLS_SOFTWARE

PROTO_END_DECLS

#endif // PROTOCORE_TLS_RECORD_H
