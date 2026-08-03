// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_tls13_msg.c
 * @brief TLS 1.3 handshake messages for the QUIC handshake (see pc_tls13_msg.h).
 */

#include "network_drivers/presentation/http/http3/tls13_msg.h"

#if (PC_ENABLE_HTTP3 || PC_ENABLE_DTLS)

#include "crypto/asymmetric/ed25519.h"
#if PC_ENABLE_PQC_KEX
#include "crypto/pqc/mlkem.h" // MLKEM768_EK_BYTES (X25519MLKEM768 share sizing)
#endif
#include <string.h>

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
    if (w->pos > w->cap || n > w->cap - w->pos) // GCOVR_EXCL_LINE  w_u8 refuses to advance past cap, so pos <= cap
    {                                           // is an invariant and the first arm can never be taken
        w->ok = PROTO_FALSE;
        return;
    }
    memcpy(w->buf + w->pos, b, n);
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
            memcpy(out->client_x25519, body + i, 32);
            out->has_key_share = PROTO_TRUE;
        }
#if PC_ENABLE_PQC_KEX
        else if (group == TLS_GROUP_X25519MLKEM768 && klen == MLKEM768_EK_BYTES + 32)
        {
            out->client_mlkem_ek = body + i;                              // ML-KEM-768 ek (first)
            memcpy(out->client_x25519, body + i + MLKEM768_EK_BYTES, 32); // X25519 (second)
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
        out->offers_tls13 = list16_contains(body + 1, blen - 1, ll, dtls ? PC_TLS_VERSION_DTLS_1_3 : TLS_VERSION_1_3);
        break;
    }
    case TLS_EXT_SUPPORTED_GROUPS: {
        if (blen < 2)
        {
            return;
        }
        size_t ll = (body[0] << 8) | body[1];
        out->offers_x25519 = list16_contains(body + 2, blen - 2, ll, TLS_GROUP_X25519);
#if PC_ENABLE_PQC_KEX
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
#if PC_ENABLE_TLS_RPK
    case TLS_EXT_SERVER_CERTIFICATE_TYPE: {
        // server_certificate_type (RFC 7250 sec 4.2): a 1-byte list length then 1-byte CertificateType
        // values. RawPublicKey(2) in the list means the client accepts a bare SubjectPublicKeyInfo from us.
        if (blen < 1)
        {
            return;
        }
        size_t ll = body[0];
        if (1 + ll > blen)
        {
            return;
        }
        for (size_t i = 0; i < ll; i++)
        {
            if (body[1 + i] == TLS_CERT_TYPE_RAW_PUBLIC_KEY)
            {
                out->offers_rpk_server_cert = PROTO_TRUE;
            }
        }
        break;
    }
#endif
    case TLS_EXT_QUIC_TRANSPORT_PARAMS:
        out->pc_quic_tp = body;
        out->pc_quic_tp_len = blen;
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

proto_bool pc_tls13_parse_client_hello(const uint8_t *msg, size_t len, Tls13ClientHello *out, proto_bool dtls)
{
    memset(out, 0, sizeof(*out));

    Reader r = {msg, len, 0};
    uint8_t type = 0;
    uint32_t body_len = 0;
    if (!r_u8(&r, &type) || type != TLS_HS_CLIENT_HELLO || !r_u24(&r, &body_len))
    {
        return PROTO_FALSE;
    }
    // The handshake body must fit; trailing bytes past it are not part of this message.
    if (r.pos + body_len > len)
    {
        return PROTO_FALSE;
    }
    r.len = r.pos + body_len;

    uint16_t legacy_version = 0;
    const uint8_t *random = NULL;
    if (!r_u16(&r, &legacy_version) || !r_take(&r, 32, &random))
    {
        return PROTO_FALSE;
    }

    uint8_t sid_len = 0;
    if (!r_u8(&r, &sid_len) || sid_len > 32)
    {
        return PROTO_FALSE;
    }
    if (!r_take(&r, sid_len, &out->session_id))
    {
        return PROTO_FALSE;
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
            return PROTO_FALSE;
        }
    }

    uint16_t cs_len = 0;
    const uint8_t *cs = NULL;
    if (!r_u16(&r, &cs_len) || (cs_len % 2) != 0 || !r_take(&r, cs_len, &cs))
    {
        return PROTO_FALSE;
    }

    uint8_t comp_len = 0;
    const uint8_t *comp = NULL;
    if (!r_u8(&r, &comp_len) || !r_take(&r, comp_len, &comp))
    {
        return PROTO_FALSE;
    }

    // Extensions (a ClientHello for TLS 1.3 always has them).
    uint16_t ext_total = 0;
    if (!r_u16(&r, &ext_total))
    {
        return PROTO_FALSE;
    }
    size_t ext_end = r.pos + ext_total;
    if (ext_end > r.len)
    {
        return PROTO_FALSE;
    }
    while (r.pos < ext_end)
    {
        uint16_t etype = 0;
        uint16_t elen = 0;
        const uint8_t *ebody = NULL;
        if (!r_u16(&r, &etype) || !r_u16(&r, &elen) || !r_take(&r, elen, &ebody))
        {
            return PROTO_FALSE;
        }
        parse_extension(etype, ebody, elen, out, dtls);
    }
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------
size_t pc_tls13_build_server_hello(uint8_t *out, size_t cap, const uint8_t random[32], const uint8_t *session_id,
                                   uint8_t session_id_len, const uint8_t *share, size_t share_len, uint16_t group,
                                   proto_bool dtls, const uint8_t *conn_id, size_t conn_id_len)
{
    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_SERVER_HELLO);
    size_t hs_len = w_mark(&w, 3);

    // legacy_version is 0x0303 (TLS 1.2) for TLS/QUIC, 0xFEFD (DTLS 1.2) for DTLS (RFC 9147 §5.3).
    w_u16(&w, dtls ? PC_TLS_LEGACY_VERSION_DTLS : (uint16_t)0x0303);
    w_bytes(&w, random, 32);
    w_u8(&w, session_id_len);
    w_bytes(&w, session_id, session_id_len);
    w_u16(&w, TLS_CIPHER_AES_128_GCM_SHA256);
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
    w_u16(&w, dtls ? PC_TLS_VERSION_DTLS_1_3 : TLS_VERSION_1_3);
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
    return w.ok ? w.pos : 0;
}

// SHA-256("HelloRetryRequest") - RFC 8446 §4.1.3. A ServerHello with this random is a HelloRetryRequest.
const uint8_t pc_tls13_hrr_random[32] = {0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C,
                                         0x02, 0x1E, 0x65, 0xB8, 0x91, 0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB,
                                         0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};

size_t pc_tls13_build_hello_retry_request(uint8_t *out, size_t cap, const uint8_t *session_id, uint8_t session_id_len,
                                          uint16_t selected_group, const uint8_t *cookie, size_t cookie_len,
                                          proto_bool dtls)
{
    if (cookie_len > 0xFFFD)
    {
        return 0; // cookie extension body (cookie_len + 2) must fit a uint16
    }
    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_SERVER_HELLO);
    size_t hs_len = w_mark(&w, 3);

    // legacy_version and the supported_versions selection use the DTLS codepoints for DTLS 1.3
    // (0xFEFD / 0xFEFC, RFC 9147 §5.3), the TLS ones (0x0303 / 0x0304) otherwise - a HelloRetryRequest
    // is a ServerHello, so it carries the same version fields.
    w_u16(&w, dtls ? PC_TLS_LEGACY_VERSION_DTLS : (uint16_t)0x0303); // legacy_version
    w_bytes(&w, pc_tls13_hrr_random, 32);
    w_u8(&w, session_id_len);
    w_bytes(&w, session_id, session_id_len);
    w_u16(&w, TLS_CIPHER_AES_128_GCM_SHA256);
    w_u8(&w, 0x00); // legacy_compression_method

    size_t ext_len = w_mark(&w, 2);
    // supported_versions -> the selected version.
    w_u16(&w, TLS_EXT_SUPPORTED_VERSIONS);
    w_u16(&w, 2);
    w_u16(&w, dtls ? PC_TLS_VERSION_DTLS_1_3 : TLS_VERSION_1_3);
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
    return w.ok ? w.pos : 0;
}

#if PC_ENABLE_TLS_RPK
// server_certificate_type response (RFC 7250 sec 4.2): a single CertificateType value, RawPublicKey.
static void w_server_cert_type_rpk(Writer *w)
{
    w_u16(w, TLS_EXT_SERVER_CERTIFICATE_TYPE);
    w_u16(w, 1); // extension_data length
    w_u8(w, TLS_CERT_TYPE_RAW_PUBLIC_KEY);
}
#endif

size_t pc_tls13_build_encrypted_extensions_empty(uint8_t *out, size_t cap, proto_bool rpk_server_cert)
{
    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_ENCRYPTED_EXTENSIONS);
    size_t hs_len = w_mark(&w, 3);
    // The DTLS profile carries no ALPN / transport params; the only possible extension is the
    // negotiated server_certificate_type (RFC 7250) when RawPublicKey was selected.
    size_t ext_len = w_mark(&w, 2);
#if PC_ENABLE_TLS_RPK
    if (rpk_server_cert)
    {
        w_server_cert_type_rpk(&w);
    }
#else
    (void)rpk_server_cert;
#endif
    w_patch16(&w, ext_len);
    w_patch24(&w, hs_len);
    return w.ok ? w.pos : 0;
}

size_t pc_tls13_build_message_hash(uint8_t *out, size_t cap, const uint8_t ch1_hash[32])
{
    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, 254); // message_hash synthetic handshake type (RFC 8446 §4.4.1)
    w_u24(&w, 32); // Hash.length for SHA-256
    w_bytes(&w, ch1_hash, 32);
    return w.ok ? w.pos : 0;
}

size_t pc_tls13_build_encrypted_extensions(uint8_t *out, size_t cap, const uint8_t *pc_quic_tp, size_t pc_quic_tp_len,
                                           proto_bool rpk_server_cert)
{
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
    // pc_quic_transport_parameters.
    w_u16(&w, TLS_EXT_QUIC_TRANSPORT_PARAMS);
    w_u16(&w, (uint16_t)pc_quic_tp_len);
    w_bytes(&w, pc_quic_tp, pc_quic_tp_len);
#if PC_ENABLE_TLS_RPK
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
    return w.ok ? w.pos : 0;
}

size_t pc_tls13_build_certificate(uint8_t *out, size_t cap, const uint8_t *cert_der, size_t cert_len)
{
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
    return w.ok ? w.pos : 0;
}

#if PC_ENABLE_TLS_RPK
size_t pc_tls13_ed25519_spki(uint8_t *out, size_t cap, const uint8_t pub[32])
{
    // DER SubjectPublicKeyInfo for id-Ed25519 (RFC 8410 sec 4): a fixed 12-byte prefix - SEQUENCE
    // { SEQUENCE { OID 1.3.101.112 } , BIT STRING (33, 0 unused) } - then the 32-byte public key.
    static const uint8_t PREFIX[12] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};
    if (cap < PC_TLS13_ED25519_SPKI_LEN)
    {
        return 0;
    }
    memcpy(out, PREFIX, sizeof(PREFIX));
    memcpy(out + sizeof(PREFIX), pub, 32);
    return PC_TLS13_ED25519_SPKI_LEN;
}

size_t pc_tls13_build_certificate_rpk(uint8_t *out, size_t cap, const uint8_t ed25519_pub[32])
{
    uint8_t spki[PC_TLS13_ED25519_SPKI_LEN];
    // GCOVR_EXCL_START  spki[] is exactly PC_TLS13_ED25519_SPKI_LEN, so the SPKI encode never overflows.
    if (!pc_tls13_ed25519_spki(spki, sizeof(spki), ed25519_pub))
    {
        return 0;
    }
    // GCOVR_EXCL_STOP
    return pc_tls13_build_certificate(out, cap, spki, sizeof(spki));
}
#endif

size_t pc_tls13_cert_verify_content(uint8_t *out, size_t cap, const uint8_t transcript_hash[32], proto_bool is_server)
{
    // RFC 8446 sec 4.4.3: 64 spaces || context string || 0x00 || transcript hash.
    static const char SRV[] = "TLS 1.3, server CertificateVerify";
    static const char CLI[] = "TLS 1.3, client CertificateVerify";
    const char *ctx = is_server ? SRV : CLI;
    size_t ctx_len = is_server ? sizeof(SRV) - 1 : sizeof(CLI) - 1;
    size_t total = 64 + ctx_len + 1 + 32;
    if (total > cap)
    {
        return 0;
    }
    memset(out, 0x20, 64);
    memcpy(out + 64, ctx, ctx_len);
    out[64 + ctx_len] = 0x00;
    memcpy(out + 64 + ctx_len + 1, transcript_hash, 32);
    return total;
}

size_t pc_tls13_build_cert_verify(uint8_t *out, size_t cap, const uint8_t transcript_hash[32], const uint8_t seed[32])
{
    uint8_t content[64 + 33 + 1 + 32];
    size_t clen = pc_tls13_cert_verify_content(content, sizeof(content), transcript_hash, PROTO_TRUE);
    // GCOVR_EXCL_START  content[] is sized to the exact maximum (64 + ctx 33 + 1 + hash 32), so
    // pc_tls13_cert_verify_content always succeeds here; the guard cannot fire.
    if (!clen)
    {
        return 0;
    }
    // GCOVR_EXCL_STOP
    uint8_t sig[PC_ED25519_SIG_LEN];
    pc_ed25519_sign(sig, content, clen, seed);

    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_CERTIFICATE_VERIFY);
    size_t hs_len = w_mark(&w, 3);
    w_u16(&w, TLS_SIG_ED25519);
    w_u16(&w, PC_ED25519_SIG_LEN);
    w_bytes(&w, sig, PC_ED25519_SIG_LEN);
    w_patch24(&w, hs_len);
    return w.ok ? w.pos : 0;
}

size_t pc_tls13_build_finished(uint8_t *out, size_t cap, const uint8_t verify_data[32])
{
    Writer w = {out, cap, 0, PROTO_TRUE};
    w_u8(&w, TLS_HS_FINISHED);
    size_t hs_len = w_mark(&w, 3);
    w_bytes(&w, verify_data, 32);
    w_patch24(&w, hs_len);
    return w.ok ? w.pos : 0;
}

#endif // PC_ENABLE_HTTP3 || PC_ENABLE_DTLS
