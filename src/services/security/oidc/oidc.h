// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file oidc.h
 * @brief OpenID Connect ID Token validation, RS256 (PROTOCORE_ENABLE_OIDC).
 *
 * The Relying Party side of OpenID Connect Core 1.0 sec 3.1.3.7 "ID Token Validation". That
 * document is an OpenID Foundation specification, not an IETF RFC; the token it carries is an
 * IETF one, a JWS in the Compact Serialization (RFC 7515 sec 7.1) whose `alg` is RS256, which
 * JWA (RFC 7518 sec 3.3) defines as RSASSA-PKCS1-v1_5 using SHA-256 over the JWS Signing Input
 * (RFC 7515 sec 2). Given an ID Token and the OP's JWK Set (RFC 7517 sec 5), a verify:
 *   1. splits the Compact Serialization and requires `alg` == RS256 (RFC 7515 sec 4.1.1;
 *      OIDC Core sec 3.1.3.7 step 7 makes RS256 the default),
 *   2. selects the signing key by `kid` (RFC 7515 sec 4.1.4), or the sole RSA key when the
 *      JOSE Header carries no `kid`,
 *   3. checks the signature over the JWS Signing Input (OIDC Core sec 3.1.3.7 step 6,
 *      RFC 7515 sec 5.2) with protocore_rsa_verify(), which is modular exponentiation on
 *      every target and the part's accelerator where there is one,
 *   4. matches `iss` (step 2, RFC 7519 sec 4.1.1) and `aud` in both its string and array
 *      forms (step 3, RFC 7519 sec 4.1.3), and reads `exp` (step 9, RFC 7519 sec 4.1.4) and
 *      `nbf` (RFC 7519 sec 4.1.5) against the caller's clock,
 *   5. reports `sub` (RFC 7519 sec 4.1.2), `email` (OIDC Core sec 5.1) and the times in
 *      ::protocore_oidc_claims.
 *
 * Zero heap: the decode buffers come from the per-dispatch arena and everything else is fixed.
 * The verifier fetches nothing. Retrieving the JWK Set from the OP's `jwks_uri` (OpenID Connect
 * Discovery 1.0 sec 3) over HTTPS and caching it belongs to the caller, which leaves key
 * rotation and TLS trust in the application's hands and this module deterministic.
 *
 * Only RS256 is verified. A MAC-based `alg` (OIDC Core sec 3.1.3.7 step 8) is the JWT module's
 * (services/security/jwt); ES256 is out of scope.
 *
 * The module exports one symbol, @ref Oidc. Everything in oidc.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_OIDC_H
#define PROTOCORE_OIDC_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_OIDC

PROTOCORE_BEGIN_DECLS

/** @brief RSA modulus and signature size in bytes; RFC 7518 sec 3.3 requires at least 2048 bits. */
#define PROTOCORE_OIDC_RSA_BYTES 256

/** @brief Decode cap for the JOSE Header (RFC 7515 sec 4); it carries only `alg`, `typ` and `kid`. */
#define PROTOCORE_OIDC_HDR_LEN 512

/** @brief Decode cap for the `iss` Claim (RFC 7519 sec 4.1.1), compared against the Issuer Identifier. */
#define PROTOCORE_OIDC_ISS_LEN 256

/**
 * @brief Scratch this module borrows at once: JOSE Header + signature + JWS Payload + `iss`.
 *
 * All four are live together across one verify, so the term is their sum.
 */
#define PROTOCORE_PLAINTEXT_WORK_OIDC                                                                                  \
    (PROTOCORE_OIDC_HDR_LEN + PROTOCORE_OIDC_RSA_BYTES + PROTOCORE_OIDC_MAX_LEN + PROTOCORE_OIDC_ISS_LEN)

/** @brief Validation result codes (0 = the ID Token passes every step of OIDC Core sec 3.1.3.7). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_OIDC_OK = 0,             ///< Signature and Claims all pass.
    PROTOCORE_OIDC_ERR_FORMAT = -1,    ///< Not a 3-part Compact Serialization / bad base64url / oversized.
    PROTOCORE_OIDC_ERR_ALG = -2,       ///< JOSE Header `alg` is not RS256 (RFC 7515 sec 4.1.1).
    PROTOCORE_OIDC_ERR_KEY = -3,       ///< No usable RSA JWK (`kid` not found / malformed `n` or `e`).
    PROTOCORE_OIDC_ERR_SIGNATURE = -4, ///< The RSASSA-PKCS1-v1_5 check failed (RFC 7518 sec 3.3).
    PROTOCORE_OIDC_ERR_ISS = -5,       ///< `iss` is not the Issuer Identifier (sec 3.1.3.7 step 2).
    PROTOCORE_OIDC_ERR_AUD = -6,       ///< `aud` does not contain the client_id (sec 3.1.3.7 step 3).
    PROTOCORE_OIDC_ERR_EXPIRED = -7,   ///< `exp` is missing or not after now (sec 3.1.3.7 step 9).
    PROTOCORE_OIDC_ERR_NOT_YET = -8,   ///< `nbf` is in the future (RFC 7519 sec 4.1.5).
} protocore_oidc_result;

/** @brief An RSA public key from one JWK (RFC 7518 sec 6.3.1). */
typedef struct
{
    uint8_t n[PROTOCORE_OIDC_RSA_BYTES]; ///< `n` (Modulus), big-endian, right-aligned (RFC 7518 sec 6.3.1.1).
    uint8_t e[4];                        ///< `e` (Exponent), big-endian, right-aligned (RFC 7518 sec 6.3.1.2).
    proto_bool loaded;                   ///< True once `n` and `e` are populated.
} protocore_oidc_key;
/** @brief The Claims a validated ID Token carries (OIDC Core sec 2). */
typedef struct
{
    char sub[PROTOCORE_OIDC_SUB_LEN];     ///< `sub` (Subject) Claim, RFC 7519 sec 4.1.2.
    char email[PROTOCORE_OIDC_EMAIL_LEN]; ///< `email` Standard Claim (OIDC Core sec 5.1); empty when absent.
    int64_t iat;                          ///< `iat` (Issued At), RFC 7519 sec 4.1.6; 0 when absent. 64-bit: past 2038.
    int64_t exp;                          ///< `exp` (Expiration Time), RFC 7519 sec 4.1.4. 64-bit.
} protocore_oidc_claims;
/** @brief RFC 7517 sec 5: the JWK Set a find scans, and the RSA public key it yields. */
typedef struct
{
    const char *jwks;       ///< the JWK Set document, `{"keys":[ ... ]}` (RFC 7517 sec 5.1)
    const char *kid;        ///< the `kid` a find selects on (RFC 7517 sec 4.5); NULL or "" takes the first RSA JWK,
                            ///< and a full verify sets it from the token's JOSE Header
    protocore_oidc_key rsa; ///< the key: written by a find, read by a verify (RFC 7518 sec 6.3.1)
} OidcKeyArgs;
/** @brief OIDC Core sec 3.1.3.7: what the ID Token's Claims are checked against. */
typedef struct
{
    const char *iss;   ///< the Issuer Identifier `iss` must equal (step 2, RFC 7519 sec 4.1.1); NULL or "" skips it
    const char *aud;   ///< the client_id `aud` must contain (step 3, RFC 7519 sec 4.1.3); NULL or "" skips it
    uint32_t now_unix; ///< the current time `exp` and `nbf` are read against (step 9, RFC 7519 sec 4.1.4 / 4.1.5)
} OidcExpectArgs;
/**
 * @brief The Relying Party verifier: one ID Token, one JWK Set, one verdict.
 *
 * A caller sets the members a call takes, invokes it through ::Oidc, and reads the outcome off
 * the same handle. The validation steps are OIDC Core sec 3.1.3.7's.
 *
 * @var OidcNs::token      the ID Token as a JWS Compact Serialization (RFC 7515 sec 7.1)
 * @var OidcNs::token_len  how many characters of it there are
 * @var OidcNs::key        the JWK Set a find scans and the RSA public key it yields
 * @var OidcNs::expect     the Issuer Identifier, the client_id and the clock a validation uses
 * @var OidcNs::ok         a find's or a header read's true/false outcome
 * @var OidcNs::result     a validation's ::protocore_oidc_result
 * @var OidcNs::text       the `kid` a header read reports, empty when the JOSE Header carries none
 * @var OidcNs::claims     the Claims a validated ID Token carries, cleared before every validation
 * @var OidcNs::token_kid        read `kid` out of the JOSE Header (RFC 7515 sec 4.1.4)
 * @var OidcNs::jwks_find        take the RSA JWK the `kid` names out of the JWK Set (RFC 7517 sec 5.1)
 * @var OidcNs::verify_with_key  validate the ID Token against the key already in @c key.rsa
 * @var OidcNs::verify           the two above in order: resolve the key by the token's `kid`, then validate
 */
typedef struct
{
    const char *token;     ///< the ID Token every call but a find names
    size_t token_len;      ///< how many characters of it there are
    OidcKeyArgs key;       ///< the JWK Set and the key it yields (RFC 7517 sec 5)
    OidcExpectArgs expect; ///< what the Claims are checked against (OIDC Core sec 3.1.3.7)
    proto_bool ok;
    protocore_oidc_result result;
    char text[PROTOCORE_OIDC_KID_LEN];
    protocore_oidc_claims claims;
} OidcVars;

/** @brief The operands and the outcome. */
extern OidcVars OidcV;

/** @brief The entries. */
typedef struct
{
    void (*const token_kid)(uint8_t *restrict work);
    void (*const jwks_find)(uint8_t *restrict work);
    void (*const verify_with_key)(uint8_t *restrict work);
    void (*const verify)(uint8_t *restrict work);
} OidcNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in OidcV or a region of the borrow at a fixed offset.
void protocore_oidc_token_kid(uint8_t *restrict work);
void protocore_oidc_jwks_find(uint8_t *restrict work);
void protocore_oidc_verify_with_key(uint8_t *restrict work);
void protocore_oidc_verify(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Oidc.token_kid(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const OidcNs Oidc __attribute__((unused)) = {
    .token_kid = protocore_oidc_token_kid,
    .jwks_find = protocore_oidc_jwks_find,
    .verify_with_key = protocore_oidc_verify_with_key,
    .verify = protocore_oidc_verify,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_OIDC

#endif // PROTOCORE_OIDC_H
