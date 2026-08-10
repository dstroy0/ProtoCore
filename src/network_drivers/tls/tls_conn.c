// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls_conn.c
 * @brief TLS 1.3 handshake driver over the stream record layer. See tls_conn.h.
 */

#include "network_drivers/tls/tls_conn.h"

#if PC_TLS_SOFTWARE

#include "crypto/asymmetric/curve25519.h" // pc_x25519, pc_x25519_base
#include "crypto/ct_eq.h"                 // pc_ct_eq: the Finished compare
#include "mmgr/rawmemcpy.h"               // proto_raw_read
#include "mmgr/secure.h"                  // the borrow this driver runs out of, and pc_secure_wipe

// The secure-pool term this file declares against PC_SECURE_ARENA_SIZE: one borrow per TLS
// connection, taken from the persistent end and held for the life of the connection.
#define PC_TLS_CONN_BORROW                                                                                             \
    ((size_t)PC_TLS_CONN_MSG_CAP + PC_TLS_CONN_REC_CAP + PC_TLS_CONN_TERMS_CAP + PC_TLS_CONN_STATE_CAP)
static_assert(PC_WORK_TLS_CONN >= (size_t)MAX_TLS_CONNS * PC_TLS_CONN_BORROW,
              "PC_WORK_TLS_CONN must cover one TX + RX + terms + state borrow per TLS connection: raise it in "
              "protocore_config.h");
static_assert(PC_SHA256_BORROW + sizeof(Tls13ClientHello) + PC_TLS13_KS_BORROW + PC_SHA512_BORROW <=
                  PC_TLS_CONN_STATE_CAP,
              "PC_TLS_CONN_STATE_CAP must cover the transcript's working bytes, the parsed ClientHello, the "
              "key schedule and the Ed25519 signature's SHA-512: raise it in protocore_config.h");

// Offsets into the one borrow.
#define TLS_OFF_TX 0
#define TLS_OFF_RX ((size_t)PC_TLS_CONN_MSG_CAP)
#define TLS_OFF_TERMS (TLS_OFF_RX + PC_TLS_CONN_REC_CAP)
#define TLS_OFF_HASH (TLS_OFF_TERMS + PC_TLS_CONN_TERMS_CAP)
#define TLS_OFF_HELLO (TLS_OFF_HASH + PC_SHA256_BORROW)
#define TLS_OFF_KS (TLS_OFF_HELLO + sizeof(Tls13ClientHello))
#define TLS_OFF_SIGN (TLS_OFF_KS + PC_TLS13_KS_BORROW)

// The profile in tls_conn.h: the portable arm authenticates by RFC 7250 raw public key, so the
// Certificate message it builds is the RPK one.
static_assert(PC_ENABLE_TLS_RPK, "PC_TLS_SOFTWARE authenticates by RFC 7250 raw public key: set PC_ENABLE_TLS_RPK");

// RFC 8446 sec 6.2 alerts this driver raises.
#define TLS_ALERT_HANDSHAKE_FAILURE 40
#define TLS_ALERT_ILLEGAL_PARAMETER 47
#define TLS_ALERT_DECODE_ERROR 50
#define TLS_ALERT_DECRYPT_ERROR 51
#define TLS_ALERT_INTERNAL_ERROR 80

/** @brief Handshake message header: msg_type(1) + 24-bit length. */
#define TLS_HS_HDR_LEN 4

// Offsets into TlsConn::terms. Each term is one TLS13_SECRET_LEN value.
#define TLS_TERM_SHARE 0                       // this end's X25519 public key_share
#define TLS_TERM_SECRET TLS13_SECRET_LEN       // the X25519 shared secret
#define TLS_TERM_HASH (2 * TLS13_SECRET_LEN)   // the transcript hash the step in hand needs
#define TLS_TERM_MAC (3 * TLS13_SECRET_LEN)    // the Finished MAC built, or the one expected
#define TLS_TERM_HS_FIN (4 * TLS13_SECRET_LEN) // Transcript-Hash(CH..server Finished)

// Fail the connection with an alert and report it as a driver error.
static int fail(TlsConn *c, uint8_t alert)
{
    c->state = TLS_CONN_FAILED;
    c->alert = alert;
    return -1;
}

// Fold a whole handshake message (header included) into the running Transcript-Hash.
static void transcript_add(TlsConn *c, const uint8_t *msg, size_t len)
{
    pc_sha256_update(&c->transcript, msg, len);
}

// The Transcript-Hash so far into terms[off]. Finalizing compresses the padded blocks into a copy of
// the state, so the running context is untouched and keeps taking messages.
static void transcript_peek(TlsConn *c, size_t off)
{
    pc_sha256_final(&c->transcript, c->terms + off);
}

// The body length a handshake message header declares.
static size_t hs_body_len(const uint8_t *msg)
{
    return ((size_t)msg[1] << 16) | ((size_t)msg[2] << 8) | (size_t)msg[3];
}

// Fold the message standing in TX into the transcript and seal it under the handshake write keys.
// Bytes written to out, or 0.
static size_t emit_encrypted(TlsConn *c, size_t msg_len, uint8_t *out, size_t out_cap)
{
    if (msg_len == 0)
    {
        return 0;
    }
    transcript_add(c, c->tx, msg_len);
    return TlsRecord.protect(&c->hs_tx, PC_TLS_CT_HANDSHAKE, c->tx, msg_len, out, out_cap);
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

// The whole server answer to a ClientHello: ServerHello in the clear, then the encrypted flight.
// Bytes written to out, or a negative alert-bearing failure.
static int server_flight(TlsConn *c, uint8_t *out, size_t out_cap)
{
    pc_x25519_base(c->terms + TLS_TERM_SHARE, c->cfg->ephemeral_priv);
    pc_x25519(c->terms + TLS_TERM_SECRET, c->cfg->ephemeral_priv, c->hello->client_x25519);

    size_t off = 0;

    // ServerHello travels as TLSPlaintext: the keys it establishes do not protect it.
    size_t n = pc_tls13_build_server_hello(c->tx, PC_TLS_CONN_MSG_CAP, c->cfg->random, c->hello->session_id,
                                           c->hello->session_id_len, c->terms + TLS_TERM_SHARE, TLS13_SECRET_LEN,
                                           TLS_GROUP_X25519, PROTO_FALSE, NULL, 0);
    if (n == 0)
    {
        return fail(c, TLS_ALERT_INTERNAL_ERROR);
    }
    transcript_add(c, c->tx, n);
    size_t w = TlsRecord.plaintext_build(PC_TLS_CT_HANDSHAKE, c->tx, n, out + off, out_cap - off);
    if (w == 0)
    {
        return fail(c, TLS_ALERT_INTERNAL_ERROR);
    }
    off += w;

    // Everything from here is protected by the handshake traffic keys, keyed off CH..SH.
    transcript_peek(c, TLS_TERM_HASH);
    pc_tls13_ks_handshake(&c->ks, c->terms + TLS_TERM_SECRET, c->terms + TLS_TERM_HASH, TLS13_SECRET_LEN);
    TlsRecord.keys_derive(&c->hs_tx, TLS_CIPHER_AES_128_GCM_SHA256, c->ks.s + TLS13_KS_SERVER_HS);
    TlsRecord.keys_derive(&c->hs_rx, TLS_CIPHER_AES_128_GCM_SHA256, c->ks.s + TLS13_KS_CLIENT_HS);
    c->hs_keys_ready = PROTO_TRUE;

    n = pc_tls13_build_encrypted_extensions_empty(c->tx, PC_TLS_CONN_MSG_CAP, PROTO_TRUE);
    w = emit_encrypted(c, n, out + off, out_cap - off);
    if (w == 0)
    {
        return fail(c, TLS_ALERT_INTERNAL_ERROR);
    }
    off += w;

    n = pc_tls13_build_certificate_rpk(c->tx, PC_TLS_CONN_MSG_CAP, c->cfg->ed25519_pub);
    w = emit_encrypted(c, n, out + off, out_cap - off);
    if (w == 0)
    {
        return fail(c, TLS_ALERT_INTERNAL_ERROR);
    }
    off += w;

    // CertificateVerify signs the transcript through the Certificate message.
    transcript_peek(c, TLS_TERM_HASH);
    n = pc_tls13_build_cert_verify(c->sign_work, c->tx, PC_TLS_CONN_MSG_CAP, c->terms + TLS_TERM_HASH,
                                   c->cfg->ed25519_seed);
    w = emit_encrypted(c, n, out + off, out_cap - off);
    if (w == 0)
    {
        return fail(c, TLS_ALERT_INTERNAL_ERROR);
    }
    off += w;

    // Finished covers the transcript through CertificateVerify.
    transcript_peek(c, TLS_TERM_HASH);
    pc_tls13_finished_mac(&c->ks, c->ks.s + TLS13_KS_SERVER_HS, c->terms + TLS_TERM_HASH, c->terms + TLS_TERM_MAC);
    n = pc_tls13_build_finished(c->tx, PC_TLS_CONN_MSG_CAP, c->terms + TLS_TERM_MAC);
    w = emit_encrypted(c, n, out + off, out_cap - off);
    if (w == 0)
    {
        return fail(c, TLS_ALERT_INTERNAL_ERROR);
    }
    off += w;

    // The application keys are keyed off CH..server Finished, which is the transcript right now.
    transcript_peek(c, TLS_TERM_HS_FIN);
    pc_tls13_ks_master(&c->ks, c->terms + TLS_TERM_HS_FIN);
    TlsRecord.keys_derive(&c->ap_tx, TLS_CIPHER_AES_128_GCM_SHA256, c->ks.s + TLS13_KS_SERVER_AP);
    TlsRecord.keys_derive(&c->ap_rx, TLS_CIPHER_AES_128_GCM_SHA256, c->ks.s + TLS13_KS_CLIENT_AP);
    c->ap_keys_ready = PROTO_TRUE;

    c->state = TLS_CONN_WAIT_FINISHED;
    return (int)off;
}

// A ClientHello arrived whole. Check it against the profile and answer it.
static int server_on_client_hello(TlsConn *c, const uint8_t *msg, size_t len, uint8_t *out, size_t out_cap)
{
    if (!pc_tls13_parse_client_hello(msg, len, c->hello, PROTO_FALSE))
    {
        return fail(c, TLS_ALERT_DECODE_ERROR);
    }
    // One profile: TLS 1.3, X25519 with the share up front, Ed25519. No HelloRetryRequest - the
    // stream side has no cookie to bind, so a client that omits the share is refused.
    if (!c->hello->offers_tls13 || !c->hello->offers_x25519 || !c->hello->has_key_share ||
        !c->hello->offers_ed25519 || !c->hello->offers_aes128gcm_sha256)
    {
        return fail(c, TLS_ALERT_HANDSHAKE_FAILURE);
    }
    transcript_add(c, msg, len);
    return server_flight(c, out, out_cap);
}

// The client Finished closes the handshake: its MAC covers the transcript through server Finished.
static int server_on_finished(TlsConn *c, const uint8_t *msg, size_t len)
{
    if (msg[0] != TLS_HS_FINISHED || hs_body_len(msg) != TLS13_SECRET_LEN || len != TLS_HS_HDR_LEN + TLS13_SECRET_LEN)
    {
        return fail(c, TLS_ALERT_DECODE_ERROR);
    }
    pc_tls13_finished_mac(&c->ks, c->ks.s + TLS13_KS_CLIENT_HS, c->terms + TLS_TERM_HS_FIN, c->terms + TLS_TERM_MAC);
    if (!pc_ct_eq(c->terms + TLS_TERM_MAC, msg + TLS_HS_HDR_LEN, TLS13_SECRET_LEN))
    {
        return fail(c, TLS_ALERT_DECRYPT_ERROR);
    }
    transcript_add(c, msg, len);
    c->state = TLS_CONN_DONE;
    return 0;
}

// ---------------------------------------------------------------------------
// The Ns
// ---------------------------------------------------------------------------

// Which connection holds which borrow, owned by one instance (internal linkage). The persistent
// end is never given back, and init runs again every time a slot's connection is replaced, so the
// borrow is taken once per connection and reused. One named owner, unreachable cross-TU.
typedef struct
{
    const TlsConn *owner[MAX_TLS_CONNS];
    uint8_t *region[MAX_TLS_CONNS];
    uint8_t count;
} TlsConnPoolCtx;
static TlsConnPoolCtx s_tls_conn;

// The bytes @p c runs out of: the ones it already holds, or the next borrow. NULL when every slot
// is spoken for.
static uint8_t *slot_region(const TlsConn *c)
{
    for (uint8_t i = 0; i < s_tls_conn.count; i++)
    {
        if (s_tls_conn.owner[i] == c)
        {
            return s_tls_conn.region[i];
        }
    }
    if (s_tls_conn.count >= MAX_TLS_CONNS)
    {
        return NULL;
    }
    pc_span b = secure.persist_span(PC_TLS_CONN_BORROW);
    if (!pc_span_ok(b))
    {
        return NULL;
    }
    s_tls_conn.owner[s_tls_conn.count] = c;
    s_tls_conn.region[s_tls_conn.count] = b.buf;
    s_tls_conn.count++;
    return b.buf;
}

static proto_bool conn_init(TlsConn *c, TlsRole role, const TlsConnConfig *cfg)
{
    uint8_t *base = slot_region(c);
    if (base == NULL)
    {
        return PROTO_FALSE;
    }
    pc_secure_wipe(c, sizeof(*c));
    pc_secure_wipe(base, PC_TLS_CONN_BORROW); // the previous tenant's key material does not carry over
    c->tx = base + TLS_OFF_TX;
    c->rx = base + TLS_OFF_RX;
    c->terms = base + TLS_OFF_TERMS;
    c->hash_work = base + TLS_OFF_HASH;
    c->sign_work = base + TLS_OFF_SIGN;
    c->hello = (Tls13ClientHello *)(base + TLS_OFF_HELLO);
    c->cfg = cfg;
    c->role = role;
    c->state = TLS_CONN_START;
    pc_sha256_init(&c->transcript, c->hash_work);
    return pc_tls13_ks_early(&TLS13_KDF, &c->ks, base + TLS_OFF_KS);
}

// The client half needs the mirror of pc_tls13_msg - a ClientHello builder and a ServerHello
// parser - which that module does not have yet. Until it does, a client connection refuses.
static size_t conn_start(TlsConn *c, uint8_t *out, size_t out_cap)
{
    (void)out;
    (void)out_cap;
    c->state = TLS_CONN_FAILED;
    c->alert = TLS_ALERT_INTERNAL_ERROR;
    return 0;
}

// The worker filled RX with one record and says how much. A ClientHello is in the clear, so it is
// read where it lies; everything after it opens into TX, which builds nothing while a received
// message stands in it.
static int conn_process(TlsConn *c, size_t rx_len, uint8_t *out, size_t out_cap)
{
    if (c->state == TLS_CONN_FAILED || c->role == TLS_ROLE_CLIENT)
    {
        return -1;
    }
    TlsPlaintext pt = {0};
    if (TlsRecord.plaintext_parse(c->rx, rx_len, &pt) == 0)
    {
        return fail(c, TLS_ALERT_DECODE_ERROR);
    }

    const uint8_t *msg = pt.fragment;
    size_t len = pt.frag_len;
    uint8_t inner_type = pt.content_type;
    if (inner_type == PC_TLS_CT_APPLICATION_DATA && c->hs_keys_ready)
    {
        TlsCiphertext info = {0};
        if (!TlsRecord.unprotect(&c->hs_rx, c->rx, rx_len, c->tx, PC_TLS_CONN_MSG_CAP, &info))
        {
            return fail(c, TLS_ALERT_DECRYPT_ERROR);
        }
        msg = c->tx;
        len = info.pt_len;
        inner_type = info.content_type;
    }

    if (inner_type == PC_TLS_CT_CHANGE_CIPHER_SPEC)
    {
        return 0; // middlebox compatibility record, outside the transcript (sec 5)
    }
    if (inner_type != PC_TLS_CT_HANDSHAKE || len < TLS_HS_HDR_LEN || len != TLS_HS_HDR_LEN + hs_body_len(msg))
    {
        return fail(c, TLS_ALERT_ILLEGAL_PARAMETER);
    }
    if (c->state == TLS_CONN_START && msg[0] == TLS_HS_CLIENT_HELLO)
    {
        return server_on_client_hello(c, msg, len, out, out_cap);
    }
    if (c->state == TLS_CONN_WAIT_FINISHED)
    {
        return server_on_finished(c, msg, len);
    }
    return fail(c, TLS_ALERT_ILLEGAL_PARAMETER);
}

static proto_bool conn_established(const TlsConn *c)
{
    return c->state == TLS_CONN_DONE;
}

static uint8_t conn_alert(const TlsConn *c)
{
    return c->alert;
}

static size_t conn_seal_app(TlsConn *c, const uint8_t *data, size_t len, uint8_t *out, size_t out_cap)
{
    if (!c->ap_keys_ready)
    {
        return 0;
    }
    return TlsRecord.protect(&c->ap_tx, PC_TLS_CT_APPLICATION_DATA, data, len, out, out_cap);
}

static proto_bool conn_open_app(TlsConn *c, const uint8_t *rec, size_t rec_len, uint8_t *out, size_t out_cap,
                                size_t *out_len)
{
    if (!c->ap_keys_ready)
    {
        return PROTO_FALSE;
    }
    TlsCiphertext info = {0};
    if (!TlsRecord.unprotect(&c->ap_rx, rec, rec_len, out, out_cap, &info))
    {
        return PROTO_FALSE;
    }
    if (out_len)
    {
        *out_len = info.pt_len;
    }
    return info.content_type == PC_TLS_CT_APPLICATION_DATA;
}

// Designated, so a member's position in the struct does not decide what it binds to.
const TlsConnNs TlsConnection = {.init = conn_init,
                                 .start = conn_start,
                                 .process = conn_process,
                                 .established = conn_established,
                                 .alert = conn_alert,
                                 .seal_app = conn_seal_app,
                                 .open_app = conn_open_app};

#endif // PC_TLS_SOFTWARE
