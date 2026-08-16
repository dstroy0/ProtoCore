// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file jwt.c
 * @brief The HS256 JWT verifier: JWS Compact Serialization split (RFC 7515 sec 7.1), `alg`
 *        enforcement (RFC 7515 sec 4.1.1), MAC validation (RFC 7518 sec 3.2), and claim reads out
 *        of the JWS Payload (RFC 7519 sec 4).
 */

#include "services/security/jwt/jwt.h"
#include "mmgr/membuild.h" // protocore_sb: the quoted claim key a payload scan searches for
#include "mmgr/protomem.h" // mem.chr / mem.cmp: the segment scan and the fixed-length alg compare

#if PROTOCORE_ENABLE_JWT

#include "crypto/ct_eq.h"           // protocore_ct_eq: the RFC 7518 sec 3.2 constant-time compare
#include "crypto/mac/hmac_sha256.h" // the HS256 MAC (RFC 7518 sec 3.1)
#include "mmgr/protostr.h"          // str.len / find / starts / to_long
#include "mmgr/secure.h"            // the MAC's working set, wiped on release
#include "network_drivers/presentation/codec/base64/base64.h" // base64url, RFC 4648 sec 5

// HS256 emits a 32-byte MAC, and 32 bytes in base64url with the padding skipped are 43 characters
// (RFC 4648 sec 5).
#define JWT_SIG_B64_LEN 43u

// The 43 characters plus the terminator the encoder writes, rounded up.
#define JWT_SIG_B64_CAP 48u

// The JOSE header carries `alg`, `typ` and `kid`; 96 bytes decode all three.
#define JWT_JOSE_HDR_CAP 96u

// A claim key is the name with its two quotes.
#define JWT_CLAIM_KEY_CAP 40u

/**
 * @brief RFC 7515 sec 7.1: the three segments of a compact serialization, and the signing input.
 *
 * @var JwsParts::header         BASE64URL(UTF8(JWS Protected Header))
 * @var JwsParts::header_len     its length
 * @var JwsParts::payload        BASE64URL(JWS Payload)
 * @var JwsParts::payload_len    its length
 * @var JwsParts::signature      BASE64URL(JWS Signature)
 * @var JwsParts::signature_len  its length
 * @var JwsParts::signing_len    header '.' payload: the JWS Signing Input (RFC 7515 sec 2)
 */
typedef struct
{
    const char *header;
    size_t header_len;
    const char *payload;
    size_t payload_len;
    const char *signature;
    size_t signature_len;
    size_t signing_len;
} JwsParts;

/**
 * @brief The verifier's handle - what JwtNs points at.
 *
 * @var JwtInternal::ns  the handle a caller sets a call's members on
 */
struct JwtInternal
{
    JwtNs *ns;
};

static struct JwtInternal s_jwt = {.ns = &Jwt};

// Split on the two periods of the compact serialization (RFC 7515 sec 7.1). A third period is a
// different serialization and fails here.
static proto_bool jws_split(const char *jws, size_t jws_len, JwsParts *parts)
{
    const char *d1 = (const char *)mem.chr(jws, jws_len, (uint8_t)'.');
    if (!d1)
    {
        return PROTO_FALSE;
    }
    const size_t rem1 = jws_len - (size_t)(d1 + 1 - jws);
    const char *d2 = (const char *)mem.chr(d1 + 1, rem1, (uint8_t)'.');
    if (!d2)
    {
        return PROTO_FALSE;
    }
    const size_t rem2 = jws_len - (size_t)(d2 + 1 - jws);
    if (mem.chr(d2 + 1, rem2, (uint8_t)'.'))
    {
        return PROTO_FALSE;
    }
    parts->header = jws;
    parts->header_len = (size_t)(d1 - jws);
    parts->payload = d1 + 1;
    parts->payload_len = (size_t)(d2 - (d1 + 1));
    parts->signature = d2 + 1;
    parts->signature_len = rem2;
    parts->signing_len = (size_t)(d2 - jws);
    return PROTO_TRUE;
}

// RFC 7515 sec 4.1.1: `alg` names the algorithm that secures the JWS, and RFC 8725 sec 3.1 requires
// the operation performed to be the one it names. Decode the header segment and demand HS256
// (RFC 7518 sec 3.1), which settles `none`, RS256 and every other substitution before the MAC runs.
static proto_bool alg_is_hs256(const char *header, size_t header_len)
{
    uint8_t buf[JWT_JOSE_HDR_CAP];
    const size_t n = Base64.url_decode(header, header_len, buf, sizeof(buf) - 1u);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    buf[n] = '\0';

    const char *p = str.find((const char *)buf, n + 1u, "\"alg\"", sizeof("\"alg\""), PROTO_FALSE);
    if (!p)
    {
        return PROTO_FALSE;
    }
    p += sizeof("\"alg\"") - 1u;
    while (*p == ' ' || *p == ':' || *p == '\t')
    {
        p++;
    }
    if (*p != '"')
    {
        return PROTO_FALSE;
    }
    p++;
    // The value plus its closing quote, compared against what is left of the decoded header.
    const size_t left = n - (size_t)(p - (const char *)buf);
    if (left < sizeof("HS256\"") - 1u)
    {
        return PROTO_FALSE;
    }
    return mem.cmp(p, "HS256\"", sizeof("HS256\"") - 1u) == 0;
}

// RFC 6750 sec 2.1: credentials = "Bearer" 1*SP b64token. The scheme name is a case-insensitive
// token (RFC 7235 sec 2.1), so the compare folds case. The length is measured first, which is what
// makes the compare's read bound a promise the value keeps.
static const char *bearer_token(const char *credentials)
{
    if (!credentials || str.len(credentials, sizeof("Bearer ")) < sizeof("Bearer ") - 1u)
    {
        return NULL;
    }
    if (!str.starts(credentials, "Bearer ", sizeof("Bearer "), PROTO_TRUE))
    {
        return NULL;
    }
    const char *tok = credentials + sizeof("Bearer ") - 1u;
    while (*tok == ' ')
    {
        tok++;
    }
    return tok;
}

// Decode the JWS Payload into @p buf and return the first character of claim @p name's value, or
// NULL when the token is malformed or the claim is absent (RFC 7519 sec 4: the claims are the
// members of the JSON object the payload carries).
static const char *claim_value(const char *jws, size_t jws_len, const char *name, uint8_t *buf, size_t buf_cap)
{
    if (!jws || !name)
    {
        return NULL;
    }
    JwsParts parts;
    if (!jws_split(jws, jws_len, &parts))
    {
        return NULL;
    }
    const size_t n = Base64.url_decode(parts.payload, parts.payload_len, buf, buf_cap - 1u);
    if (n == 0)
    {
        return NULL;
    }
    buf[n] = '\0';

    char key[JWT_CLAIM_KEY_CAP];
    protocore_sb sb_key = {key, sizeof(key), 0, PROTO_TRUE};
    Sb.put(&sb_key, "\"");
    Sb.put(&sb_key, name);
    Sb.put(&sb_key, "\"");
    const size_t kn = Sb.finish(&sb_key); // 0 when the name did not fit its two quotes
    if (kn == 0)
    {
        return NULL;
    }
    const char *p = str.find((const char *)buf, n + 1u, key, kn + 1u, PROTO_FALSE);
    if (!p)
    {
        return NULL;
    }
    p += kn;
    while (*p == ' ' || *p == ':' || *p == '\t')
    {
        p++;
    }
    return p;
}

// The integer claim @p name, or false when the claim is absent or its value is not a number. Both
// claim reads and the time-claim check go through this one scan.
static proto_bool claim_num(const char *jws, size_t jws_len, const char *name, long *out)
{
    uint8_t buf[PROTOCORE_JWT_MAX_LEN];
    const char *v = claim_value(jws, jws_len, name, buf, sizeof(buf));
    if (!v)
    {
        return PROTO_FALSE;
    }
    const char *end = v;
    const long parsed = str.to_long(v, &end);
    if (end == v) // no digit converted
    {
        return PROTO_FALSE;
    }
    *out = parsed;
    return PROTO_TRUE;
}

// RFC 7515 sec 5.2: the JWS Signature is validated against the JWS Signing Input under the
// algorithm `alg` names. RFC 7518 sec 3.2: that MAC is HMAC-SHA-256 of the signing input under the
// shared key, and the comparison against the signature is constant time.
static void verify_mac(struct JwtInternal *restrict ctx)
{
    const char *jws = ctx->ns->token.jws;
    const size_t jws_len = ctx->ns->token.jws_len;
    ctx->ns->ok = PROTO_FALSE;
    if (!jws || jws_len < 5u || jws_len > PROTOCORE_JWT_MAX_LEN)
    {
        return;
    }
    // A key length with no key octets behind it would be hashed from a null address.
    if (!ctx->ns->key.secret && ctx->ns->key.secret_len)
    {
        return;
    }

    JwsParts parts;
    if (!jws_split(jws, jws_len, &parts))
    {
        return;
    }
    if (!alg_is_hs256(parts.header, parts.header_len))
    {
        return;
    }
    if (parts.signature_len != JWT_SIG_B64_LEN)
    {
        return;
    }

    uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
    // One borrow for this token's MAC, returned before the compare.
    const size_t mark = protocore_secure_mark();
    protocore_span ws = protocore_secure_span(PROTOCORE_HMAC_SHA256_BORROW, _Alignof(uint32_t));
    if (!span.ok(ws))
    {
        protocore_secure_release(mark);
        return;
    }
    HmacSha256.mac_args.key = ctx->ns->key.secret;
    HmacSha256.mac_args.key_len = ctx->ns->key.secret_len;
    HmacSha256.mac_args.data = (const uint8_t *)jws;
    HmacSha256.mac_args.len = parts.signing_len;
    HmacSha256.mac_args.out = mac;
    HmacSha256.mac(ws.buf);
    protocore_secure_release(mark);

    char computed[JWT_SIG_B64_CAP];
    // A 32-byte MAC is always 43 base64url characters, so this length test never fires.
    if (Base64.url_encode(mac, sizeof(mac), computed) != JWT_SIG_B64_LEN)
    {
        return;
    }
    ctx->ns->ok = protocore_ct_eq(computed, parts.signature, JWT_SIG_B64_LEN);
}

// The same validation on the b64token inside the Authorization field value (RFC 6750 sec 2.1).
static void verify_bearer(struct JwtInternal *restrict ctx)
{
    const char *tok = bearer_token(ctx->ns->token.credentials);
    if (!tok)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->ns->token.jws = tok;
    ctx->ns->token.jws_len = str.len(tok, PROTOCORE_JWT_MAX_LEN + 1u);
    verify_mac(ctx);
}

// RFC 7519 sec 4.1.4: at or after `exp` the token MUST NOT be accepted. Sec 4.1.5: before `nbf` it
// MUST NOT be accepted. Both are OPTIONAL, so an absent claim is not enforced, and both are given
// the small leeway those sections allow for clock skew. Subtracting rather than adding the leeway
// keeps a far-future NumericDate inside `long`, and each claim is tested for a positive NumericDate
// first, which keeps the subtraction's other operand inside it too: a date at or before the epoch
// is expired for `exp` and no constraint for `nbf`.
static void time_claims_valid(struct JwtInternal *restrict ctx)
{
    const char *jws = ctx->ns->token.jws;
    const size_t jws_len = ctx->ns->token.jws_len;
    const long now = ctx->ns->time.now;
    const long leeway_s = ctx->ns->time.leeway_s;
    ctx->ns->ok = PROTO_TRUE;
    if (!jws || now <= 0)
    {
        return; // no wall clock: neither claim can be judged, and the MAC is the gate
    }

    long exp = 0;
    if (claim_num(jws, jws_len, "exp", &exp) && (exp <= 0 || now - exp > leeway_s))
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }

    long nbf = 0;
    if (claim_num(jws, jws_len, "nbf", &nbf) && nbf > 0 && nbf - now > leeway_s)
    {
        ctx->ns->ok = PROTO_FALSE;
    }
}

// The MAC first, then the time claims: a token that fails either is not accepted.
static void verify_mac_at(struct JwtInternal *restrict ctx)
{
    verify_mac(ctx);
    if (!ctx->ns->ok)
    {
        return;
    }
    time_claims_valid(ctx);
}

static void verify_bearer_at(struct JwtInternal *restrict ctx)
{
    verify_bearer(ctx);
    if (!ctx->ns->ok)
    {
        return;
    }
    time_claims_valid(ctx);
}

static void claim_int(struct JwtInternal *restrict ctx)
{
    long v = 0;
    ctx->ns->ok = claim_num(ctx->ns->token.jws, ctx->ns->token.jws_len, ctx->ns->claim.name, &v);
    if (ctx->ns->ok)
    {
        ctx->ns->num = v;
    }
}

// The claim's value copied out as text: a backslash is dropped and the character after it taken
// literally.
static void claim_str(struct JwtInternal *restrict ctx)
{
    char *out = ctx->ns->claim.out;
    const size_t out_cap = ctx->ns->claim.out_cap;
    ctx->ns->ok = PROTO_FALSE;
    if (!out || out_cap == 0)
    {
        return;
    }
    out[0] = '\0';

    uint8_t buf[PROTOCORE_JWT_MAX_LEN];
    const char *p = claim_value(ctx->ns->token.jws, ctx->ns->token.jws_len, ctx->ns->claim.name, buf, sizeof(buf));
    if (!p || *p != '"') // absent, or not a string-valued claim
    {
        return;
    }
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1u < out_cap)
    {
        if (*p == '\\' && p[1])
        {
            p++;
        }
        out[i++] = *p++;
    }
    if (*p != '"') // unterminated, or longer than out_cap
    {
        out[0] = '\0';
        return;
    }
    out[i] = '\0';
    ctx->ns->ok = PROTO_TRUE;
}

// RFC 6749 sec 3.3: a scope is a list of space-delimited, case-sensitive strings, which is the
// syntax RFC 8693 sec 4.2 gives the `scope` claim. The match is on a whole token, so a prefix of one
// never passes.
static void scope_allows(struct JwtInternal *restrict ctx)
{
    const char *claim = ctx->ns->scope.claim;
    const char *required = ctx->ns->scope.required;
    ctx->ns->ok = PROTO_FALSE;
    if (!claim || !required || !*required)
    {
        return;
    }
    const size_t rlen = str.len(required, PROTOCORE_JWT_MAX_LEN + 1u);
    const char *p = claim;
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
        if ((size_t)(p - start) == rlen && mem.cmp(start, required, rlen) == 0)
        {
            ctx->ns->ok = PROTO_TRUE;
            return;
        }
    }
}

// Designated, so a member's position in the struct does not decide what it binds to.
JwtNs Jwt = {.verify_mac = verify_mac,
             .verify_bearer = verify_bearer,
             .time_claims_valid = time_claims_valid,
             .verify_mac_at = verify_mac_at,
             .verify_bearer_at = verify_bearer_at,
             .claim_int = claim_int,
             .claim_str = claim_str,
             .scope_allows = scope_allows,
             .internal = &s_jwt};

#endif // PROTOCORE_ENABLE_JWT
