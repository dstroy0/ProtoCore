// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file csrf.h
 * @brief Stateless HMAC-signed CSRF token (PROTOCORE_ENABLE_CSRF).
 *
 * A token is `<nonce_hex>.<sig_hex>` where sig is the first CSRF_SIG_BYTES of
 * HMAC-SHA256(secret, nonce). The secret is seeded once from the platform's randomness; the nonce is
 * a per-issue counter and need not be secret, because the security is the HMAC. A verify recomputes
 * the HMAC over the embedded nonce and compares the signature in constant time, so no server-side
 * session state is kept.
 *
 * The token is sized to fit a single MAX_VAL_LEN header value and a `csrf=` cookie. Nothing here
 * touches a platform, so it runs on the host with PROTOCORE_ENABLE_CSRF set and a fixed secret.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CSRF_H
#define PROTOCORE_CSRF_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_CSRF

PROTOCORE_BEGIN_DECLS

/** @brief Nonce length in bytes (hex-encoded in the token). */
#define CSRF_NONCE_BYTES 6
/** @brief Signature length in bytes (truncated HMAC, hex-encoded in the token). */
#define CSRF_SIG_BYTES 14
/** @brief Buffer size for a token string: 2*nonce + '.' + 2*sig + NUL = 42, rounded up. */
#define CSRF_TOKEN_BUF 48

// PROTOCORE_CSRF_BORROW - the bytes the issuer runs out of - is stated in protocore_config.h, which
// sums it into the secure arena. The secret lives in those bytes, so a caller takes them once for
// the life of the program and every call runs out of the same span. How they are carved is this
// module's and is never named here.

/** @brief The secret an issuer signs with. */
typedef struct
{
    const uint8_t *secret; ///< key bytes; NULL clears the secret
    size_t len;            ///< how many; past 32 the key is truncated
} CsrfSecretArgs;

/** @brief Where a fresh token lands. */
typedef struct
{
    char *out;  ///< destination, at least CSRF_TOKEN_BUF
    size_t cap; ///< its size
} CsrfIssueArgs;

/** @brief The token a verify checks. */
typedef struct
{
    const char *token; ///< the `<nonce_hex>.<sig_hex>` string
} CsrfVerifyArgs;

/**
 * @brief Stateless HMAC-signed CSRF tokens.
 *
 * A caller sets the members a call takes, invokes it through ::Csrf with the bytes it runs out of,
 * and reads the outcome off the same handle.
 *
 *   Csrf.secret_args.secret = seed;
 *   Csrf.secret_args.len = sizeof(seed);
 *   Csrf.set_secret(work);
 *   Csrf.issue_args.out = buf;
 *   Csrf.issue_args.cap = sizeof(buf);
 *   Csrf.issue(work);
 *   // Csrf.n is the token length, 0 when no secret is set or the buffer is short
 *
 * @var CsrfNs::secret_args  the secret an issuer signs with
 * @var CsrfNs::issue_args   where a fresh token lands
 * @var CsrfNs::verify_args  the token a verify checks
 * @var CsrfNs::ok           a call's true/false outcome
 * @var CsrfNs::n            the issued token's length in characters
 * @var CsrfNs::valid        whether the last @ref CsrfNs::verify accepted the token
 * @var CsrfNs::set_secret   seed the HMAC secret; call once with platform randomness
 * @var CsrfNs::issue        write a fresh signed token out
 * @var CsrfNs::verify       recompute the HMAC over the embedded nonce and compare in constant time
 * @var CsrfNs::reset        clear the secret and the nonce counter
 *
 * @c work is PROTOCORE_CSRF_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The secret is in those
 * bytes rather than in this module, so a caller takes them once for the life of the program and
 * every call runs out of the same span. The caller releases it, and the pool wipes on release; this
 * module neither takes it, holds it, nor releases it.
 *
 * No storage member and no context: a caller sets operands and reads @ref CsrfNs::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    CsrfSecretArgs secret_args;
    CsrfIssueArgs issue_args;
    CsrfVerifyArgs verify_args;

    proto_bool ok;
    int n;
    proto_bool valid;

    void (*const set_secret)(uint8_t *restrict work);
    void (*const issue)(uint8_t *restrict work);
    void (*const verify)(uint8_t *restrict work);
    void (*const reset)(uint8_t *restrict work);
} CsrfNs;

/** @brief The one symbol this module exports. */
extern CsrfNs Csrf;

/**
 * @brief The PROTOCORE_CSRF_BORROW bytes the program's issuer runs out of.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where that
 * borrow comes from. Taken once from the end of the secure pool, which no mark and no release walks,
 * so the secret and the nonce counter last the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_csrf_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CSRF

#endif // PROTOCORE_CSRF_H
