// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file oidc.h
 * @brief OpenID Connect ID-token verification, RS256 (PC_ENABLE_OIDC).
 *
 * A relying-party verifier for OIDC ID tokens (RFC 7519 JWT, OpenID Connect Core
 * 3.1.3.7). Given an ID token and the issuer's JWKS, it:
 *   1. parses the JWT header and requires `alg` == RS256,
 *   2. selects the signing key by `kid` (or the sole key if the token has none),
 *   3. verifies the RSASSA-PKCS1-v1.5 SHA-256 signature (via pc_rsa_verify -
 *      real modular exponentiation; mbedTLS-accelerated on ESP32),
 *   4. checks `iss`, `aud` (string or array), `exp`, and `nbf` against the
 *      caller's expectations and clock,
 *   5. extracts `sub` / `email` and the times into ::pc_oidc_claims.
 *
 * Zero-heap (fixed stack/BSS buffers) and host-tested against real openssl-signed
 * RS256 vectors. The verifier is pure: it does NOT fetch anything. Fetching the
 * discovery document / JWKS over HTTPS and caching keys is the caller's job (do it
 * off the request hot path with the HTTP client, then pass the JWKS JSON here) -
 * which keeps key rotation, caching policy, and TLS trust in the application's
 * hands and the verifier deterministic.
 *
 * Only RS256 (the OIDC default and by far the most common) is supported; HS256
 * shared-secret tokens are the JWT module's job (services/security/jwt), ES256 is out of
 * scope.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_OIDC_H
#define PROTOCORE_OIDC_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_OIDC

/** @brief RSA-2048 modulus size in bytes (the supported key size). */
#define PC_OIDC_RSA_BYTES 256

/** @brief Decode cap for the JOSE header segment; it carries only `alg`/`typ`/`kid`. */
#define PC_OIDC_HDR_LEN 512

/** @brief Decode cap for the `iss` claim, compared against the expected issuer URL. */
#define PC_OIDC_ISS_LEN 256

/**
 * @brief Scratch this module borrows at once (header + signature + payload + issuer).
 *
 * All four buffers are live together across the verify, so the term is their sum. Stated here,
 * next to the constants it is built from, because a worst case assembled anywhere else would have
 * to restate them. server/mmgr/scratch_budget.h collects this with every other borrower's term.
 */
#define PC_PLAINTEXT_WORK_OIDC (PC_OIDC_HDR_LEN + PC_OIDC_RSA_BYTES + PC_OIDC_MAX_LEN + PC_OIDC_ISS_LEN)

/** @brief Verification result codes (0 = success, negatives = failure reasons). */
enum class pc_oidc_result : int32_t
{
    PC_OIDC_OK = 0,             ///< Token verified and all claims pass.
    PC_OIDC_ERR_FORMAT = -1,    ///< Not a 3-part JWT / bad base64 / oversized.
    PC_OIDC_ERR_ALG = -2,       ///< Header `alg` is not RS256.
    PC_OIDC_ERR_KEY = -3,       ///< No usable RSA key (kid not found / malformed JWK).
    PC_OIDC_ERR_SIGNATURE = -4, ///< RSA signature verification failed.
    PC_OIDC_ERR_ISS = -5,       ///< `iss` does not match the expected issuer.
    PC_OIDC_ERR_AUD = -6,       ///< `aud` does not contain the expected audience.
    PC_OIDC_ERR_EXPIRED = -7,   ///< `exp` is missing or in the past.
    PC_OIDC_ERR_NOT_YET = -8,   ///< `nbf` is in the future.
};

/** @brief A parsed RSA public key (from a JWKS entry). */
struct pc_oidc_key
{
    uint8_t n[PC_OIDC_RSA_BYTES]; ///< Modulus, big-endian, right-aligned.
    uint8_t e[4];                 ///< Public exponent, big-endian (4 bytes).
    bool loaded;                  ///< True once n/e are populated.
};

/** @brief Claims extracted from a verified token. */
struct pc_oidc_claims
{
    char sub[PC_OIDC_SUB_LEN];     ///< Subject identifier.
    char email[PC_OIDC_EMAIL_LEN]; ///< Email (empty if the claim is absent).
    int64_t iat;                   ///< Issued-at (0 if absent). 64-bit: epoch seconds outlive 2038.
    int64_t exp;                   ///< Expiry (epoch seconds). 64-bit.
};

/**
 * @brief Read the `kid` from a token's JWT header.
 * @return true if a `kid` string is present (copied, null-terminated, into @p out).
 */
bool pc_oidc_token_kid(const char *token, size_t token_len, char *kid_out, size_t kid_cap);

/**
 * @brief Extract an RSA JWK by @p kid from a JWKS JSON document.
 *
 * @param jwks_json  the JWKS document (`{"keys":[ {RSA JWK}, ... ]}`).
 * @param kid        key id to match; nullptr / "" selects the first RSA key.
 * @param key        receives n/e on success.
 * @return true if a matching RSA key was found and parsed.
 */
bool pc_oidc_jwks_find(const char *jwks_json, const char *kid, pc_oidc_key *key);

/**
 * @brief Verify an ID token against an already-resolved key.
 *
 * @param token,token_len   the compact ID token.
 * @param key               the issuer's RSA public key.
 * @param expected_iss      required `iss` value (exact match).
 * @param expected_aud      required `aud` value (string match, or membership of
 *                          the `aud` array).
 * @param now_unix          current time (epoch seconds) for exp/nbf checks.
 * @param claims            receives extracted claims on success (may be nullptr).
 * @return ::PC_OIDC_OK or a negative ::pc_oidc_result.
 */
pc_oidc_result pc_oidc_verify_with_key(const char *token, size_t token_len, const pc_oidc_key *key,
                                       const char *expected_iss, const char *expected_aud, uint32_t now_unix,
                                       pc_oidc_claims *claims);

/**
 * @brief Verify an ID token, resolving the key from @p jwks_json by the token's kid.
 *
 * Convenience wrapper over pc_oidc_jwks_find() + pc_oidc_verify_with_key().
 * @return ::PC_OIDC_OK or a negative ::pc_oidc_result (ERR_KEY if no key matches).
 */
pc_oidc_result pc_oidc_verify(const char *token, size_t token_len, const char *jwks_json, const char *expected_iss,
                              const char *expected_aud, uint32_t now_unix, pc_oidc_claims *claims);

#endif // PC_ENABLE_OIDC
#endif // PROTOCORE_OIDC_H
