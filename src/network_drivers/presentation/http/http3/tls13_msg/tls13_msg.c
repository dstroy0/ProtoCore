// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_tls13_msg.c
 * @brief TLS 1.3 handshake messages for the QUIC handshake (see protocore_tls13_msg.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE)

#if PROTOCORE_ENABLE_PQC_KEX
#include "crypto/pqc/mlkem/mlkem.h" // MLKEM768_EK_BYTES (X25519MLKEM768 share sizing)
#endif
#include "crypto/asymmetric/ed25519/ed25519.h"
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h" // str.len: the selected ProtocolName's length
#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"

PROTOCORE_BEGIN_DECLS

// TLS extension types used here (RFC 8446 sec 4.2 + RFC 9001).
#define TLS_EXT_SERVER_NAME 0x0000
#define TLS_EXT_SUPPORTED_GROUPS 0x000a
#define TLS_EXT_SIGNATURE_ALGORITHMS 0x000d
#define TLS_EXT_ALPN 0x0010
#define TLS_EXT_CLIENT_CERTIFICATE_TYPE 0x0013 ///< RFC 7250 (IANA 19)
#define TLS_EXT_SERVER_CERTIFICATE_TYPE 0x0014 ///< RFC 7250 (IANA 20)
#define TLS_EXT_SUPPORTED_VERSIONS 0x002b
#define TLS_EXT_COOKIE 0x002c
#define TLS_EXT_KEY_SHARE 0x0033
#define TLS_EXT_CONNECTION_ID 0x0036 ///< RFC 9146 / RFC 9147 §9

// ---------------------------------------------------------------------------
// A minimal bounds-checked byte writer (back-patches length prefixes).
// ---------------------------------------------------------------------------
typedef struct
{
    uint8_t *buf;
    size_t cap;
    size_t pos;
    proto_bool ok;
} Writer;

static void w_u8(Writer *w, uint8_t v)
{
    // pos <= cap is an invariant (every write advances pos by exactly the checked amount), so the
    // comparison is written to avoid a size_t addition that could wrap (cpp:S3519).
    if (w->pos >= w->cap)
    {
        w->ok = PROTO_FALSE;
        return;
    }
    w->buf[w->pos++] = v;
}
static void w_u16(Writer *w, uint16_t v)
{
    w_u8(w, (uint8_t)(v >> 8));
    w_u8(w, (uint8_t)v);
}
static void w_u24(Writer *w, uint32_t v)
{
    w_u8(w, (uint8_t)(v >> 16));
    w_u8(w, (uint8_t)(v >> 8));
    w_u8(w, (uint8_t)v);
}
static void w_bytes(Writer *w, const uint8_t *b, size_t n)
{
    if (n == 0)
    {
        return;
    }
    // Direct pre-copy bound: pos must be within cap AND n must fit in the remaining cap - pos. Written as
    // two explicit guards (no intermediate) so the copy runs only when pos + n <= cap - no underflow of
    // cap - pos (guarded by pos <= cap) and no pos + n wrap (never formed).
    if (w->pos > w->cap || n > w->cap - w->pos)
    { // is an invariant and the first arm can never be taken
        w->ok = PROTO_FALSE;
        return;
    }
    mem.cpy(w->buf + w->pos, b, n);
    w->pos += n;
}
// Reserve a 2- or 3-byte length placeholder; returns its position for w_patch16/24.
static size_t w_mark(Writer *w, size_t nbytes)
{
    size_t at = w->pos;
    for (size_t i = 0; i < nbytes; i++)
    {
        w_u8(w, 0);
    }
    return at;
}
static void w_patch16(Writer *w, size_t at)
{
    if (!w->ok)
    {
        return;
    }
    uint16_t len = (uint16_t)(w->pos - at - 2);
    w->buf[at] = (uint8_t)(len >> 8);
    w->buf[at + 1] = (uint8_t)len;
}
static void w_patch24(Writer *w, size_t at)
{
    if (!w->ok)
    {
        return;
    }
    uint32_t len = (uint32_t)(w->pos - at - 3);
    w->buf[at] = (uint8_t)(len >> 16);
    w->buf[at + 1] = (uint8_t)(len >> 8);
    w->buf[at + 2] = (uint8_t)len;
}

// A bounds-checked reader.
typedef struct
{
    const uint8_t *buf;
    size_t len;
    size_t pos;
} Reader;
static proto_bool r_u8(Reader *r, uint8_t *v)
{
    if (r->pos + 1 > r->len)
    {
        return PROTO_FALSE;
    }
    *v = r->buf[r->pos++];
    return PROTO_TRUE;
}
static proto_bool r_u16(Reader *r, uint16_t *v)
{
    if (r->pos + 2 > r->len)
    {
        return PROTO_FALSE;
    }
    *v = (uint16_t)((r->buf[r->pos] << 8) | r->buf[r->pos + 1]);
    r->pos += 2;
    return PROTO_TRUE;
}
static proto_bool r_u24(Reader *r, uint32_t *v)
{
    if (r->pos + 3 > r->len)
    {
        return PROTO_FALSE;
    }
    *v = (uint32_t)((r->buf[r->pos] << 16) | (r->buf[r->pos + 1] << 8) | r->buf[r->pos + 2]);
    r->pos += 3;
    return PROTO_TRUE;
}
// Take a view of the next n bytes.
static proto_bool r_take(Reader *r, size_t n, const uint8_t **out)
{
    if (r->pos + n > r->len)
    {
        return PROTO_FALSE;
    }
    *out = r->buf + r->pos;
    r->pos += n;
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// ClientHello parsing
// ---------------------------------------------------------------------------
// Scan a list of 2-byte values for a target (used for versions/groups/sig algs).
static proto_bool list16_contains(const uint8_t *body, size_t body_len, size_t list_len, uint16_t target)
{
    if (list_len > body_len || (list_len % 2) != 0)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i + 1 < list_len; i += 2)
    {
        if (((body[i] << 8) | body[i + 1]) == target)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

// KEY_SHARE (RFC 8446 §4.2.8): 2-byte client_shares length, then KeyShareEntry { group(2), key_exchange<2> }.
static void parse_key_share(const uint8_t *body, size_t blen, Tls13ClientHello *out)
{
    if (blen < 2)
    {
        return;
    }
    size_t ll = (body[0] << 8) | body[1];
    if (ll + 2 > blen)
    {
        return;
    }
    size_t i = 2;
    size_t end = 2 + ll;
    while (i + 4 <= end)
    {
        uint16_t group = (uint16_t)((body[i] << 8) | body[i + 1]);
        uint16_t klen = (uint16_t)((body[i + 2] << 8) | body[i + 3]);
        i += 4;
        if (i + klen > end)
        {
            return;
        }
        if (group == TLS_GROUP_X25519 && klen == 32)
        {
            mem.cpy(out->client_x25519, body + i, 32);
            out->has_key_share = PROTO_TRUE;
        }
#if PROTOCORE_ENABLE_PQC_KEX
        else if (group == TLS_GROUP_X25519MLKEM768 && klen == MLKEM768_EK_BYTES + 32)
        {
            out->client_mlkem_ek = body + i;                               // ML-KEM-768 ek (first)
            mem.cpy(out->client_x25519, body + i + MLKEM768_EK_BYTES, 32); // X25519 (second)
            out->has_hybrid_share = PROTO_TRUE;
        }
#endif
        i += klen;
    }
}

// ALPN (RFC 7301): 2-byte list length, then entries of 1-byte name length + name. Flags an "h3" offer.
static void parse_alpn(const uint8_t *body, size_t blen, Tls13ClientHello *out)
{
    if (blen < 2)
    {
        return;
    }
    size_t ll = (body[0] << 8) | body[1];
    if (ll + 2 > blen)
    {
        return;
    }
    size_t i = 2;
    size_t end = 2 + ll;
    while (i + 1 <= end)
    {
        size_t nl = body[i++];
        if (i + nl > end)
        {
            return;
        }
        if (nl == 2 && body[i] == 'h' && body[i + 1] == '3')
        {
            out->offers_h3_alpn = PROTO_TRUE;
        }
        // The whole ProtocolNameList, kept so a server can select from it (RFC 7301 sec 3.1).
        out->alpn_list = body;
        out->alpn_list_len = end;
        i += nl;
    }
}

// SNI (RFC 6066 §3): ServerNameList of 2-byte length, then entries type(1), name<2>. Take the first host_name.
static void parse_server_name(const uint8_t *body, size_t blen, Tls13ClientHello *out)
{
    if (blen < 2)
    {
        return;
    }
    size_t ll = (body[0] << 8) | body[1];
    if (ll + 2 > blen)
    {
        return;
    }
    size_t i = 2;
    size_t end = 2 + ll;
    if (i + 3 > end)
    {
        return;
    }
    uint8_t nt = body[i++];
    size_t nl = (body[i] << 8) | body[i + 1];
    i += 2;
    if (nt == 0 && i + nl <= end)
    {
        out->sni = body + i;
        out->sni_len = nl;
    }
}

static void parse_extension(uint16_t type, const uint8_t *body, size_t blen, Tls13ClientHello *out, proto_bool dtls)
{
    switch (type)
    {
    case TLS_EXT_SUPPORTED_VERSIONS: {
        // 1-byte list length, then 2-byte versions. DTLS 1.3 advertises 0xFEFC, TLS 1.3 advertises 0x0304.
        if (blen < 1)
        {
            return;
        }
        size_t ll = body[0];
        out->offers_tls13 =
            list16_contains(body + 1, blen - 1, ll, dtls ? PROTOCORE_TLS_VERSION_DTLS_1_3 : TLS_VERSION_1_3);
        break;
    }
    case TLS_EXT_SUPPORTED_GROUPS: {
        if (blen < 2)
        {
            return;
        }
        size_t ll = (body[0] << 8) | body[1];
        out->offers_x25519 = list16_contains(body + 2, blen - 2, ll, TLS_GROUP_X25519);
#if PROTOCORE_ENABLE_PQC_KEX
        out->offers_x25519mlkem768 = list16_contains(body + 2, blen - 2, ll, TLS_GROUP_X25519MLKEM768);
#endif
        break;
    }
    case TLS_EXT_SIGNATURE_ALGORITHMS: {
        if (blen < 2)
        {
            return;
        }
        size_t ll = (body[0] << 8) | body[1];
        out->offers_ed25519 = list16_contains(body + 2, blen - 2, ll, TLS_SIG_ED25519);
        break;
    }
    case TLS_EXT_KEY_SHARE:
        parse_key_share(body, blen, out);
        break;
    case TLS_EXT_ALPN:
        parse_alpn(body, blen, out);
        break;
    case TLS_EXT_SERVER_CERTIFICATE_TYPE: {
        // server_certificate_type (RFC 7250 sec 4.2): a 1-byte list length then 1-byte CertificateType
        // values. The list is read whatever this build can answer with, because sec 4.2 outcome 2
        // turns on which types the client named, not on which of them we support.
        if (blen < 1)
        {
            return;
        }
        size_t ll = body[0];
        if (1 + ll > blen)
        {
            return;
        }
        out->has_server_cert_type = PROTO_TRUE;
        for (size_t i = 0; i < ll; i++)
        {
            if (body[1 + i] == TLS_CERT_TYPE_X509)
            {
                out->offers_x509_server_cert = PROTO_TRUE;
            }
#if PROTOCORE_ENABLE_TLS_RPK
            if (body[1 + i] == TLS_CERT_TYPE_RAW_PUBLIC_KEY)
            {
                out->offers_rpk_server_cert = PROTO_TRUE;
            }
#endif
        }
        break;
    }
    case TLS_EXT_QUIC_TRANSPORT_PARAMS:
        out->quic_tp = body;
        out->quic_tp_len = blen;
        break;
    case TLS_EXT_COOKIE: {
        // Cookie { opaque cookie<1..2^16-1> } (RFC 8446 §4.2.2): 2-byte length then the cookie bytes.
        if (blen < 2)
        {
            return;
        }
        size_t cl = (size_t)((body[0] << 8) | body[1]);
        if (cl + 2 > blen)
        {
            return;
        }
        out->cookie = body + 2;
        out->cookie_len = cl;
        break;
    }
    case TLS_EXT_CONNECTION_ID: {
        // ConnectionId { opaque cid<0..2^8-1> } (RFC 9146 §3): a 1-byte length then the CID the client
        // wants the server to place in records it sends to the client (an empty CID is legal).
        if (blen < 1)
        {
            return;
        }
        size_t cl = body[0];
        if (1 + cl > blen)
        {
            return;
        }
        out->has_conn_id = PROTO_TRUE;
        out->conn_id = body + 1;
        out->conn_id_len = cl;
        break;
    }
    case TLS_EXT_SERVER_NAME:
        parse_server_name(body, blen, out);
        break;
    default:
        break;
    }
}

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_tls13_msg_cert_verify_content(uint8_t *restrict work);

void protocore_tls13_msg_parse_client_hello(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *msg = Tls13MsgV.parse_client_hello_args.msg;
    size_t len = Tls13MsgV.parse_client_hello_args.len;
    Tls13ClientHello *out = Tls13MsgV.parse_client_hello_args.out;
    proto_bool dtls = Tls13MsgV.parse_client_hello_args.dtls;

    mem.zero(out, sizeof(*out));

    Reader r = {msg, len, 0};
    uint8_t type = 0;
    uint32_t body_len = 0;
    if (!r_u8(&r, &type) || type != TLS_HS_CLIENT_HELLO || !r_u24(&r, &body_len))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    // The handshake body must fit; trailing bytes past it are not part of this message.
    if (r.pos + body_len > len)
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    r.len = r.pos + body_len;

    uint16_t legacy_version = 0;
    const uint8_t *random = NULL;
    if (!r_u16(&r, &legacy_version) || !r_take(&r, 32, &random))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }

    uint8_t sid_len = 0;
    if (!r_u8(&r, &sid_len) || sid_len > 32)
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    if (!r_take(&r, sid_len, &out->session_id))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    out->session_id_len = sid_len;

    // DTLS ClientHello carries a legacy_cookie between session_id and cipher_suites (RFC 9147 §5.3);
    // it is zero-length in DTLS 1.3 but the field is always present. TLS/QUIC ClientHellos omit it.
    if (dtls)
    {
        uint8_t cookie_len = 0;
        const uint8_t *cookie = NULL;
        if (!r_u8(&r, &cookie_len) || !r_take(&r, cookie_len, &cookie))
        {
            Tls13MsgV.ok = PROTO_FALSE;
            return;
        }
        // sec 5.3: "A DTLS 1.3-only client MUST set the legacy_cookie field to zero length. If a
        // DTLS 1.3 ClientHello is received with any other value in this field, the server MUST
        // abort the handshake with an illegal_parameter alert." The cookie rides the extension.
        if (cookie_len != 0)
        {
            Tls13MsgV.ok = PROTO_FALSE;
            return;
        }
    }

    uint16_t cs_len = 0;
    const uint8_t *cs = NULL;
    if (!r_u16(&r, &cs_len) || (cs_len % 2) != 0 || !r_take(&r, cs_len, &cs))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    // RFC 8446 sec 4.1.3: the server selects its suite "from the list in ClientHello.cipher_suites",
    // so what the client offered has to reach the caller rather than being read past.
    out->offers_aes128gcm_sha256 = list16_contains(cs, cs_len, cs_len, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256);
    out->offers_aes256gcm_sha384 = list16_contains(cs, cs_len, cs_len, PROTOCORE_TLS_SUITE_AES_256_GCM_SHA384);

    uint8_t comp_len = 0;
    const uint8_t *comp = NULL;
    if (!r_u8(&r, &comp_len) || !r_take(&r, comp_len, &comp))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    // sec 4.1.2: legacy_compression_methods "MUST contain exactly one byte, set to zero", and a
    // TLS 1.3 server aborts on anything else.
    if (comp_len != 1 || comp[0] != 0)
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }

    // Extensions (a ClientHello for TLS 1.3 always has them).
    uint16_t ext_total = 0;
    if (!r_u16(&r, &ext_total))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    size_t ext_end = r.pos + ext_total;
    if (ext_end > r.len)
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    while (r.pos < ext_end)
    {
        uint16_t etype = 0;
        uint16_t elen = 0;
        const uint8_t *ebody = NULL;
        if (!r_u16(&r, &etype) || !r_u16(&r, &elen) || !r_take(&r, elen, &ebody))
        {
            Tls13MsgV.ok = PROTO_FALSE;
            return;
        }
        parse_extension(etype, ebody, elen, out, dtls);
    }
    Tls13MsgV.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------
void protocore_tls13_msg_build_server_hello(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Tls13MsgV.build_server_hello_args.out;
    size_t cap = Tls13MsgV.build_server_hello_args.cap;
    const uint8_t *random = Tls13MsgV.build_server_hello_args.random;
    const uint8_t *session_id = Tls13MsgV.build_server_hello_args.session_id;
    uint8_t session_id_len = Tls13MsgV.build_server_hello_args.session_id_len;
    const uint8_t *share = Tls13MsgV.build_server_hello_args.share;
    size_t share_len = Tls13MsgV.build_server_hello_args.share_len;
    uint16_t group = Tls13MsgV.build_server_hello_args.group;
    uint16_t suite = Tls13MsgV.build_server_hello_args.suite;
    proto_bool dtls = Tls13MsgV.build_server_hello_args.dtls;
    const uint8_t *conn_id = Tls13MsgV.build_server_hello_args.conn_id;
    size_t conn_id_len = Tls13MsgV.build_server_hello_args.conn_id_len;

    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_SERVER_HELLO);
    size_t hs_len = w_mark(&w, 3);

    // legacy_version is 0x0303 (TLS 1.2) for TLS/QUIC, 0xFEFD (DTLS 1.2) for DTLS (RFC 9147 §5.3).
    w_u16(&w, dtls ? PROTOCORE_TLS_LEGACY_VERSION_DTLS : (uint16_t)0x0303);
    w_bytes(&w, random, 32);
    w_u8(&w, session_id_len);
    w_bytes(&w, session_id, session_id_len);
    w_u16(&w, suite);
    w_u8(&w, 0x00); // legacy_compression_method

    size_t ext_len = w_mark(&w, 2);
    // key_share -> server KeyShareEntry { group, key_exchange }. (Ordered key_share then
    // supported_versions to match the RFC 8448 sec 3 ServerHello; extension order is not significant.)
    w_u16(&w, TLS_EXT_KEY_SHARE);
    w_u16(&w, (uint16_t)(4 + share_len));
    w_u16(&w, group);
    w_u16(&w, (uint16_t)share_len);
    w_bytes(&w, share, share_len);
    // supported_versions -> selected version (DTLS 1.3 = 0xFEFC, TLS 1.3 = 0x0304).
    w_u16(&w, TLS_EXT_SUPPORTED_VERSIONS);
    w_u16(&w, 2);
    w_u16(&w, dtls ? PROTOCORE_TLS_VERSION_DTLS_1_3 : TLS_VERSION_1_3);
    // connection_id (RFC 9146 / RFC 9147 §9) -> the server's CID the client must place in the records
    // it sends. Sent in the ServerHello (epoch 0) so the client uses it from its first protected record.
    if (conn_id)
    {
        w_u16(&w, TLS_EXT_CONNECTION_ID);
        w_u16(&w, (uint16_t)(1 + conn_id_len));
        w_u8(&w, (uint8_t)conn_id_len);
        w_bytes(&w, conn_id, conn_id_len);
    }
    w_patch16(&w, ext_len);

    w_patch24(&w, hs_len);
    Tls13MsgV.n = w.ok ? w.pos : 0;
}

void protocore_tls13_msg_build_client_hello(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Tls13MsgV.build_client_hello_args.out;
    size_t cap = Tls13MsgV.build_client_hello_args.cap;
    const uint8_t *random = Tls13MsgV.build_client_hello_args.random;
    const uint8_t *session_id = Tls13MsgV.build_client_hello_args.session_id;
    uint8_t session_id_len = Tls13MsgV.build_client_hello_args.session_id_len;
    const uint8_t *share = Tls13MsgV.build_client_hello_args.share;
    size_t share_len = Tls13MsgV.build_client_hello_args.share_len;
    uint16_t group = Tls13MsgV.build_client_hello_args.group;
    uint16_t suite = Tls13MsgV.build_client_hello_args.suite;
    const char *sni = Tls13MsgV.build_client_hello_args.sni;
    const char *alpn = Tls13MsgV.build_client_hello_args.alpn;
    const uint8_t *cookie = Tls13MsgV.build_client_hello_args.cookie;
    size_t cookie_len = Tls13MsgV.build_client_hello_args.cookie_len;
    proto_bool rpk_server_cert = Tls13MsgV.build_client_hello_args.rpk_server_cert;
    proto_bool dtls = Tls13MsgV.build_client_hello_args.dtls;

    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_CLIENT_HELLO);
    size_t hs_len = w_mark(&w, 3);

    // sec 4.1.2: legacy_version "MUST be set to 0x0303"; DTLS spells the same field 0xFEFD (RFC 9147 §5.3).
    w_u16(&w, dtls ? PROTOCORE_TLS_LEGACY_VERSION_DTLS : (uint16_t)0x0303);
    w_bytes(&w, random, 32);
    w_u8(&w, session_id_len);
    w_bytes(&w, session_id, session_id_len);
    if (dtls)
    {
        w_u8(&w, 0); // legacy_cookie: zero length for a DTLS 1.3-only client (RFC 9147 §5.3)
    }
    w_u16(&w, 2); // cipher_suites: the one suite this hello offers
    w_u16(&w, suite);
    w_u8(&w, 1);    // legacy_compression_methods "MUST contain exactly one byte,
    w_u8(&w, 0x00); // set to zero" (sec 4.1.2)

    size_t ext_len = w_mark(&w, 2);
    // supported_versions: the ClientHello form is a u8-length-prefixed list (sec 4.2.1).
    w_u16(&w, TLS_EXT_SUPPORTED_VERSIONS);
    w_u16(&w, 3);
    w_u8(&w, 2);
    w_u16(&w, dtls ? PROTOCORE_TLS_VERSION_DTLS_1_3 : TLS_VERSION_1_3);
    // supported_groups (sec 4.2.7): the one group this share is from.
    w_u16(&w, TLS_EXT_SUPPORTED_GROUPS);
    w_u16(&w, 4);
    w_u16(&w, 2);
    w_u16(&w, group);
    // signature_algorithms (sec 4.2.3): the one scheme this stack produces and verifies.
    w_u16(&w, TLS_EXT_SIGNATURE_ALGORITHMS);
    w_u16(&w, 4);
    w_u16(&w, 2);
    w_u16(&w, TLS_SIG_ED25519);
    // key_share: KeyShareEntry client_shares<0..2^16-1>, one entry (sec 4.2.8).
    w_u16(&w, TLS_EXT_KEY_SHARE);
    w_u16(&w, (uint16_t)(share_len + 6));
    w_u16(&w, (uint16_t)(share_len + 4));
    w_u16(&w, group);
    w_u16(&w, (uint16_t)share_len);
    w_bytes(&w, share, share_len);
    // server_name: one host_name in a ServerNameList (RFC 6066 sec 3).
    if (sni)
    {
        const size_t nl = str.len(sni, 255);
        w_u16(&w, TLS_EXT_SERVER_NAME);
        w_u16(&w, (uint16_t)(nl + 5));
        w_u16(&w, (uint16_t)(nl + 3));
        w_u8(&w, 0); // NameType host_name
        w_u16(&w, (uint16_t)nl);
        w_bytes(&w, (const uint8_t *)sni, nl);
    }
    // ALPN: one ProtocolName in the list (RFC 7301 sec 3.1).
    if (alpn)
    {
        const size_t nl = str.len(alpn, 255);
        w_u16(&w, TLS_EXT_ALPN);
        w_u16(&w, (uint16_t)(nl + 3));
        w_u16(&w, (uint16_t)(nl + 1));
        w_u8(&w, (uint8_t)nl);
        w_bytes(&w, (const uint8_t *)alpn, nl);
    }
    // cookie: echoed from the HelloRetryRequest that asked for it (sec 4.2.2).
    if (cookie && cookie_len)
    {
        w_u16(&w, TLS_EXT_COOKIE);
        w_u16(&w, (uint16_t)(cookie_len + 2));
        w_u16(&w, (uint16_t)cookie_len);
        w_bytes(&w, cookie, cookie_len);
    }
    // server_certificate_type: a 1-byte list of CertificateType (RFC 7250 sec 4.2), the form the
    // server reads back.
    if (rpk_server_cert)
    {
        w_u16(&w, TLS_EXT_SERVER_CERTIFICATE_TYPE);
        w_u16(&w, 2);
        w_u8(&w, 1);
        w_u8(&w, TLS_CERT_TYPE_RAW_PUBLIC_KEY);
    }
    w_patch16(&w, ext_len);

    w_patch24(&w, hs_len);
    Tls13MsgV.n = w.ok ? w.pos : 0;
}

// The handshake header of a message of the expected type, with the body framed exactly. Every
// parser below starts here, so a length that does not frame its own message is refused once.
static proto_bool hs_body(const uint8_t *msg, size_t len, uint8_t want, Reader *r)
{
    uint8_t type = 0;
    uint32_t body_len = 0;
    r->buf = msg;
    r->len = len;
    r->pos = 0;
    if (!r_u8(r, &type) || type != want || !r_u24(r, &body_len))
    {
        return PROTO_FALSE;
    }
    if (r->pos + body_len != len)
    {
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

void protocore_tls13_msg_parse_certificate(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *msg = Tls13MsgV.parse_certificate_args.msg;
    size_t len = Tls13MsgV.parse_certificate_args.len;
    const uint8_t **cert = Tls13MsgV.parse_certificate_args.cert;
    size_t *cert_len = Tls13MsgV.parse_certificate_args.cert_len;

    Reader r;
    if (!hs_body(msg, len, TLS_HS_CERTIFICATE, &r))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    uint8_t ctx_len = 0;
    const uint8_t *ctx = NULL;
    if (!r_u8(&r, &ctx_len) || !r_take(&r, ctx_len, &ctx))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    uint32_t list_len = 0;
    if (!r_u24(&r, &list_len) || r.pos + list_len != r.len)
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    // The end-entity certificate is the first CertificateEntry (sec 4.4.2).
    uint32_t entry_len = 0;
    if (!r_u24(&r, &entry_len) || entry_len == 0 || !r_take(&r, entry_len, cert))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    *cert_len = entry_len;
    uint16_t ext_len = 0;
    const uint8_t *ext = NULL;
    if (!r_u16(&r, &ext_len) || !r_take(&r, ext_len, &ext))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    Tls13MsgV.ok = PROTO_TRUE;
}

void protocore_tls13_msg_parse_cert_verify(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *msg = Tls13MsgV.parse_cert_verify_args.msg;
    size_t len = Tls13MsgV.parse_cert_verify_args.len;
    uint16_t *scheme = Tls13MsgV.parse_cert_verify_args.scheme;
    const uint8_t **sig = Tls13MsgV.parse_cert_verify_args.sig;
    size_t *sig_len = Tls13MsgV.parse_cert_verify_args.sig_len;

    Reader r;
    if (!hs_body(msg, len, TLS_HS_CERTIFICATE_VERIFY, &r))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    uint16_t slen = 0;
    if (!r_u16(&r, scheme) || !r_u16(&r, &slen) || !r_take(&r, slen, sig) || r.pos != r.len)
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    *sig_len = slen;
    Tls13MsgV.ok = PROTO_TRUE;
}

void protocore_tls13_msg_parse_finished(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *msg = Tls13MsgV.parse_finished_args.msg;
    size_t len = Tls13MsgV.parse_finished_args.len;
    const uint8_t **vd = Tls13MsgV.parse_finished_args.vd;
    size_t verify_len = Tls13MsgV.parse_finished_args.verify_len;

    Reader r;
    if (!hs_body(msg, len, TLS_HS_FINISHED, &r))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    // sec 4.4.4: verify_data is Hash.length octets, and the body is exactly that.
    Tls13MsgV.ok = r_take(&r, verify_len, vd) && r.pos == r.len;
}

void protocore_tls13_msg_parse_server_hello(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *msg = Tls13MsgV.parse_server_hello_args.msg;
    size_t len = Tls13MsgV.parse_server_hello_args.len;
    Tls13ServerHello *out = Tls13MsgV.parse_server_hello_args.out;
    proto_bool dtls = Tls13MsgV.parse_server_hello_args.dtls;

    mem.zero(out, sizeof(*out));

    Reader r = {msg, len, 0};
    uint8_t type = 0;
    uint32_t body_len = 0;
    if (!r_u8(&r, &type) || type != TLS_HS_SERVER_HELLO || !r_u24(&r, &body_len))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    if (r.pos + body_len > len)
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    r.len = r.pos + body_len;

    uint16_t legacy_version = 0;
    if (!r_u16(&r, &legacy_version) || !r_take(&r, 32, &out->random))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    // sec 4.1.3: a ServerHello carrying this Random is a HelloRetryRequest, not a real one.
    out->is_hrr = mem.cmp(out->random, protocore_tls13_hrr_random, 32) == 0;

    uint8_t sid_len = 0;
    if (!r_u8(&r, &sid_len) || sid_len > 32 || !r_take(&r, sid_len, &out->session_id))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    out->session_id_len = sid_len;

    uint8_t comp = 0;
    if (!r_u16(&r, &out->cipher_suite) || !r_u8(&r, &comp))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    // sec 4.1.3: legacy_compression_method is a single byte and "MUST be 0".
    if (comp != 0)
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }

    uint16_t exts_len = 0;
    const uint8_t *exts = NULL;
    if (!r_u16(&r, &exts_len) || !r_take(&r, exts_len, &exts))
    {
        Tls13MsgV.ok = PROTO_FALSE;
        return;
    }
    const uint16_t want_version = dtls ? PROTOCORE_TLS_VERSION_DTLS_1_3 : TLS_VERSION_1_3;
    for (size_t off = 0; off + 4 <= exts_len;)
    {
        const uint16_t ext = (uint16_t)((exts[off] << 8) | exts[off + 1]);
        const size_t blen = (size_t)((exts[off + 2] << 8) | exts[off + 3]);
        const uint8_t *body = exts + off + 4;
        if (off + 4 + blen > exts_len)
        {
            Tls13MsgV.ok = PROTO_FALSE;
            return;
        }
        off += 4 + blen;
        switch (ext)
        {
        case TLS_EXT_SUPPORTED_VERSIONS:
            // The ServerHello form is one selected_version, not a list (sec 4.2.1).
            if (blen == 2 && ((body[0] << 8) | body[1]) == want_version)
            {
                out->selected_tls13 = PROTO_TRUE;
            }
            break;
        case TLS_EXT_KEY_SHARE:
            out->has_key_share = PROTO_TRUE;
            if (out->is_hrr)
            {
                // KeyShareHelloRetryRequest is the selected_group alone (sec 4.2.8).
                if (blen != 2)
                {
                    Tls13MsgV.ok = PROTO_FALSE;
                    return;
                }
                out->group = (uint16_t)((body[0] << 8) | body[1]);
                break;
            }
            if (blen < 4)
            {
                Tls13MsgV.ok = PROTO_FALSE;
                return;
            }
            out->group = (uint16_t)((body[0] << 8) | body[1]);
            out->share_len = (size_t)((body[2] << 8) | body[3]);
            if (4 + out->share_len != blen)
            {
                Tls13MsgV.ok = PROTO_FALSE;
                return;
            }
            out->share = body + 4;
            break;
        case TLS_EXT_COOKIE:
            if (blen < 2)
            {
                Tls13MsgV.ok = PROTO_FALSE;
                return;
            }
            out->cookie_len = (size_t)((body[0] << 8) | body[1]);
            if (2 + out->cookie_len != blen)
            {
                Tls13MsgV.ok = PROTO_FALSE;
                return;
            }
            out->cookie = body + 2;
            break;
        case TLS_EXT_CONNECTION_ID:
            if (blen < 1 || (size_t)body[0] + 1 != blen)
            {
                Tls13MsgV.ok = PROTO_FALSE;
                return;
            }
            out->has_conn_id = PROTO_TRUE;
            out->conn_id_len = body[0];
            out->conn_id = body + 1;
            break;
        default:
            break; // unknown extensions are read past
        }
    }
    Tls13MsgV.ok = PROTO_TRUE;
}

// SHA-256("HelloRetryRequest") - RFC 8446 §4.1.3. A ServerHello with this random is a HelloRetryRequest.
const uint8_t protocore_tls13_hrr_random[32] = {0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C,
                                                0x02, 0x1E, 0x65, 0xB8, 0x91, 0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB,
                                                0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};

void protocore_tls13_msg_build_hello_retry_request(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Tls13MsgV.build_hello_retry_request_args.out;
    size_t cap = Tls13MsgV.build_hello_retry_request_args.cap;
    const uint8_t *session_id = Tls13MsgV.build_hello_retry_request_args.session_id;
    uint8_t session_id_len = Tls13MsgV.build_hello_retry_request_args.session_id_len;
    uint16_t selected_group = Tls13MsgV.build_hello_retry_request_args.selected_group;
    uint16_t suite = Tls13MsgV.build_hello_retry_request_args.suite;
    const uint8_t *cookie = Tls13MsgV.build_hello_retry_request_args.cookie;
    size_t cookie_len = Tls13MsgV.build_hello_retry_request_args.cookie_len;
    proto_bool dtls = Tls13MsgV.build_hello_retry_request_args.dtls;

    if (cookie_len > 0xFFFD)
    {
        Tls13MsgV.n = 0; // cookie extension body (cookie_len + 2) must fit a uint16
        return;
    }
    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_SERVER_HELLO);
    size_t hs_len = w_mark(&w, 3);

    // legacy_version and the supported_versions selection use the DTLS codepoints for DTLS 1.3
    // (0xFEFD / 0xFEFC, RFC 9147 §5.3), the TLS ones (0x0303 / 0x0304) otherwise - a HelloRetryRequest
    // is a ServerHello, so it carries the same version fields.
    w_u16(&w, dtls ? PROTOCORE_TLS_LEGACY_VERSION_DTLS : (uint16_t)0x0303); // legacy_version
    w_bytes(&w, protocore_tls13_hrr_random, 32);
    w_u8(&w, session_id_len);
    w_bytes(&w, session_id, session_id_len);
    w_u16(&w, suite);
    w_u8(&w, 0x00); // legacy_compression_method

    size_t ext_len = w_mark(&w, 2);
    // supported_versions -> the selected version.
    w_u16(&w, TLS_EXT_SUPPORTED_VERSIONS);
    w_u16(&w, 2);
    w_u16(&w, dtls ? PROTOCORE_TLS_VERSION_DTLS_1_3 : TLS_VERSION_1_3);
    // key_share (HelloRetryRequest form) -> just the selected group (RFC 8446 §4.2.8).
    w_u16(&w, TLS_EXT_KEY_SHARE);
    w_u16(&w, 2);
    w_u16(&w, selected_group);
    // cookie -> the return-routability token the client must echo (RFC 8446 §4.2.2).
    if (cookie_len)
    {
        w_u16(&w, TLS_EXT_COOKIE);
        w_u16(&w, (uint16_t)(cookie_len + 2));
        w_u16(&w, (uint16_t)cookie_len);
        w_bytes(&w, cookie, cookie_len);
    }
    w_patch16(&w, ext_len);

    w_patch24(&w, hs_len);
    Tls13MsgV.n = w.ok ? w.pos : 0;
}

#if PROTOCORE_ENABLE_TLS_RPK
// server_certificate_type response (RFC 7250 sec 4.2): a single CertificateType value, RawPublicKey.
static void w_server_cert_type_rpk(Writer *w)
{
    w_u16(w, TLS_EXT_SERVER_CERTIFICATE_TYPE);
    w_u16(w, 1); // extension_data length
    w_u8(w, TLS_CERT_TYPE_RAW_PUBLIC_KEY);
}
#endif

void protocore_tls13_msg_build_encrypted_extensions_empty(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Tls13MsgV.build_encrypted_extensions_empty_args.out;
    size_t cap = Tls13MsgV.build_encrypted_extensions_empty_args.cap;
    proto_bool rpk_server_cert = Tls13MsgV.build_encrypted_extensions_empty_args.rpk_server_cert;
    const char *alpn = Tls13MsgV.build_encrypted_extensions_empty_args.alpn;

    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_ENCRYPTED_EXTENSIONS);
    size_t hs_len = w_mark(&w, 3);
    // No transport params here; the extensions this can carry are the negotiated
    // server_certificate_type (RFC 7250) when RawPublicKey was selected, and the one selected ALPN
    // protocol (RFC 7301 sec 3.2 - the list MUST hold exactly one name).
    size_t ext_len = w_mark(&w, 2);
    if (alpn)
    {
        const size_t nl = str.len(alpn, 255); // ProtocolName is 1..255 octets (RFC 7301 sec 3.1)
        w_u16(&w, TLS_EXT_ALPN);
        w_u16(&w, (uint16_t)(nl + 3)); // 2 list length + 1 name length + the name
        w_u16(&w, (uint16_t)(nl + 1));
        w_u8(&w, (uint8_t)nl);
        w_bytes(&w, (const uint8_t *)alpn, nl);
    }
#if PROTOCORE_ENABLE_TLS_RPK
    if (rpk_server_cert)
    {
        w_server_cert_type_rpk(&w);
    }
#else
    (void)rpk_server_cert;
#endif
    w_patch16(&w, ext_len);
    w_patch24(&w, hs_len);
    Tls13MsgV.n = w.ok ? w.pos : 0;
}

void protocore_tls13_msg_build_message_hash(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Tls13MsgV.build_message_hash_args.out;
    size_t cap = Tls13MsgV.build_message_hash_args.cap;
    const uint8_t *ch1_hash = Tls13MsgV.build_message_hash_args.ch1_hash;

    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, 254); // message_hash synthetic handshake type (RFC 8446 §4.4.1)
    w_u24(&w, 32); // Hash.length for SHA-256
    w_bytes(&w, ch1_hash, 32);
    Tls13MsgV.n = w.ok ? w.pos : 0;
}

void protocore_tls13_msg_build_encrypted_extensions(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Tls13MsgV.build_encrypted_extensions_args.out;
    size_t cap = Tls13MsgV.build_encrypted_extensions_args.cap;
    const uint8_t *quic_tp = Tls13MsgV.build_encrypted_extensions_args.quic_tp;
    size_t quic_tp_len = Tls13MsgV.build_encrypted_extensions_args.quic_tp_len;
    proto_bool rpk_server_cert = Tls13MsgV.build_encrypted_extensions_args.rpk_server_cert;

    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_ENCRYPTED_EXTENSIONS);
    size_t hs_len = w_mark(&w, 3);

    size_t ext_len = w_mark(&w, 2);
    // ALPN -> ProtocolNameList [ "h3" ].
    w_u16(&w, TLS_EXT_ALPN);
    w_u16(&w, 5); // ext body length: 2 (list len) + 1 + 2
    w_u16(&w, 3); // ProtocolNameList length
    w_u8(&w, 2);  // name length
    w_bytes(&w, (const uint8_t *)"h3", 2);
    // quic_transport_parameters.
    w_u16(&w, TLS_EXT_QUIC_TRANSPORT_PARAMS);
    w_u16(&w, (uint16_t)quic_tp_len);
    w_bytes(&w, quic_tp, quic_tp_len);
#if PROTOCORE_ENABLE_TLS_RPK
    // negotiated server_certificate_type = RawPublicKey (RFC 7250), when selected.
    if (rpk_server_cert)
    {
        w_server_cert_type_rpk(&w);
    }
#else
    (void)rpk_server_cert;
#endif
    w_patch16(&w, ext_len);

    w_patch24(&w, hs_len);
    Tls13MsgV.n = w.ok ? w.pos : 0;
}

void protocore_tls13_msg_build_certificate(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Tls13MsgV.build_certificate_args.out;
    size_t cap = Tls13MsgV.build_certificate_args.cap;
    const uint8_t *cert_der = Tls13MsgV.build_certificate_args.cert_der;
    size_t cert_len = Tls13MsgV.build_certificate_args.cert_len;

    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_CERTIFICATE);
    size_t hs_len = w_mark(&w, 3);

    w_u8(&w, 0); // certificate_request_context: empty
    size_t list_len = w_mark(&w, 3);
    w_u24(&w, (uint32_t)cert_len); // CertificateEntry cert_data length
    w_bytes(&w, cert_der, cert_len);
    w_u16(&w, 0); // entry extensions: empty
    w_patch24(&w, list_len);

    w_patch24(&w, hs_len);
    Tls13MsgV.n = w.ok ? w.pos : 0;
}

void protocore_tls13_msg_cert_verify_content(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Tls13MsgV.cert_verify_content_args.out;
    size_t cap = Tls13MsgV.cert_verify_content_args.cap;
    const uint8_t *transcript_hash = Tls13MsgV.cert_verify_content_args.transcript_hash;
    size_t hash_len = Tls13MsgV.cert_verify_content_args.hash_len;
    proto_bool is_server = Tls13MsgV.cert_verify_content_args.is_server;

    // RFC 8446 sec 4.4.3: 64 spaces || context string || 0x00 || transcript hash.
    static const char SRV[] = "TLS 1.3, server CertificateVerify";
    static const char CLI[] = "TLS 1.3, client CertificateVerify";
    const char *ctx = is_server ? SRV : CLI;
    size_t ctx_len = is_server ? sizeof(SRV) - 1 : sizeof(CLI) - 1;
    if (hash_len > PROTOCORE_TLS13_SECRET_MAX)
    {
        Tls13MsgV.n = 0;
        return;
    }
    size_t total = 64 + ctx_len + 1 + hash_len;
    if (total > cap)
    {
        Tls13MsgV.n = 0;
        return;
    }
    mem.set(out, 0x20, 64);
    mem.cpy(out + 64, ctx, ctx_len);
    out[64 + ctx_len] = 0x00;
    mem.cpy(out + 64 + ctx_len + 1, transcript_hash, hash_len);
    Tls13MsgV.n = total;
}

void protocore_tls13_msg_build_cert_verify(uint8_t *restrict work)
{
    uint8_t *sign_work = Tls13MsgV.build_cert_verify_args.sign_work;
    uint8_t *out = Tls13MsgV.build_cert_verify_args.out;
    size_t cap = Tls13MsgV.build_cert_verify_args.cap;
    const uint8_t *transcript_hash = Tls13MsgV.build_cert_verify_args.transcript_hash;
    size_t hash_len = Tls13MsgV.build_cert_verify_args.hash_len;
    const uint8_t *seed = Tls13MsgV.build_cert_verify_args.seed;

    uint8_t content[64 + 33 + 1 + PROTOCORE_TLS13_SECRET_MAX];
    Tls13MsgV.cert_verify_content_args.out = content;
    Tls13MsgV.cert_verify_content_args.cap = sizeof(content);
    Tls13MsgV.cert_verify_content_args.transcript_hash = transcript_hash;
    Tls13MsgV.cert_verify_content_args.hash_len = hash_len;
    Tls13MsgV.cert_verify_content_args.is_server = PROTO_TRUE;
    protocore_tls13_msg_cert_verify_content(work);
    size_t clen = Tls13MsgV.n;
    if (!clen)
    {
        Tls13MsgV.n = 0;
        return;
    }
    uint8_t sig[PROTOCORE_ED25519_SIG_LEN];
    Ed25519.sign_args.seed = seed;
    Ed25519.sign_args.msg = content;
    Ed25519.sign_args.msg_len = clen;
    Ed25519.sign_args.sig = sig;
    Ed25519.sign(sign_work);

    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_CERTIFICATE_VERIFY);
    size_t hs_len = w_mark(&w, 3);
    w_u16(&w, TLS_SIG_ED25519);
    w_u16(&w, PROTOCORE_ED25519_SIG_LEN);
    w_bytes(&w, sig, PROTOCORE_ED25519_SIG_LEN);
    w_patch24(&w, hs_len);
    Tls13MsgV.n = w.ok ? w.pos : 0;
}

void protocore_tls13_msg_build_finished(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Tls13MsgV.build_finished_args.out;
    size_t cap = Tls13MsgV.build_finished_args.cap;
    const uint8_t *verify_data = Tls13MsgV.build_finished_args.verify_data;
    size_t verify_len = Tls13MsgV.build_finished_args.verify_len;

    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_FINISHED);
    size_t hs_len = w_mark(&w, 3);
    w_bytes(&w, verify_data, verify_len);
    w_patch24(&w, hs_len);
    Tls13MsgV.n = w.ok ? w.pos : 0;
}

/** @brief The operands and the outcome. */
Tls13MsgVars Tls13MsgV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE
