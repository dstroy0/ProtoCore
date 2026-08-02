// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file csrf.h
 * @brief Stateless HMAC-signed CSRF token (PC_ENABLE_CSRF).
 *
 * A CSRF token is `<nonce_hex>.<sig_hex>` where sig = the first CSRF_SIG_BYTES of
 * HMAC-SHA256(secret, nonce). The secret is seeded once from the hardware RNG at
 * begin(); the nonce is a per-issue counter (it need not be secret - the security
 * is the HMAC). Verification recomputes the HMAC over the embedded nonce and
 * constant-time compares the signature, so no server-side session state is kept.
 *
 * The token is sized to fit a single MAX_VAL_LEN header value and a `csrf=`
 * cookie. These functions are pure (no Arduino dependency) so they unit-test on
 * the host (with PC_ENABLE_CSRF set) using a fixed secret.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CSRF_H
#define PROTOCORE_CSRF_H

#include "protocore_config.h"

#if PC_ENABLE_CSRF

/** @brief Nonce length in bytes (hex-encoded in the token). */
#define CSRF_NONCE_BYTES 6
/** @brief Signature length in bytes (truncated HMAC, hex-encoded in the token). */
#define CSRF_SIG_BYTES 14
/** @brief Buffer size for a token string: 2*nonce + '.' + 2*sig + NUL = 42, rounded up. */
#define CSRF_TOKEN_BUF 48

/**
 * @brief Set the HMAC secret (call once at begin() with hardware-random bytes).
 *
 * @param secret  Secret key bytes.
 * @param len     Length in bytes; capped at 32 (longer keys are truncated).
 */
void pc_csrf_set_secret(const uint8_t *secret, size_t len);

/**
 * @brief Issue a fresh signed token into @p out.
 *
 * @param out  Destination buffer (>= CSRF_TOKEN_BUF).
 * @param cap  Size of @p out.
 * @return Token length in characters, or 0 if no secret is set or @p cap is too small.
 */
int pc_csrf_issue(char *out, size_t cap);

/**
 * @brief Verify a token's signature against the current secret.
 *
 * @return true if @p token is well-formed and its HMAC signature is valid;
 *         false otherwise (or if no secret is set).
 */
proto_bool pc_csrf_verify(const char *token);

/** @brief Clear the secret and nonce counter (e.g. between tests). */
void pc_csrf_reset(void);

#endif // PC_ENABLE_CSRF

#endif // PROTOCORE_CSRF_H
