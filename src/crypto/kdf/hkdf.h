// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hkdf.h
 * @brief HKDF-SHA256 (RFC 5869) and TLS 1.3 HKDF-Expand-Label (RFC 8446 sec 7.1).
 *
 * QUIC packet protection keys are derived with the TLS 1.3 key schedule (RFC 9001 sec 5.2):
 * an Initial secret is HKDF-Extract'd from a fixed salt and the client's Destination Connection
 * ID, and every packet-protection value (key / iv / hp) is an HKDF-Expand-Label of a traffic
 * secret. This is the same HMAC-SHA256 the SSH transport already ships, so these two routines are
 * a thin layer over pc_hmac_sha256 rather than a second HMAC.
 *
 * Pure, zero heap, host-tested against the RFC 9001 Appendix A worked examples (the HkdfLabel
 * byte strings and the derived client/server secrets).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HKDF_H
#define PROTOCORE_HKDF_H

#include "protocore_config.h"

// Shared by the HTTP/3 (QUIC) key schedule and the DTLS 1.3 record layer.
#if (PC_ENABLE_HTTP3 || PC_ENABLE_DTLS)

#include <stddef.h>
#include <stdint.h>

/** @brief HKDF-SHA256 output block length (== SHA-256 digest length). */
#define PC_HKDF_HASH_LEN 32

/**
 * @brief HKDF-Extract (RFC 5869 sec 2.2): PRK = HMAC-SHA256(salt, ikm).
 *
 * @param salt      Optional salt (may be NULL only when @p salt_len is 0).
 * @param salt_len  Salt length in bytes.
 * @param ikm       Input keying material.
 * @param ikm_len   Input keying material length.
 * @param prk       Output pseudo-random key, must be PC_HKDF_HASH_LEN bytes.
 */
void pc_hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len,
                     uint8_t prk[PC_HKDF_HASH_LEN]);

/** @brief The RFC 8446 sec 7.1 HKDF-Expand-Label prefix used by TLS 1.3 and QUIC. DTLS 1.3 overrides
 *  it with "dtls13" (RFC 9147 sec 5.9); callers that need it pass it explicitly. */
static constexpr char PC_HKDF_LABEL_PREFIX[] = "tls13 ";

/**
 * @brief HKDF-Expand-Label (RFC 8446 sec 7.1) with the QUIC/TLS 1.3 "tls13 " label prefix.
 *
 * Builds the HkdfLabel structure
 *   struct { uint16 length; opaque label<7..255> = "tls13 " + label; opaque context<0..255>; }
 * and runs HKDF-Expand (RFC 5869 sec 2.3) with an empty context (all QUIC packet-protection uses of
 * this function pass an empty context). @p out_len must not exceed 255*32 bytes; QUIC only ever asks
 * for <= 32, which is a single HMAC block.
 *
 * @param secret        Traffic secret (HKDF PRK), PC_HKDF_HASH_LEN bytes.
 * @param label         Short ASCII label, e.g. "quic key" (without the prefix), <= 249 bytes.
 * @param out           Output keying material.
 * @param out_len       Number of output bytes requested.
 * @param label_prefix  HkdfLabel prefix; defaults to the TLS 1.3 "tls13 " prefix. DTLS 1.3 passes "dtls13".
 */
void pc_hkdf_expand_label(const uint8_t secret[PC_HKDF_HASH_LEN], const char *label, uint8_t *out, size_t out_len,
                          const char *label_prefix = PC_HKDF_LABEL_PREFIX);

/**
 * @brief HKDF-Expand-Label with an explicit context (RFC 8446 sec 7.1, the general form).
 *
 * Identical to pc_hkdf_expand_label() but the HkdfLabel context is @p context (0..255 bytes)
 * instead of empty. The TLS 1.3 key schedule's Derive-Secret (sec 7.1) is exactly this with the
 * context set to a Transcript-Hash, so the whole handshake key schedule layers on this one routine.
 *
 * @param secret       PRK, PC_HKDF_HASH_LEN bytes.
 * @param label        Short ASCII label without the "tls13 " prefix, <= 249 bytes.
 * @param context      Context bytes (may be NULL only when @p context_len is 0), <= 255 bytes.
 * @param context_len  Context length.
 * @param out          Output keying material.
 * @param out_len      Number of output bytes requested.
 * @param label_prefix HkdfLabel prefix; defaults to the TLS 1.3 "tls13 " prefix. DTLS 1.3 passes "dtls13".
 */
void pc_hkdf_expand_label_ctx(const uint8_t secret[PC_HKDF_HASH_LEN], const char *label, const uint8_t *context,
                              size_t context_len, uint8_t *out, size_t out_len,
                              const char *label_prefix = PC_HKDF_LABEL_PREFIX);

#endif // PC_ENABLE_HTTP3 || PC_ENABLE_DTLS
#endif // PROTOCORE_HKDF_H
