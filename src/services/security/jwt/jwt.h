// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file jwt.h
 * @brief JSON Web Token verification, HS256: RFC 7519 claims carried in an RFC 7515 JWS.
 *
 * A token arrives in the JWS Compact Serialization (RFC 7515 sec 7.1),
 * `BASE64URL(UTF8(JWS Protected Header)) || '.' || BASE64URL(JWS Payload) || '.' ||
 * BASE64URL(JWS Signature)`, normally inside `Authorization: Bearer <token>` (RFC 6750 sec 2.1).
 * The verifier recomputes the MAC over the JWS Signing Input (RFC 7515 sec 2), compares it against
 * the signature segment in constant time (RFC 7518 sec 3.2), and reads claims out of the payload
 * (RFC 7519 sec 4).
 *
 * Only HS256 is served: that `alg` value is HMAC using SHA-256 (RFC 7518 sec 3.1), and the JOSE
 * header's `alg` is required to name it before any MAC is computed (RFC 7515 sec 4.1.1, RFC 8725
 * sec 3.1), which rejects `none` and every other algorithm substitution. RS256 ID tokens belong to
 * the OIDC module. Every segment decodes with the URL and filename safe alphabet, padding skipped
 * (RFC 4648 sec 5).
 *
 * The base calls judge the signature alone. The `_at` calls also judge `exp` (RFC 7519 sec 4.1.4)
 * and `nbf` (sec 4.1.5) against a caller-supplied NumericDate (sec 2) with a skew leeway; `iat`
 * (sec 4.1.6) is read like any other integer claim. Nothing here allocates: every buffer is a local
 * of the call that fills it, and the whole path runs on a host build.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_JWT_H
#define PROTOCORE_JWT_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_JWT

/** @brief RFC 7515 sec 7.1: the compact serialization a call reads, bare or inside Bearer credentials. */
typedef struct
{
    const char *jws;         ///< BASE64URL(header) '.' BASE64URL(payload) '.' BASE64URL(signature)
    size_t jws_len;          ///< readable characters of @c jws, at most PROTOCORE_JWT_MAX_LEN
    const char *credentials; ///< an Authorization field value: "Bearer" 1*SP b64token (RFC 6750 sec 2.1)
} JwtTokenArgs;

/** @brief RFC 7518 sec 3.2: the shared key the HMAC-SHA-256 runs under, 256 bits or larger. */
typedef struct
{
    const uint8_t *secret; ///< the key octets
    size_t secret_len;     ///< how many of them there are
} JwtKeyArgs;

/** @brief RFC 7519 sec 4.1.4 / 4.1.5: the clock `exp` and `nbf` are judged against. */
typedef struct
{
    long now;      ///< NumericDate now (RFC 7519 sec 2); 0 or less states there is no wall clock
    long leeway_s; ///< seconds of clock skew both claims are given
} JwtTimeArgs;

/** @brief RFC 7519 sec 4: the claim a read names, and where a string claim lands. */
typedef struct
{
    const char *name; ///< the claim's member name inside the JWS Payload, unquoted
    char *out;        ///< where a string claim is written, NUL-terminated
    size_t out_cap;   ///< how many bytes @c out holds
} JwtClaimArgs;

/** @brief RFC 8693 sec 4.2: the `scope` claim's value and the scope a check demands. */
typedef struct
{
    const char *claim;    ///< the claim value: space-delimited, case-sensitive tokens (RFC 6749 sec 3.3)
    const char *required; ///< the one scope token being looked for
} JwtScopeArgs;

/** @brief The verifier's calls and the handle they read, described only in jwt.c. */
struct JwtInternal;

/**
 * @brief The HS256 JWT verifier.
 *
 * A caller sets the members a call takes, invokes it through ::Jwt, and reads the outcome off the
 * same handle. No slot member: the verifier owns no table, so a call names its own inputs and
 * nothing else. No storage member: nothing survives a call, so every buffer is a local of the call
 * that fills it.
 *
 * @var JwtNs::token  how the token arrives: the compact serialization, or the Bearer credentials
 *                    carrying it
 * @var JwtNs::key    the HS256 shared key (RFC 7518 sec 3.2)
 * @var JwtNs::time   the NumericDate an `_at` call judges the time claims against
 * @var JwtNs::claim  the claim a read names and the buffer a string claim is copied into
 * @var JwtNs::scope  the `scope` claim value and the scope a check demands
 * @var JwtNs::ok     a call's true/false outcome
 * @var JwtNs::num    the integer a claim read returns; a NumericDate for `exp` / `nbf` / `iat`
 *
 * @var JwtNs::verify_mac
 * Validate the JWS Signature over the JWS Signing Input (RFC 7515 sec 5.2). The JOSE header's `alg`
 * must be HS256, the signature segment must be the 43 characters an unpadded base64url 256-bit MAC
 * takes, and the compare is constant time (RFC 7518 sec 3.2). Claims are not read.
 *
 * @var JwtNs::verify_bearer
 * ::JwtNs::verify_mac on the b64token inside @c token.credentials (RFC 6750 sec 2.1). The scheme
 * name is matched without regard to case (RFC 7235 sec 2.1) and @c token.jws is left pointing at
 * the token that was found.
 *
 * @var JwtNs::time_claims_valid
 * True when the token is inside its validity window: not at or after `exp` (RFC 7519 sec 4.1.4) and
 * not before `nbf` (sec 4.1.5), each given @c time.leeway_s of skew. An absent claim is not
 * enforced; a @c time.now of 0 or less states there is no wall clock, so neither claim can be judged
 * and the answer is true. The signature is not checked here, so pair this with ::JwtNs::verify_mac.
 *
 * @var JwtNs::verify_mac_at
 * ::JwtNs::verify_mac and then ::JwtNs::time_claims_valid.
 *
 * @var JwtNs::verify_bearer_at
 * ::JwtNs::verify_bearer and then ::JwtNs::time_claims_valid.
 *
 * @var JwtNs::claim_int
 * Read the integer claim @c claim.name out of the JWS Payload into @c num. The signature is not
 * checked, so a verify call comes first.
 *
 * @var JwtNs::claim_str
 * Copy the string claim @c claim.name into @c claim.out, bounded by @c claim.out_cap. A backslash is
 * dropped and the character after it taken literally, which carries a quoted or backslashed
 * character through a `sub`, `role` or `scope` value. The signature is not checked.
 *
 * @var JwtNs::scope_allows
 * True when @c scope.required is one whole token of the space-delimited @c scope.claim (RFC 6749
 * sec 3.3, the syntax RFC 8693 sec 4.2 gives the claim). A prefix of a token never passes.
 *
 * @var JwtNs::internal  the handle a call reads its members from
 */
typedef struct
{
    JwtTokenArgs token; ///< how the token arrives
    JwtKeyArgs key;     ///< the HS256 shared key
    JwtTimeArgs time;   ///< the clock the time claims are judged against
    JwtClaimArgs claim; ///< the claim a read names
    JwtScopeArgs scope; ///< the scope claim and the scope demanded of it

    proto_bool ok;
    long num;

    void (*verify_mac)(struct JwtInternal *ctx);
    void (*verify_bearer)(struct JwtInternal *ctx);
    void (*time_claims_valid)(struct JwtInternal *ctx);
    void (*verify_mac_at)(struct JwtInternal *ctx);
    void (*verify_bearer_at)(struct JwtInternal *ctx);
    void (*claim_int)(struct JwtInternal *ctx);
    void (*claim_str)(struct JwtInternal *ctx);
    void (*scope_allows)(struct JwtInternal *ctx);

    struct JwtInternal *internal;
} JwtNs;

/** @brief The one symbol this module exports. */
extern JwtNs Jwt;

#endif // PROTOCORE_ENABLE_JWT

PROTOCORE_END_DECLS

#endif // PROTOCORE_JWT_H
