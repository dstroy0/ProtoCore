// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file handshake.c
 * @brief TLS 1.3 handshake driver over the stream record layer. See handshake.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_TLS

#include "network_drivers/tls/tls.h"

#include "crypto/asymmetric/curve25519/curve25519.h" // protocore_x25519, protocore_x25519_base
#include "crypto/asymmetric/ed25519/ed25519.h"       // Ed25519: the peer's CertificateVerify
#include "crypto/ct_eq.h"                            // protocore_ct_eq: the Finished compare
#include "crypto/x509/x509_types/x509_types.h"       // X509Cert, protocore_x509_sig_alg: both credentials
#include "crypto/x509/x509_verify/x509_verify.h"     // X509Verify: CertificateVerify, and the path to an anchor
#if PROTOCORE_ENABLE_X509
#include "crypto/x509/x509/x509.h" // X509: the peer's certificate and the name it speaks for
#endif
#include "mmgr/protomem/protomem.h"   // mem.cpy: the peer's presented key
#include "mmgr/protostr/protostr.h"   // str.len: the ALPN protocol name lengths
#include "mmgr/rawmemcpy/rawmemcpy.h" // raw.read
#include "mmgr/secure/secure.h"       // the borrow this driver runs out of, and protocore_secure_wipe
#include "network_drivers/presentation/http/http3/tls13_rpk/tls13_rpk.h" // the RFC 7250 RawPublicKey credential

// The secure-pool term this file declares against PROTOCORE_SECURE_ARENA_SIZE: one borrow per TLS
// connection, taken from the persistent end and held for the life of the connection.
#define PROTOCORE_TLS_CONN_BORROW                                                                                      \
    ((size_t)PROTOCORE_TLS_CONN_MSG_CAP + PROTOCORE_TLS_CONN_REC_CAP + PROTOCORE_TLS_CONN_TERMS_CAP +                  \
     PROTOCORE_TLS_CONN_STATE_CAP + PROTOCORE_TLS_CONN_PEERKEY_CAP)
static_assert(PROTOCORE_WORK_TLS_CONN >= (size_t)MAX_TLS_CONNS * PROTOCORE_TLS_CONN_BORROW,
              "PROTOCORE_WORK_TLS_CONN must cover one TX + RX + terms + state borrow per TLS connection: raise it in "
              "protocore_config.h");
static_assert(PROTOCORE_TLS13_TRANSCRIPT_BORROW + sizeof(Tls13ClientHello) + PROTOCORE_TLS13_KS_BORROW +
                      PROTOCORE_SHA512_BORROW <=
                  PROTOCORE_TLS_CONN_STATE_CAP,
              "PROTOCORE_TLS_CONN_STATE_CAP must cover the transcript's working bytes, the parsed ClientHello, the "
              "key schedule and the Ed25519 signature's SHA-512: raise it in protocore_config.h");

// Offsets into the one borrow.
#define TLS_OFF_TX 0
#define TLS_OFF_RX ((size_t)PROTOCORE_TLS_CONN_MSG_CAP)
#define TLS_OFF_TERMS (TLS_OFF_RX + PROTOCORE_TLS_CONN_REC_CAP)
#define TLS_OFF_HASH (TLS_OFF_TERMS + PROTOCORE_TLS_CONN_TERMS_CAP)
#define TLS_OFF_HELLO (TLS_OFF_HASH + PROTOCORE_TLS13_TRANSCRIPT_BORROW)
#define TLS_OFF_KS (TLS_OFF_HELLO + sizeof(Tls13ClientHello))
#define TLS_OFF_SIGN (TLS_OFF_KS + PROTOCORE_TLS13_KS_BORROW)
#define TLS_OFF_PEERKEY (TLS_OFF_SIGN + PROTOCORE_SHA512_BORROW)

// The peer's subjectPublicKey, outliving the message buffer its Certificate arrived in: the
// algorithm it is in, its length, then the octets.
#define TLS_PEERKEY_ALG(w) ((w)[TLS_OFF_PEERKEY])
#define TLS_PEERKEY_LEN(w) ((size_t)(((w)[TLS_OFF_PEERKEY + 1] << 8) | (w)[TLS_OFF_PEERKEY + 2]))
#define TLS_PEERKEY_P(w) ((w) + TLS_OFF_PEERKEY + 4)
#define TLS_PEERKEY_MAX ((size_t)PROTOCORE_TLS_CONN_PEERKEY_CAP - 4u)

// The profile in handshake.h: the portable arm authenticates by RFC 7250 raw public key, so the
// Certificate message it builds is the RPK one.
static_assert(PROTOCORE_ENABLE_TLS_RPK,
              "PROTOCORE_ENABLE_TLS authenticates by RFC 7250 raw public key: set PROTOCORE_ENABLE_TLS_RPK");

// RFC 8446 sec 6.2 alerts this driver raises.
#define TLS_ALERT_UNEXPECTED_MESSAGE 10
#define TLS_ALERT_HANDSHAKE_FAILURE 40
#define TLS_ALERT_ILLEGAL_PARAMETER 47
#define TLS_ALERT_DECODE_ERROR 50
#define TLS_ALERT_DECRYPT_ERROR 51
#define TLS_ALERT_INTERNAL_ERROR 80
#define TLS_ALERT_NO_APPLICATION_PROTOCOL 120

/** @brief Handshake message header: msg_type(1) + 24-bit length. */
#define TLS_HS_HDR_LEN 4

// Offsets into TlsConn::terms. Each term is one TLS13_SECRET_MAX slot: the three hash-length terms
// hold 32 or 48 octets depending on the suite the connection bound, and the two X25519 terms hold 32
// whatever it bound, so the stride is the widest of them and a term never moves.
#define TLS_TERM_SHARE 0                       // this end's X25519 public key_share
#define TLS_TERM_SECRET TLS13_SECRET_MAX       // the X25519 shared secret
#define TLS_TERM_HASH (2 * TLS13_SECRET_MAX)   // the transcript hash the step in hand needs
#define TLS_TERM_MAC (3 * TLS13_SECRET_MAX)    // the Finished MAC built, or the one expected
#define TLS_TERM_HS_FIN (4 * TLS13_SECRET_MAX) // Transcript-Hash(CH..server Finished)

// Fail the connection with an alert and report it as a driver error.
static void fail(uint8_t alert)
{
    TlsConnectionV.conn->state = TLS_CONN_FAILED;
    TlsConnectionV.conn->alert = alert;
    TlsConnectionV.i32 = -1;
}

// Start the Transcript-Hash, under whichever hash the connection's suite binds.
static void transcript_start(void)
{
    Tls13KsV.bind.ks = &TlsConnectionV.conn->ks;
    Tls13Ks.transcript_init(TlsConnectionV.conn->transcript);
}

// Fold a whole handshake message (header included) into the running Transcript-Hash.
static void transcript_add(const uint8_t *msg, size_t len)
{
    Tls13KsV.bind.ks = &TlsConnectionV.conn->ks;
    Tls13KsV.transcript_args.data = msg;
    Tls13KsV.transcript_args.len = len;
    Tls13Ks.transcript_update(TlsConnectionV.conn->transcript);
}

// The Transcript-Hash so far into terms[off]. Finalizing compresses the padded blocks into a copy of
// the state, so the running context is untouched and keeps taking messages.
static void transcript_peek(size_t off)
{
    Tls13KsV.bind.ks = &TlsConnectionV.conn->ks;
    Tls13KsV.transcript_args.out = TlsConnectionV.conn->terms + off;
    Tls13Ks.transcript_peek(TlsConnectionV.conn->transcript);
}

// The body length a handshake message header declares.
static size_t hs_body_len(const uint8_t *msg)
{
    return ((size_t)msg[1] << 16) | ((size_t)msg[2] << 8) | (size_t)msg[3];
}

// Derive one direction's record keys from a traffic secret.
static void keys_derive(uint8_t *restrict work, TlsRecordKeys *keys, const uint8_t *secret)
{
    TlsRecordV.key.keys = keys;
    TlsRecordV.key.cipher = TlsConnectionV.conn->cfg->cipher;
    TlsRecordV.key.secret = secret;
    TlsRecord.keys_derive(work);
}

// Seal one record under keys; bytes written to out, or 0.
static size_t record_seal(uint8_t *restrict work, TlsRecordKeys *keys, uint8_t content_type, const uint8_t *pt,
                          size_t pt_len, uint8_t *out, size_t out_cap)
{
    TlsRecordV.key.keys = keys;
    TlsRecordV.content_type = content_type;
    TlsRecordV.sealed.pt = pt;
    TlsRecordV.sealed.pt_len = pt_len;
    TlsRecordV.out_args.out = out;
    TlsRecordV.out_args.out_cap = out_cap;
    TlsRecord.protect(work);
    return TlsRecordV.n;
}

// Open one received record under keys into out; false on an AEAD failure.
static proto_bool record_open(uint8_t *restrict work, TlsRecordKeys *keys, const uint8_t *rec, size_t rec_len,
                              uint8_t *out, size_t out_cap, TlsCiphertext *info)
{
    TlsRecordV.key.keys = keys;
    TlsRecordV.sealed.rec = rec;
    TlsRecordV.sealed.rec_len = rec_len;
    TlsRecordV.sealed.info = info;
    TlsRecordV.out_args.out = out;
    TlsRecordV.out_args.out_cap = out_cap;
    TlsRecord.unprotect(work);
    return TlsRecordV.ok;
}

// The Finished verify_data over base_secret and the transcript hash at off, into terms[TLS_TERM_MAC].
static void finished_mac(uint8_t *restrict work, const uint8_t *base_secret, size_t off)
{
    Tls13KsV.bind.ks = &TlsConnectionV.conn->ks;
    Tls13KsV.finished_args.base_secret = base_secret;
    Tls13KsV.finished_args.transcript_hash = TlsConnectionV.conn->terms + off;
    Tls13KsV.finished_args.out = TlsConnectionV.conn->terms + TLS_TERM_MAC;
    Tls13Ks.finished_mac(work);
}

// Fold the message standing in TX into the transcript and seal it under the handshake write keys.
// Bytes written to out, or 0.
static size_t emit_encrypted(uint8_t *restrict work, size_t msg_len, uint8_t *out, size_t out_cap)
{
    if (msg_len == 0)
    {
        return 0;
    }
    transcript_add(TlsConnectionV.conn->tx, msg_len);
    return record_seal(work, &TlsConnectionV.conn->hs_tx, PROTOCORE_TLS_CT_HANDSHAKE, TlsConnectionV.conn->tx, msg_len,
                       out, out_cap);
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

// The whole server answer to a ClientHello: ServerHello in the clear, then the encrypted flight.
// Bytes written to out, or a negative alert-bearing failure, in ns->i32.
static void server_flight(uint8_t *restrict work)
{
    TlsConn *c = TlsConnectionV.conn;
    uint8_t *out = TlsConnectionV.out_args.out;
    const size_t out_cap = TlsConnectionV.out_args.out_cap;

    Curve25519V.x25519_base_args.scalar = c->cfg->ephemeral_priv;
    Curve25519V.x25519_base_args.out = c->terms + TLS_TERM_SHARE;
    Curve25519.x25519_base(c->sign_work);
    Curve25519V.x25519_args.scalar = c->cfg->ephemeral_priv;
    Curve25519V.x25519_args.point = c->hello->client_x25519;
    Curve25519V.x25519_args.out = c->terms + TLS_TERM_SECRET;
    Curve25519.x25519(c->sign_work);
    if (!Curve25519V.ok)
    {
        // RFC 8446 sec 7.4.2: the shared secret came out all-zero, so the client's key share was a
        // point of small order and the secret is a constant it chose. Abort (RFC 7748 sec 6.1).
        fail(TLS_ALERT_ILLEGAL_PARAMETER);
        return;
    }

    size_t off = 0;

    // ServerHello travels as TLSPlaintext: the keys it establishes do not protect it.
    Tls13MsgV.build_server_hello_args.out = c->tx;
    Tls13MsgV.build_server_hello_args.cap = PROTOCORE_TLS_CONN_MSG_CAP;
    Tls13MsgV.build_server_hello_args.random = c->cfg->random;
    Tls13MsgV.build_server_hello_args.session_id = c->hello->session_id;
    Tls13MsgV.build_server_hello_args.session_id_len = c->hello->session_id_len;
    Tls13MsgV.build_server_hello_args.share = c->terms + TLS_TERM_SHARE;
    Tls13MsgV.build_server_hello_args.share_len = TLS_X25519_SHARE_LEN;
    Tls13MsgV.build_server_hello_args.group = TLS_GROUP_X25519;
    Tls13MsgV.build_server_hello_args.suite = protocore_tls_cipher_code(c->cfg->cipher);
    Tls13MsgV.build_server_hello_args.dtls = PROTO_FALSE;
    Tls13MsgV.build_server_hello_args.conn_id = NULL;
    Tls13MsgV.build_server_hello_args.conn_id_len = 0;
    Tls13Msg.build_server_hello(work);
    size_t n = Tls13MsgV.n;
    if (n == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    transcript_add(c->tx, n);
    TlsRecordV.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecordV.plain.fragment = c->tx;
    TlsRecordV.plain.frag_len = n;
    TlsRecordV.out_args.out = out + off;
    TlsRecordV.out_args.out_cap = out_cap - off;
    TlsRecord.plaintext_build(work);
    size_t w = TlsRecordV.n;
    if (w == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    off += w;

    // Everything from here is protected by the handshake traffic keys, keyed off CH..SH.
    transcript_peek(TLS_TERM_HASH);
    Tls13KsV.bind.ks = &c->ks;
    Tls13KsV.step.ecdhe = c->terms + TLS_TERM_SECRET;
    Tls13KsV.step.ecdhe_len = TLS_X25519_SHARE_LEN;
    Tls13KsV.step.ch_sh_hash = c->terms + TLS_TERM_HASH;
    Tls13Ks.handshake(work);
    keys_derive(work, &c->hs_tx, c->ks.s + TLS13_KS_SERVER_HS);
    keys_derive(work, &c->hs_rx, c->ks.s + TLS13_KS_CLIENT_HS);
    c->hs_keys_ready = PROTO_TRUE;

    // A configured certificate is presented as itself; without one this end's credential is the
    // RFC 7250 raw public key, and the negotiated server_certificate_type says which.
    const proto_bool rpk = (c->cfg->cert_der == NULL);
    Tls13MsgV.build_encrypted_extensions_empty_args.out = c->tx;
    Tls13MsgV.build_encrypted_extensions_empty_args.cap = PROTOCORE_TLS_CONN_MSG_CAP;
    Tls13MsgV.build_encrypted_extensions_empty_args.rpk_server_cert = rpk;
    Tls13MsgV.build_encrypted_extensions_empty_args.alpn = c->alpn;
    Tls13Msg.build_encrypted_extensions_empty(work);
    n = Tls13MsgV.n;
    w = emit_encrypted(work, n, out + off, out_cap - off);
    if (w == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    off += w;

    if (rpk)
    {
        Tls13RpkV.build_certificate_args.out = c->tx;
        Tls13RpkV.build_certificate_args.cap = PROTOCORE_TLS_CONN_MSG_CAP;
        Tls13RpkV.build_certificate_args.ed25519_pub = c->cfg->ed25519_pub;
        Tls13Rpk.build_certificate(work);
        n = Tls13RpkV.n;
    }
    else
    {
        Tls13MsgV.build_certificate_args.out = c->tx;
        Tls13MsgV.build_certificate_args.cap = PROTOCORE_TLS_CONN_MSG_CAP;
        Tls13MsgV.build_certificate_args.cert_der = c->cfg->cert_der;
        Tls13MsgV.build_certificate_args.cert_len = c->cfg->cert_len;
        Tls13Msg.build_certificate(work);
        n = Tls13MsgV.n;
    }
    w = emit_encrypted(work, n, out + off, out_cap - off);
    if (w == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    off += w;

    // CertificateVerify signs the transcript through the Certificate message.
    transcript_peek(TLS_TERM_HASH);
    Tls13MsgV.build_cert_verify_args.sign_work = c->sign_work;
    Tls13MsgV.build_cert_verify_args.out = c->tx;
    Tls13MsgV.build_cert_verify_args.cap = PROTOCORE_TLS_CONN_MSG_CAP;
    Tls13MsgV.build_cert_verify_args.transcript_hash = c->terms + TLS_TERM_HASH;
    Tls13MsgV.build_cert_verify_args.hash_len = c->ks.len;
    Tls13MsgV.build_cert_verify_args.seed = c->cfg->ed25519_seed;
    Tls13Msg.build_cert_verify(work);
    n = Tls13MsgV.n;
    w = emit_encrypted(work, n, out + off, out_cap - off);
    if (w == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    off += w;

    // Finished covers the transcript through CertificateVerify.
    transcript_peek(TLS_TERM_HASH);
    finished_mac(work, c->ks.s + TLS13_KS_SERVER_HS, TLS_TERM_HASH);
    Tls13MsgV.build_finished_args.out = c->tx;
    Tls13MsgV.build_finished_args.cap = PROTOCORE_TLS_CONN_MSG_CAP;
    Tls13MsgV.build_finished_args.verify_data = c->terms + TLS_TERM_MAC;
    Tls13MsgV.build_finished_args.verify_len = c->ks.len;
    Tls13Msg.build_finished(work);
    n = Tls13MsgV.n;
    w = emit_encrypted(work, n, out + off, out_cap - off);
    if (w == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    off += w;

    // The application keys are keyed off CH..server Finished, which is the transcript right now.
    transcript_peek(TLS_TERM_HS_FIN);
    Tls13KsV.bind.ks = &c->ks;
    Tls13KsV.step.ch_sfin_hash = c->terms + TLS_TERM_HS_FIN;
    Tls13Ks.master(work);
    keys_derive(work, &c->ap_tx, c->ks.s + TLS13_KS_SERVER_AP);
    keys_derive(work, &c->ap_rx, c->ks.s + TLS13_KS_CLIENT_AP);
    c->ap_keys_ready = PROTO_TRUE;

    c->state = TLS_CONN_WAIT_FINISHED;
    TlsConnectionV.i32 = (int)off;
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

// RFC 8446 sec 4.1.4: the parameters are acceptable but the ClientHello does not carry enough to
// proceed, so the answer is a HelloRetryRequest naming the group whose share is wanted. sec 4.4.1:
// ClientHello1 leaves the transcript as a synthetic message_hash, so the running hash restarts over
// that stand-in before the HelloRetryRequest is folded in.
static void server_hello_retry(uint8_t *restrict work, const uint8_t *msg, size_t len)
{
    TlsConn *c = TlsConnectionV.conn;

    transcript_add(msg, len);
    transcript_peek(TLS_TERM_HASH);
    transcript_start();
    Tls13MsgV.build_message_hash_args.out = c->tx;
    Tls13MsgV.build_message_hash_args.cap = PROTOCORE_TLS_CONN_MSG_CAP;
    Tls13MsgV.build_message_hash_args.ch1_hash = c->terms + TLS_TERM_HASH;
    Tls13Msg.build_message_hash(work);
    size_t n = Tls13MsgV.n;
    if (n == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    transcript_add(c->tx, n);

    // No cookie: the stream side has a connection to bind the retry to, so there is nothing to
    // carry the return-routability check a datagram transport needs.
    Tls13MsgV.build_hello_retry_request_args.out = c->tx;
    Tls13MsgV.build_hello_retry_request_args.cap = PROTOCORE_TLS_CONN_MSG_CAP;
    Tls13MsgV.build_hello_retry_request_args.session_id = c->hello->session_id;
    Tls13MsgV.build_hello_retry_request_args.session_id_len = c->hello->session_id_len;
    Tls13MsgV.build_hello_retry_request_args.selected_group = TLS_GROUP_X25519;
    Tls13MsgV.build_hello_retry_request_args.suite = protocore_tls_cipher_code(c->cfg->cipher);
    Tls13MsgV.build_hello_retry_request_args.cookie = NULL;
    Tls13MsgV.build_hello_retry_request_args.cookie_len = 0;
    Tls13MsgV.build_hello_retry_request_args.dtls = PROTO_FALSE;
    Tls13Msg.build_hello_retry_request(work);
    n = Tls13MsgV.n;
    if (n == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    transcript_add(c->tx, n);

    // A HelloRetryRequest travels as TLSPlaintext, like the ServerHello it shares its format with.
    TlsRecordV.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecordV.plain.fragment = c->tx;
    TlsRecordV.plain.frag_len = n;
    TlsRecordV.out_args.out = TlsConnectionV.out_args.out;
    TlsRecordV.out_args.out_cap = TlsConnectionV.out_args.out_cap;
    TlsRecord.plaintext_build(work);
    if (TlsRecordV.n == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    c->hrr_sent = PROTO_TRUE;
    TlsConnectionV.i32 = (int)TlsRecordV.n;
}

// A ClientHello arrived whole. Check it against the profile and answer it.
static void server_on_client_hello(uint8_t *restrict work, const uint8_t *msg, size_t len)
{
    TlsConn *c = TlsConnectionV.conn;
    Tls13MsgV.parse_client_hello_args.msg = msg;
    Tls13MsgV.parse_client_hello_args.len = len;
    Tls13MsgV.parse_client_hello_args.out = c->hello;
    Tls13MsgV.parse_client_hello_args.dtls = PROTO_FALSE;
    Tls13Msg.parse_client_hello(work);
    if (!Tls13MsgV.ok)
    {
        fail(TLS_ALERT_DECODE_ERROR);
        return;
    }
    // One profile: TLS 1.3, X25519, Ed25519, and the suite this listener was configured with. sec
    // 4.1.1: no overlap in the parameters themselves is a handshake_failure. sec 4.1.3 makes the
    // selection one of the suites the client listed, so a client that did not offer it is refused
    // rather than answered with it.
    const proto_bool offered = protocore_tls_cipher_is384(c->cfg->cipher) ? c->hello->offers_aes256gcm_sha384
                                                                          : c->hello->offers_aes128gcm_sha256;
    if (!c->hello->offers_tls13 || !c->hello->offers_x25519 || !c->hello->offers_ed25519 || !offered)
    {
        fail(TLS_ALERT_HANDSHAKE_FAILURE);
        return;
    }
    // sec 4.1.1: the group is acceptable and the share is absent, which is the HelloRetryRequest
    // case, not a failure. sec 4.1.4: a second one on the same connection never happens.
    if (!c->hello->has_key_share)
    {
        if (c->hrr_sent)
        {
            fail(TLS_ALERT_UNEXPECTED_MESSAGE);
            return;
        }
        server_hello_retry(work, msg, len);
        return;
    }
    // ALPN (RFC 7301 sec 3.2): take the first configured protocol the client also offers. A client
    // that offered the extension and shares none of them ends the handshake here.
    c->alpn = alpn_select(c->cfg, c->hello->alpn_list, c->hello->alpn_list_len);
    if (c->hello->alpn_list && c->cfg->alpn && !c->alpn)
    {
        fail(TLS_ALERT_NO_APPLICATION_PROTOCOL);
        return;
    }
    transcript_add(msg, len);
    server_flight(NULL);
}

// The client Finished closes the handshake: its MAC covers the transcript through server Finished.
static void server_on_finished(uint8_t *restrict work, const uint8_t *msg, size_t len)
{
    TlsConn *c = TlsConnectionV.conn;
    // sec 4.4.4: verify_data is Hash.length octets, so its width is the suite's, not a constant.
    if (msg[0] != TLS_HS_FINISHED || hs_body_len(msg) != c->ks.len || len != TLS_HS_HDR_LEN + c->ks.len)
    {
        fail(TLS_ALERT_DECODE_ERROR);
        return;
    }
    finished_mac(work, c->ks.s + TLS13_KS_CLIENT_HS, TLS_TERM_HS_FIN);
    if (!protocore_ct_eq(c->terms + TLS_TERM_MAC, msg + TLS_HS_HDR_LEN, c->ks.len))
    {
        fail(TLS_ALERT_DECRYPT_ERROR);
        return;
    }
    transcript_add(msg, len);
    c->state = TLS_CONN_DONE;
    TlsConnectionV.i32 = 0;
}

// ---------------------------------------------------------------------------
// The Ns
// ---------------------------------------------------------------------------

// The connection's persistent storage, split by offset. One borrow from the secure pool's
// persistent end on first use, kept for the connection's life, so a connection that is initialised
// again reuses the bytes it already holds.
static proto_bool slot_storage(uint8_t *restrict work)
{
    TlsConn *c = TlsConnectionV.conn;
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

void protocore_tls_connection_init(uint8_t *restrict work)
{
    TlsConn *c = TlsConnectionV.conn;
    if (!slot_storage(work))
    {
        TlsConnectionV.ok = PROTO_FALSE;
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
    c->cfg = TlsConnectionV.init_args.cfg;
    c->role = TlsConnectionV.init_args.role;
    c->state = TLS_CONN_START;
    c->transcript = c->hash_work;

    Tls13KsV.bind.kdf = &TLS13_KDF;
    Tls13KsV.bind.ks = &c->ks;
    Tls13KsV.bind.s = c->ks_work;
    // The suite the caller stated fixes the schedule's hash, and with it the Transcript-Hash the
    // messages fold into (RFC 8446 sec 4.4.1, 7.1). Bound before the transcript starts, because the
    // first message hashed has to go into the right one.
    Tls13KsV.bind.is384 = protocore_tls_cipher_is384(c->cfg->cipher);
    Tls13Ks.early(work);
    transcript_start();
    TlsConnectionV.ok = Tls13KsV.ok;
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

// The ServerHello answers the offer: it fixes the group, so the ECDHE secret and the handshake
// traffic keys both fall out here. This end writes with the client secret and reads with the
// server's - the mirror of what server_flight derives.
static void client_on_server_hello(uint8_t *restrict work, const uint8_t *msg, size_t len)
{
    TlsConn *c = TlsConnectionV.conn;
    Tls13ServerHello sh;
    Tls13MsgV.parse_server_hello_args.msg = msg;
    Tls13MsgV.parse_server_hello_args.len = len;
    Tls13MsgV.parse_server_hello_args.out = &sh;
    Tls13MsgV.parse_server_hello_args.dtls = PROTO_FALSE;
    Tls13Msg.parse_server_hello(work);
    if (!Tls13MsgV.ok)
    {
        fail(TLS_ALERT_DECODE_ERROR);
        return;
    }
    if (sh.is_hrr)
    {
        // A HelloRetryRequest asks for a group other than the one offered, and this arm offers the
        // only one it has (sec 4.1.4).
        fail(TLS_ALERT_HANDSHAKE_FAILURE);
        return;
    }
    // sec 4.1.3: the selected suite must be the one this end offered; anything else is a parameter
    // it never proposed.
    if (!sh.selected_tls13 || sh.cipher_suite != protocore_tls_cipher_code(c->cfg->cipher) || !sh.has_key_share ||
        sh.group != TLS_GROUP_X25519 || sh.share_len != TLS_X25519_SHARE_LEN)
    {
        fail(TLS_ALERT_ILLEGAL_PARAMETER);
        return;
    }

    Curve25519V.x25519_args.scalar = c->cfg->ephemeral_priv;
    Curve25519V.x25519_args.point = sh.share;
    Curve25519V.x25519_args.out = c->terms + TLS_TERM_SECRET;
    Curve25519.x25519(c->sign_work);
    if (!Curve25519V.ok)
    {
        // sec 7.4.2 / RFC 7748 sec 6.1: an all-zero secret means a small-order share.
        fail(TLS_ALERT_ILLEGAL_PARAMETER);
        return;
    }

    transcript_add(msg, len);
    transcript_peek(TLS_TERM_HASH);
    Tls13KsV.bind.ks = &c->ks;
    Tls13KsV.step.ecdhe = c->terms + TLS_TERM_SECRET;
    Tls13KsV.step.ecdhe_len = TLS_X25519_SHARE_LEN;
    Tls13KsV.step.ch_sh_hash = c->terms + TLS_TERM_HASH;
    Tls13Ks.handshake(work);
    keys_derive(work, &c->hs_tx, c->ks.s + TLS13_KS_CLIENT_HS);
    keys_derive(work, &c->hs_rx, c->ks.s + TLS13_KS_SERVER_HS);
    c->hs_keys_ready = PROTO_TRUE;

    c->state = TLS_CONN_WAIT_FLIGHT;
    TlsConnectionV.i32 = 0;
}

// Keep the peer's subjectPublicKey past the message it arrived in. c->tx is the borrow's base, so
// the region is far beyond the buffer the next handshake message overwrites.
static proto_bool peer_key_keep(protocore_x509_key_alg alg, const uint8_t *key, size_t key_len)
{
    uint8_t *w = TlsConnectionV.conn->tx;
    if (key_len > TLS_PEERKEY_MAX)
    {
        return PROTO_FALSE;
    }
    w[TLS_OFF_PEERKEY] = (uint8_t)alg;
    w[TLS_OFF_PEERKEY + 1] = (uint8_t)(key_len >> 8);
    w[TLS_OFF_PEERKEY + 2] = (uint8_t)key_len;
    mem.cpy(TLS_PEERKEY_P(w), key, key_len);
    return PROTO_TRUE;
}

// The peer's Certificate, by whichever credential this connection was configured to accept: an
// X.509 chain to cfg->ca_der (RFC 5280 sec 6.1) plus the RFC 6125 name match, or the RFC 7250 raw
// public key. Either way the key it carries is kept, because CertificateVerify is checked under it.
static void client_on_certificate(uint8_t *restrict work, const uint8_t *msg, size_t len)
{
    TlsConn *c = TlsConnectionV.conn;
    const uint8_t *entry = NULL;
    size_t entry_len = 0;
    Tls13MsgV.parse_certificate_args.msg = msg;
    Tls13MsgV.parse_certificate_args.len = len;
    Tls13MsgV.parse_certificate_args.cert = &entry;
    Tls13MsgV.parse_certificate_args.cert_len = &entry_len;
    Tls13Msg.parse_certificate(work);
    if (!Tls13MsgV.ok)
    {
        fail(TLS_ALERT_DECODE_ERROR);
        return;
    }

#if PROTOCORE_ENABLE_X509
    if (c->cfg->ca_der)
    {
        X509V.parse_args.der = entry;
        X509V.parse_args.len = entry_len;
        X509.parse(work);
        if (!X509V.ok)
        {
            fail(TLS_ALERT_DECODE_ERROR);
            return;
        }
        const X509Cert leaf = X509V.cert;

        X509V.parse_args.der = c->cfg->ca_der;
        X509V.parse_args.len = c->cfg->ca_len;
        X509.parse(work);
        if (!X509V.ok)
        {
            fail(TLS_ALERT_INTERNAL_ERROR); // the anchor this build was given does not parse
            return;
        }
        const X509Cert anchor = X509V.cert;

        // sec 6.1: one link to the anchor - the signature, the validity window, the issuer name and
        // whether the anchor may sign at this depth.
        X509VerifyV.link_args.cert = &leaf;
        X509VerifyV.link_args.issuer = &anchor;
        X509VerifyV.time_args.cert = &leaf;
        X509VerifyV.time_args.now = c->cfg->now;
        X509VerifyV.issuer_args.issuer = &anchor;
        X509VerifyV.issuer_args.depth = 0;
        X509Verify.link(protocore_x509_verify_span());
        if (!X509VerifyV.ok)
        {
            fail(TLS_ALERT_HANDSHAKE_FAILURE);
            return;
        }
        // RFC 6125 sec 6.4: the certificate has to speak for the name that was asked for.
        if (c->cfg->hostname)
        {
            X509V.match_args.cert = &leaf;
            X509V.match_args.host = c->cfg->hostname;
            X509V.match_args.host_len = 0;
            X509.name_match(work);
            if (!X509V.ok)
            {
                fail(TLS_ALERT_HANDSHAKE_FAILURE);
                return;
            }
        }
        if (!peer_key_keep(leaf.key_alg, leaf.key.p, leaf.key.len))
        {
            fail(TLS_ALERT_INTERNAL_ERROR);
            return;
        }
        transcript_add(msg, len);
        TlsConnectionV.i32 = 0;
        return;
    }
#else
    // A configured anchor this build cannot read is refused rather than ignored: falling through
    // would authenticate the peer by its raw public key on a connection that asked for a chain.
    if (c->cfg->ca_der)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
#endif

    const uint8_t *pub = NULL;
    Tls13RpkV.ed25519_from_spki_args.spki = entry;
    Tls13RpkV.ed25519_from_spki_args.len = entry_len;
    Tls13RpkV.ed25519_from_spki_args.pub = &pub;
    Tls13Rpk.ed25519_from_spki(work);
    if (!Tls13RpkV.ok)
    {
        fail(TLS_ALERT_DECODE_ERROR);
        return;
    }
    // sec 4.4.2: the credential has to be the one this connection was configured to accept. A
    // connection with no configured key is unauthenticated by its own choice (see TlsConnConfig).
    if (c->cfg->peer_pub && !protocore_ct_eq(c->cfg->peer_pub, pub, PROTOCORE_ED25519_PUBKEY_LEN))
    {
        fail(TLS_ALERT_HANDSHAKE_FAILURE);
        return;
    }
    if (!peer_key_keep(PROTOCORE_X509_KEY_ED25519, pub, PROTOCORE_ED25519_PUBKEY_LEN))
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    transcript_add(msg, len);
    TlsConnectionV.i32 = 0;
}

// The scheme a CertificateVerify names, as the algorithm the verifier knows it by. An unmapped
// value is refused: sec 4.2.3 excludes the RSASSA-PKCS1-v1_5 code points from signed handshake
// messages outright, and anything else is a scheme this build does not verify.
static protocore_x509_sig_alg sig_alg_of(uint16_t scheme)
{
    switch (scheme)
    {
    case TLS_SIG_ED25519:
        return PROTOCORE_X509_SIG_ED25519;
    case TLS_SIG_ECDSA_SECP256R1_SHA256:
        return PROTOCORE_X509_SIG_ECDSA_SHA256;
    case TLS_SIG_RSA_PSS_RSAE_SHA256:
        return PROTOCORE_X509_SIG_RSA_PSS;
    default:
        return PROTOCORE_X509_SIG_UNKNOWN;
    }
}

// CertificateVerify signs the transcript through the Certificate, so the hash is taken before this
// message joins it (sec 4.4.3).
static void client_on_cert_verify(uint8_t *restrict work, const uint8_t *msg, size_t len)
{
    TlsConn *c = TlsConnectionV.conn;
    uint16_t scheme = 0;
    const uint8_t *sig = NULL;
    size_t sig_len = 0;
    Tls13MsgV.parse_cert_verify_args.msg = msg;
    Tls13MsgV.parse_cert_verify_args.len = len;
    Tls13MsgV.parse_cert_verify_args.scheme = &scheme;
    Tls13MsgV.parse_cert_verify_args.sig = &sig;
    Tls13MsgV.parse_cert_verify_args.sig_len = &sig_len;
    Tls13Msg.parse_cert_verify(work);
    if (!Tls13MsgV.ok)
    {
        fail(TLS_ALERT_DECODE_ERROR);
        return;
    }
    const protocore_x509_sig_alg alg = sig_alg_of(scheme);
    if (alg == PROTOCORE_X509_SIG_UNKNOWN)
    {
        fail(TLS_ALERT_ILLEGAL_PARAMETER);
        return;
    }
    transcript_peek(TLS_TERM_HASH);
    uint8_t content[64 + 33 + 1 + TLS13_SECRET_MAX];
    Tls13MsgV.cert_verify_content_args.out = content;
    Tls13MsgV.cert_verify_content_args.cap = sizeof(content);
    Tls13MsgV.cert_verify_content_args.transcript_hash = c->terms + TLS_TERM_HASH;
    Tls13MsgV.cert_verify_content_args.hash_len = c->ks.len;
    Tls13MsgV.cert_verify_content_args.is_server = PROTO_TRUE;
    Tls13Msg.cert_verify_content(work);
    size_t clen = Tls13MsgV.n;
    if (clen == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    // The key the Certificate carried, as much of a certificate as a signature check reads.
    X509Cert signer;
    mem.set(&signer, 0, sizeof(signer));
    signer.key_alg = (protocore_x509_key_alg)TLS_PEERKEY_ALG(c->tx);
    signer.key.p = TLS_PEERKEY_P(c->tx);
    signer.key.len = TLS_PEERKEY_LEN(c->tx);

    X509VerifyV.message_args.signer = &signer;
    X509VerifyV.message_args.alg = alg;
    X509VerifyV.message_args.msg = content;
    X509VerifyV.message_args.msg_len = clen;
    X509VerifyV.message_args.sig = sig;
    X509VerifyV.message_args.sig_len = sig_len;
    X509Verify.message(protocore_x509_verify_span());
    if (!X509VerifyV.ok)
    {
        fail(TLS_ALERT_DECRYPT_ERROR);
        return;
    }
    transcript_add(msg, len);
    TlsConnectionV.i32 = 0;
}

// The server Finished closes its flight. Checking it fixes the transcript the application keys are
// taken over, and this end answers with its own Finished under the handshake write keys.
static void client_on_server_finished(uint8_t *restrict work, const uint8_t *msg, size_t len)
{
    TlsConn *c = TlsConnectionV.conn;
    const uint8_t *vd = NULL;
    Tls13MsgV.parse_finished_args.msg = msg;
    Tls13MsgV.parse_finished_args.len = len;
    Tls13MsgV.parse_finished_args.vd = &vd;
    Tls13MsgV.parse_finished_args.verify_len = c->ks.len;
    Tls13Msg.parse_finished(work);
    if (!Tls13MsgV.ok)
    {
        fail(TLS_ALERT_DECODE_ERROR);
        return;
    }
    transcript_peek(TLS_TERM_HASH);
    finished_mac(work, c->ks.s + TLS13_KS_SERVER_HS, TLS_TERM_HASH);
    if (!protocore_ct_eq(c->terms + TLS_TERM_MAC, vd, c->ks.len))
    {
        fail(TLS_ALERT_DECRYPT_ERROR);
        return;
    }
    transcript_add(msg, len);

    // The application keys are keyed off CH..server Finished, which is the transcript right now.
    transcript_peek(TLS_TERM_HS_FIN);
    Tls13KsV.bind.ks = &c->ks;
    Tls13KsV.step.ch_sfin_hash = c->terms + TLS_TERM_HS_FIN;
    Tls13Ks.master(work);
    keys_derive(work, &c->ap_tx, c->ks.s + TLS13_KS_CLIENT_AP);
    keys_derive(work, &c->ap_rx, c->ks.s + TLS13_KS_SERVER_AP);
    c->ap_keys_ready = PROTO_TRUE;

    // This end's Finished covers the same transcript, under the client handshake secret.
    finished_mac(work, c->ks.s + TLS13_KS_CLIENT_HS, TLS_TERM_HS_FIN);
    Tls13MsgV.build_finished_args.out = c->tx;
    Tls13MsgV.build_finished_args.cap = PROTOCORE_TLS_CONN_MSG_CAP;
    Tls13MsgV.build_finished_args.verify_data = c->terms + TLS_TERM_MAC;
    Tls13MsgV.build_finished_args.verify_len = c->ks.len;
    Tls13Msg.build_finished(work);
    size_t n = Tls13MsgV.n;
    size_t w = emit_encrypted(work, n, TlsConnectionV.out_args.out, TlsConnectionV.out_args.out_cap);
    if (w == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    c->state = TLS_CONN_DONE;
    TlsConnectionV.i32 = (int)w;
}

// One message of the server's encrypted flight. The order is EncryptedExtensions, Certificate,
// CertificateVerify, Finished (sec 4); anything else here is out of order.
static void client_on_flight(uint8_t *restrict work, const uint8_t *msg, size_t len)
{
    switch (msg[0])
    {
    case TLS_HS_ENCRYPTED_EXTENSIONS:
        transcript_add(msg, len);
        TlsConnectionV.i32 = 0;
        return;
    case TLS_HS_CERTIFICATE:
        client_on_certificate(work, msg, len);
        return;
    case TLS_HS_CERTIFICATE_VERIFY:
        client_on_cert_verify(work, msg, len);
        return;
    case TLS_HS_FINISHED:
        client_on_server_finished(work, msg, len);
        return;
    default:
        fail(TLS_ALERT_UNEXPECTED_MESSAGE);
        return;
    }
}

// The ClientHello opens the handshake: this end's key_share, the one suite and group it offers, and
// the SNI and first ALPN name its configuration names. It travels as TLSPlaintext.
void protocore_tls_connection_start(uint8_t *restrict work)
{
    TlsConn *c = TlsConnectionV.conn;
    TlsConnectionV.n = 0;
    if (c->role != TLS_ROLE_CLIENT || c->state != TLS_CONN_START)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }

    Curve25519V.x25519_base_args.scalar = c->cfg->ephemeral_priv;
    Curve25519V.x25519_base_args.out = c->terms + TLS_TERM_SHARE;
    Curve25519.x25519_base(c->sign_work);

    const char *alpn = (c->cfg->alpn && c->cfg->alpn_count) ? c->cfg->alpn[0] : NULL;
    Tls13MsgV.build_client_hello_args.out = c->tx;
    Tls13MsgV.build_client_hello_args.cap = PROTOCORE_TLS_CONN_MSG_CAP;
    Tls13MsgV.build_client_hello_args.random = c->cfg->random;
    Tls13MsgV.build_client_hello_args.session_id = NULL;
    Tls13MsgV.build_client_hello_args.session_id_len = 0;
    Tls13MsgV.build_client_hello_args.share = c->terms + TLS_TERM_SHARE;
    Tls13MsgV.build_client_hello_args.share_len = TLS_X25519_SHARE_LEN;
    Tls13MsgV.build_client_hello_args.group = TLS_GROUP_X25519;
    Tls13MsgV.build_client_hello_args.suite = protocore_tls_cipher_code(c->cfg->cipher);
    Tls13MsgV.build_client_hello_args.sni = c->cfg->hostname;
    Tls13MsgV.build_client_hello_args.alpn = alpn;
    Tls13MsgV.build_client_hello_args.cookie = NULL;
    Tls13MsgV.build_client_hello_args.cookie_len = 0;
    Tls13MsgV.build_client_hello_args.rpk_server_cert = PROTO_TRUE;
    Tls13MsgV.build_client_hello_args.dtls = PROTO_FALSE;
    Tls13Msg.build_client_hello(work);
    size_t n = Tls13MsgV.n;
    if (n == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    transcript_add(c->tx, n);

    TlsRecordV.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecordV.plain.fragment = c->tx;
    TlsRecordV.plain.frag_len = n;
    TlsRecordV.out_args.out = TlsConnectionV.out_args.out;
    TlsRecordV.out_args.out_cap = TlsConnectionV.out_args.out_cap;
    TlsRecord.plaintext_build(work);
    if (TlsRecordV.n == 0)
    {
        fail(TLS_ALERT_INTERNAL_ERROR);
        return;
    }
    c->state = TLS_CONN_WAIT_SH;
    TlsConnectionV.n = TlsRecordV.n;
}

// The worker filled RX with one record and says how much. A ClientHello is in the clear, so it is
// read where it lies; everything after it opens into TX, which builds nothing while a received
// message stands in it.
void protocore_tls_connection_process(uint8_t *restrict work)
{
    TlsConn *c = TlsConnectionV.conn;
    const size_t rx_len = TlsConnectionV.io.rx_len;

    if (c->state == TLS_CONN_FAILED)
    {
        TlsConnectionV.i32 = -1;
        return;
    }
    TlsPlaintext pt = {0};
    TlsRecordV.sealed.rec = c->rx;
    TlsRecordV.sealed.rec_len = rx_len;
    TlsRecordV.plain.view = &pt;
    TlsRecord.plaintext_parse(work);
    if (TlsRecordV.n == 0)
    {
        fail(TLS_ALERT_DECODE_ERROR);
        return;
    }

    const uint8_t *msg = pt.fragment;
    size_t len = pt.frag_len;
    uint8_t inner_type = pt.content_type;
    if (inner_type == PROTOCORE_TLS_CT_APPLICATION_DATA && c->hs_keys_ready)
    {
        TlsCiphertext info = {0};
        if (!record_open(work, &c->hs_rx, c->rx, rx_len, c->tx, PROTOCORE_TLS_CONN_MSG_CAP, &info))
        {
            fail(TLS_ALERT_DECRYPT_ERROR);
            return;
        }
        msg = c->tx;
        len = info.pt_len;
        inner_type = info.content_type;
    }

    if (inner_type == PROTOCORE_TLS_CT_CHANGE_CIPHER_SPEC)
    {
        TlsConnectionV.i32 = 0; // middlebox compatibility record, outside the transcript (sec 5)
        return;
    }
    // sec 6.2: a record that is not a handshake record here is premature application data or an
    // unexpected record type, which is unexpected_message; a header whose 24-bit length does not
    // frame the message is a length past the message boundary, which sec 6 makes decode_error.
    if (inner_type != PROTOCORE_TLS_CT_HANDSHAKE)
    {
        fail(TLS_ALERT_UNEXPECTED_MESSAGE);
        return;
    }
    if (len < TLS_HS_HDR_LEN || len != TLS_HS_HDR_LEN + hs_body_len(msg))
    {
        fail(TLS_ALERT_DECODE_ERROR);
        return;
    }
    if (c->role == TLS_ROLE_CLIENT)
    {
        if (c->state == TLS_CONN_WAIT_SH && msg[0] == TLS_HS_SERVER_HELLO)
        {
            client_on_server_hello(work, msg, len);
            return;
        }
        if (c->state == TLS_CONN_WAIT_FLIGHT)
        {
            client_on_flight(work, msg, len);
            return;
        }
        fail(TLS_ALERT_UNEXPECTED_MESSAGE);
        return;
    }
    if (c->state == TLS_CONN_START && msg[0] == TLS_HS_CLIENT_HELLO)
    {
        server_on_client_hello(work, msg, len);
        return;
    }
    if (c->state == TLS_CONN_WAIT_FINISHED)
    {
        server_on_finished(work, msg, len);
        return;
    }
    // sec 4: a handshake message received in an unexpected order is unexpected_message.
    fail(TLS_ALERT_UNEXPECTED_MESSAGE);
}

void protocore_tls_connection_established(uint8_t *restrict work)
{
    TlsConnectionV.ok = (TlsConnectionV.conn->state == TLS_CONN_DONE);
}

void protocore_tls_connection_alert(uint8_t *restrict work)
{
    TlsConnectionV.u8 = TlsConnectionV.conn->alert;
}

void protocore_tls_connection_seal_app(uint8_t *restrict work)
{
    TlsConnectionV.n = 0;
    if (!TlsConnectionV.conn->ap_keys_ready)
    {
        return;
    }
    TlsConnectionV.n =
        record_seal(work, &TlsConnectionV.conn->ap_tx, PROTOCORE_TLS_CT_APPLICATION_DATA, TlsConnectionV.io.data,
                    TlsConnectionV.io.len, TlsConnectionV.out_args.out, TlsConnectionV.out_args.out_cap);
}

void protocore_tls_connection_open_app(uint8_t *restrict work)
{
    TlsConnectionV.ok = PROTO_FALSE;
    if (!TlsConnectionV.conn->ap_keys_ready)
    {
        return;
    }
    TlsCiphertext info = {0};
    if (!record_open(work, &TlsConnectionV.conn->ap_rx, TlsConnectionV.io.rec, TlsConnectionV.io.rec_len,
                     TlsConnectionV.out_args.out, TlsConnectionV.out_args.out_cap, &info))
    {
        return;
    }
    if (TlsConnectionV.out_args.out_len)
    {
        *TlsConnectionV.out_args.out_len = info.pt_len;
    }
    TlsConnectionV.ok = (info.content_type == PROTOCORE_TLS_CT_APPLICATION_DATA);
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
TlsConnectionVars TlsConnectionV;

#endif // PROTOCORE_ENABLE_TLS
