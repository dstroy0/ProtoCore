// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file handshake.c
 * @brief TLS 1.3 handshake driver over the stream record layer. See handshake.h.
 */

#include "network_drivers/tls/handshake/handshake.h"

#if PROTOCORE_TLS_SOFTWARE

#include "crypto/asymmetric/curve25519.h" // protocore_x25519, protocore_x25519_base
#include "crypto/ct_eq.h"                 // protocore_ct_eq: the Finished compare
#include "mmgr/rawmemcpy.h"               // raw.read
#include "mmgr/secure.h"                  // the borrow this driver runs out of, and protocore_secure_wipe

// The secure-pool term this file declares against PROTOCORE_SECURE_ARENA_SIZE: one borrow per TLS
// connection, taken from the persistent end and held for the life of the connection.
#define PROTOCORE_TLS_CONN_BORROW                                                                                      \
    ((size_t)PROTOCORE_TLS_CONN_MSG_CAP + PROTOCORE_TLS_CONN_REC_CAP + PROTOCORE_TLS_CONN_TERMS_CAP +                  \
     PROTOCORE_TLS_CONN_STATE_CAP)
static_assert(PROTOCORE_WORK_TLS_CONN >= (size_t)MAX_TLS_CONNS * PROTOCORE_TLS_CONN_BORROW,
              "PROTOCORE_WORK_TLS_CONN must cover one TX + RX + terms + state borrow per TLS connection: raise it in "
              "protocore_config.h");
static_assert(PROTOCORE_SHA256_BORROW + sizeof(Tls13ClientHello) + PROTOCORE_TLS13_KS_BORROW +
                      PROTOCORE_SHA512_BORROW <=
                  PROTOCORE_TLS_CONN_STATE_CAP,
              "PROTOCORE_TLS_CONN_STATE_CAP must cover the transcript's working bytes, the parsed ClientHello, the "
              "key schedule and the Ed25519 signature's SHA-512: raise it in protocore_config.h");

// Offsets into the one borrow.
#define TLS_OFF_TX 0
#define TLS_OFF_RX ((size_t)PROTOCORE_TLS_CONN_MSG_CAP)
#define TLS_OFF_TERMS (TLS_OFF_RX + PROTOCORE_TLS_CONN_REC_CAP)
#define TLS_OFF_HASH (TLS_OFF_TERMS + PROTOCORE_TLS_CONN_TERMS_CAP)
#define TLS_OFF_HELLO (TLS_OFF_HASH + PROTOCORE_SHA256_BORROW)
#define TLS_OFF_KS (TLS_OFF_HELLO + sizeof(Tls13ClientHello))
#define TLS_OFF_SIGN (TLS_OFF_KS + PROTOCORE_TLS13_KS_BORROW)

// The profile in handshake.h: the portable arm authenticates by RFC 7250 raw public key, so the
// Certificate message it builds is the RPK one.
static_assert(PROTOCORE_ENABLE_TLS_RPK,
              "PROTOCORE_TLS_SOFTWARE authenticates by RFC 7250 raw public key: set PROTOCORE_ENABLE_TLS_RPK");

// RFC 8446 sec 6.2 alerts this driver raises.
#define TLS_ALERT_UNEXPECTED_MESSAGE 10
#define TLS_ALERT_HANDSHAKE_FAILURE 40
#define TLS_ALERT_DECODE_ERROR 50
#define TLS_ALERT_DECRYPT_ERROR 51
#define TLS_ALERT_INTERNAL_ERROR 80
#define TLS_ALERT_NO_APPLICATION_PROTOCOL 120

/** @brief Handshake message header: msg_type(1) + 24-bit length. */
#define TLS_HS_HDR_LEN 4

// Offsets into TlsConn::terms. Each term is one TLS13_SECRET_LEN value.
#define TLS_TERM_SHARE 0                       // this end's X25519 public key_share
#define TLS_TERM_SECRET TLS13_SECRET_LEN       // the X25519 shared secret
#define TLS_TERM_HASH (2 * TLS13_SECRET_LEN)   // the transcript hash the step in hand needs
#define TLS_TERM_MAC (3 * TLS13_SECRET_LEN)    // the Finished MAC built, or the one expected
#define TLS_TERM_HS_FIN (4 * TLS13_SECRET_LEN) // Transcript-Hash(CH..server Finished)

/**
 * @brief The driver's state - what TlsConnNs points at.
 *
 * @var TlsConnInternal::ns  the handle a caller sets a call's members on
 * @var TlsConnInternal::c   the connection the steps below act on, taken from the handle per call
 */
struct TlsConnInternal
{
    TlsConnNs *ns;
    TlsConn *c;
};

static struct TlsConnInternal s_conn = {.ns = &TlsConnection, .c = NULL};

// Point ctx->c at the connection the handle names.
static void bind_conn(struct TlsConnInternal *restrict ctx)
{
    ctx->c = ctx->ns->conn;
}

// Fail the connection with an alert and report it as a driver error.
static void fail(struct TlsConnInternal *restrict ctx, uint8_t alert)
{
    ctx->c->state = TLS_CONN_FAILED;
    ctx->c->alert = alert;
    ctx->ns->i32 = -1;
}

// Fold a whole handshake message (header included) into the running Transcript-Hash.
static void transcript_add(struct TlsConnInternal *restrict ctx, const uint8_t *msg, size_t len)
{
    protocore_sha256_update(&ctx->c->transcript, msg, len);
}

// The Transcript-Hash so far into terms[off]. Finalizing compresses the padded blocks into a copy of
// the state, so the running context is untouched and keeps taking messages.
static void transcript_peek(struct TlsConnInternal *restrict ctx, size_t off)
{
    protocore_sha256_final(&ctx->c->transcript, ctx->c->terms + off);
}

// The body length a handshake message header declares.
static size_t hs_body_len(const uint8_t *msg)
{
    return ((size_t)msg[1] << 16) | ((size_t)msg[2] << 8) | (size_t)msg[3];
}

// Derive one direction's record keys from a traffic secret.
static void keys_derive(TlsRecordKeys *keys, const uint8_t *secret)
{
    TlsRecord.key.keys = keys;
    TlsRecord.key.cipher = TLS_CIPHER_AES_128_GCM_SHA256;
    TlsRecord.key.secret = secret;
    TlsRecord.keys_derive(TlsRecord.internal);
}

// Seal one record under keys; bytes written to out, or 0.
static size_t record_seal(TlsRecordKeys *keys, uint8_t content_type, const uint8_t *pt, size_t pt_len, uint8_t *out,
                          size_t out_cap)
{
    TlsRecord.key.keys = keys;
    TlsRecord.content_type = content_type;
    TlsRecord.sealed.pt = pt;
    TlsRecord.sealed.pt_len = pt_len;
    TlsRecord.out_args.out = out;
    TlsRecord.out_args.out_cap = out_cap;
    TlsRecord.protect(TlsRecord.internal);
    return TlsRecord.n;
}

// Open one received record under keys into out; false on an AEAD failure.
static proto_bool record_open(TlsRecordKeys *keys, const uint8_t *rec, size_t rec_len, uint8_t *out, size_t out_cap,
                              TlsCiphertext *info)
{
    TlsRecord.key.keys = keys;
    TlsRecord.sealed.rec = rec;
    TlsRecord.sealed.rec_len = rec_len;
    TlsRecord.sealed.info = info;
    TlsRecord.out_args.out = out;
    TlsRecord.out_args.out_cap = out_cap;
    TlsRecord.unprotect(TlsRecord.internal);
    return TlsRecord.ok;
}

// The Finished verify_data over base_secret and the transcript hash at off, into terms[TLS_TERM_MAC].
static void finished_mac(struct TlsConnInternal *restrict ctx, const uint8_t *base_secret, size_t off)
{
    Tls13Ks.bind.ks = &ctx->c->ks;
    Tls13Ks.finished_args.base_secret = base_secret;
    Tls13Ks.finished_args.transcript_hash = ctx->c->terms + off;
    Tls13Ks.finished_args.out = ctx->c->terms + TLS_TERM_MAC;
    Tls13Ks.finished_mac(Tls13Ks.internal);
}

// Fold the message standing in TX into the transcript and seal it under the handshake write keys.
// Bytes written to out, or 0.
static size_t emit_encrypted(struct TlsConnInternal *restrict ctx, size_t msg_len, uint8_t *out, size_t out_cap)
{
    if (msg_len == 0)
    {
        return 0;
    }
    transcript_add(ctx, ctx->c->tx, msg_len);
    return record_seal(&ctx->c->hs_tx, PROTOCORE_TLS_CT_HANDSHAKE, ctx->c->tx, msg_len, out, out_cap);
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

// The whole server answer to a ClientHello: ServerHello in the clear, then the encrypted flight.
// Bytes written to out, or a negative alert-bearing failure, in ns->i32.
static void server_flight(struct TlsConnInternal *restrict ctx)
{
    TlsConn *c = ctx->c;
    uint8_t *out = ctx->ns->out_args.out;
    const size_t out_cap = ctx->ns->out_args.out_cap;

    protocore_x25519_base(c->terms + TLS_TERM_SHARE, c->cfg->ephemeral_priv);
    protocore_x25519(c->terms + TLS_TERM_SECRET, c->cfg->ephemeral_priv, c->hello->client_x25519);

    size_t off = 0;

    // ServerHello travels as TLSPlaintext: the keys it establishes do not protect it.
    size_t n = protocore_tls13_build_server_hello(
        c->tx, PROTOCORE_TLS_CONN_MSG_CAP, c->cfg->random, c->hello->session_id, c->hello->session_id_len,
        c->terms + TLS_TERM_SHARE, TLS13_SECRET_LEN, TLS_GROUP_X25519, PROTO_FALSE, NULL, 0);
    if (n == 0)
    {
        fail(ctx, TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    transcript_add(ctx, c->tx, n);
    TlsRecord.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecord.plain.fragment = c->tx;
    TlsRecord.plain.frag_len = n;
    TlsRecord.out_args.out = out + off;
    TlsRecord.out_args.out_cap = out_cap - off;
    TlsRecord.plaintext_build(TlsRecord.internal);
    size_t w = TlsRecord.n;
    if (w == 0)
    {
        fail(ctx, TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    off += w;

    // Everything from here is protected by the handshake traffic keys, keyed off CH..SH.
    transcript_peek(ctx, TLS_TERM_HASH);
    Tls13Ks.bind.ks = &c->ks;
    Tls13Ks.step.ecdhe = c->terms + TLS_TERM_SECRET;
    Tls13Ks.step.ecdhe_len = TLS13_SECRET_LEN;
    Tls13Ks.step.ch_sh_hash = c->terms + TLS_TERM_HASH;
    Tls13Ks.handshake(Tls13Ks.internal);
    keys_derive(&c->hs_tx, c->ks.s + TLS13_KS_SERVER_HS);
    keys_derive(&c->hs_rx, c->ks.s + TLS13_KS_CLIENT_HS);
    c->hs_keys_ready = PROTO_TRUE;

    n = protocore_tls13_build_encrypted_extensions_empty(c->tx, PROTOCORE_TLS_CONN_MSG_CAP, PROTO_TRUE, c->alpn);
    w = emit_encrypted(ctx, n, out + off, out_cap - off);
    if (w == 0)
    {
        fail(ctx, TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    off += w;

    n = protocore_tls13_build_certificate_rpk(c->tx, PROTOCORE_TLS_CONN_MSG_CAP, c->cfg->ed25519_pub);
    w = emit_encrypted(ctx, n, out + off, out_cap - off);
    if (w == 0)
    {
        fail(ctx, TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    off += w;

    // CertificateVerify signs the transcript through the Certificate message.
    transcript_peek(ctx, TLS_TERM_HASH);
    n = protocore_tls13_build_cert_verify(c->sign_work, c->tx, PROTOCORE_TLS_CONN_MSG_CAP, c->terms + TLS_TERM_HASH,
                                          c->cfg->ed25519_seed);
    w = emit_encrypted(ctx, n, out + off, out_cap - off);
    if (w == 0)
    {
        fail(ctx, TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    off += w;

    // Finished covers the transcript through CertificateVerify.
    transcript_peek(ctx, TLS_TERM_HASH);
    finished_mac(ctx, c->ks.s + TLS13_KS_SERVER_HS, TLS_TERM_HASH);
    n = protocore_tls13_build_finished(c->tx, PROTOCORE_TLS_CONN_MSG_CAP, c->terms + TLS_TERM_MAC);
    w = emit_encrypted(ctx, n, out + off, out_cap - off);
    if (w == 0)
    {
        fail(ctx, TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    off += w;

    // The application keys are keyed off CH..server Finished, which is the transcript right now.
    transcript_peek(ctx, TLS_TERM_HS_FIN);
    Tls13Ks.bind.ks = &c->ks;
    Tls13Ks.step.ch_sfin_hash = c->terms + TLS_TERM_HS_FIN;
    Tls13Ks.master(Tls13Ks.internal);
    keys_derive(&c->ap_tx, c->ks.s + TLS13_KS_SERVER_AP);
    keys_derive(&c->ap_rx, c->ks.s + TLS13_KS_CLIENT_AP);
    c->ap_keys_ready = PROTO_TRUE;

    c->state = TLS_CONN_WAIT_FINISHED;
    ctx->ns->i32 = (int)off;
}

// The first configured protocol that also appears in the client's ProtocolNameList, or NULL. The
// list is name(1) then that many bytes, after a 2-byte total length (RFC 7301 sec 3.1). Server
// preference decides, so the configured order is the one walked first.
static const char *alpn_select(const TlsConnConfig *cfg, const uint8_t *list, size_t list_len)
{
    if (!cfg || !cfg->alpn || !list || list_len < 2)
    {
        return NULL;
    }
    for (uint8_t k = 0; k < cfg->alpn_count; k++)
    {
        const char *want = cfg->alpn[k];
        const size_t want_len = str.len(want, 255);
        size_t i = 2;
        while (i < list_len)
        {
            const size_t nl = list[i++];
            if (i + nl > list_len)
            {
                return NULL; // malformed list: nothing selectable
            }
            if (nl == want_len && mem.cmp(list + i, want, nl) == 0)
            {
                return want;
            }
            i += nl;
        }
    }
    return NULL;
}

// A ClientHello arrived whole. Check it against the profile and answer it.
static void server_on_client_hello(struct TlsConnInternal *restrict ctx, const uint8_t *msg, size_t len)
{
    TlsConn *c = ctx->c;
    if (!protocore_tls13_parse_client_hello(msg, len, c->hello, PROTO_FALSE))
    {
        fail(ctx, TLS_ALERT_DECODE_ERROR);
        return;
    }
    // One profile: TLS 1.3, X25519 with the share up front, Ed25519. No HelloRetryRequest - the
    // stream side has no cookie to bind, so a client that omits the share is refused.
    if (!c->hello->offers_tls13 || !c->hello->offers_x25519 || !c->hello->has_key_share || !c->hello->offers_ed25519 ||
        !c->hello->offers_aes128gcm_sha256)
    {
        fail(ctx, TLS_ALERT_HANDSHAKE_FAILURE);
        return;
    }
    // ALPN (RFC 7301 sec 3.2): take the first configured protocol the client also offers. A client
    // that offered the extension and shares none of them ends the handshake here.
    c->alpn = alpn_select(c->cfg, c->hello->alpn_list, c->hello->alpn_list_len);
    if (c->hello->alpn_list && c->cfg->alpn && !c->alpn)
    {
        fail(ctx, TLS_ALERT_NO_APPLICATION_PROTOCOL);
        return;
    }
    transcript_add(ctx, msg, len);
    server_flight(ctx);
}

// The client Finished closes the handshake: its MAC covers the transcript through server Finished.
static void server_on_finished(struct TlsConnInternal *restrict ctx, const uint8_t *msg, size_t len)
{
    TlsConn *c = ctx->c;
    if (msg[0] != TLS_HS_FINISHED || hs_body_len(msg) != TLS13_SECRET_LEN || len != TLS_HS_HDR_LEN + TLS13_SECRET_LEN)
    {
        fail(ctx, TLS_ALERT_DECODE_ERROR);
        return;
    }
    finished_mac(ctx, c->ks.s + TLS13_KS_CLIENT_HS, TLS_TERM_HS_FIN);
    if (!protocore_ct_eq(c->terms + TLS_TERM_MAC, msg + TLS_HS_HDR_LEN, TLS13_SECRET_LEN))
    {
        fail(ctx, TLS_ALERT_DECRYPT_ERROR);
        return;
    }
    transcript_add(ctx, msg, len);
    c->state = TLS_CONN_DONE;
    ctx->ns->i32 = 0;
}

// ---------------------------------------------------------------------------
// The Ns
// ---------------------------------------------------------------------------

// The connection's persistent storage, split by offset. One borrow from the secure pool's
// persistent end on first use, kept for the connection's life, so a connection that is initialised
// again reuses the bytes it already holds.
static proto_bool slot_storage(struct TlsConnInternal *restrict ctx)
{
    TlsConn *c = ctx->c;
    if (c->tx != NULL)
    {
        return PROTO_TRUE;
    }
    protocore_span b = secure.persist_span(PROTOCORE_TLS_CONN_BORROW);
    if (!span.ok(b))
    {
        return PROTO_FALSE;
    }
    c->tx = b.buf + TLS_OFF_TX;
    c->rx = b.buf + TLS_OFF_RX;
    c->terms = b.buf + TLS_OFF_TERMS;
    c->hash_work = b.buf + TLS_OFF_HASH;
    c->sign_work = b.buf + TLS_OFF_SIGN;
    c->ks_work = b.buf + TLS_OFF_KS;
    c->hello = (Tls13ClientHello *)(b.buf + TLS_OFF_HELLO);
    return PROTO_TRUE;
}

static void conn_init(struct TlsConnInternal *restrict ctx)
{
    bind_conn(ctx);
    TlsConn *c = ctx->c;
    if (!slot_storage(ctx))
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    // The bytes carry the previous tenant's key material; the pointers to them stay, or the next
    // init would ask the persistent end for a second borrow it never gives back.
    protocore_secure_wipe(c->tx, PROTOCORE_TLS_CONN_BORROW);
    protocore_secure_wipe(&c->ks, sizeof(c->ks));
    protocore_secure_wipe(&c->hs_tx, sizeof(c->hs_tx));
    protocore_secure_wipe(&c->hs_rx, sizeof(c->hs_rx));
    protocore_secure_wipe(&c->ap_tx, sizeof(c->ap_tx));
    protocore_secure_wipe(&c->ap_rx, sizeof(c->ap_rx));
    c->hs_keys_ready = PROTO_FALSE;
    c->ap_keys_ready = PROTO_FALSE;
    c->alert = 0;
    c->cfg = ctx->ns->init_args.cfg;
    c->role = ctx->ns->init_args.role;
    c->state = TLS_CONN_START;
    protocore_sha256_init(&c->transcript, c->hash_work);

    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &c->ks;
    Tls13Ks.bind.s = c->ks_work;
    Tls13Ks.early(Tls13Ks.internal);
    ctx->ns->ok = Tls13Ks.ok;
}

// The client half needs the mirror of protocore_tls13_msg - a ClientHello builder and a ServerHello
// parser - which that module does not have yet. Until it does, a client connection refuses.
static void conn_start(struct TlsConnInternal *restrict ctx)
{
    bind_conn(ctx);
    ctx->c->state = TLS_CONN_FAILED;
    ctx->c->alert = TLS_ALERT_INTERNAL_ERROR;
    ctx->ns->n = 0;
}

// The worker filled RX with one record and says how much. A ClientHello is in the clear, so it is
// read where it lies; everything after it opens into TX, which builds nothing while a received
// message stands in it.
static void conn_process(struct TlsConnInternal *restrict ctx)
{
    bind_conn(ctx);
    TlsConn *c = ctx->c;
    const size_t rx_len = ctx->ns->io.rx_len;

    if (c->state == TLS_CONN_FAILED || c->role == TLS_ROLE_CLIENT)
    {
        ctx->ns->i32 = -1;
        return;
    }
    TlsPlaintext pt = {0};
    TlsRecord.sealed.rec = c->rx;
    TlsRecord.sealed.rec_len = rx_len;
    TlsRecord.plain.view = &pt;
    TlsRecord.plaintext_parse(TlsRecord.internal);
    if (TlsRecord.n == 0)
    {
        fail(ctx, TLS_ALERT_DECODE_ERROR);
        return;
    }

    const uint8_t *msg = pt.fragment;
    size_t len = pt.frag_len;
    uint8_t inner_type = pt.content_type;
    if (inner_type == PROTOCORE_TLS_CT_APPLICATION_DATA && c->hs_keys_ready)
    {
        TlsCiphertext info = {0};
        if (!record_open(&c->hs_rx, c->rx, rx_len, c->tx, PROTOCORE_TLS_CONN_MSG_CAP, &info))
        {
            fail(ctx, TLS_ALERT_DECRYPT_ERROR);
            return;
        }
        msg = c->tx;
        len = info.pt_len;
        inner_type = info.content_type;
    }

    if (inner_type == PROTOCORE_TLS_CT_CHANGE_CIPHER_SPEC)
    {
        ctx->ns->i32 = 0; // middlebox compatibility record, outside the transcript (sec 5)
        return;
    }
    // sec 6.2: a record that is not a handshake record here is premature application data or an
    // unexpected record type, which is unexpected_message; a header whose 24-bit length does not
    // frame the message is a length past the message boundary, which sec 6 makes decode_error.
    if (inner_type != PROTOCORE_TLS_CT_HANDSHAKE)
    {
        fail(ctx, TLS_ALERT_UNEXPECTED_MESSAGE);
        return;
    }
    if (len < TLS_HS_HDR_LEN || len != TLS_HS_HDR_LEN + hs_body_len(msg))
    {
        fail(ctx, TLS_ALERT_DECODE_ERROR);
        return;
    }
    if (c->state == TLS_CONN_START && msg[0] == TLS_HS_CLIENT_HELLO)
    {
        server_on_client_hello(ctx, msg, len);
        return;
    }
    if (c->state == TLS_CONN_WAIT_FINISHED)
    {
        server_on_finished(ctx, msg, len);
        return;
    }
    // sec 4: a handshake message received in an unexpected order is unexpected_message.
    fail(ctx, TLS_ALERT_UNEXPECTED_MESSAGE);
}

static void conn_established(struct TlsConnInternal *restrict ctx)
{
    bind_conn(ctx);
    ctx->ns->ok = (ctx->c->state == TLS_CONN_DONE);
}

static void conn_alert(struct TlsConnInternal *restrict ctx)
{
    bind_conn(ctx);
    ctx->ns->u8 = ctx->c->alert;
}

static void conn_seal_app(struct TlsConnInternal *restrict ctx)
{
    bind_conn(ctx);
    ctx->ns->n = 0;
    if (!ctx->c->ap_keys_ready)
    {
        return;
    }
    ctx->ns->n = record_seal(&ctx->c->ap_tx, PROTOCORE_TLS_CT_APPLICATION_DATA, ctx->ns->io.data, ctx->ns->io.len,
                             ctx->ns->out_args.out, ctx->ns->out_args.out_cap);
}

static void conn_open_app(struct TlsConnInternal *restrict ctx)
{
    bind_conn(ctx);
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->c->ap_keys_ready)
    {
        return;
    }
    TlsCiphertext info = {0};
    if (!record_open(&ctx->c->ap_rx, ctx->ns->io.rec, ctx->ns->io.rec_len, ctx->ns->out_args.out,
                     ctx->ns->out_args.out_cap, &info))
    {
        return;
    }
    if (ctx->ns->out_args.out_len)
    {
        *ctx->ns->out_args.out_len = info.pt_len;
    }
    ctx->ns->ok = (info.content_type == PROTOCORE_TLS_CT_APPLICATION_DATA);
}

// Designated, so a member's position in the struct does not decide what it binds to.
TlsConnNs TlsConnection = {.init = conn_init,
                           .start = conn_start,
                           .process = conn_process,
                           .established = conn_established,
                           .alert = conn_alert,
                           .seal_app = conn_seal_app,
                           .open_app = conn_open_app,
                           .internal = &s_conn};

#endif // PROTOCORE_TLS_SOFTWARE
