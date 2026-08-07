// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "crypto/ct_eq.h"       // pc_ct_eq
#include "crypto/hash/sha256.h" // pc_sha256, PC_SHA256_DIGEST_LEN (Digest)
#include "mmgr/membuild.h"      // pc_sb frame builder
#include "mmgr/protomem.h"      // mem.chr: a span scan, for the decoded credential that carries NULs
#include "mmgr/protostr.h"      // str.len / find / starts / eq / copy
#include "mmgr/secure.h"        // the credential table is key material
#include "network_drivers/presentation/codec/base64/base64.h" // Base64.decode (Basic)
#include "network_drivers/presentation/http/http.h"
#include "network_drivers/transport/tcp.h" // conn_pool, Tcp.conn->send, TcpConn/ConnState
#include "protocore.h"
#include "server/clock/clock.h"    // pc_millis() for the stateless nonce
#include "shared_primitives/hex.h" // pc_hex_encode/decode

// ---------------------------------------------------------------------------
// Basic Auth helpers
// ---------------------------------------------------------------------------

#if PC_ENABLE_AUTH
// One-shot SHA-256 of @p data, written as 64 lowercase hex chars + NUL.
static void sha256_hex(const uint8_t *data, size_t len, char out[65])
{
    uint8_t d[PC_SHA256_DIGEST_LEN];
    pc_sha256(data, len, d);
    pc_hex_encode(d, PC_SHA256_DIGEST_LEN, out, PROTO_FALSE);
}

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
        proto_raw_read(out, vs, vlen);
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
typedef struct
{
    uint8_t digest_secret[16];
    AuthCred cred[MAX_ROUTES];
    uint8_t count;
} AuthCtx;
static AuthCtx *s_auth;

_Static_assert(sizeof(AuthCtx) <= PC_WORK_AUTH_TABLE, "credential table outgrew PC_WORK_AUTH_TABLE");

// Bound on first use rather than at an init the caller has to remember: registration and the
// first rekey both arrive before any request is served, and either one is a fine moment. The borrow
// is from the persistent end, which no mark walks and no release reclaims, and it comes back zeroed.
static AuthCtx *bind_auth(void)
{
    if (s_auth == NULL)
    {
        pc_span sp = pc_secure_persist_span(sizeof(AuthCtx));
        if (pc_span_ok(sp))
        {
            s_auth = (AuthCtx *)sp.buf;
        }
    }
    return s_auth;
}

// The set @p id names, or NULL when it names nothing - an unregistered id, or a pool that could
// not be borrowed. Every caller fails closed on NULL.
static const AuthCred *cred_at(uint8_t id)
{
    const AuthCtx *a = s_auth;
    if (a == NULL || id >= a->count)
    {
        return NULL;
    }
    return &a->cred[id];
}

// Record one credential set and return the id that names it, or PC_AUTH_NONE when the table is
// full. Registration runs at setup, so there is no release path and none is offered.
static uint8_t add(const char *realm, const char *user, const char *pass, proto_bool digest)
{
    AuthCtx *a = bind_auth();
    if (a == NULL || a->count >= MAX_ROUTES)
    {
        return PC_AUTH_NONE;
    }
    AuthCred *c = &a->cred[a->count];
    (void)str.copy(c->realm, realm, MAX_AUTH_LEN);
    (void)str.copy(c->user, user, MAX_AUTH_LEN);
    (void)str.copy(c->pass, pass, MAX_AUTH_LEN);
    c->digest = digest;
    return a->count++;
}

static void rekey(void)
{
    AuthCtx *a = bind_auth();
    if (a == NULL)
    {
        return;
    }
    // Seed a 128-bit keying secret from the hardware CSPRNG (pc_platform_rand_u32() on
    // ESP32; a non-crypto mock on native test builds), folded through SHA-256 with
    // a counter + millis() so even a weak host RNG yields distinct values across
    // calls. The secret keys every timestamped nonce this server issues; it lives
    // only in BSS and is never sent on the wire.
    static uint32_t counter = 0;
    counter++;
    uint8_t seed[24];
    for (int i = 0; i < 4; i++)
    {
        uint32_t r = pc_platform_rand_u32();
        proto_raw_put_u32(seed + i * 4, r);
    }
    uint32_t c = counter;
    uint32_t t = (uint32_t)pc_millis();
    proto_raw_put_u32(seed + 16, c);
    proto_raw_put_u32(seed + 20, t);
    uint8_t d[PC_SHA256_DIGEST_LEN];
    pc_sha256(seed, sizeof(seed), d);
    proto_raw_read(a->digest_secret, d, sizeof(a->digest_secret)); // first 128 bits
}

// Stateless Digest nonce (RFC 7616 3.3): "<issue_ms_hex>.<mac_hex>" where the MAC
// is SHA-256(secret || issue_ms) truncated to 128 bits. The server holds no
// per-nonce state - it recomputes the MAC to authenticate a returned nonce and
// reads the embedded issue time to age it - so the scheme is safe under the
// shared-nothing worker model (the secret is set once at begin(), read-only after).
static uint32_t digest_nonce_mac(const uint8_t *secret, uint32_t issue, char *mac_hex)
{
    uint8_t material[20];
    proto_raw_read(material, secret, 16);
    proto_raw_put_u32(material + 16, issue); // endian-symmetric: minted and verified the same way
    uint8_t d[PC_SHA256_DIGEST_LEN];
    pc_sha256(material, sizeof(material), d);
    pc_hex_encode(d, 16, mac_hex, PROTO_FALSE); // 16 bytes -> 32 hex chars + NUL
    return issue;
}

static void mint_nonce(char *out, size_t cap)
{
    if (bind_auth() == NULL)
    {
        out[0] = '\0';
        return;
    }
    uint32_t issue = pc_millis();
    char issue_hex[9];
    pc_hex_encode((const uint8_t *)&issue, 4, issue_hex, PROTO_FALSE); // 4 bytes -> 8 hex chars
    char mac_hex[33];
    digest_nonce_mac(s_auth->digest_secret, issue, mac_hex);
    pc_sb sb_out = {out, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_out, issue_hex);
    pc_sb_put(&sb_out, ".");
    pc_sb_put(&sb_out, mac_hex);
    if (pc_sb_finish(&sb_out) == 0)
    {
        out[0] = '\0';
    }
}

static proto_bool verify_nonce(const char *nonce, proto_bool *expired)
{
    *expired = PROTO_FALSE;
    if (bind_auth() == NULL)
    {
        return PROTO_FALSE;
    }
    // Expected shape: 8 hex (issue) + '.' + 32 hex (MAC).
    if (str.len(nonce, 42) != 8 + 1 + 32 || nonce[8] != '.')
    {
        return PROTO_FALSE;
    }
    uint32_t issue;
    if (pc_hex_decode(nonce, 8, (uint8_t *)&issue, 4) != 4)
    {
        return PROTO_FALSE;
    }
    char mac_hex[33];
    digest_nonce_mac(s_auth->digest_secret, issue, mac_hex);
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
        return PROTO_FALSE; // not a nonce this server minted
    }
    uint32_t age = pc_millis() - issue; // unsigned: tolerant of the 32-bit millis wrap
    *expired = (age > PC_DIGEST_NONCE_LIFETIME_MS);
    return PROTO_TRUE;
}

static void challenge(uint8_t slot_id, uint8_t id, proto_bool stale)
{
    const AuthCred *c = cred_at(id);
    if (c == NULL)
    {
        http_reset(slot_id);
        return;
    }
    if (!pc_conn_active(slot_id))
    {
        http_reset(slot_id);
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
        mint_nonce(nonce, sizeof(nonce)); // a fresh, timestamped nonce per challenge
        pc_sb sb_challenge = {challenge, sizeof(challenge), 0, PROTO_TRUE};
        pc_sb_put(&sb_challenge, "WWW-Authenticate: Digest realm=\"");
        pc_sb_put(&sb_challenge, c->realm);
        pc_sb_put(&sb_challenge, "\", qop=\"auth\", algorithm=SHA-256, nonce=\"");
        pc_sb_put(&sb_challenge, nonce);
        pc_sb_put(&sb_challenge, "\"");
        pc_sb_put(&sb_challenge, stale ? ", stale=true" : "");
        pc_sb_put(&sb_challenge, "\r\n");
        if (pc_sb_finish(&sb_challenge) == 0)
        {
            challenge[0] = '\0';
        }
    }
    else
    {
        pc_sb sb_challenge2 = {challenge, sizeof(challenge), 0, PROTO_TRUE};
        pc_sb_put(&sb_challenge2, "WWW-Authenticate: Basic realm=\"");
        pc_sb_put(&sb_challenge2, c->realm);
        pc_sb_put(&sb_challenge2, "\"\r\n");
        if (pc_sb_finish(&sb_challenge2) == 0)
        {
            challenge[0] = '\0';
        }
    }

    proto_bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    static const char body[] = "Unauthorized";
    char header[RESP_HDR_BUF_SIZE];
    pc_sb sb_header = {header, sizeof(header), 0, PROTO_TRUE};
    pc_sb_put(&sb_header, "HTTP/1.1 401 Unauthorized\r\n");
    pc_sb_put(&sb_header, challenge);
    pc_sb_put(&sb_header, "Content-Type: text/plain\r\nContent-Length: ");
    pc_sb_i64(&sb_header, (int64_t)((int)(sizeof(body) - 1)));
    pc_sb_put(&sb_header, "\r\n");
    pc_sb_put(&sb_header, pc_resp_cors_enabled() ? pc_resp_cors_header() : "");
    pc_sb_put(&sb_header, cl);
    pc_sb_put(&sb_header, "\r\n");
    int hlen = (int)pc_sb_finish(&sb_header);

    // The flush rides the final write, so the challenge leaves in one marshal whether or not a body
    // follows the header.
    if (!Http.req_is_head(slot_id))
    {
        Tcp.conn->send(slot_id, header, (proto_u16)hlen);
        Tcp.conn->send_flush(slot_id, body, (proto_u16)(sizeof(body) - 1));
    }
    else
    {
        Tcp.conn->send_flush(slot_id, header, (proto_u16)hlen);
    }

    pc_resp_end(slot_id, 401, (int)(sizeof(body) - 1), keep, /*pre_flushed=*/PROTO_TRUE);
}

static proto_bool check_basic(uint8_t slot_id, HttpReq *req, const AuthCred *c)
{
    (void)slot_id;
    const char *auth_hdr = http_get_header(req, "Authorization");
    if (!auth_hdr || !str.starts(auth_hdr, "Basic ", sizeof("Basic "), PROTO_FALSE))
    {
        return PROTO_FALSE;
    }

    uint8_t decoded[MAX_AUTH_LEN * 2 + 2];
    // Bound the write to leave room for the null terminator at decoded[n]; an
    // over-long Authorization value now fails the decode instead of overrunning.
    size_t n = Base64.decode(auth_hdr + 6, decoded, sizeof(decoded) - 1);
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
    proto_bool user_ok = (ulen == str.len(c->user, MAX_AUTH_LEN)) && pc_ct_eq(decoded, c->user, ulen);
    proto_bool pass_ok = (plen == str.len(c->pass, MAX_AUTH_LEN)) && pc_ct_eq(pass, c->pass, plen);
    return user_ok && pass_ok;
}

// Validate an Authorization: Digest header (RFC 7616, SHA-256, qop=auth).
// HA1 = SHA256(user:realm:pass), HA2 = SHA256(method:uri),
// response = SHA256(HA1:nonce:nc:cnonce:qop:HA2).
static proto_bool check_digest(uint8_t slot_id, HttpReq *req, const AuthCred *c, proto_bool *stale)
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
    const size_t dcap = PC_AUTH_HDR_CAP - 7u;

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
    proto_bool nonce_expired = PROTO_FALSE;
    if (!verify_nonce(nonce, &nonce_expired))
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
        pc_sb sb_target = {target, sizeof(target), 0, PROTO_TRUE};
        pc_sb_put(&sb_target, req->path);
        pc_sb_put(&sb_target, "?");
        pc_sb_put(&sb_target, req->query);
        if (pc_sb_finish(&sb_target) == 0)
        {
            target[0] = '\0';
        }
    }
    else
    {
        pc_sb sb_target2 = {target, sizeof(target), 0, PROTO_TRUE};
        pc_sb_put(&sb_target2, req->path);
        if (pc_sb_finish(&sb_target2) == 0)
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

    pc_sb sb_tmp = {tmp, sizeof(tmp), 0, PROTO_TRUE};
    pc_sb_put(&sb_tmp, c->user);
    pc_sb_put(&sb_tmp, ":");
    pc_sb_put(&sb_tmp, c->realm);
    pc_sb_put(&sb_tmp, ":");
    pc_sb_put(&sb_tmp, c->pass);
    int n = (int)pc_sb_finish(&sb_tmp);
    sha256_hex((const uint8_t *)tmp, (size_t)n, ha1);

    char tmp2[sizeof(uri) + 16];
    pc_sb sb_tmp2 = {tmp2, sizeof(tmp2), 0, PROTO_TRUE};
    pc_sb_put(&sb_tmp2, req->method);
    pc_sb_put(&sb_tmp2, ":");
    pc_sb_put(&sb_tmp2, uri);
    n = (int)pc_sb_finish(&sb_tmp2);
    sha256_hex((const uint8_t *)tmp2, (size_t)n, ha2);

    char tmp3[65 + 48 + 16 + 64 + 8 + 65 + 8];
    pc_sb sb_tmp3 = {tmp3, sizeof(tmp3), 0, PROTO_TRUE};
    pc_sb_put(&sb_tmp3, ha1);
    pc_sb_put(&sb_tmp3, ":");
    pc_sb_put(&sb_tmp3, nonce);
    pc_sb_put(&sb_tmp3, ":");
    pc_sb_put(&sb_tmp3, nc);
    pc_sb_put(&sb_tmp3, ":");
    pc_sb_put(&sb_tmp3, cnonce);
    pc_sb_put(&sb_tmp3, ":");
    pc_sb_put(&sb_tmp3, qop);
    pc_sb_put(&sb_tmp3, ":");
    pc_sb_put(&sb_tmp3, ha2);
    n = (int)pc_sb_finish(&sb_tmp3);
    sha256_hex((const uint8_t *)tmp3, (size_t)n, expected);

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
static proto_bool check(uint8_t slot_id, HttpReq *req, uint8_t id, proto_bool *stale)
{
    const AuthCred *c = cred_at(id);
    if (c == NULL)
    {
        return PROTO_FALSE;
    }
    if (c->digest)
    {
        return check_digest(slot_id, req, c, stale);
    }
    return check_basic(slot_id, req, c);
}

// The count is the table, and a row is wiped on hand-out, so nothing below the count can carry a
// previous tenant's credential and there is nothing to wipe here. The keying secret survives: it is
// the server's, not a route's, and rekey() is what replaces it.
static void reset(void)
{
    if (s_auth != NULL)
    {
        s_auth->count = 0;
    }
}

const AuthNs Auth = {add, check, challenge, rekey, mint_nonce, verify_nonce, reset};

#endif // PC_ENABLE_AUTH
