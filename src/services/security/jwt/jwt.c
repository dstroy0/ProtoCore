// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file jwt.c
 * @brief JWT HS256 verification + claim extraction (base64url over base64).
 */

#include "services/security/jwt/jwt.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_JWT

#include "crypto/mac/hmac_sha256.h"
#include "network_drivers/presentation/codec/base64/base64.h"
#include <stdio.h>
#include <string.h>

// Constant-time equality over @p n bytes (no early-out timing oracle).
static proto_bool ct_eq(const char *a, const char *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++)
    {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

// base64url encode/decode are shared with OIDC in the base64 module
// (pc_base64url_encode / pc_base64url_decode).

// Split a compact JWT into header.payload (signing input) and the signature.
// Requires exactly two '.' separators. Returns false on a malformed shape.
static proto_bool pc_jwt_split(const char *token, size_t token_len, size_t *signing_len, const char **sig,
                               size_t *sig_len)
{
    const char *d1 = (const char *)memchr(token, '.', token_len);
    if (!d1)
    {
        return PROTO_FALSE;
    }
    size_t rem = token_len - (size_t)(d1 + 1 - token);
    const char *d2 = (const char *)memchr(d1 + 1, '.', rem);
    if (!d2)
    {
        return PROTO_FALSE;
    }
    size_t rem2 = token_len - (size_t)(d2 + 1 - token);
    if (memchr(d2 + 1, '.', rem2)) // a third '.' is not a valid JWT
    {
        return PROTO_FALSE;
    }
    *signing_len = (size_t)(d2 - token);
    *sig = d2 + 1;
    *sig_len = rem2;
    return PROTO_TRUE;
}

// RFC 7515 §5.2: the algorithm used MUST be the one named by the JWS header "alg".
// Decode the header segment and require alg == "HS256" - this rejects "none",
// RS256, HS384, and any other algorithm-substitution attempt before the HMAC check.
static proto_bool pc_jwt_header_alg_is_hs256(const char *header, size_t hlen)
{
    uint8_t buf[96];
    size_t n = pc_base64url_decode(header, hlen, buf, sizeof(buf) - 1);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    buf[n] = '\0';
    const char *p = strstr((const char *)buf, "\"alg\"");
    if (!p)
    {
        return PROTO_FALSE;
    }
    p += 5;
    while (*p == ' ' || *p == ':' || *p == '\t')
    {
        p++;
    }
    if (*p != '"')
    {
        return PROTO_FALSE;
    }
    p++;
    return strncmp(p, "HS256", 5) == 0 && p[5] == '"';
}

proto_bool pc_jwt_verify_hs256(const char *token, size_t token_len, const uint8_t *secret, size_t secret_len)
{
    if (!token || token_len < 5 || token_len > PC_JWT_MAX_LEN)
    {
        return PROTO_FALSE;
    }

    size_t signing_len;
    size_t sig_len;
    const char *sig;
    if (!pc_jwt_split(token, token_len, &signing_len, &sig, &sig_len))
    {
        return PROTO_FALSE;
    }

    // Validate the declared algorithm matches what we verify (RFC 7515 §5.2).
    const char *d1 = (const char *)memchr(token, '.', token_len);
    if (!pc_jwt_header_alg_is_hs256(token, (size_t)(d1 - token)))
    {
        return PROTO_FALSE;
    }

    // HS256 -> 32-byte MAC -> 43 base64url chars (no padding).
    if (sig_len != 43)
    {
        return PROTO_FALSE;
    }

    uint8_t mac[PC_HMAC_SHA256_LEN];
    pc_hmac_sha256(secret, secret_len, (const uint8_t *)token, signing_len, mac);

    char computed[48];
    // PC_HMAC_SHA256_LEN is a fixed 32 bytes, and unpadded base64url of 32 bytes is always
    // 43 characters, so this length check can never fail.
    if (pc_base64url_encode(mac, sizeof(mac), computed) != 43)
    {
        return PROTO_FALSE;
    }
    return ct_eq(computed, sig, 43);
}

proto_bool pc_jwt_bearer_valid(const char *auth_header, const uint8_t *secret, size_t secret_len)
{
    if (!auth_header || strncasecmp(auth_header, "Bearer ", 7) != 0)
    {
        return PROTO_FALSE;
    }
    const char *tok = auth_header + 7;
    while (*tok == ' ')
    {
        tok++;
    }
    return pc_jwt_verify_hs256(tok, strnlen(tok, PC_JWT_MAX_LEN + 1), secret, secret_len);
}

proto_bool pc_jwt_time_valid(const char *token, size_t token_len, long now_epoch, long leeway_s)
{
    if (!token || now_epoch <= 0)
    {
        return PROTO_TRUE; // no wall clock -> time claims cannot be evaluated (the signature is the gate)
    }

    // Subtraction form (not exp + leeway) so a far-future claim cannot overflow `long`.
    long exp = 0;
    if (pc_jwt_claim_int(token, token_len, "exp", &exp) && now_epoch - exp > leeway_s)
    {
        return PROTO_FALSE; // expired (RFC 7519 §4.1.4)
    }

    long nbf = 0;
    if (pc_jwt_claim_int(token, token_len, "nbf", &nbf) && nbf - now_epoch > leeway_s)
    {
        return PROTO_FALSE; // not yet valid (RFC 7519 §4.1.5)
    }

    return PROTO_TRUE;
}

proto_bool pc_jwt_verify_hs256_at(const char *token, size_t token_len, const uint8_t *secret, size_t secret_len,
                                  long now_epoch, long leeway_s)
{
    return pc_jwt_verify_hs256(token, token_len, secret, secret_len) &&
           pc_jwt_time_valid(token, token_len, now_epoch, leeway_s);
}

proto_bool pc_jwt_bearer_valid_at(const char *auth_header, const uint8_t *secret, size_t secret_len, long now_epoch,
                                  long leeway_s)
{
    if (!auth_header || strncasecmp(auth_header, "Bearer ", 7) != 0)
    {
        return PROTO_FALSE;
    }
    const char *tok = auth_header + 7;
    while (*tok == ' ')
    {
        tok++;
    }
    return pc_jwt_verify_hs256_at(tok, strnlen(tok, PC_JWT_MAX_LEN + 1), secret, secret_len, now_epoch, leeway_s);
}

proto_bool pc_jwt_claim_int(const char *token, size_t token_len, const char *name, long *out)
{
    if (!token || !name || !out)
    {
        return PROTO_FALSE;
    }

    const char *d1 = (const char *)memchr(token, '.', token_len);
    if (!d1)
    {
        return PROTO_FALSE;
    }
    size_t rem = token_len - (size_t)(d1 + 1 - token);
    const char *d2 = (const char *)memchr(d1 + 1, '.', rem);
    if (!d2)
    {
        return PROTO_FALSE;
    }
    const char *payload = d1 + 1;
    size_t payload_len = (size_t)(d2 - payload);

    uint8_t buf[PC_JWT_MAX_LEN];
    size_t n = pc_base64url_decode(payload, payload_len, buf, sizeof(buf) - 1);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    buf[n] = '\0';

    char key[40];
    pc_sb sb_key = {key, sizeof(key), 0, PROTO_TRUE};
    pc_sb_put(&sb_key, "\"");
    pc_sb_put(&sb_key, name);
    pc_sb_put(&sb_key, "\"");
    int kn = (int)pc_sb_finish(&sb_key);
    // kn <= 0 is unreachable: snprintf on a plain "%s" format into a valid buffer cannot report an
    // encoding error, and the two quotes make the would-be length at least 2.
    if (kn <= 0 || kn >= (int)sizeof(key))
    {
        return PROTO_FALSE;
    }
    const char *p = strstr((const char *)buf, key);
    if (!p)
    {
        return PROTO_FALSE;
    }
    p += kn;
    while (*p == ' ' || *p == ':' || *p == '\t')
    {
        p++;
    }
    proto_bool neg = PROTO_FALSE;
    if (*p == '-')
    {
        neg = PROTO_TRUE;
        p++;
    }
    if (*p < '0' || *p > '9')
    {
        return PROTO_FALSE;
    }
    unsigned long v = 0; // accumulate unsigned: signed overflow (a huge claim value) is UB
    while (*p >= '0' && *p <= '9')
    {
        v = v * 10UL + (unsigned long)(*p++ - '0');
    }
    *out = neg ? (long)(0UL - v) : (long)v; // two's-complement reinterpret, no negation UB
    return PROTO_TRUE;
}

proto_bool pc_jwt_claim_str(const char *token, size_t token_len, const char *name, char *out, size_t out_cap)
{
    if (!token || !name || !out || out_cap == 0)
    {
        return PROTO_FALSE;
    }
    out[0] = '\0';

    const char *d1 = (const char *)memchr(token, '.', token_len);
    if (!d1)
    {
        return PROTO_FALSE;
    }
    size_t rem = token_len - (size_t)(d1 + 1 - token);
    const char *d2 = (const char *)memchr(d1 + 1, '.', rem);
    if (!d2)
    {
        return PROTO_FALSE;
    }
    const char *payload = d1 + 1;
    size_t payload_len = (size_t)(d2 - payload);

    uint8_t buf[PC_JWT_MAX_LEN];
    size_t n = pc_base64url_decode(payload, payload_len, buf, sizeof(buf) - 1);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    buf[n] = '\0';

    char key[40];
    pc_sb sb_key2 = {key, sizeof(key), 0, PROTO_TRUE};
    pc_sb_put(&sb_key2, "\"");
    pc_sb_put(&sb_key2, name);
    pc_sb_put(&sb_key2, "\"");
    int kn = (int)pc_sb_finish(&sb_key2);
    // kn <= 0 is unreachable for the same reason as in pc_jwt_claim_int() above.
    if (kn <= 0 || kn >= (int)sizeof(key))
    {
        return PROTO_FALSE;
    }
    const char *p = strstr((const char *)buf, key);
    if (!p)
    {
        return PROTO_FALSE;
    }
    p += kn;
    while (*p == ' ' || *p == ':' || *p == '\t')
    {
        p++;
    }
    if (*p != '"') // not a string-valued claim
    {
        return PROTO_FALSE;
    }
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_cap)
    {
        if (*p == '\\' && p[1]) // minimal unescape: drop the backslash, copy the next char
        {
            p++;
        }
        out[i++] = *p++;
    }
    if (*p != '"') // unterminated string or value too long for out
    {
        out[0] = '\0';
        return PROTO_FALSE;
    }
    out[i] = '\0';
    return PROTO_TRUE;
}

proto_bool pc_jwt_scope_allows(const char *scope_claim, const char *required)
{
    if (!scope_claim || !required || !*required)
    {
        return PROTO_FALSE;
    }
    size_t rlen = strnlen(required, PC_JWT_MAX_LEN + 1);
    const char *p = scope_claim;
    while (*p)
    {
        while (*p == ' ')
        {
            p++;
        }
        const char *start = p;
        while (*p && *p != ' ')
        {
            p++;
        }
        if ((size_t)(p - start) == rlen && memcmp(start, required, rlen) == 0)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

#endif // PC_ENABLE_JWT
