// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file csrf.cpp
 * @brief Stateless HMAC-signed CSRF token implementation (PC_ENABLE_CSRF).
 *
 * The token is `<nonce_hex>.<sig_hex>`; the signature is the first CSRF_SIG_BYTES
 * of HMAC-SHA256(secret, nonce). Verification recomputes the HMAC over the
 * embedded nonce and constant-time compares - no server-side session state.
 */

#include "csrf.h"
#include "shared_primitives/frame.h" // the one frame engine

#if PC_ENABLE_CSRF

#include "crypto/mac/hmac_sha256.h"
#include "shared_primitives/hex.h"
#include <stdio.h>
#include <string.h>

// nonce-hex "." signature-hex
static const pc_field CSRF_TOKEN[] = {PC_STR, {PC_FK_LIT, 0, 1, "."}, PC_STR, PC_END};

namespace
{

// All CSRF state, owned by one instance (internal linkage): the HMAC secret and the
// monotonic nonce counter, grouped so it is one named owner, unreachable cross-TU.
struct CsrfCtx
{
    uint8_t secret[32] = {};
    size_t secret_len = 0;
    uint64_t counter = 0;
};
CsrfCtx s_csrf;

// hex encode/decode now via the shared hex.h primitive.

// Constant-time compare of n characters (no early exit on mismatch).
bool ct_equal(const char *a, const char *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++)
    {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

// Hex of the truncated HMAC-SHA256(secret, nonce) into sig_hex (2*CSRF_SIG_BYTES + 1).
void sign_nonce(const CsrfCtx &c, const uint8_t *nonce, size_t nlen, char *sig_hex)
{
    uint8_t mac[PC_HMAC_SHA256_LEN];
    pc_hmac_sha256(c.secret, c.secret_len, nonce, nlen, mac);
    pc_hex_encode(mac, CSRF_SIG_BYTES, sig_hex); // truncate the MAC to CSRF_SIG_BYTES
}

} // namespace

void pc_csrf_set_secret(const uint8_t *secret, size_t len)
{
    if (!secret)
    {
        s_csrf.secret_len = 0;
        return;
    }
    s_csrf.secret_len = len > sizeof(s_csrf.secret) ? sizeof(s_csrf.secret) : len;
    memcpy(s_csrf.secret, secret, s_csrf.secret_len);
}

int pc_csrf_issue(char *out, size_t cap)
{
    if (s_csrf.secret_len == 0 || !out || cap < CSRF_TOKEN_BUF)
    {
        return 0;
    }

    uint8_t nonce[CSRF_NONCE_BYTES];
    uint64_t c = ++s_csrf.counter;
    for (size_t i = 0; i < CSRF_NONCE_BYTES; i++)
    {
        nonce[i] = (uint8_t)(c >> (8 * i));
    }

    char nhex[CSRF_NONCE_BYTES * 2 + 1];
    char shex[CSRF_SIG_BYTES * 2 + 1];
    pc_hex_encode(nonce, CSRF_NONCE_BYTES, nhex);
    sign_nonce(s_csrf, nonce, CSRF_NONCE_BYTES, shex);

    // The frame's contract is this function's contract: the length written, or 0 and out emptied.
    return pc_frame_build(out, cap, CSRF_TOKEN, nhex, shex);
}

bool pc_csrf_verify(const char *token)
{
    if (s_csrf.secret_len == 0 || !token)
    {
        return false;
    }

    const char *dot = strchr(token, '.');
    if (!dot)
    {
        return false;
    }

    size_t nhexlen = (size_t)(dot - token);
    if (nhexlen != CSRF_NONCE_BYTES * 2)
    {
        return false;
    }

    uint8_t nonce[CSRF_NONCE_BYTES];
    if (pc_hex_decode(token, nhexlen, nonce, sizeof(nonce)) != CSRF_NONCE_BYTES)
    {
        return false;
    }

    const char *sig = dot + 1;
    if (strnlen(sig, CSRF_SIG_BYTES * 2 + 1) != CSRF_SIG_BYTES * 2)
    {
        return false;
    }

    char expect[CSRF_SIG_BYTES * 2 + 1];
    sign_nonce(s_csrf, nonce, CSRF_NONCE_BYTES, expect);
    return ct_equal(sig, expect, CSRF_SIG_BYTES * 2);
}

void pc_csrf_reset(void)
{
    memset(s_csrf.secret, 0, sizeof(s_csrf.secret));
    s_csrf.secret_len = 0;
    s_csrf.counter = 0;
}

#endif // PC_ENABLE_CSRF
