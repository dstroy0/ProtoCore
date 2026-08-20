// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file csrf.c
 * @brief Stateless HMAC-signed CSRF token - implementation. See csrf.h.
 *
 * The context is this file's. The module's own borrow is split by offset into the secret and the
 * nonce counter, then the region the nested HMAC-SHA256 runs out of. The secret is key material the
 * caller keeps for the life of the program, so those bytes are the span it took once rather than
 * storage declared here.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t hex_work[16]; // the borrow an entry takes; Hex never reads it

#if PROTOCORE_ENABLE_CSRF

#include "crypto/ct_eq.h" // protocore_ct_eq: the constant-time signature compare
#include "crypto/mac/hmac_sha256/hmac_sha256.h"
#include "mmgr/protoframe/protoframe.h" // the one frame engine
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h" // str: the bounded-run walks
#include "mmgr/secure/secure.h"     // the persistent end the secret is taken from
#include "server/security/csrf/csrf.h"
#include "shared/hex/hex.h"

PROTOCORE_BEGIN_DECLS

// nonce-hex "." signature-hex
static const protocore_field CSRF_TOKEN[] = {
    PROTOCORE_STR, {PROTOCORE_FK_LIT, 0, 1, "."}, PROTOCORE_STR, PROTOCORE_END};

// The one definition, private to this TU. It sits at CSRF_OFF_CTX in the caller's borrow, so its
// size never leaves this file and no consumer can name it.
//
// Only what is not derivable: the secret, its length, and the monotonic nonce counter. The region
// the nested HMAC runs out of lives at a fixed offset, so a macro computes it from the pointer.
typedef struct
{
    uint8_t secret[32];
    size_t secret_len;
    uint64_t counter;
} CsrfCtx;

// The caller's borrow, split: the context, then the region the nested HMAC-SHA256 runs out of. That
// one is driven through its own namespace, so this borrow carries a region for it rather than
// naming any term of it.
#define CSRF_OFF_CTX 0u
#define CSRF_OFF_MAC (CSRF_OFF_CTX + sizeof(CsrfCtx))
static_assert(CSRF_OFF_MAC + PROTOCORE_HMAC_SHA256_BORROW <= PROTOCORE_CSRF_BORROW,
              "PROTOCORE_CSRF_BORROW is short of the secret, the counter and the nested HMAC borrow - raise "
              "it in protocore_config.h, which derives PROTOCORE_SECURE_ARENA_SIZE from it");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(CSRF_OFF_CTX % _Alignof(CsrfCtx) == 0,
              "CSRF_OFF_CTX is not a multiple of alignof(CsrfCtx) - CSRF_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define CSRF_CTX(w) ((CsrfCtx *)(void *)((w) + CSRF_OFF_CTX))
#define CSRF_MAC(w) ((w) + CSRF_OFF_MAC)

// Lowercase hex of @p n bytes at @p in, plus a NUL, into @p out.
static void hex_of(const uint8_t *in, uint32_t n, char *out)
{
    Hex.args.upper = PROTO_FALSE;
    Hex.io.in = in;
    Hex.io.n = n;
    Hex.io.out = out;
    Hex.encode(hex_work);
}

// Hex of the truncated HMAC-SHA256(secret, nonce) into sig_hex (2*CSRF_SIG_BYTES + 1).
static void sign_nonce(uint8_t *restrict work, const uint8_t *nonce, size_t nlen, char *sig_hex)
{
    const CsrfCtx *c = CSRF_CTX(work);
    uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
    HmacSha256V.mac_args.key = c->secret;
    HmacSha256V.mac_args.key_len = c->secret_len;
    HmacSha256V.mac_args.data = nonce;
    HmacSha256V.mac_args.len = nlen;
    HmacSha256V.mac_args.out = mac;
    HmacSha256.mac(CSRF_MAC(work));
    hex_of(mac, CSRF_SIG_BYTES, sig_hex); // truncate the MAC to CSRF_SIG_BYTES
}

// --- the program's shared issuer, beside the namespace not on it ------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for itself.
// A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_CSRF_BORROW persistent bytes
} CsrfOwnCtx;
static CsrfOwnCtx s_csrf;

// Not an entry: an entry takes a borrow and this is where that borrow comes from. Stated flat, as
// bn_expmod_group14 is stated beside BignumNs, so the namespace keeps the one shape every module has
// - args in, ok out, every entry over the caller's bytes - and storage stays off it.
uint8_t *protocore_csrf_span(void)
{
    if (s_csrf.span == NULL)
    {
        s_csrf.span = protocore_secure_persist_span(PROTOCORE_CSRF_BORROW).buf;
    }
    return s_csrf.span;
}

// --- the entries -----------------------------------------------------------

void protocore_csrf_set_secret(uint8_t *restrict work)
{
    CsrfV.ok = PROTO_FALSE;
    CsrfCtx *c = CSRF_CTX(work);
    const uint8_t *secret = CsrfV.secret_args.secret;
    if (!secret)
    {
        c->secret_len = 0;
        CsrfV.ok = PROTO_TRUE;
        return;
    }
    const size_t len = CsrfV.secret_args.len;
    c->secret_len = len > sizeof(c->secret) ? sizeof(c->secret) : len;
    mem.cpy(c->secret, secret, c->secret_len);
    CsrfV.ok = PROTO_TRUE;
}

void protocore_csrf_issue(uint8_t *restrict work)
{
    CsrfV.ok = PROTO_FALSE;
    CsrfV.n = 0;
    if (!CsrfV.issue_args.out || CsrfV.issue_args.cap < CSRF_TOKEN_BUF)
    {
        return;
    }
    CsrfCtx *c = CSRF_CTX(work);
    if (c->secret_len == 0)
    {
        return;
    }

    uint8_t nonce[CSRF_NONCE_BYTES];
    uint64_t n = ++c->counter;
    for (size_t i = 0; i < CSRF_NONCE_BYTES; i++)
    {
        nonce[i] = (uint8_t)(n >> (8 * i));
    }

    char nhex[CSRF_NONCE_BYTES * 2 + 1];
    char shex[CSRF_SIG_BYTES * 2 + 1];
    hex_of(nonce, CSRF_NONCE_BYTES, nhex);
    sign_nonce(work, nonce, CSRF_NONCE_BYTES, shex);

    // The frame's contract is this entry's contract: the length written, or 0 and out emptied.
    CsrfV.n = frame.build(CsrfV.issue_args.out, CsrfV.issue_args.cap, CSRF_TOKEN,
                          (const protocore_fval[]){PROTOCORE_VSTR(nhex), PROTOCORE_VSTR(shex)}, 2);
    CsrfV.ok = (CsrfV.n > 0);
}

void protocore_csrf_verify(uint8_t *restrict work)
{
    CsrfV.ok = PROTO_FALSE;
    CsrfV.valid = PROTO_FALSE;
    const char *token = CsrfV.verify_args.token;
    if (!token)
    {
        return;
    }
    CsrfCtx *c = CSRF_CTX(work);
    if (c->secret_len == 0)
    {
        return;
    }

    const char *dot = str.find(token, str.len(token, 0xFFFF) + 1u, ".", sizeof("."), PROTO_FALSE);
    if (!dot)
    {
        return;
    }

    size_t nhexlen = (size_t)(dot - token);
    if (nhexlen != CSRF_NONCE_BYTES * 2)
    {
        return;
    }

    uint8_t nonce[CSRF_NONCE_BYTES];
    Hex.io.text = token;
    Hex.io.n = (uint32_t)nhexlen;
    Hex.io.bytes = nonce;
    Hex.io.cap = (uint32_t)sizeof(nonce);
    Hex.decode(hex_work);
    if (Hex.i32 != CSRF_NONCE_BYTES)
    {
        return;
    }

    const char *sig = dot + 1;
    if (str.len(sig, CSRF_SIG_BYTES * 2 + 1) != CSRF_SIG_BYTES * 2)
    {
        return;
    }

    char expect[CSRF_SIG_BYTES * 2 + 1];
    sign_nonce(work, nonce, CSRF_NONCE_BYTES, expect);
    CsrfV.valid = protocore_ct_eq(sig, expect, CSRF_SIG_BYTES * 2);
    CsrfV.ok = PROTO_TRUE;
}

void protocore_csrf_reset(uint8_t *restrict work)
{
    CsrfV.ok = PROTO_FALSE;
    CsrfCtx *c = CSRF_CTX(work);
    mem.set(c->secret, 0, sizeof(c->secret));
    c->secret_len = 0;
    c->counter = 0;
    CsrfV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
CsrfVars CsrfV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CSRF
