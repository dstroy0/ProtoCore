// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file jwt.h
 * @brief Zero-heap JWT (JSON Web Token) bearer-auth verification, HS256.
 *
 * Stateless request authentication: a client presents `Authorization: Bearer
 * <jwt>` and the server verifies the token's HMAC-SHA-256 signature against a
 * shared secret (reusing the SSH crypto layer's HMAC). No sessions, no per-client
 * state, no heap - all work happens in fixed stack/BSS buffers and the whole core
 * is host-testable (env:native_jwt).
 *
 * Only HS256 (HMAC-SHA-256) is supported - the deterministic, allocation-free
 * choice for a constrained device sharing a secret with its issuer. RS256/ES256
 * (asymmetric) are out of scope. Signature verification is constant-time.
 *
 * The base verifier (pc_jwt_verify_hs256 / pc_jwt_bearer_valid) checks only the
 * signature. The `*_at` variants additionally enforce the RFC 7519 time claims
 * (`exp` §4.1.4 and `nbf` §4.1.5) against a caller-supplied wall-clock epoch, with a
 * skew leeway - pass now=0 on a clockless device to skip the time checks (the
 * signature still gates). `iat` (§4.1.6) is informational; read it with
 * pc_jwt_claim_int() if you want it.
 */

#ifndef PROTOCORE_JWT_H
#define PROTOCORE_JWT_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_JWT

/**
 * @brief Verify the HS256 signature of a JWT.
 *
 * Checks that the token has exactly the `header.payload.signature` shape and that
 * base64url(HMAC-SHA-256(secret, "header.payload")) equals the signature segment
 * (constant-time). Does not inspect claims.
 *
 * @param token       the compact JWT string.
 * @param token_len   length of @p token (e.g. strlen).
 * @param secret      HMAC key bytes.
 * @param secret_len  key length.
 * @return true if the signature is valid.
 */
proto_bool pc_jwt_verify_hs256(const char *token, size_t token_len, const uint8_t *secret, size_t secret_len);

/**
 * @brief Validate an `Authorization` header value carrying a Bearer JWT.
 *
 * Accepts a value beginning with `Bearer ` (case-insensitive scheme), then
 * verifies the token via pc_jwt_verify_hs256().
 *
 * @param auth_header the full Authorization header value (may be nullptr).
 * @param secret      HMAC key bytes.
 * @param secret_len  key length.
 * @return true if a well-formed Bearer token validates.
 */
proto_bool pc_jwt_bearer_valid(const char *auth_header, const uint8_t *secret, size_t secret_len);

/**
 * @brief Check a JWT's time-based validity (RFC 7519 `exp` / `nbf`) against a clock.
 *
 * Enforces `exp` (§4.1.4: rejected once @p now_epoch has passed it) and `nbf`
 * (§4.1.5: rejected before it), each within @p leeway_s seconds of skew tolerance.
 * An absent claim is not enforced. Does NOT check the signature - pair with
 * pc_jwt_verify_hs256(). The comparisons are written to avoid integer overflow near the
 * `long` epoch range.
 *
 * @param now_epoch  current Unix time in seconds; <= 0 means "no wall clock", so the
 *                   time claims cannot be evaluated and the function returns true (the
 *                   signature check remains the gate).
 * @param leeway_s   allowed clock skew in seconds (0 for none).
 * @return true if the token is currently within its validity window (or no clock is set).
 */
proto_bool pc_jwt_time_valid(const char *token, size_t token_len, long now_epoch, long leeway_s);

/**
 * @brief Verify a JWT's HS256 signature AND its `exp` / `nbf` time claims.
 *
 * pc_jwt_verify_hs256() && pc_jwt_time_valid(). On a clockless device (@p now_epoch <= 0)
 * this reduces to the signature-only check.
 */
proto_bool pc_jwt_verify_hs256_at(const char *token, size_t token_len, const uint8_t *secret, size_t secret_len,
                                  long now_epoch, long leeway_s);

/**
 * @brief Validate a Bearer `Authorization` header, enforcing `exp` / `nbf` when clocked.
 *
 * pc_jwt_bearer_valid() plus the RFC 7519 time-claim check. Pass @p now_epoch from your
 * time source (e.g. `(long)pc_time_now()`); 0 skips the time checks.
 */
proto_bool pc_jwt_bearer_valid_at(const char *auth_header, const uint8_t *secret, size_t secret_len, long now_epoch,
                                  long leeway_s);

/**
 * @brief Read an integer claim (e.g. "exp", "iat", "nbf") from a JWT payload.
 *
 * base64url-decodes the payload segment and scans the JSON for a top-level
 * numeric member @p name. Does not verify the signature - call
 * pc_jwt_verify_hs256() first.
 *
 * @param token      the compact JWT string.
 * @param token_len  length of @p token.
 * @param name       claim name (without quotes).
 * @param out        receives the parsed value on success.
 * @return true if the claim is present and parses as an integer.
 */
proto_bool pc_jwt_claim_int(const char *token, size_t token_len, const char *name, long *out);

/**
 * @brief Read a string claim (e.g. "sub", "role", "scope") from a JWT payload.
 *
 * base64url-decodes the payload and copies the top-level string member @p name
 * into @p out (null-terminated, bounded by @p out_cap). Does not verify the
 * signature - call pc_jwt_verify_hs256() first. Minimal unescaping (handles `\"` and
 * `\\`; other escapes are copied without their backslash), which suits
 * scope / role / sub values.
 *
 * @return true if the claim is present and is a string that fit in @p out.
 */
proto_bool pc_jwt_claim_str(const char *token, size_t token_len, const char *name, char *out, size_t out_cap);

/**
 * @brief Test whether a space-separated OAuth2 scope claim grants @p required.
 *
 * @param scope_claim the `scope` claim value (space-delimited scopes, RFC 6749 3.3).
 * @param required    the scope to look for.
 * @return true if @p required is one of the whole space-separated tokens.
 */
proto_bool pc_jwt_scope_allows(const char *scope_claim, const char *required);

#endif // PC_ENABLE_JWT

PROTO_END_DECLS

#endif // PROTOCORE_JWT_H
