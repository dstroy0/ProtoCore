// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file oidc.c
 * @brief OpenID Connect ID Token validation, RS256 - implementation.
 *
 * The shared base64url decoder (Base64.url_decode, RFC 7515 sec 2 encoding), bounded JSON member
 * scanners over the decoded JOSE Header and JWS Payload, and the RSASSA-PKCS1-v1_5 SHA-256 check
 * (RFC 7518 sec 3.3) delegated to protocore_rsa_verify(). Claims are read only after the signature
 * verifies, which is the order OIDC Core sec 3.1.3.7 puts steps 6 and 9 in.
 */

#include "services/security/oidc/oidc.h"
#include "mmgr/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_OIDC

#include "crypto/asymmetric/rsa.h"
#include "mmgr/plaintext.h" // per-dispatch arena (keeps the decode buffers off the worker stack)
#include "mmgr/protostr.h"  // str.len / eq: the bounded walks, in place of <string.h>
#include "mmgr/secure.h"    // the signature digest's working set, wiped on release
#include "network_drivers/presentation/codec/base64/base64.h" // shared Base64.url_decode

// The three parts of a JWS Compact Serialization (RFC 7515 sec 7.1).
#define OIDC_SEG_HEADER 0u    ///< BASE64URL(UTF8(JWS Protected Header))
#define OIDC_SEG_PAYLOAD 1u   ///< BASE64URL(JWS Payload)
#define OIDC_SEG_SIGNATURE 2u ///< BASE64URL(JWS Signature)

/**
 * @brief The verifier's calls - what OidcNs points at.
 *
 * No storage member: every buffer a validation uses is either the caller's handle, a bounded local,
 * or a borrow from the per-dispatch arena returned before the call ends, so the module keeps
 * nothing between calls.
 *
 * @var OidcInternal::ns  the handle a caller sets a call's members on
 */
struct OidcInternal
{
    OidcNs *ns;
};

static struct OidcInternal s_oidc = {.ns = &Oidc};

// Bounded substring search of [hs, he) for the NUL-terminated needle.
static const char *mem_find(const char *hs, const char *he, const char *needle)
{
    size_t nl = str.len(needle, (size_t)(he - hs) + 1u);
    if (nl == 0 || (size_t)(he - hs) < nl)
    {
        return NULL;
    }
    for (const char *p = hs; p + nl <= he; p++)
    {
        if (mem.cmp(p, needle, nl) == 0)
        {
            return p;
        }
    }
    return NULL;
}

// Locate the JSON member @p name within [s, e). On success sets *vstart/*vlen to the value extent
// and *type to 's' (string, between the quotes), 'n' (number, including a leading '-'), or 'a'
// (array, including the brackets).
static proto_bool find_field(const char *s, const char *e, const char *name, const char **vstart, size_t *vlen,
                             char *type)
{
    // Set before the first guard, so every failure path leaves the out-params defined.
    *vstart = NULL;
    *vlen = 0;
    *type = '\0';
    char needle[96];
    protocore_sb sb_needle = {needle, sizeof(needle), 0, PROTO_TRUE};
    Sb.put(&sb_needle, "\"");
    Sb.put(&sb_needle, name);
    Sb.put(&sb_needle, "\"");
    int nn = (int)Sb.finish(&sb_needle);
    if (nn <= 0 || nn >= (int)sizeof(needle))
    {
        return PROTO_FALSE;
    }
    const char *p = mem_find(s, e, needle);
    if (!p)
    {
        return PROTO_FALSE;
    }
    p += nn;
    while (p < e && (*p == ' ' || *p == '\t' || *p == ':' || *p == '\n' || *p == '\r'))
    {
        p++;
    }
    if (p >= e)
    {
        return PROTO_FALSE;
    }

    if (*p == '"')
    {
        const char *q = ++p;
        while (q < e && *q != '"')
        {
            if (*q == '\\' && q + 1 < e)
            {
                q++;
            }
            q++;
        }
        if (q >= e)
        {
            return PROTO_FALSE;
        }
        *vstart = p;
        *vlen = (size_t)(q - p);
        *type = 's';
        return PROTO_TRUE;
    }
    if (*p == '[')
    {
        const char *q = p;
        while (q < e && *q != ']')
        {
            q++;
        }
        if (q >= e)
        {
            return PROTO_FALSE;
        }
        *vstart = p;
        *vlen = (size_t)(q - p + 1);
        *type = 'a';
        return PROTO_TRUE;
    }
    if (*p == '-' || (*p >= '0' && *p <= '9'))
    {
        const char *q = p;
        if (*q == '-')
        {
            q++;
        }
        while (q < e && *q >= '0' && *q <= '9')
        {
            q++;
        }
        *vstart = p;
        *vlen = (size_t)(q - p);
        *type = 'n';
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// Copy a string member into @p out, taking the char after a backslash literally. False if absent,
// not a string, or too long for @p cap.
static proto_bool get_str(const char *s, const char *e, const char *name, char *out, size_t cap)
{
    const char *v = NULL;
    size_t vl = 0;
    char t = '\0';
    if (!find_field(s, e, name, &v, &vl, &t) || t != 's')
    {
        return PROTO_FALSE;
    }
    // A while loop, so an escape consumes its second char without mutating a for-loop counter.
    size_t o = 0;
    size_t i = 0;
    while (i < vl)
    {
        char ch = v[i];
        if (ch == '\\' && i + 1 < vl)
        {
            ch = v[++i];
        }
        if (o + 1 >= cap)
        {
            return PROTO_FALSE;
        }
        out[o++] = ch;
        i++;
    }
    out[o] = '\0';
    return PROTO_TRUE;
}

// Read a numeric member as 64-bit: epoch seconds (RFC 7519 sec 2, NumericDate) exceed 32-bit. False
// if absent, not a number, or past the signed 64-bit range.
static proto_bool get_int64(const char *s, const char *e, const char *name, int64_t *out)
{
    const char *v = NULL;
    size_t vl = 0;
    char t = '\0';
    if (!find_field(s, e, name, &v, &vl, &t) || t != 'n' || vl == 0)
    {
        return PROTO_FALSE;
    }
    proto_bool neg = (*v == '-');
    size_t i = neg ? 1 : 0;
    // Accumulate unsigned, refuse a digit run past the signed range, apply the sign after.
    uint64_t val = 0U;
    for (; i < vl; i++)
    {
        if (val > (uint64_t)INT64_MAX / 10U)
        {
            return PROTO_FALSE;
        }
        val = val * 10U + (uint64_t)(v[i] - '0');
        if (val > (uint64_t)INT64_MAX)
        {
            return PROTO_FALSE;
        }
    }
    *out = neg ? -(int64_t)val : (int64_t)val;
    return PROTO_TRUE;
}

// OIDC Core sec 3.1.3.7 step 3: true when the `aud` Claim equals @p want or, in the array form RFC
// 7519 sec 4.1.3 allows, contains it.
static proto_bool aud_contains(const char *s, const char *e, const char *want)
{
    const char *v = NULL;
    size_t vl = 0;
    char t = '\0';
    if (!find_field(s, e, "aud", &v, &vl, &t))
    {
        return PROTO_FALSE;
    }
    size_t wl = str.len(want, vl + 1u);
    if (t == 's')
    {
        return vl == wl && mem.cmp(v, want, wl) == 0;
    }
    if (t == 'a')
    {
        const char *p = v;        // points at '['
        const char *end = v + vl; // just past ']'
        while (p < end)
        {
            const char *q = (const char *)mem.chr(p, (size_t)(end - p), '"');
            const char *r = q ? (const char *)mem.chr(q + 1, (size_t)(end - (q + 1)), '"') : NULL;
            if (!q || !r)
            {
                break;
            }
            q++;
            if ((size_t)(r - q) == wl && mem.cmp(q, want, wl) == 0)
            {
                return PROTO_TRUE;
            }
            p = r + 1;
        }
    }
    return PROTO_FALSE;
}

// Split a JWS Compact Serialization (RFC 7515 sec 7.1) into its three parts. False unless there are
// exactly two '.' separators and three non-empty parts.
static proto_bool split_compact(const char *tok, size_t len, const char **seg, size_t *seglen)
{
    const char *d1 = (const char *)mem.chr(tok, len, '.');
    if (!d1)
    {
        return PROTO_FALSE;
    }
    size_t rem = len - (size_t)(d1 + 1 - tok);
    const char *d2 = (const char *)mem.chr(d1 + 1, rem, '.');
    if (!d2)
    {
        return PROTO_FALSE;
    }
    size_t rem2 = len - (size_t)(d2 + 1 - tok);
    if (mem.chr(d2 + 1, rem2, '.'))
    {
        return PROTO_FALSE;
    }
    seg[OIDC_SEG_HEADER] = tok;
    seglen[OIDC_SEG_HEADER] = (size_t)(d1 - tok);
    seg[OIDC_SEG_PAYLOAD] = d1 + 1;
    seglen[OIDC_SEG_PAYLOAD] = (size_t)(d2 - d1 - 1);
    seg[OIDC_SEG_SIGNATURE] = d2 + 1;
    seglen[OIDC_SEG_SIGNATURE] = rem2;
    return seglen[OIDC_SEG_HEADER] && seglen[OIDC_SEG_PAYLOAD] && seglen[OIDC_SEG_SIGNATURE];
}

// Right-align @p len decoded bytes into a fixed @p width big-endian field. A Base64urlUInt (RFC 7518
// sec 2) omits leading zero octets and some encoders emit one, so both a short value and a single
// leading zero fit. False if the value does not.
static proto_bool right_align(const uint8_t *src, size_t len, uint8_t *dst, size_t width)
{
    if (len > width)
    {
        if (len == width + 1 && src[0] == 0)
        {
            src++;
            len--;
        }
        else
        {
            return PROTO_FALSE;
        }
    }
    mem.set(dst, 0, width);
    mem.cpy(dst + (width - len), src, len);
    return PROTO_TRUE;
}

// The `n` and `e` parameters of one RSA JWK (RFC 7518 sec 6.3.1.1 / 6.3.1.2), base64url-decoded and
// right-aligned into the key.
static proto_bool parse_rsa_jwk(const char *s, const char *e, protocore_oidc_key *key)
{
    char b64[400];
    if (!get_str(s, e, "n", b64, sizeof(b64)))
    {
        return PROTO_FALSE;
    }
    uint8_t tmp[PROTOCORE_OIDC_RSA_BYTES + 8];
    size_t nlen = Base64.url_decode(b64, str.len(b64, sizeof(b64)), tmp, sizeof(tmp));
    if (nlen == 0 || !right_align(tmp, nlen, key->n, PROTOCORE_OIDC_RSA_BYTES))
    {
        return PROTO_FALSE;
    }

    if (!get_str(s, e, "e", b64, sizeof(b64)))
    {
        return PROTO_FALSE;
    }
    uint8_t e_tmp[8];
    size_t elen = Base64.url_decode(b64, str.len(b64, sizeof(b64)), e_tmp, sizeof(e_tmp));
    if (elen == 0 || !right_align(e_tmp, elen, key->e, 4))
    {
        return PROTO_FALSE;
    }
    key->loaded = PROTO_TRUE;
    return PROTO_TRUE;
}

// Empty every Claim, so a failed validation never leaves the previous token's subject readable.
static void claims_clear(protocore_oidc_claims *claims)
{
    claims->sub[0] = '\0';
    claims->email[0] = '\0';
    claims->iat = 0;
    claims->exp = 0;
}

// The `kid` Header Parameter (RFC 7515 sec 4.1.4) out of the JOSE Header: split the Compact
// Serialization, decode the header part, scan it for the member.
static void token_kid(struct OidcInternal *restrict ctx)
{
    ctx->ns->text[0] = '\0';
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->token)
    {
        return;
    }
    const char *seg[3];
    size_t seglen[3];
    if (!split_compact(ctx->ns->token, ctx->ns->token_len, seg, seglen))
    {
        return;
    }
    uint8_t hdr[PROTOCORE_OIDC_HDR_LEN];
    size_t hn = Base64.url_decode(seg[OIDC_SEG_HEADER], seglen[OIDC_SEG_HEADER], hdr, sizeof(hdr) - 1);
    if (hn == 0)
    {
        return;
    }
    hdr[hn] = '\0';
    ctx->ns->ok = get_str((const char *)hdr, (const char *)hdr + hn, "kid", ctx->ns->text, sizeof(ctx->ns->text));
}

// The RSA JWK the `kid` names, out of the JWK Set (RFC 7517 sec 5.1). Each member of the "keys"
// array is taken between its braces; an empty `kid` takes the first JWK whose `n` and `e` parse.
static void jwks_find(struct OidcInternal *restrict ctx)
{
    size_t scratch = protocore_plaintext_mark();
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->key.rsa.loaded = PROTO_FALSE;
    if (!ctx->ns->key.jwks)
    {
        protocore_plaintext_release(scratch);
        return;
    }
    const char *want_kid = ctx->ns->key.kid;
    size_t want_len = want_kid ? str.len(want_kid, PROTOCORE_OIDC_KID_LEN) : 0u;
    const char *jwks = ctx->ns->key.jwks;
    const char *all_end = jwks + str.len(jwks, PROTOCORE_OIDC_JWKS_MAX);
    const char *p = mem_find(jwks, all_end, "\"keys\"");
    p = p ? (const char *)mem.chr(p, (size_t)(all_end - p), '[') : NULL;
    if (!p)
    {
        protocore_plaintext_release(scratch);
        return;
    }
    p++;

    while (p < all_end)
    {
        const char *obj = (const char *)mem.chr(p, (size_t)(all_end - p), '{');
        if (!obj)
        {
            break;
        }
        const char *end = (const char *)mem.chr(obj, (size_t)(all_end - obj), '}');
        if (!end)
        {
            break;
        }
        end++; // include '}'

        proto_bool want;
        char this_kid[PROTOCORE_OIDC_KID_LEN];
        proto_bool has_kid = get_str(obj, end, "kid", this_kid, sizeof(this_kid));
        if (want_kid && *want_kid)
        {
            // The compare reads the wanted `kid` and its terminator, which is why one that fills the
            // capture buffer matches nothing.
            want =
                has_kid && want_len < PROTOCORE_OIDC_KID_LEN && str.eq(this_kid, want_kid, want_len + 1u, PROTO_FALSE);
        }
        else
        {
            want = PROTO_TRUE; // no `kid` requested -> first usable RSA JWK
        }

        if (want && parse_rsa_jwk(obj, end, &ctx->ns->key.rsa))
        {
            ctx->ns->ok = PROTO_TRUE;
            protocore_plaintext_release(scratch);
            return;
        }
        if (want && want_kid && *want_kid)
        {
            protocore_plaintext_release(scratch);
            return; // the `kid` matched but the JWK is unusable
        }
        p = end;
    }
    protocore_plaintext_release(scratch);
}

// OIDC Core sec 3.1.3.7 against the key already in ns->key.rsa: steps 6 and 7 (signature and `alg`),
// then 2, 3 and 9 (`iss`, `aud`, `exp`), then the Claims.
static void verify_with_key(struct OidcInternal *restrict ctx)
{
    claims_clear(&ctx->ns->claims);
    const char *token = ctx->ns->token;
    size_t token_len = ctx->ns->token_len;
    const protocore_oidc_key *key = &ctx->ns->key.rsa;
    if (!token || !key->loaded || token_len == 0 || token_len > PROTOCORE_OIDC_MAX_LEN)
    {
        ctx->ns->result = PROTOCORE_OIDC_ERR_FORMAT;
        return;
    }

    const char *seg[3];
    size_t seglen[3];
    if (!split_compact(token, token_len, seg, seglen))
    {
        ctx->ns->result = PROTOCORE_OIDC_ERR_FORMAT;
        return;
    }

    // Borrow the large decode buffers from the per-dispatch scratch arena rather than the worker
    // stack. The mark below is released on every return path. The four are live together, so
    // PROTOCORE_PLAINTEXT_WORK_OIDC is their sum and the arena is sized to hold it.
    static_assert(PROTOCORE_PLAINTEXT_WORK_OIDC <= PROTOCORE_PLAINTEXT_ARENA_SIZE, "OIDC scratch exceeds the arena");
    size_t scope = protocore_plaintext_mark();
    uint8_t *hdr = (uint8_t *)protocore_plaintext_alloc(PROTOCORE_OIDC_HDR_LEN, 1);
    uint8_t *sig = (uint8_t *)protocore_plaintext_alloc(PROTOCORE_OIDC_RSA_BYTES, 1);
    uint8_t *pl = (uint8_t *)protocore_plaintext_alloc(PROTOCORE_OIDC_MAX_LEN, 1);
    char *iss = (char *)protocore_plaintext_alloc(PROTOCORE_OIDC_ISS_LEN, 1);
    if (!hdr || !sig || !pl || !iss)
    {
        protocore_plaintext_release(scope);
        ctx->ns->result = PROTOCORE_OIDC_ERR_FORMAT; // scratch exhausted: fail closed
        return;
    }

    // JOSE Header: require `alg` == RS256 (RFC 7515 sec 4.1.1), which rejects alg:none and the
    // MAC-based algorithms of OIDC Core sec 3.1.3.7 step 8.
    size_t hn = Base64.url_decode(seg[OIDC_SEG_HEADER], seglen[OIDC_SEG_HEADER], hdr, PROTOCORE_OIDC_HDR_LEN - 1);
    if (hn == 0)
    {
        protocore_plaintext_release(scope);
        ctx->ns->result = PROTOCORE_OIDC_ERR_FORMAT;
        return;
    }
    hdr[hn] = '\0';
    char alg[16];
    if (!get_str((const char *)hdr, (const char *)hdr + hn, "alg", alg, sizeof(alg)) ||
        !str.eq(alg, "RS256", sizeof("RS256"), PROTO_FALSE))
    {
        protocore_plaintext_release(scope);
        ctx->ns->result = PROTOCORE_OIDC_ERR_ALG;
        return;
    }

    // JWS Signature: RSA-2048 -> exactly 256 bytes (RFC 7518 sec 3.3).
    if (Base64.url_decode(seg[OIDC_SEG_SIGNATURE], seglen[OIDC_SEG_SIGNATURE], sig, PROTOCORE_OIDC_RSA_BYTES) !=
        PROTOCORE_OIDC_RSA_BYTES)
    {
        protocore_plaintext_release(scope);
        ctx->ns->result = PROTOCORE_OIDC_ERR_FORMAT;
        return;
    }

    // Step 6: check the signature over the JWS Signing Input, the header and payload parts with
    // their '.' (RFC 7515 sec 2). protocore_rsa_verify() hashes it; RS256 is SHA-256. One borrow for
    // that digest, returned either way before the Claims are read.
    size_t signing_input_len = (size_t)(seg[OIDC_SEG_PAYLOAD] + seglen[OIDC_SEG_PAYLOAD] - token);
    size_t vmark = protocore_secure_mark();
    protocore_span vws = protocore_secure_span(PROTOCORE_SHA256_BORROW, _Alignof(uint32_t));
    if (!span.ok(vws))
    {
        protocore_secure_release(vmark);
        protocore_plaintext_release(scope);
        ctx->ns->result = PROTOCORE_OIDC_ERR_SIGNATURE; // pool exhausted: fail closed
        return;
    }
    int vrc = protocore_rsa_verify(key->n, key->e, vws.buf, (const uint8_t *)token, signing_input_len, sig,
                                   PROTOCORE_OIDC_RSA_BYTES, PROTOCORE_RSA_HASH_SHA256);
    protocore_secure_release(vmark);
    if (vrc != 0)
    {
        protocore_plaintext_release(scope);
        ctx->ns->result = PROTOCORE_OIDC_ERR_SIGNATURE;
        return;
    }

    // JWS Payload: the Claims, trusted only now that the signature verifies.
    size_t pn = Base64.url_decode(seg[OIDC_SEG_PAYLOAD], seglen[OIDC_SEG_PAYLOAD], pl, PROTOCORE_OIDC_MAX_LEN - 1);
    if (pn == 0)
    {
        protocore_plaintext_release(scope);
        ctx->ns->result = PROTOCORE_OIDC_ERR_FORMAT;
        return;
    }
    pl[pn] = '\0';
    const char *ps = (const char *)pl;
    const char *pe = ps + pn;

    // Step 2: `iss` must equal the Issuer Identifier exactly (RFC 7519 sec 4.1.1). The compare reads
    // one byte past the expected string's characters, which is its terminator.
    const char *want_iss = ctx->ns->expect.iss;
    if (want_iss && *want_iss)
    {
        size_t want_len = str.len(want_iss, PROTOCORE_OIDC_ISS_LEN);
        if (want_len >= PROTOCORE_OIDC_ISS_LEN || !get_str(ps, pe, "iss", iss, PROTOCORE_OIDC_ISS_LEN) ||
            !str.eq(iss, want_iss, want_len + 1u, PROTO_FALSE))
        {
            protocore_plaintext_release(scope);
            ctx->ns->result = PROTOCORE_OIDC_ERR_ISS;
            return;
        }
    }
    // Step 3: `aud` must contain the client_id.
    const char *want_aud = ctx->ns->expect.aud;
    if (want_aud && *want_aud)
    {
        if (!aud_contains(ps, pe, want_aud))
        {
            protocore_plaintext_release(scope);
            ctx->ns->result = PROTOCORE_OIDC_ERR_AUD;
            return;
        }
    }

    // Step 9: now must be before `exp` (RFC 7519 sec 4.1.4), and at or after `nbf` when the token
    // carries one (RFC 7519 sec 4.1.5).
    int64_t now = (int64_t)ctx->ns->expect.now_unix;
    int64_t exp = 0;
    if (!get_int64(ps, pe, "exp", &exp) || now >= exp)
    {
        protocore_plaintext_release(scope);
        ctx->ns->result = PROTOCORE_OIDC_ERR_EXPIRED;
        return;
    }
    int64_t nbf = 0;
    if (get_int64(ps, pe, "nbf", &nbf) && now < nbf)
    {
        protocore_plaintext_release(scope);
        ctx->ns->result = PROTOCORE_OIDC_ERR_NOT_YET;
        return;
    }

    ctx->ns->claims.exp = exp;
    get_str(ps, pe, "sub", ctx->ns->claims.sub, sizeof(ctx->ns->claims.sub));
    get_str(ps, pe, "email", ctx->ns->claims.email, sizeof(ctx->ns->claims.email));
    get_int64(ps, pe, "iat", &ctx->ns->claims.iat);
    protocore_plaintext_release(scope);
    ctx->ns->result = PROTOCORE_OIDC_OK;
}

// The whole of OIDC Core sec 3.1.3.7: resolve the signing key from the JWK Set by the token's `kid`,
// then validate against it. A token with no `kid` takes the first usable RSA JWK.
static void verify(struct OidcInternal *restrict ctx)
{
    claims_clear(&ctx->ns->claims);
    token_kid(ctx);
    ctx->ns->key.kid = ctx->ns->text[0] ? ctx->ns->text : NULL;
    jwks_find(ctx);
    if (!ctx->ns->ok)
    {
        ctx->ns->result = PROTOCORE_OIDC_ERR_KEY;
        return;
    }
    verify_with_key(ctx);
}

// Designated, so a member's position in the struct does not decide what it binds to.
OidcNs Oidc = {.token_kid = token_kid,
               .jwks_find = jwks_find,
               .verify_with_key = verify_with_key,
               .verify = verify,
               .internal = &s_oidc};

#endif // PROTOCORE_ENABLE_OIDC
