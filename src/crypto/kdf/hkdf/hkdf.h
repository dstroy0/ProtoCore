// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_HKDF_H
#define PROTOCORE_HKDF_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file hkdf.h
 * @brief HKDF-SHA256 (RFC 5869) and TLS 1.3 HKDF-Expand-Label (RFC 8446 sec 7.1).
 *
 * QUIC packet protection keys are derived with the TLS 1.3 key schedule (RFC 9001 sec 5.2):
 * an Initial secret is HKDF-Extract'd from a fixed salt and the client's Destination Connection
 * ID, and every packet-protection value (key / iv / hp) is an HKDF-Expand-Label of a traffic
 * secret. This is the same HMAC-SHA256 the SSH transport already ships, so these entries are a
 * thin layer over the @ref HmacSha256Ns entries rather than a second HMAC.
 *
 * Pure, zero heap, host-tested against the RFC 9001 Appendix A worked examples (the HkdfLabel
 * byte strings and the derived client/server secrets).
 *
 * @ref HkdfNs::expand caps out_len at 255*PROTOCORE_HKDF_HASH_LEN, the point past which the single-octet
 * block counter has no encoding: out is zeroed and @ref HkdfNs::ok comes back false.
 *
 * @c work is PROTOCORE_HKDF_BORROW secure bytes the CALLER took, at an address it knows. It is not held past the call,
 * so nothing here aliases it. The caller releases it, and the pool wipes on release; this module neither takes it,
 * holds it, releases it, nor wipes it. The borrow carries the PRK and the T(i) block, so two derivations in flight are
 * two borrows and never collide.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief HKDF-SHA256 output block length (== SHA-256 digest length). */
#define PROTOCORE_HKDF_HASH_LEN 32

/** @brief The RFC 8446 sec 7.1 HKDF-Expand-Label prefix used by TLS 1.3 and QUIC. DTLS 1.3 overrides
 *  it with "dtls13" (RFC 9147 sec 5.9); callers that need it pass it explicitly. */
#define PROTOCORE_HKDF_LABEL_PREFIX "tls13 "

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*extract)(uint8_t *, const uint8_t *, size_t, const uint8_t *, size_t, uint8_t *);
    proto_bool (*expand)(uint8_t *, const uint8_t *, const uint8_t *, size_t, uint8_t *, size_t);
    proto_bool (*expand_label)(uint8_t *, const uint8_t *, const char *, uint8_t *, size_t, const char *);
    proto_bool (*expand_label_ctx)(uint8_t *, const uint8_t *, const char *, const uint8_t *, size_t, uint8_t *, size_t,
                                   const char *);
} HkdfNs;
PROTOCORE_NS_LAYOUT(HkdfNs, extract, expand, expand_label, expand_label_ctx);

/**
 * @brief PRK = HMAC-SHA256(salt, ikm) (RFC 5869 sec 2.2).
 * @param work PROTOCORE_HKDF_BORROW bytes the caller took. Not held past the call.
 * @param salt salt bytes; NULL only when salt_len is 0
 * @param salt_len salt length
 * @param ikm input keying material
 * @param ikm_len its length
 * @param prk PROTOCORE_HKDF_HASH_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hkdf_extract(uint8_t *work, const uint8_t *salt, size_t salt_len, const uint8_t *ikm,
                                  size_t ikm_len, uint8_t *prk);
/**
 * @brief OKM = T(1) | T(2) | ..., info taken verbatim (RFC 5869 sec 2.3).
 * @param work PROTOCORE_HKDF_BORROW bytes the caller took. Not held past the call.
 * @param prk PROTOCORE_HKDF_HASH_LEN bytes from extract
 * @param info context taken verbatim; NULL only when info_len is 0
 * @param info_len its length
 * @param out output keying material
 * @param out_len bytes requested; past 255*PROTOCORE_HKDF_HASH_LEN out is zeroed instead
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hkdf_expand(uint8_t *work, const uint8_t *prk, const uint8_t *info, size_t info_len, uint8_t *out,
                                 size_t out_len);
/**
 * @brief Expand under an HkdfLabel with an empty context.
 * @param work PROTOCORE_HKDF_BORROW bytes the caller took. Not held past the call.
 * @param secret traffic secret (HKDF PRK), PROTOCORE_HKDF_HASH_LEN bytes
 * @param label ASCII label without the prefix, <= 249 bytes
 * @param out output keying material
 * @param out_len bytes requested
 * @param label_prefix PROTOCORE_HKDF_LABEL_PREFIX, or "dtls13" for DTLS 1.3
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hkdf_expand_label(uint8_t *work, const uint8_t *secret, const char *label, uint8_t *out,
                                       size_t out_len, const char *label_prefix);
/**
 * @brief Expand under an HkdfLabel carrying a context, the Derive-Secret form.
 * @param work PROTOCORE_HKDF_BORROW bytes the caller took. Not held past the call.
 * @param secret PRK, PROTOCORE_HKDF_HASH_LEN bytes
 * @param label ASCII label without the prefix, <= 249 bytes
 * @param context context bytes, <= 255; NULL only when context_len is 0
 * @param context_len context length
 * @param out output keying material
 * @param out_len bytes requested
 * @param label_prefix PROTOCORE_HKDF_LABEL_PREFIX, or "dtls13" for DTLS 1.3
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hkdf_expand_label_ctx(uint8_t *work, const uint8_t *secret, const char *label,
                                           const uint8_t *context, size_t context_len, uint8_t *out, size_t out_len,
                                           const char *label_prefix);

/** @brief Module namespace. */
PROTOCORE_NS HkdfNs Hkdf PROTOCORE_UNUSED = {.extract = protocore_hkdf_extract,
                                             .expand = protocore_hkdf_expand,
                                             .expand_label = protocore_hkdf_expand_label,
                                             .expand_label_ctx = protocore_hkdf_expand_label_ctx};

PROTOCORE_END_DECLS

#endif // PROTOCORE_HKDF_H
