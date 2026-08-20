// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

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
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HKDF_H
#define PROTOCORE_HKDF_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HKDF

PROTOCORE_BEGIN_DECLS

/** @brief HKDF-SHA256 output block length (== SHA-256 digest length). */
#define PROTOCORE_HKDF_HASH_LEN 32

/** @brief The RFC 8446 sec 7.1 HKDF-Expand-Label prefix used by TLS 1.3 and QUIC. DTLS 1.3 overrides
 *  it with "dtls13" (RFC 9147 sec 5.9); callers that need it pass it explicitly. */
#define PROTOCORE_HKDF_LABEL_PREFIX "tls13 "

// PROTOCORE_HKDF_BORROW - the bytes a derivation runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The salt and input keying material HKDF-Extract folds into a PRK. */
typedef struct
{
    const uint8_t *salt; ///< salt bytes; NULL only when salt_len is 0
    size_t salt_len;     ///< salt length
    const uint8_t *ikm;  ///< input keying material
    size_t ikm_len;      ///< its length
    uint8_t *prk;        ///< PROTOCORE_HKDF_HASH_LEN bytes
} HkdfExtractArgs;

/** @brief The PRK, context and output span of a bare HKDF-Expand. */
typedef struct
{
    const uint8_t *prk;  ///< PROTOCORE_HKDF_HASH_LEN bytes from extract
    const uint8_t *info; ///< context taken verbatim; NULL only when info_len is 0
    size_t info_len;     ///< its length
    uint8_t *out;        ///< output keying material
    size_t out_len;      ///< bytes requested; past 255*PROTOCORE_HKDF_HASH_LEN out is zeroed instead
} HkdfExpandArgs;

/** @brief The secret and label an HKDF-Expand-Label derives from, with an empty HkdfLabel context. */
typedef struct
{
    const uint8_t *secret;    ///< traffic secret (HKDF PRK), PROTOCORE_HKDF_HASH_LEN bytes
    const char *label;        ///< ASCII label without the prefix, <= 249 bytes
    uint8_t *out;             ///< output keying material
    size_t out_len;           ///< bytes requested
    const char *label_prefix; ///< PROTOCORE_HKDF_LABEL_PREFIX, or "dtls13" for DTLS 1.3
} HkdfExpandLabelArgs;

/** @brief The same with an explicit HkdfLabel context. */
typedef struct
{
    const uint8_t *secret;    ///< PRK, PROTOCORE_HKDF_HASH_LEN bytes
    const char *label;        ///< ASCII label without the prefix, <= 249 bytes
    const uint8_t *context;   ///< context bytes, <= 255; NULL only when context_len is 0
    size_t context_len;       ///< context length
    uint8_t *out;             ///< output keying material
    size_t out_len;           ///< bytes requested
    const char *label_prefix; ///< PROTOCORE_HKDF_LABEL_PREFIX, or "dtls13" for DTLS 1.3
} HkdfExpandLabelCtxArgs;

/**
 * @brief HKDF-SHA256 (RFC 5869) and HKDF-Expand-Label (RFC 8446 sec 7.1).
 *
 * A caller sets the members a call takes, invokes it through ::Hkdf with the bytes it runs out of, and
 * reads the outcome off the same handle. How those bytes are carved is this module's and is never named
 * here.
 *
 *   Hkdf.extract_args.salt = salt;
 *   Hkdf.extract_args.salt_len = sizeof(salt);
 *   Hkdf.extract_args.ikm = dcid;
 *   Hkdf.extract_args.ikm_len = dcid_len;
 *   Hkdf.extract_args.prk = initial_secret;
 *   Hkdf.extract(work);
 *   Hkdf.expand_label_args.secret = initial_secret;
 *   Hkdf.expand_label_args.label = "quic key";
 *   Hkdf.expand_label_args.out = key;
 *   Hkdf.expand_label_args.out_len = 16;
 *   Hkdf.expand_label_args.label_prefix = PROTOCORE_HKDF_LABEL_PREFIX;
 *   Hkdf.expand_label(work);
 *
 * @var HkdfNs::extract_args           the salt and IKM HKDF-Extract folds into a PRK
 * @var HkdfNs::expand_args            the PRK, context and output span of a bare HKDF-Expand
 * @var HkdfNs::expand_label_args      the secret and label of an empty-context HKDF-Expand-Label
 * @var HkdfNs::expand_label_ctx_args  the same with an explicit HkdfLabel context
 * @var HkdfNs::ok                     a call's true/false outcome
 * @var HkdfNs::extract                PRK = HMAC-SHA256(salt, ikm) (RFC 5869 sec 2.2)
 * @var HkdfNs::expand                 OKM = T(1) | T(2) | ..., info taken verbatim (RFC 5869 sec 2.3)
 * @var HkdfNs::expand_label           expand under an HkdfLabel with an empty context
 * @var HkdfNs::expand_label_ctx       expand under an HkdfLabel carrying a context, the Derive-Secret form
 *
 * @ref HkdfNs::expand caps out_len at 255*PROTOCORE_HKDF_HASH_LEN, the point past which the single-octet
 * block counter has no encoding: out is zeroed and @ref HkdfNs::ok comes back false.
 *
 * @c work is PROTOCORE_HKDF_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and the
 * pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. The borrow
 * carries the PRK and the T(i) block, so two derivations in flight are two borrows and never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref HkdfNs::ok, and that is all
 * the surface there is.
 */
typedef struct
{
    HkdfExtractArgs extract_args;
    HkdfExpandArgs expand_args;
    HkdfExpandLabelArgs expand_label_args;
    HkdfExpandLabelCtxArgs expand_label_ctx_args;
    proto_bool ok;
} HkdfVars;

/** @brief The operands and the outcome. */
extern HkdfVars HkdfV;

/** @brief The entries. */
typedef struct
{
    void (*const extract)(uint8_t *restrict work);
    void (*const expand)(uint8_t *restrict work);
    void (*const expand_label)(uint8_t *restrict work);
    void (*const expand_label_ctx)(uint8_t *restrict work);
} HkdfNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HkdfV or a region of the borrow at a fixed offset.
void protocore_hkdf_extract(uint8_t *restrict work);
void protocore_hkdf_expand(uint8_t *restrict work);
void protocore_hkdf_expand_label(uint8_t *restrict work);
void protocore_hkdf_expand_label_ctx(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Hkdf.extract(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HkdfNs Hkdf __attribute__((unused)) = {
    .extract = protocore_hkdf_extract,
    .expand = protocore_hkdf_expand,
    .expand_label = protocore_hkdf_expand_label,
    .expand_label_ctx = protocore_hkdf_expand_label_ctx,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HKDF

#endif // PROTOCORE_HKDF_H
