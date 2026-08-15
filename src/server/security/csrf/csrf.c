// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file csrf.c
 * @brief Stateless HMAC-signed CSRF token implementation (PROTOCORE_ENABLE_CSRF).
 *
 * The token is `<nonce_hex>.<sig_hex>`; the signature is the first CSRF_SIG_BYTES
 * of HMAC-SHA256(secret, nonce). Verification recomputes the HMAC over the
 * embedded nonce and constant-time compares - no server-side session state.
 */

#include "csrf.h"
#include "mmgr/protoframe.h" // the one frame engine
#include "mmgr/protomem.h"
#include "mmgr/protostr.h" // str: the bounded-run walks

#if PROTOCORE_ENABLE_CSRF

#include "crypto/ct_eq.h" // protocore_ct_eq
#include "crypto/mac/hmac_sha256.h"
#include "mmgr/secure.h" // the token MAC's working set, wiped on release
#include "shared/hex/hex.h"

// nonce-hex "." signature-hex
static const protocore_field CSRF_TOKEN[] = {
    PROTOCORE_STR, {PROTOCORE_FK_LIT, 0, 1, "."}, PROTOCORE_STR, PROTOCORE_END};

// All CSRF state, owned by one instance (internal linkage): the HMAC secret and the
// monotonic nonce counter, grouped so it is one named owner, unreachable cross-TU.
typedef struct
{
    uint8_t secret[32];
    size_t secret_len;
    uint64_t counter;
} CsrfCtx;
static CsrfCtx s_csrf = {{0}, 0, 0};

// Lowercase hex of @p n bytes at @p in, plus a NUL, into @p out.
static void hex_of(const uint8_t *in, uint32_t n, char *out)
{
    Hex.args.upper = PROTO_FALSE;
    Hex.io.in = in;
    Hex.io.n = n;
    Hex.io.out = out;
    Hex.encode(Hex.internal);
}

// Hex of the truncated HMAC-SHA256(secret, nonce) into sig_hex (2*CSRF_SIG_BYTES + 1).
static void sign_nonce(uint8_t *work, const CsrfCtx *c, const uint8_t *nonce, size_t nlen, char *sig_hex)
{
    uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
    protocore_hmac_sha256(work, c->secret, c->secret_len, nonce, nlen, mac);
    hex_of(mac, CSRF_SIG_BYTES, sig_hex); // truncate the MAC to CSRF_SIG_BYTES
}

void protocore_csrf_set_secret(const uint8_t *secret, size_t len)
{
    if (!secret)
    {
        s_csrf.secret_len = 0;
        return;
    }
    s_csrf.secret_len = len > sizeof(s_csrf.secret) ? sizeof(s_csrf.secret) : len;
    mem.cpy(s_csrf.secret, secret, s_csrf.secret_len);
}

int protocore_csrf_issue(char *out, size_t cap)
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
    hex_of(nonce, CSRF_NONCE_BYTES, nhex);
    // One borrow for this token's MAC, returned before the frame is built.
    size_t mark = protocore_secure_mark();
    protocore_span ws = protocore_secure_span(PROTOCORE_HMAC_SHA256_BORROW, _Alignof(uint32_t));
    if (!span.ok(ws))
    {
        protocore_secure_release(mark);
        return 0;
    }
    sign_nonce(ws.buf, &s_csrf, nonce, CSRF_NONCE_BYTES, shex);
    protocore_secure_release(mark);

    // The frame's contract is this function's contract: the length written, or 0 and out emptied.
    return frame.build(out, cap, CSRF_TOKEN, (const protocore_fval[]){PROTOCORE_VSTR(nhex), PROTOCORE_VSTR(shex)}, 2);
}

proto_bool protocore_csrf_verify(const char *token)
{
    if (s_csrf.secret_len == 0 || !token)
    {
        return PROTO_FALSE;
    }

    const char *dot = str.find(token, str.len(token, 0xFFFF) + 1u, ".", sizeof("."), PROTO_FALSE);
    if (!dot)
    {
        return PROTO_FALSE;
    }

    size_t nhexlen = (size_t)(dot - token);
    if (nhexlen != CSRF_NONCE_BYTES * 2)
    {
        return PROTO_FALSE;
    }

    uint8_t nonce[CSRF_NONCE_BYTES];
    Hex.io.text = token;
    Hex.io.n = (uint32_t)nhexlen;
    Hex.io.bytes = nonce;
    Hex.io.cap = (uint32_t)sizeof(nonce);
    Hex.decode(Hex.internal);
    if (Hex.i32 != CSRF_NONCE_BYTES)
    {
        return PROTO_FALSE;
    }

    const char *sig = dot + 1;
    if (str.len(sig, CSRF_SIG_BYTES * 2 + 1) != CSRF_SIG_BYTES * 2)
    {
        return PROTO_FALSE;
    }

    char expect[CSRF_SIG_BYTES * 2 + 1];
    // One borrow for the MAC this compare rebuilds, returned before the answer.
    size_t mark = protocore_secure_mark();
    protocore_span ws = protocore_secure_span(PROTOCORE_HMAC_SHA256_BORROW, _Alignof(uint32_t));
    if (!span.ok(ws))
    {
        protocore_secure_release(mark);
        return PROTO_FALSE;
    }
    sign_nonce(ws.buf, &s_csrf, nonce, CSRF_NONCE_BYTES, expect);
    protocore_secure_release(mark);
    return protocore_ct_eq(sig, expect, CSRF_SIG_BYTES * 2);
}

void protocore_csrf_reset(void)
{
    mem.set(s_csrf.secret, 0, sizeof(s_csrf.secret));
    s_csrf.secret_len = 0;
    s_csrf.counter = 0;
}

#endif // PROTOCORE_ENABLE_CSRF
