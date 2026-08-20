// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file auth.c
 * @brief HTTP authentication for PC: Basic (RFC 7617) and stateless Digest
 *        (RFC 7616, SHA-256, qop=auth).
 *
 * The Basic credential check, the Digest field parser, the keyed stateless-nonce mint/verify (no
 * per-nonce server state), and the 401 challenge builder. The route dispatcher calls these when a
 * matched route carries auth.
 */

#include "crypto/ct_eq.h"              // protocore_ct_eq
#include "crypto/hash/sha256/sha256.h" // protocore_sha256, PROTOCORE_SHA256_DIGEST_LEN (Digest)
#include "mmgr/membuild/membuild.h"    // protocore_sb frame builder
#include "mmgr/protomem/protomem.h"    // mem.chr: a span scan, for the decoded credential that carries NULs
#include "mmgr/protostr/protostr.h"    // str.len / find / starts / eq / copy
#include "mmgr/secure/secure.h"        // the credential table is key material
#include "network_drivers/presentation/codec/base64/base64.h" // Base64.decode (Basic)
#include "network_drivers/presentation/http/http.h"
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot a challenge writes on
#include "protocore.h"
#include "server/clock/clock.h" // protocore_millis() for the stateless nonce
#include "shared/hex/hex.h"     // protocore_hex_encode/decode

static uint8_t base64_work[16]; // the borrow an entry takes; Base64 never reads it

static uint8_t hex_work[16]; // the borrow an entry takes; Hex never reads it

// ---------------------------------------------------------------------------
// Basic Auth helpers
// ---------------------------------------------------------------------------

#if PROTOCORE_ENABLE_AUTH
// Extract the value of @p key from a Digest auth header into @p out.
// Handles both quoted ("value") and token (value) forms. The match must sit on
// a field boundary (start, or after ' '/',') and be immediately followed by '='
// so "nc" does not match inside "cnonce", etc.
static proto_bool digest_field(const char *hdr, size_t hdr_cap, const char *key, char *out, size_t out_size)
{
    // Every step is measured against `end` rather than against the capacity: a remaining length
    // computed as cap-minus-offset underflows the moment a cursor passes the end, and an unsigned
    // underflow hands the search a bound of nearly the whole address space.
    const size_t klen = str.len(key, 32);
    const char *const end = hdr + hdr_cap;
    const char *p = hdr;

    while (p < end)
    {
        p = str.find(p, (size_t)(end - p), key, klen + 1u, PROTO_FALSE);
        if (p == NULL)
        {
            return PROTO_FALSE;
        }
        const char *after = p + klen;
        if (after >= end)
        {
            return PROTO_FALSE;
        }
        proto_bool left_ok = (p == hdr) || p[-1] == ' ' || p[-1] == ',';
        if (!left_ok || *after != '=')
        {
            p = after;
            continue;
        }
        after++;
        if (after >= end)
        {
            return PROTO_FALSE;
        }
        const char *vs;
        const char *ve;
        if (*after == '"')
        {
            vs = after + 1;
            if (vs >= end)
            {
                return PROTO_FALSE;
            }
            ve = str.find(vs, (size_t)(end - vs), "\"", 2u, PROTO_FALSE);
            if (!ve)
            {
                return PROTO_FALSE;
            }
        }
        else
        {
            vs = after;
            ve = vs;
            while (ve < end && *ve && *ve != ',' && *ve != ' ')
            {
                ve++;
            }
        }
        size_t vlen = (size_t)(ve - vs);
        if (vlen > out_size - 1)
        {
            vlen = out_size - 1;
        }
        raw.read(out, vs, vlen);
        out[vlen] = '\0';
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// One credential set: what a challenge announces and what a submitted credential is checked
// against. `digest` is the scheme the set was registered for, so nothing above this file has to
// know which of the two checks applies.
typedef struct
{
    char realm[MAX_AUTH_LEN];
    char user[MAX_AUTH_LEN];
    char pass[MAX_AUTH_LEN];
    proto_bool digest;
} AuthCred;

// The module's storage: the keying secret and every credential set. It is borrowed from the
// secure pool rather than declared, because these are the bytes an attacker wants and that pool
// is the one that wipes on release. One table serves every route, so a route slot carries an id
// rather than a copy of the credential.
struct AuthStorage
{
    uint8_t digest_secret[16]; ///< the Digest keying secret; never leaves this file
    AuthCred cred[MAX_ROUTES]; ///< every credential set, indexed by the id a route carries
    uint8_t count;             ///< how many rows are recorded
};

// The caller's borrow, split: the credential table, then the bytes SHA-256 runs out of. The table
// persists between calls and the hash scratch does not, so they are two regions of one span rather
// than two borrows - the worst case over everything an entry here reaches, taken once.
#define AUTH_OFF_TABLE 0u
#define AUTH_OFF_SHA (AUTH_OFF_TABLE + sizeof(struct AuthStorage))
static_assert(AUTH_OFF_SHA + PROTOCORE_SHA256_BORROW <= PROTOCORE_HTTP_AUTH_BORROW,
              "PROTOCORE_HTTP_AUTH_BORROW must cover the credential table and the SHA-256 borrow behind it: raise"
              " it in protocore_config.h, which sums it into its arena");

// The regions, at their offsets in the caller's borrow.
#define AUTH_TABLE(w) ((struct AuthStorage *)(void *)((w) + AUTH_OFF_TABLE))
#define AUTH_SHA(w) ((w) + AUTH_OFF_SHA)

// One-shot SHA-256 of @p data, written as 64 lowercase hex chars + NUL. Takes the module span: the
// hash runs on its own region of it.
static void sha256_hex(uint8_t *work, const uint8_t *data, size_t len, char out[65])
{
    uint8_t d[PROTOCORE_SHA256_DIGEST_LEN];
    Sha256.hash_args.data = data;
    Sha256.hash_args.len = len;
    Sha256.hash_args.out = d;
    Sha256.hash(AUTH_SHA(work));
    Hex.io.in = d;
    Hex.io.n = PROTOCORE_SHA256_DIGEST_LEN;
    Hex.io.out = out;
    Hex.args.upper = PROTO_FALSE;
    Hex.encode(hex_work);
}

// The one owned instance, private to this TU: the pointer to the bytes taken for the table.
static uint8_t *s_span;

// Not an entry: an entry takes a borrow and this is where that borrow comes from. Registration, the
// rekey and every request read one table, so the bytes are the module's rather than any one
// caller's. Taken from the persistent end, which no mark walks and no release reclaims, and it comes
// back zeroed.
uint8_t *protocore_http_auth_span(void)
{
    if (s_span == NULL)
    {
        s_span = protocore_secure_persist_span(PROTOCORE_HTTP_AUTH_BORROW).buf;
    }
    return s_span;
}

// The set ns->id names, or NULL when it names nothing - an unregistered id, or a pool that could
// not be borrowed. Every caller fails closed on NULL.
static const AuthCred *cred_at(uint8_t *work, uint8_t id)
{
    const struct AuthStorage *a = AUTH_TABLE(work);
    if (id >= a->count)
    {
        return NULL;
    }
    return &a->cred[id];
}

// Record one credential set and return the id that names it, or PROTOCORE_AUTH_NONE when the table is
// full. Registration runs at setup, so there is no release path and none is offered.
static void add(uint8_t *restrict work)
{
    struct AuthStorage *a = AUTH_TABLE(work);
    if (a->count >= MAX_ROUTES)
    {
        Auth.u8 = PROTOCORE_AUTH_NONE;
        return;
    }
    AuthCred *c = &a->cred[a->count];
    (void)str.copy(c->realm, Auth.cred.realm, MAX_AUTH_LEN);
    (void)str.copy(c->user, Auth.cred.user, MAX_AUTH_LEN);
    (void)str.copy(c->pass, Auth.cred.pass, MAX_AUTH_LEN);
    c->digest = Auth.cred.digest;
    Auth.u8 = a->count++;
}

static void rekey(uint8_t *restrict work)
{
    struct AuthStorage *a = AUTH_TABLE(work);
    // Seed a 128-bit keying secret from the hardware CSPRNG (protocore_platform_rand_u32() on
    // ESP32; a non-crypto mock on native test builds), folded through SHA-256 with
    // a counter + millis() so even a weak host RNG yields distinct values across
    // calls. The secret keys every timestamped nonce this server issues; it lives
    // only in BSS and is never sent on the wire.
    static uint32_t counter = 0;
    counter++;
    uint8_t seed[24];
    for (int i = 0; i < 4; i++)
    {
        uint32_t r = protocore_platform_rand_u32();
        raw.put_u32(seed + i * 4, r);
    }
    uint32_t c = counter;
    uint32_t t = (uint32_t)Clock.ms;
    raw.put_u32(seed + 16, c);
    raw.put_u32(seed + 20, t);
    uint8_t d[PROTOCORE_SHA256_DIGEST_LEN];
    Sha256.hash_args.data = seed;
    Sha256.hash_args.len = sizeof(seed);
    Sha256.hash_args.out = d;
    Sha256.hash(AUTH_SHA(work));
    raw.read(a->digest_secret, d, sizeof(a->digest_secret)); // first 128 bits
}

// Stateless Digest nonce (RFC 7616 3.3): "<issue_ms_hex>.<mac_hex>" where the MAC
// is SHA-256(secret || issue_ms) truncated to 128 bits. The server holds no
// per-nonce state - it recomputes the MAC to authenticate a returned nonce and
// reads the embedded issue time to age it - so the scheme is safe under the
// shared-nothing worker model (the secret is set once at begin(), read-only after).
static uint32_t digest_nonce_mac(uint8_t *work, const uint8_t *secret, uint32_t issue, char *mac_hex)
{
    uint8_t material[20];
    raw.read(material, secret, 16);
    raw.put_u32(material + 16, issue); // endian-symmetric: minted and verified the same way
    uint8_t d[PROTOCORE_SHA256_DIGEST_LEN];
    Sha256.hash_args.data = material;
    Sha256.hash_args.len = sizeof(material);
    Sha256.hash_args.out = d;
    Sha256.hash(AUTH_SHA(work));
    Hex.io.in = d;
    Hex.io.n = 16;
    Hex.io.out = mac_hex;
    Hex.args.upper = PROTO_FALSE;
    Hex.encode(hex_work); // 16 bytes -> 32 hex chars + NUL
    return issue;
}

static void mint_nonce(uint8_t *restrict work)
{
    char *out = Auth.nonce_args.out;
    const size_t cap = Auth.nonce_args.cap;
    uint32_t issue = Clock.ms;
    char issue_hex[9];
    Hex.io.in = (const uint8_t *)&issue;
    Hex.io.n = 4;
    Hex.io.out = issue_hex;
    Hex.args.upper = PROTO_FALSE;
    Hex.encode(hex_work); // 4 bytes -> 8 hex chars
    char mac_hex[33];
    digest_nonce_mac(work, AUTH_TABLE(work)->digest_secret, issue, mac_hex);
    protocore_sb sb_out = {out, cap, 0, PROTO_TRUE};
    Sb.put(&sb_out, issue_hex);
    Sb.put(&sb_out, ".");
    Sb.put(&sb_out, mac_hex);
    if (Sb.finish(&sb_out) == 0)
    {
        out[0] = '\0';
    }
}

static void verify_nonce(uint8_t *restrict work)
{
    const char *nonce = Auth.nonce_args.nonce;
    Auth.expired = PROTO_FALSE;
    Auth.ok = PROTO_FALSE;
    // Expected shape: 8 hex (issue) + '.' + 32 hex (MAC).
    if (str.len(nonce, 42) != 8 + 1 + 32 || nonce[8] != '.')
    {
        return;
    }
    uint32_t issue;
    Hex.io.text = nonce;
    Hex.io.n = 8;
    Hex.io.bytes = (uint8_t *)&issue;
    Hex.io.cap = 4;
    Hex.decode(hex_work);
    if (Hex.i32 != 4)
    {
        return;
    }
    char mac_hex[33];
    digest_nonce_mac(work, AUTH_TABLE(work)->digest_secret, issue, mac_hex);
    // Constant-time compare of the 32 MAC hex chars: a forged nonce never reveals
    // how many leading characters matched.
    const char *got = nonce + 9;
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++)
    {
        diff |= (uint8_t)(mac_hex[i] ^ got[i]);
    }
    if (diff != 0)
    {
        return; // not a nonce this server minted
    }
    uint32_t age = Clock.ms - issue; // unsigned: tolerant of the 32-bit millis wrap
    Auth.expired = (age > PROTOCORE_DIGEST_NONCE_LIFETIME_MS);
    Auth.ok = PROTO_TRUE;
}

static void challenge(uint8_t *restrict work)
{
    const uint8_t slot_id = Auth.slot;
    const proto_bool stale = Auth.nonce_args.stale;
    const AuthCred *c = cred_at(work, Auth.id);
    if (c == NULL)
    {
        HttpConn.slot = slot_id;
        HttpConn.reset(protocore_http_conn_span());
        return;
    }
    ConnPool.slot = slot_id;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPool.ok)
    {
        HttpConn.slot = slot_id;
        HttpConn.reset(protocore_http_conn_span());
        return;
    }

    // Sized for the worst-case Digest challenge without truncation: the fixed field text (~76) + a
    // max-length realm (MAX_AUTH_LEN-1) + the fixed 41-char nonce ("8hex.32hex") + ", stale=true" (12)
    // + NUL is ~161 bytes; MAX_AUTH_LEN + 160 clears that with margin. (A truncated WWW-Authenticate
    // would be a malformed challenge that breaks Digest auth - a real, if narrow, defect.)
    char challenge[MAX_AUTH_LEN + 160];
    if (c->digest)
    {
        char nonce[48];
        Auth.nonce_args.out = nonce;
        Auth.nonce_args.cap = sizeof(nonce);
        mint_nonce(work); // a fresh, timestamped nonce per challenge
        protocore_sb sb_challenge = {challenge, sizeof(challenge), 0, PROTO_TRUE};
        Sb.put(&sb_challenge, "WWW-Authenticate: Digest realm=\"");
        Sb.put(&sb_challenge, c->realm);
        Sb.put(&sb_challenge, "\", qop=\"auth\", algorithm=SHA-256, nonce=\"");
        Sb.put(&sb_challenge, nonce);
        Sb.put(&sb_challenge, "\"");
        Sb.put(&sb_challenge, stale ? ", stale=true" : "");
        Sb.put(&sb_challenge, "\r\n");
        if (Sb.finish(&sb_challenge) == 0)
        {
            challenge[0] = '\0';
        }
    }
    else
    {
        protocore_sb sb_challenge2 = {challenge, sizeof(challenge), 0, PROTO_TRUE};
        Sb.put(&sb_challenge2, "WWW-Authenticate: Basic realm=\"");
        Sb.put(&sb_challenge2, c->realm);
        Sb.put(&sb_challenge2, "\"\r\n");
        if (Sb.finish(&sb_challenge2) == 0)
        {
            challenge[0] = '\0';
        }
    }

    proto_bool keep;
    const char *cl = protocore_resp_conn_hdr(slot_id, &keep);

    static const char body[] = "Unauthorized";
    char header[RESP_HDR_BUF_SIZE];
    protocore_sb sb_header = {header, sizeof(header), 0, PROTO_TRUE};
    Sb.put(&sb_header, "HTTP/1.1 401 Unauthorized\r\n");
    Sb.put(&sb_header, challenge);
    Sb.put(&sb_header, "Content-Type: text/plain\r\nContent-Length: ");
    Sb.i64(&sb_header, (int64_t)((int)(sizeof(body) - 1)));
    Sb.put(&sb_header, "\r\n");
    Sb.put(&sb_header, protocore_resp_cors_enabled() ? protocore_resp_cors_header() : "");
    Sb.put(&sb_header, cl);
    Sb.put(&sb_header, "\r\n");
    int hlen = (int)Sb.finish(&sb_header);

    // The flush rides the final write, so the challenge leaves in one marshal whether or not a body
    // follows the header.
    Http.slot = slot_id;
    Http.req_is_head(protocore_http_span());
    if (!Http.ok)
    {
        ConnPool.slot = slot_id;
        ConnPool.io.data = header;
        ConnPool.io.len = (proto_u16)hlen;
        ConnPool.send(protocore_conn_pool_span());
        ConnPool.io.data = body;
        ConnPool.io.len = (proto_u16)(sizeof(body) - 1);
        ConnPool.send_flush(protocore_conn_pool_span());
    }
    else
    {
        ConnPool.slot = slot_id;
        ConnPool.io.data = header;
        ConnPool.io.len = (proto_u16)hlen;
        ConnPool.send_flush(protocore_conn_pool_span());
    }

    protocore_resp_end(slot_id, 401, (int)(sizeof(body) - 1), keep, /*pre_flushed=*/PROTO_TRUE);
}

static proto_bool check_basic(uint8_t slot_id, HttpReq *req, const AuthCred *c)
{
    (void)slot_id;
    HttpParser.get_header_args.req = req;
    HttpParser.get_header_args.key = "Authorization";
    HttpParser.get_header(protocore_http_parser_span());
    const char *auth_hdr = HttpParser.text;
    if (!auth_hdr || !str.starts(auth_hdr, "Basic ", sizeof("Basic "), PROTO_FALSE))
    {
        return PROTO_FALSE;
    }

    uint8_t decoded[MAX_AUTH_LEN * 2 + 2];
    // Bound the write to leave room for the null terminator at decoded[n]; an
    // over-long Authorization value now fails the decode instead of overrunning.
    Base64.decode_args.src = auth_hdr + 6;
    Base64.decode_args.dst = decoded;
    Base64.decode_args.dst_cap = sizeof(decoded) - 1;
    Base64.decode(base64_work);
    size_t n = Base64.n;
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    decoded[n] = '\0';

    const char *colon = (const char *)mem.chr(decoded, n, ':');
    if (!colon)
    {
        return PROTO_FALSE;
    }

    size_t ulen = (size_t)(colon - (const char *)decoded);
    const char *pass = colon + 1;
    size_t plen = n - (size_t)(pass - (const char *)decoded); // real password byte length (may hold NULs)

    // Length-bounded, constant-time compare of BOTH fields (never strcmp): an embedded NUL in the decoded
    // credential must not truncate the submitted password ("pass\0junk" must not equal "pass"), and the
    // byte compare must run to completion so it does not leak how many leading bytes matched.
    proto_bool user_ok = (ulen == str.len(c->user, MAX_AUTH_LEN)) && protocore_ct_eq(decoded, c->user, ulen);
    proto_bool pass_ok = (plen == str.len(c->pass, MAX_AUTH_LEN)) && protocore_ct_eq(pass, c->pass, plen);
    return user_ok && pass_ok;
}

// Validate an Authorization: Digest header (RFC 7616, SHA-256, qop=auth).
// HA1 = SHA256(user:realm:pass), HA2 = SHA256(method:uri),
// response = SHA256(HA1:nonce:nc:cnonce:qop:HA2).
static proto_bool check_digest(uint8_t *work, uint8_t slot_id, HttpReq *req, const AuthCred *c, proto_bool *stale)
{
    (void)slot_id;
    // Use the full-length Authorization capture (the scratch header value is
    // capped at MAX_VAL_LEN, far shorter than a Digest header).
    const char *hdr = req->authorization;
    if (!str.starts(hdr, "Digest ", sizeof("Digest "), PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    const char *d = hdr + 7;
    const size_t dcap = PROTOCORE_AUTH_HDR_CAP - 7u;

    char username[MAX_AUTH_LEN];
    char nonce[48];
    char uri[MAX_PATH_LEN + MAX_QUERY_LEN + 2];
    char qop[16];
    char nc[16];
    char cnonce[64];
    char response[80];

    if (!digest_field(d, dcap, "username", username, sizeof(username)) ||
        !digest_field(d, dcap, "nonce", nonce, sizeof(nonce)) || !digest_field(d, dcap, "uri", uri, sizeof(uri)) ||
        !digest_field(d, dcap, "qop", qop, sizeof(qop)) || !digest_field(d, dcap, "nc", nc, sizeof(nc)) ||
        !digest_field(d, dcap, "cnonce", cnonce, sizeof(cnonce)) ||
        !digest_field(d, dcap, "response", response, sizeof(response)))
    {
        return PROTO_FALSE;
    }

    // Identity + challenge binding must match before any hashing.
    if (!str.eq(username, c->user, sizeof(username), PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    // The nonce must be one this server minted (authentic MAC). A stale (expired)
    // nonce is still authentic - we finish the credential check below and let the
    // caller reissue with stale=true rather than rejecting outright (RFC 7616 3.3).
    Auth.nonce_args.nonce = nonce;
    verify_nonce(work);
    const proto_bool nonce_expired = Auth.expired;
    if (!Auth.ok)
    {
        return PROTO_FALSE;
    }
    if (!str.eq(qop, "auth", sizeof("auth"), PROTO_FALSE))
    {
        return PROTO_FALSE;
    }

    // RFC 7616 3.4: the resource named by the "uri" parameter MUST be the same as the
    // request target; otherwise a Digest response captured for one route could be
    // replayed against another route under the same realm/nonce.
    char target[MAX_PATH_LEN + MAX_QUERY_LEN + 2];
    if (req->query[0])
    {
        protocore_sb sb_target = {target, sizeof(target), 0, PROTO_TRUE};
        Sb.put(&sb_target, req->path);
        Sb.put(&sb_target, "?");
        Sb.put(&sb_target, req->query);
        if (Sb.finish(&sb_target) == 0)
        {
            target[0] = '\0';
        }
    }
    else
    {
        protocore_sb sb_target2 = {target, sizeof(target), 0, PROTO_TRUE};
        Sb.put(&sb_target2, req->path);
        if (Sb.finish(&sb_target2) == 0)
        {
            target[0] = '\0';
        }
    }
    if (!str.eq(uri, target, sizeof(uri), PROTO_FALSE))
    {
        return PROTO_FALSE;
    }

    char tmp[3 * MAX_AUTH_LEN + 4];
    char ha1[65];
    char ha2[65];
    char expected[65];

    protocore_sb sb_tmp = {tmp, sizeof(tmp), 0, PROTO_TRUE};
    Sb.put(&sb_tmp, c->user);
    Sb.put(&sb_tmp, ":");
    Sb.put(&sb_tmp, c->realm);
    Sb.put(&sb_tmp, ":");
    Sb.put(&sb_tmp, c->pass);
    int n = (int)Sb.finish(&sb_tmp);
    sha256_hex(work, (const uint8_t *)tmp, (size_t)n, ha1);

    char tmp2[sizeof(uri) + 16];
    protocore_sb sb_tmp2 = {tmp2, sizeof(tmp2), 0, PROTO_TRUE};
    Sb.put(&sb_tmp2, req->method);
    Sb.put(&sb_tmp2, ":");
    Sb.put(&sb_tmp2, uri);
    n = (int)Sb.finish(&sb_tmp2);
    sha256_hex(work, (const uint8_t *)tmp2, (size_t)n, ha2);

    char tmp3[65 + 48 + 16 + 64 + 8 + 65 + 8];
    protocore_sb sb_tmp3 = {tmp3, sizeof(tmp3), 0, PROTO_TRUE};
    Sb.put(&sb_tmp3, ha1);
    Sb.put(&sb_tmp3, ":");
    Sb.put(&sb_tmp3, nonce);
    Sb.put(&sb_tmp3, ":");
    Sb.put(&sb_tmp3, nc);
    Sb.put(&sb_tmp3, ":");
    Sb.put(&sb_tmp3, cnonce);
    Sb.put(&sb_tmp3, ":");
    Sb.put(&sb_tmp3, qop);
    Sb.put(&sb_tmp3, ":");
    Sb.put(&sb_tmp3, ha2);
    n = (int)Sb.finish(&sb_tmp3);
    sha256_hex(work, (const uint8_t *)tmp3, (size_t)n, expected);

    if (!str.eq(expected, response, sizeof(expected), PROTO_TRUE))
    {
        return PROTO_FALSE; // wrong credentials - leave *stale untouched (no transparent retry)
    }
    if (nonce_expired)
    {
        // Correct credentials but an aged nonce: signal a transparent retry so the
        // client recomputes against a fresh challenge without re-prompting the user.
        *stale = PROTO_TRUE;
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

// The scheme belongs to the credential, so the caller states which credential set applies and
// nothing above this file has to know whether that set is Basic or Digest.
static void check(uint8_t *restrict work)
{
    const AuthCred *c = cred_at(work, Auth.id);
    if (c == NULL)
    {
        Auth.ok = PROTO_FALSE;
        return;
    }
    if (c->digest)
    {
        Auth.ok = check_digest(work, Auth.slot, Auth.req, c, &Auth.nonce_args.stale);
        return;
    }
    Auth.ok = check_basic(Auth.slot, Auth.req, c);
}

// The count is the table, and a row is wiped on hand-out, so nothing below the count can carry a
// previous tenant's credential and there is nothing to wipe here. The keying secret survives: it is
// the server's, not a route's, and rekey() is what replaces it.
static void reset(uint8_t *restrict work)
{
    AUTH_TABLE(work)->count = 0;
}

// Designated, so a member's position in the struct does not decide what it binds to.
AuthNs Auth = {.add = add,
               .check = check,
               .challenge = challenge,
               .rekey = rekey,
               .mint_nonce = mint_nonce,
               .verify_nonce = verify_nonce,
               .reset = reset};

#endif // PROTOCORE_ENABLE_AUTH
