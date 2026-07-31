// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file oidc.cpp
 * @brief OIDC ID-token (RS256) verification - implementation.
 *
 * The shared base64url decoder (base64 module), small bounded JSON field scanners
 * (no full parser, no heap), and the RS256 signature check delegated to
 * pc_rsa_verify() (real RSA modexp; mbedTLS on ESP32). Claims are read only
 * after the signature verifies.
 */

#include "services/security/oidc/oidc.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder

#if PC_ENABLE_OIDC

#include "crypto/asymmetric/rsa.h"
#include "network_drivers/presentation/codec/base64/base64.h" // shared pc_base64url_decode
#include "server/mmgr/plaintext.h" // per-dispatch arena (keeps the decode buffers off the worker stack)

#include <stdio.h>
#include <string.h>

namespace
{
// base64url decoding is shared with JWT in the base64 module (pc_base64url_decode).

// Bounded substring search of [hs, he) for the NUL-terminated needle.
const char *mem_find(const char *hs, const char *he, const char *needle)
{
    size_t nl = strnlen(needle, (size_t)(he - hs) + 1);
    // Both callers pass a quoted member name ("\"alg\"", "\"keys\"", ...), never the empty
    // string, and strnlen's limit is always >= 1, so nl >= 1 here.
    if (nl == 0 || (size_t)(he - hs) < nl) // GCOVR_EXCL_LINE  nl == 0 unreachable (see above)
    {
        return nullptr;
    }
    for (const char *p = hs; p + nl <= he; p++)
    {
        if (memcmp(p, needle, nl) == 0)
        {
            return p;
        }
    }
    return nullptr;
}

// Locate the JSON member @p name within [s, e). On success sets *vstart/*vlen to
// the value extent and *type to 's' (string, between the quotes), 'n' (number,
// including a leading '-'), or 'a' (array, including the brackets).
bool find_field(const char *s, const char *e, const char *name, const char **vstart, size_t *vlen, char *type)
{
    char needle[96];
    pc_sb sb_needle = {needle, sizeof(needle), 0, true};
    pc_sb_put(&sb_needle, "\"");
    pc_sb_put(&sb_needle, name);
    pc_sb_put(&sb_needle, "\"");
    int nn = (int)pc_sb_finish(&sb_needle);
    // Every internal caller passes a short fixed field name (alg/iss/aud/exp/nbf/sub/email/n/e/kid), so the
    // 96-byte needle never overflows, and snprintf of one %s into memory cannot report an error.
    if (nn <= 0 || nn >= (int)sizeof(needle)) // GCOVR_EXCL_LINE  unreachable: field names are short literals
    {
        return false; // GCOVR_EXCL_LINE  (see above)
    }
    const char *p = mem_find(s, e, needle);
    if (!p)
    {
        return false;
    }
    p += nn;
    while (p < e && (*p == ' ' || *p == '\t' || *p == ':' || *p == '\n' || *p == '\r'))
    {
        p++;
    }
    if (p >= e)
    {
        return false;
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
            return false;
        }
        *vstart = p;
        *vlen = (size_t)(q - p);
        *type = 's';
        return true;
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
            return false;
        }
        *vstart = p;
        *vlen = (size_t)(q - p + 1);
        *type = 'a';
        return true;
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
        return true;
    }
    return false;
}

// Copy a string member into @p out (minimal unescape: drop the backslash). False
// if absent / not a string / does not fit.
bool get_str(const char *s, const char *e, const char *name, char *out, size_t cap)
{
    const char *v;
    size_t vl;
    char t;
    if (!find_field(s, e, name, &v, &vl, &t) || t != 's')
    {
        return false;
    }
    // A while loop so a JSON "\x" escape can consume its second char without
    // mutating a for-loop counter: take the char after the backslash literally.
    size_t o = 0;
    size_t i = 0;
    while (i < vl)
    {
        char ch = v[i];
        // i + 1 < vl always holds when ch is a backslash: find_field walks this same value with
        // the same pairwise escape skip and only stops at an UNescaped '"', so a backslash it
        // examined always had a following byte inside the value. A value can therefore never end
        // on a backslash that this loop reaches as a fresh iteration - the bound is defensive.
        if (ch == '\\' && i + 1 < vl) // GCOVR_EXCL_LINE  i + 1 < vl cannot be false here (see above)
        {
            ch = v[++i];
        }
        if (o + 1 >= cap)
        {
            return false;
        }
        out[o++] = ch;
        i++;
    }
    out[o] = '\0';
    return true;
}

// Read a numeric member as 64-bit (epoch seconds exceed 32-bit). False if absent
// / not a number.
bool get_int64(const char *s, const char *e, const char *name, int64_t *out)
{
    const char *v;
    size_t vl;
    char t;
    // vl == 0 is unreachable for a type-'n' value: find_field only reports 'n' after consuming a
    // leading '-' or at least one digit, so the extent it hands back is always >= 1 byte.
    if (!find_field(s, e, name, &v, &vl, &t) || t != 'n' || vl == 0) // GCOVR_EXCL_LINE  vl == 0 unreachable
    {
        return false;
    }
    bool neg = (*v == '-');
    size_t i = neg ? 1 : 0;
    int64_t val = 0;
    for (; i < vl; i++)
    {
        val = val * 10 + (v[i] - '0');
    }
    *out = neg ? -val : val;
    return true;
}

// True if the `aud` member equals @p want (string form) or contains it (array).
bool aud_contains(const char *s, const char *e, const char *want)
{
    const char *v;
    size_t vl;
    char t;
    if (!find_field(s, e, "aud", &v, &vl, &t))
    {
        return false;
    }
    size_t wl = strnlen(want, vl + 1);
    if (t == 's')
    {
        return vl == wl && memcmp(v, want, wl) == 0;
    }
    if (t == 'a')
    {
        const char *p = v;        // points at '['
        const char *end = v + vl; // just past ']'
        // The loop always leaves via break or return, never via this condition: find_field sizes
        // vl to include the closing ']', so a quote found inside can be at most end - 2 and the
        // p = r + 1 step below therefore always lands strictly before end.
        while (p < end) // GCOVR_EXCL_LINE  p < end cannot be false (see above)
        {
            const char *q = (const char *)memchr(p, '"', (size_t)(end - p));
            const char *r = q ? (const char *)memchr(q + 1, '"', (size_t)(end - (q + 1))) : nullptr;
            if (!q || !r)
            {
                break;
            }
            q++;
            if ((size_t)(r - q) == wl && memcmp(q, want, wl) == 0)
            {
                return true;
            }
            p = r + 1;
        }
    }
    return false;
}

// Split a compact JWT into its three segments. Returns false unless there are
// exactly two '.' separators and three non-empty parts.
bool split3(const char *tok, size_t len, const char **seg, size_t *seglen)
{
    const char *d1 = (const char *)memchr(tok, '.', len);
    if (!d1)
    {
        return false;
    }
    size_t rem = len - (size_t)(d1 + 1 - tok);
    const char *d2 = (const char *)memchr(d1 + 1, '.', rem);
    if (!d2)
    {
        return false;
    }
    size_t rem2 = len - (size_t)(d2 + 1 - tok);
    if (memchr(d2 + 1, '.', rem2))
    {
        return false;
    }
    seg[0] = tok;
    seglen[0] = (size_t)(d1 - tok);
    seg[1] = d1 + 1;
    seglen[1] = (size_t)(d2 - d1 - 1);
    seg[2] = d2 + 1;
    seglen[2] = rem2;
    return seglen[0] && seglen[1] && seglen[2];
}

// Right-align @p len decoded bytes into a fixed @p width big-endian field,
// tolerating a single leading zero byte (some encoders pad n) and leading-zero
// omission. Returns false if the value does not fit.
bool right_align(const uint8_t *src, size_t len, uint8_t *dst, size_t width)
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
            return false;
        }
    }
    memset(dst, 0, width);
    memcpy(dst + (width - len), src, len);
    return true;
}

bool parse_rsa_jwk(const char *s, const char *e, pc_oidc_key *key)
{
    char b64[400];
    if (!get_str(s, e, "n", b64, sizeof(b64)))
    {
        return false;
    }
    uint8_t tmp[PC_OIDC_RSA_BYTES + 8];
    size_t nlen = pc_base64url_decode(b64, strnlen(b64, sizeof(b64)), tmp, sizeof(tmp));
    if (nlen == 0 || !right_align(tmp, nlen, key->n, PC_OIDC_RSA_BYTES))
    {
        return false;
    }

    if (!get_str(s, e, "e", b64, sizeof(b64)))
    {
        return false;
    }
    uint8_t e_tmp[8];
    size_t elen = pc_base64url_decode(b64, strnlen(b64, sizeof(b64)), e_tmp, sizeof(e_tmp));
    if (elen == 0 || !right_align(e_tmp, elen, key->e, 4))
    {
        return false;
    }
    key->loaded = true;
    return true;
}
} // namespace

bool pc_oidc_token_kid(const char *token, size_t token_len, char *kid_out, size_t kid_cap)
{
    if (!token || !kid_out || kid_cap == 0)
    {
        return false;
    }
    const char *seg[3];
    size_t seglen[3];
    if (!split3(token, token_len, seg, seglen))
    {
        return false;
    }
    uint8_t hdr[512];
    size_t hn = pc_base64url_decode(seg[0], seglen[0], hdr, sizeof(hdr) - 1);
    if (hn == 0)
    {
        return false;
    }
    hdr[hn] = '\0';
    return get_str((const char *)hdr, (const char *)hdr + hn, "kid", kid_out, kid_cap);
}

bool pc_oidc_jwks_find(const char *jwks_json, const char *kid, pc_oidc_key *key)
{
    if (!jwks_json || !key)
    {
        return false;
    }
    const char *all_end = jwks_json + strnlen(jwks_json, PC_OIDC_JWKS_MAX);
    const char *p = mem_find(jwks_json, all_end, "\"keys\"");
    p = p ? (const char *)memchr(p, '[', (size_t)(all_end - p)) : nullptr;
    if (!p)
    {
        return false;
    }
    p++;

    while (p < all_end)
    {
        const char *obj = (const char *)memchr(p, '{', (size_t)(all_end - p));
        if (!obj)
        {
            break;
        }
        const char *end = (const char *)memchr(obj, '}', (size_t)(all_end - obj));
        if (!end)
        {
            break;
        }
        end++; // include '}'

        bool want;
        char this_kid[PC_OIDC_KID_LEN];
        bool has_kid = get_str(obj, end, "kid", this_kid, sizeof(this_kid));
        if (kid && *kid)
        {
            want = has_kid && strcmp(this_kid, kid) == 0;
        }
        else
        {
            want = true; // no kid requested -> first usable RSA key
        }

        if (want && parse_rsa_jwk(obj, end, key))
        {
            return true;
        }
        if (want && kid && *kid)
        {
            return false; // kid matched but the key was unusable
        }
        p = end;
    }
    return false;
}

pc_oidc_result pc_oidc_verify_with_key(const char *token, size_t token_len, const pc_oidc_key *key,
                                       const char *expected_iss, const char *expected_aud, uint32_t now_unix,
                                       pc_oidc_claims *claims)
{
    if (!token || !key || !key->loaded || token_len == 0 || token_len > PC_OIDC_MAX_LEN)
    {
        return pc_oidc_result::PC_OIDC_ERR_FORMAT;
    }

    const char *seg[3];
    size_t seglen[3];
    if (!split3(token, token_len, seg, seglen))
    {
        return pc_oidc_result::PC_OIDC_ERR_FORMAT;
    }

    // Borrow the large decode buffers from the per-dispatch scratch arena rather
    // than the worker stack (was ~2.6 KB of stack frame: hdr + sig + pl + iss).
    // PlaintextScope reclaims them on every return path. The four are live together,
    // so PC_PLAINTEXT_WORK_OIDC is their sum and the arena is sized to hold it.
    static_assert(PC_PLAINTEXT_WORK_OIDC <= PC_PLAINTEXT_ARENA_SIZE, "OIDC scratch exceeds the arena");
    PlaintextScope scratch;
    uint8_t *hdr = (uint8_t *)pc_plaintext_alloc(PC_OIDC_HDR_LEN, 1);
    uint8_t *sig = (uint8_t *)pc_plaintext_alloc(PC_OIDC_RSA_BYTES, 1);
    uint8_t *pl = (uint8_t *)pc_plaintext_alloc(PC_OIDC_MAX_LEN, 1);
    char *iss = (char *)pc_plaintext_alloc(PC_OIDC_ISS_LEN, 1);
    if (!hdr || !sig || !pl || !iss)
    {
        return pc_oidc_result::PC_OIDC_ERR_FORMAT; // scratch exhausted: fail closed
    }

    // Header: require alg == RS256 (rejects alg:none / HS256 confusion).
    size_t hn = pc_base64url_decode(seg[0], seglen[0], hdr, PC_OIDC_HDR_LEN - 1);
    if (hn == 0)
    {
        return pc_oidc_result::PC_OIDC_ERR_FORMAT;
    }
    hdr[hn] = '\0';
    char alg[16];
    if (!get_str((const char *)hdr, (const char *)hdr + hn, "alg", alg, sizeof(alg)) || strcmp(alg, "RS256") != 0)
    {
        return pc_oidc_result::PC_OIDC_ERR_ALG;
    }

    // Signature: RSA-2048 -> exactly 256 bytes.
    if (pc_base64url_decode(seg[2], seglen[2], sig, PC_OIDC_RSA_BYTES) != PC_OIDC_RSA_BYTES)
    {
        return pc_oidc_result::PC_OIDC_ERR_FORMAT;
    }

    // Verify over the signing input "header.payload" (pc_rsa_verify hashes it). RS256 = SHA-256.
    size_t signing_len = (size_t)(seg[1] + seglen[1] - token);
    if (pc_rsa_verify(key->n, key->e, (const uint8_t *)token, signing_len, sig, PC_OIDC_RSA_BYTES,
                      pc_rsa_hash::SHA256) != 0)
    {
        return pc_oidc_result::PC_OIDC_ERR_SIGNATURE;
    }

    // Claims (trusted only now that the signature is valid).
    size_t pn = pc_base64url_decode(seg[1], seglen[1], pl, PC_OIDC_MAX_LEN - 1);
    if (pn == 0)
    {
        return pc_oidc_result::PC_OIDC_ERR_FORMAT;
    }
    pl[pn] = '\0';
    const char *ps = (const char *)pl;
    const char *pe = ps + pn;

    if (expected_iss && *expected_iss)
    {
        if (!get_str(ps, pe, "iss", iss, PC_OIDC_ISS_LEN) || strcmp(iss, expected_iss) != 0)
        {
            return pc_oidc_result::PC_OIDC_ERR_ISS;
        }
    }
    if (expected_aud && *expected_aud)
    {
        if (!aud_contains(ps, pe, expected_aud))
        {
            return pc_oidc_result::PC_OIDC_ERR_AUD;
        }
    }

    int64_t exp = 0;
    if (!get_int64(ps, pe, "exp", &exp) || (int64_t)now_unix >= exp)
    {
        return pc_oidc_result::PC_OIDC_ERR_EXPIRED;
    }
    int64_t nbf = 0;
    if (get_int64(ps, pe, "nbf", &nbf) && (int64_t)now_unix < nbf)
    {
        return pc_oidc_result::PC_OIDC_ERR_NOT_YET;
    }

    if (claims)
    {
        claims->sub[0] = '\0';
        claims->email[0] = '\0';
        claims->exp = exp;
        claims->iat = 0;
        get_str(ps, pe, "sub", claims->sub, sizeof(claims->sub));
        get_str(ps, pe, "email", claims->email, sizeof(claims->email));
        get_int64(ps, pe, "iat", &claims->iat);
    }
    return pc_oidc_result::PC_OIDC_OK;
}

pc_oidc_result pc_oidc_verify(const char *token, size_t token_len, const char *jwks_json, const char *expected_iss,
                              const char *expected_aud, uint32_t now_unix, pc_oidc_claims *claims)
{
    char kid[PC_OIDC_KID_LEN];
    if (!pc_oidc_token_kid(token, token_len, kid, sizeof(kid)))
    {
        kid[0] = '\0'; // no kid -> let jwks_find pick the sole key
    }
    pc_oidc_key key;
    key.loaded = false;
    if (!pc_oidc_jwks_find(jwks_json, kid[0] ? kid : nullptr, &key))
    {
        return pc_oidc_result::PC_OIDC_ERR_KEY;
    }
    return pc_oidc_verify_with_key(token, token_len, &key, expected_iss, expected_aud, now_unix, claims);
}

#endif // PC_ENABLE_OIDC
