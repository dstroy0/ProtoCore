// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_tls.c
 * @brief TLS 1.3 server handshake state machine for QUIC (see protocore_quic_tls.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t quic_crypto_work[16]; // the borrow an entry takes; QuicCrypto never reads it

static uint8_t quic_tp_work[16]; // the borrow an entry takes; QuicTp never reads it

#if PROTOCORE_ENABLE_HTTP3

#if PROTOCORE_ENABLE_PQC_KEX
#include "crypto/pqc/mlkem/mlkem.h" // MlKem (X25519MLKEM768 hybrid)
#endif
#include "crypto/asymmetric/curve25519/curve25519.h"
#include "crypto/ct_eq.h" // protocore_ct_eq: the Finished compare
#include "mmgr/protomem/protomem.h"
#include "network_drivers/presentation/http/http3/quic_tls/quic_tls.h"
#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"
// TLS alert codes we may raise (RFC 8446 sec 6).
#define TLS_ALERT_UNEXPECTED_MESSAGE 10
#define TLS_ALERT_HANDSHAKE_FAILURE 40
#define TLS_ALERT_ILLEGAL_PARAMETER 47
#define TLS_ALERT_DECODE_ERROR 50
#define TLS_ALERT_DECRYPT_ERROR 51
#define TLS_ALERT_PROTOCOL_VERSION 70
#define TLS_ALERT_INTERNAL_ERROR 80
#define TLS_ALERT_MISSING_EXTENSION 109
#define TLS_ALERT_NO_APPLICATION_PROTOCOL 120

/// Capacity of the encoded-transport-parameters scratch the EncryptedExtensions builder is fed.
#define PROTOCORE_QUIC_TLS_TP_ENC_CAP 512

/// Largest EncryptedExtensions this module can emit: handshake header (4) + extensions length (2)
/// + the ALPN "h3" extension (9) + the transport-parameters extension header (4) + the parameters.
#define PROTOCORE_QUIC_TLS_EE_MAX (4 + 2 + 9 + 4 + PROTOCORE_QUIC_TLS_TP_ENC_CAP)

// EncryptedExtensions is the FIRST message written into flight_hs (flight_hs_len is reset to 0
// immediately before it), so its emit() cannot overflow - which is why that failure path carries a
// coverage exclusion. That only holds while flight_hs is at least one whole EncryptedExtensions,
// and PROTOCORE_H3_CRYPTO_BUF is an overridable macro (protocore_config.h), so pin the relationship here:
// a build that shrank it would silently make the excluded path reachable.
static_assert(PROTOCORE_H3_CRYPTO_BUF >= PROTOCORE_QUIC_TLS_EE_MAX,
              "PROTOCORE_H3_CRYPTO_BUF (QuicTls.flight_hs) must hold a whole EncryptedExtensions: the fixed "
              "512-byte transport-parameter buffer plus the ALPN and extension framing");

static void fail(QuicTls *qt, uint8_t alert)
{
    qt->state = QTLS_FAILED;
    qt->alert = alert;
}

// Point the key schedule's handle at this connection's schedule and its bytes. Every step below
// reads them off the handle, so the binding is set before each call.
static void ks_bind(QuicTls *qt)
{
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &qt->ks;
    Tls13Ks.bind.s = qt->ks_store;
    // RFC 9001 sec 5.1: this handshake offers TLS_AES_128_GCM_SHA256 only, so the schedule's hash is
    // SHA-256. It is stated rather than assumed because the schedule binds either.
    Tls13Ks.bind.is384 = PROTO_FALSE;
}

// verify_data over @p transcript_hash under @p base_secret (RFC 8446 sec 4.4.4).
static void ks_finished(QuicTls *qt, const uint8_t *base_secret, const uint8_t *transcript_hash, uint8_t *out)
{
    ks_bind(qt);
    Tls13Ks.finished_args.base_secret = base_secret;
    Tls13Ks.finished_args.transcript_hash = transcript_hash;
    Tls13Ks.finished_args.out = out;
    Tls13Ks.finished_mac(NULL);
}

// The Transcript-Hash runs under the suite's hash (RFC 8446 sec 4.4.1), so it goes through the key
// schedule that bound it rather than naming one here.

// Start a Transcript-Hash in @p ctx.
static void transcript_start(QuicTls *qt, uint8_t *ctx)
{
    Tls13Ks.bind.ks = &qt->ks;
    Tls13Ks.transcript_init(ctx);
}

// Fold @p len bytes of @p data into the transcript in @p ctx.
static void transcript_add(QuicTls *qt, uint8_t *ctx, const uint8_t *data, size_t len)
{
    Tls13Ks.bind.ks = &qt->ks;
    Tls13Ks.transcript_args.data = data;
    Tls13Ks.transcript_args.len = len;
    Tls13Ks.transcript_update(ctx);
}

// The running Transcript-Hash so far. Finalizing compresses the padded blocks into a copy of the
// state, so the context comes out untouched and keeps taking messages.
static void snapshot_hash(QuicTls *qt, uint8_t *ctx, uint8_t *out)
{
    Tls13Ks.bind.ks = &qt->ks;
    Tls13Ks.transcript_args.out = out;
    Tls13Ks.transcript_peek(ctx);
}

// Append a handshake message to both the outbound flight buffer and the transcript.
static proto_bool emit(QuicTls *qt, uint8_t *flight, size_t cap, size_t *plen, size_t written)
{
    // *plen <= cap is the invariant this append maintains, so cap - *plen never underflows; refuse a
    // write that would run past the flight buffer (each builder already caps to what it was told, so
    // this only fires if that contract is ever broken - it keeps flight+*plen in bounds regardless).
    if (!written || written > cap - *plen)
    { // as its own capacity, so a non-zero return can never exceed it
        fail(qt, TLS_ALERT_INTERNAL_ERROR);
        return PROTO_FALSE;
    }
    transcript_add(qt, qt->transcript, flight + *plen, written);
    *plen += written;
    return PROTO_TRUE;
}

#if PROTOCORE_ENABLE_PQC_KEX
// Emit a HelloRetryRequest (RFC 8446 §4.1.4) asking the client to retry with an X25519MLKEM768
// key_share, and restart the transcript per §4.4.1: message_hash(Hash(ClientHello1)) || HRR, so the
// eventual transcript is message_hash || HRR || ClientHello2 || ServerHello || ... QUIC does its own
// return-routability (Retry tokens), so the HRR carries no cookie. @p msg is ClientHello1.
static proto_bool send_hello_retry(QuicTls *qt, const uint8_t *msg, size_t msg_len, const Tls13ClientHello *ch)
{
    uint8_t ch1_hash[TLS13_SECRET_MAX];
    {
        uint8_t *t;
        t = qt->hash_work2;
        transcript_start(qt, t);
        transcript_add(qt, t, msg, msg_len);
        snapshot_hash(qt, t, ch1_hash);
    }
    qt->transcript = qt->hash_work;
    transcript_start(qt, qt->transcript);
    uint8_t mh[40];
    size_t mhn = protocore_tls13_build_message_hash(mh, sizeof(mh), ch1_hash);
    if (!mhn)
    {
        fail(qt, TLS_ALERT_INTERNAL_ERROR);
        return PROTO_FALSE;
    }
    transcript_add(qt, qt->transcript, mh, mhn); // message_hash is transcript-only, never sent

    qt->flight_initial_len = 0;
    size_t n = protocore_tls13_build_hello_retry_request(qt->flight_initial, sizeof(qt->flight_initial), ch->session_id,
                                                         ch->session_id_len, TLS_GROUP_X25519MLKEM768,
                                                         PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, NULL, 0,
                                                         /*dtls=*/PROTO_FALSE);
    if (!emit(qt, qt->flight_initial, sizeof(qt->flight_initial), &qt->flight_initial_len, n))
    {
        return PROTO_FALSE;
    }
    qt->hrr_sent = PROTO_TRUE;
    return PROTO_TRUE; // stay in QTLS_START, awaiting ClientHello2 at the Initial level
}
#endif

static proto_bool process_client_hello(QuicTls *qt, const uint8_t *msg, size_t msg_len)
{
    Tls13ClientHello ch;
    if (!protocore_tls13_parse_client_hello(msg, msg_len, &ch, /*dtls=*/PROTO_FALSE))
    {
        fail(qt, TLS_ALERT_DECODE_ERROR);
        return PROTO_FALSE;
    }
    if (!ch.offers_tls13)
    {
        fail(qt, TLS_ALERT_PROTOCOL_VERSION);
        return PROTO_FALSE;
    }
    // RFC 8446 sec 4.1.3: the ServerHello names a suite the client offered. This stack answers with
    // one suite, so a ClientHello that did not offer it cannot be answered at all.
    if (!ch.offers_aes128gcm_sha256)
    {
        fail(qt, TLS_ALERT_HANDSHAKE_FAILURE);
        return PROTO_FALSE;
    }
    proto_bool use_hybrid = PROTO_FALSE;
#if PROTOCORE_ENABLE_PQC_KEX
    // Prefer the PQ/T hybrid whenever the client sent a usable X25519MLKEM768 key_share.
    use_hybrid = ch.has_hybrid_share && ch.offers_x25519mlkem768;
    // The client offered X25519MLKEM768 but sent only a classical key_share: ask it (once) to retry with
    // the hybrid share rather than silently downgrading to X25519 (RFC 8446 §4.1.4).
    if (!use_hybrid && ch.offers_x25519mlkem768 && !ch.has_hybrid_share && !qt->hrr_sent)
    {
        return send_hello_retry(qt, msg, msg_len, &ch);
    }
    // A retry that still lacks the hybrid share is fatal - one HRR only, so a client cannot loop us.
    if (qt->hrr_sent && !use_hybrid)
    {
        fail(qt, TLS_ALERT_HANDSHAKE_FAILURE);
        return PROTO_FALSE;
    }
#endif
    if (!ch.offers_ed25519 || (!use_hybrid && (!ch.has_key_share || !ch.offers_x25519)))
    {
        fail(qt, TLS_ALERT_HANDSHAKE_FAILURE);
        return PROTO_FALSE;
    }
    if (!ch.offers_h3_alpn)
    {
        fail(qt, TLS_ALERT_NO_APPLICATION_PROTOCOL);
        return PROTO_FALSE;
    }
    if (!ch.quic_tp)
    {
        fail(qt, TLS_ALERT_MISSING_EXTENSION);
        return PROTO_FALSE;
    }
    QuicTp.parse_args.buf = ch.quic_tp;
    QuicTp.parse_args.len = ch.quic_tp_len;
    QuicTp.parse_args.tp = &qt->peer;
    QuicTp.parse(quic_tp_work);
    if (!QuicTp.ok)
    {
        fail(qt, TLS_ALERT_ILLEGAL_PARAMETER);
        return PROTO_FALSE;
    }
    qt->have_peer = PROTO_TRUE;

    // (EC)DHE shared secret + the server's key_share, per negotiated group. The hybrid secret is the
    // 64-byte ML-KEM_secret || X25519_secret (ML-KEM first, per draft-ietf-tls-ecdhe-mlkem).
    uint8_t ecdhe[64];
    size_t ecdhe_len;
    uint16_t group;
    size_t share_len;
#if PROTOCORE_ENABLE_PQC_KEX
    uint8_t server_share[MLKEM768_CT_BYTES + 32]; // S_CT2(1088) || Q_S(32) for the hybrid
    if (use_hybrid)
    {
        uint8_t ml_ss[32];
        if (!(MlKem.encaps_args.ek = ch.client_mlkem_ek, MlKem.encaps_args.m = qt->cfg.mlkem_m,
              MlKem.encaps_args.ct = server_share, MlKem.encaps_args.ss = ml_ss, MlKem.encaps(qt->sign_work), MlKem.ok))
        {
            fail(qt, TLS_ALERT_HANDSHAKE_FAILURE); // malformed ML-KEM key
            return PROTO_FALSE;
        }
        uint8_t x_ss[32];
        uint8_t server_pub[32];
        Curve25519.x25519_args.scalar = qt->cfg.ephemeral_priv;
        Curve25519.x25519_args.point = ch.client_x25519;
        Curve25519.x25519_args.out = x_ss;
        Curve25519.x25519(qt->sign_work);
        if (!Curve25519.ok)
        {
            // RFC 8446 sec 7.4.2: the shared secret came out all-zero, so the client's key share was
            // a point of small order and the secret is a constant it chose. Abort (RFC 7748 sec 6.1).
            fail(qt, TLS_ALERT_ILLEGAL_PARAMETER);
            return PROTO_FALSE;
        }
        Curve25519.x25519_base_args.scalar = qt->cfg.ephemeral_priv;
        Curve25519.x25519_base_args.out = server_pub;
        Curve25519.x25519_base(qt->sign_work);
        mem.cpy(server_share + MLKEM768_CT_BYTES, server_pub, 32);
        mem.cpy(ecdhe, ml_ss, 32);
        mem.cpy(ecdhe + 32, x_ss, 32);
        ecdhe_len = 64;
        share_len = MLKEM768_CT_BYTES + 32;
        group = TLS_GROUP_X25519MLKEM768;
    }
    else
#else
    uint8_t server_share[32];
#endif
    {
        Curve25519.x25519_args.scalar = qt->cfg.ephemeral_priv;
        Curve25519.x25519_args.point = ch.client_x25519;
        Curve25519.x25519_args.out = ecdhe;
        Curve25519.x25519(qt->sign_work);
        if (!Curve25519.ok)
        {
            // RFC 8446 sec 7.4.2: the shared secret came out all-zero, so the client's key share was
            // a point of small order and the secret is a constant it chose. Abort (RFC 7748 sec 6.1).
            fail(qt, TLS_ALERT_ILLEGAL_PARAMETER);
            return PROTO_FALSE;
        }
        Curve25519.x25519_base_args.scalar = qt->cfg.ephemeral_priv;
        Curve25519.x25519_base_args.out = server_share;
        Curve25519.x25519_base(qt->sign_work);
        ecdhe_len = 32;
        share_len = 32;
        group = TLS_GROUP_X25519;
    }

    // Fold the ClientHello into the transcript. On the happy path it is the first message; after a
    // HelloRetryRequest the transcript already holds message_hash || HRR, so this is ClientHello2.
    transcript_add(qt, qt->transcript, msg, msg_len);

    // ServerHello (Initial-level flight). The Initial CRYPTO is one contiguous byte stream, so after a
    // HelloRetryRequest the ServerHello is appended after the HRR already in flight_initial - build at the
    // current offset (0 on the happy path, the HRR's end on a retry) and do not reset the length.
    size_t n = protocore_tls13_build_server_hello(qt->flight_initial + qt->flight_initial_len,
                                                  sizeof(qt->flight_initial) - qt->flight_initial_len, qt->cfg.random,
                                                  ch.session_id, ch.session_id_len, server_share, share_len, group,
                                                  PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, /*dtls=*/PROTO_FALSE,
                                                  /*conn_id=*/NULL, /*conn_id_len=*/0);
    if (!emit(qt, qt->flight_initial, sizeof(qt->flight_initial), &qt->flight_initial_len, n))
    {
        return PROTO_FALSE;
        // hybrid's ~1.2 KB share fits the PQC-sized 1400B buffer)
    }

    // Handshake keys from Transcript-Hash(ClientHello..ServerHello).
    uint8_t hash[TLS13_SECRET_MAX];
    snapshot_hash(qt, qt->transcript, hash);
    ks_bind(qt);
    Tls13Ks.early(NULL);
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = ecdhe_len;
    Tls13Ks.step.ch_sh_hash = hash;
    Tls13Ks.handshake(NULL);
    QuicCrypto.keys_from_secret_args.keys_work = qt->keys_work;
    QuicCrypto.keys_from_secret_args.secret = qt->ks.s + TLS13_KS_CLIENT_HS;
    QuicCrypto.keys_from_secret_args.out = &qt->hs_client;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCrypto.keys_from_secret_args.keys_work = qt->keys_work;
    QuicCrypto.keys_from_secret_args.secret = qt->ks.s + TLS13_KS_SERVER_HS;
    QuicCrypto.keys_from_secret_args.out = &qt->hs_server;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    qt->hs_keys_ready = PROTO_TRUE;

    // Handshake-level flight: EncryptedExtensions, Certificate, CertificateVerify, Finished.
    qt->flight_hs_len = 0;
    uint8_t tp_enc[PROTOCORE_QUIC_TLS_TP_ENC_CAP];
    QuicTp.encode_args.tp = &qt->cfg.params;
    QuicTp.encode_args.out = tp_enc;
    QuicTp.encode_args.cap = sizeof(tp_enc);
    QuicTp.encode(quic_tp_work);
    size_t tp_len = QuicTp.n;

    n = protocore_tls13_build_encrypted_extensions(qt->flight_hs + qt->flight_hs_len,
                                                   sizeof(qt->flight_hs) - qt->flight_hs_len, tp_enc, tp_len,
                                                   /*rpk_server_cert=*/PROTO_FALSE);
    if (!emit(qt, qt->flight_hs, sizeof(qt->flight_hs), &qt->flight_hs_len, n))
    {
        return PROTO_FALSE;
        // PROTOCORE_H3_CRYPTO_BUF >= PROTOCORE_QUIC_TLS_EE_MAX is pinned by the static_assert above
    }

    n = protocore_tls13_build_certificate(qt->flight_hs + qt->flight_hs_len, sizeof(qt->flight_hs) - qt->flight_hs_len,
                                          qt->cfg.cert_der, qt->cfg.cert_len);
    if (!emit(qt, qt->flight_hs, sizeof(qt->flight_hs), &qt->flight_hs_len, n))
    {
        return PROTO_FALSE;
    }

    // CertificateVerify signs Transcript-Hash(ClientHello..Certificate).
    snapshot_hash(qt, qt->transcript, hash);
    n = protocore_tls13_build_cert_verify(qt->sign_work, qt->flight_hs + qt->flight_hs_len,
                                          sizeof(qt->flight_hs) - qt->flight_hs_len, hash, qt->ks.len,
                                          qt->cfg.ed25519_seed);
    if (!emit(qt, qt->flight_hs, sizeof(qt->flight_hs), &qt->flight_hs_len, n))
    {
        return PROTO_FALSE;
    }

    // Server Finished over Transcript-Hash(ClientHello..CertificateVerify).
    snapshot_hash(qt, qt->transcript, hash);
    uint8_t verify[TLS13_SECRET_MAX];
    ks_finished(qt, qt->ks.s + TLS13_KS_SERVER_HS, hash, verify);
    n = protocore_tls13_build_finished(qt->flight_hs + qt->flight_hs_len, sizeof(qt->flight_hs) - qt->flight_hs_len,
                                       verify, qt->ks.len);
    if (!emit(qt, qt->flight_hs, sizeof(qt->flight_hs), &qt->flight_hs_len, n))
    {
        return PROTO_FALSE;
    }

    // 1-RTT keys from Transcript-Hash(ClientHello..server Finished); also the hash we verify the
    // client Finished against.
    snapshot_hash(qt, qt->transcript, qt->hs_finished_hash);
    ks_bind(qt);
    Tls13Ks.step.ch_sfin_hash = qt->hs_finished_hash;
    Tls13Ks.master(NULL);
    QuicCrypto.keys_from_secret_args.keys_work = qt->keys_work;
    QuicCrypto.keys_from_secret_args.secret = qt->ks.s + TLS13_KS_CLIENT_AP;
    QuicCrypto.keys_from_secret_args.out = &qt->ap_client;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCrypto.keys_from_secret_args.keys_work = qt->keys_work;
    QuicCrypto.keys_from_secret_args.secret = qt->ks.s + TLS13_KS_SERVER_AP;
    QuicCrypto.keys_from_secret_args.out = &qt->ap_server;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    qt->ap_keys_ready = PROTO_TRUE;

    qt->state = QTLS_WAIT_FINISHED;
    return PROTO_TRUE;
}

static proto_bool process_client_finished(QuicTls *qt, const uint8_t *msg, size_t msg_len)
{
    if (msg[0] != TLS_HS_FINISHED || msg_len != 4 + 32)
    { // Finished here, so the type arm cannot be taken
        fail(qt, TLS_ALERT_DECODE_ERROR);
        return PROTO_FALSE;
    }
    ks_finished(qt, qt->ks.s + TLS13_KS_CLIENT_HS, qt->hs_finished_hash, qt->ks.s + TLS13_KS_VERIFY);
    if (!protocore_ct_eq(qt->ks.s + TLS13_KS_VERIFY, msg + 4, qt->ks.len))
    {
        fail(qt, TLS_ALERT_DECRYPT_ERROR);
        return PROTO_FALSE;
    }
    transcript_add(qt, qt->transcript, msg, msg_len);
    qt->complete = PROTO_TRUE;
    qt->state = QTLS_DONE;
    return PROTO_TRUE;
}

static proto_bool process_message(QuicTls *qt, int level, const uint8_t *msg, size_t msg_len)
{
    if (level == QUIC_ENC_INITIAL && qt->state == QTLS_START && msg[0] == TLS_HS_CLIENT_HELLO)
    {
        return process_client_hello(qt, msg, msg_len);
    }
    if (level == QUIC_ENC_HANDSHAKE && qt->state == QTLS_WAIT_FINISHED && msg[0] == TLS_HS_FINISHED)
    {
        return process_client_finished(qt, msg, msg_len);
    }
    fail(qt, TLS_ALERT_UNEXPECTED_MESSAGE);
    return PROTO_FALSE;
}

void protocore_quic_tls_server_init(QuicTls *qt, const QuicTlsConfig *cfg)
{
    mem.zero(qt, sizeof(*qt));
    qt->cfg = *cfg;
    qt->transcript = qt->hash_work;
    transcript_start(qt, qt->transcript);
    qt->state = QTLS_START;
}

size_t protocore_quic_tls_recv_crypto(QuicTls *qt, int level, const uint8_t *data, size_t len)
{
    if (qt->state == QTLS_FAILED)
    {
        return len; // drain; the connection is closing
    }
    size_t off = 0;
    while (off + 4 <= len)
    {
        uint32_t mlen = (uint32_t)((data[off + 1] << 16) | (data[off + 2] << 8) | data[off + 3]);
        size_t total = 4 + mlen;
        if (off + total > len)
        {
            break; // an incomplete trailing message; wait for more bytes
        }
        if (!process_message(qt, level, data + off, total))
        {
            return off + total; // consumed through the offending message; state is FAILED/handled
        }
        off += total;
        if (qt->state == QTLS_DONE)
        {
            break;
        }
    }
    return off;
}

const uint8_t *protocore_quic_tls_flight(const QuicTls *qt, int level, size_t *len)
{
    if (level == QUIC_ENC_INITIAL)
    {
        *len = qt->flight_initial_len;
        return qt->flight_initial;
    }
    if (level == QUIC_ENC_HANDSHAKE)
    {
        *len = qt->flight_hs_len;
        return qt->flight_hs;
    }
    *len = 0;
    return NULL;
}

QuicPacketKeys *protocore_quic_tls_keys(QuicTls *qt, int level, proto_bool is_server)
{
    if (level == QUIC_ENC_HANDSHAKE && qt->hs_keys_ready)
    {
        return is_server ? &qt->hs_server : &qt->hs_client;
    }
    if (level == QUIC_ENC_APP && qt->ap_keys_ready)
    {
        return is_server ? &qt->ap_server : &qt->ap_client;
    }
    return NULL;
}

const QuicTransportParams *protocore_quic_tls_peer_params(const QuicTls *qt)
{
    return qt->have_peer ? &qt->peer : NULL;
}

#endif // PROTOCORE_ENABLE_HTTP3
