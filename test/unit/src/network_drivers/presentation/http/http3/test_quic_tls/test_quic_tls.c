// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the TLS 1.3 server handshake state machine QUIC runs
// (network_drivers/presentation/http/http3/quic_tls.h, RFC 9001 / RFC 8446).
//
// No published trace exists for a handshake with this server's own ephemeral key, so the anchor is
// the property the specification is built on: the two ends must arrive at the same secrets from the
// same transcript. test_handshake_interop_round_trip runs the CLIENT half in the test - its own
// X25519, its own transcript hash, its own RFC 8446 sec 7.1 key schedule - and requires the packet
// keys to match the server's octet for octet, then makes the server accept a client Finished the
// test computed. A server that hashed the wrong messages, keyed off the wrong secret or ordered its
// flight differently cannot pass it. The flight ordering is RFC 8446 sec 4.4 / sec 2 Figure 1, and
// each rejection below cites the sentence that requires it.

#include "crypto/asymmetric/curve25519.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/quic_tls.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// A self-signed DER blob stands in for the leaf certificate: this module only length-prefixes it.
static const uint8_t CERT_DER[8] = {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x02};

static uint8_t g_ch[512];
static size_t g_ch_len;
static uint8_t g_client_priv[32];
static uint8_t g_client_pub[32];

static QuicTls g_qt;
static QuicTlsConfig g_cfg;

// --- a hand-built ClientHello ------------------------------------------------------------------

typedef struct
{
    uint8_t *buf;
    size_t pos;
} W;

static void w8(W *w, uint8_t v)
{
    w->buf[w->pos++] = v;
}
static void w16(W *w, uint16_t v)
{
    w8(w, (uint8_t)(v >> 8));
    w8(w, (uint8_t)v);
}
static void wn(W *w, const uint8_t *b, size_t n)
{
    memcpy(w->buf + w->pos, b, n);
    w->pos += n;
}

// Options that make an otherwise valid ClientHello miss one thing the server requires.
typedef struct
{
    proto_bool tls13;    ///< supported_versions carries 0x0304
    proto_bool suite;    ///< cipher_suites carries TLS_AES_128_GCM_SHA256
    proto_bool group;    ///< supported_groups carries x25519
    proto_bool share;    ///< key_share carries an x25519 entry
    proto_bool ed25519;  ///< signature_algorithms carries ed25519
    proto_bool alpn_h3;  ///< ALPN carries "h3"
    proto_bool quic_tp;  ///< the quic_transport_parameters extension is present
} ChOpts;

static const ChOpts CH_FULL = {PROTO_TRUE, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE};

// RFC 8446 sec 4.1.2 ClientHello, with the extensions this server reads.
static size_t build_client_hello(uint8_t *out, const ChOpts *o)
{
    uint8_t tp[256];
    QuicTransportParams params;
    protocore_quic_tp_defaults(&params);
    params.initial_max_data = 1048576;
    params.initial_max_streams_bidi = 8;
    size_t tp_len = protocore_quic_tp_encode(&params, tp, sizeof(tp));

    W w = {out, 0};
    w8(&w, TLS_HS_CLIENT_HELLO);
    size_t hs_at = w.pos;
    w8(&w, 0);
    w8(&w, 0);
    w8(&w, 0);

    w16(&w, 0x0303); // legacy_version
    for (int i = 0; i < 32; i++)
    {
        w8(&w, (uint8_t)i); // random
    }
    w8(&w, 0);                                                        // legacy_session_id: empty
    w16(&w, 2);                                                       // cipher_suites length
    w16(&w, o->suite ? PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256 : 0x1305); // TLS_AES_128_CCM_8_SHA256
    w8(&w, 1);                                                        // legacy_compression_methods
    w8(&w, 0);

    size_t ext_at = w.pos;
    w16(&w, 0);

    w16(&w, 0x002b); // supported_versions
    w16(&w, 3);
    w8(&w, 2);
    w16(&w, o->tls13 ? 0x0304 : 0x0303);

    w16(&w, 0x000a); // supported_groups
    w16(&w, 4);
    w16(&w, 2);
    w16(&w, o->group ? TLS_GROUP_X25519 : 0x0017); // secp256r1

    w16(&w, 0x000d); // signature_algorithms
    w16(&w, 4);
    w16(&w, 2);
    w16(&w, o->ed25519 ? TLS_SIG_ED25519 : 0x0403); // ecdsa_secp256r1_sha256

    if (o->share)
    {
        w16(&w, 0x0033); // key_share
        w16(&w, 38);
        w16(&w, 36);
        w16(&w, TLS_GROUP_X25519);
        w16(&w, 32);
        wn(&w, g_client_pub, 32);
    }

    w16(&w, 0x0010); // application_layer_protocol_negotiation (RFC 7301)
    w16(&w, 5);
    w16(&w, 3);
    w8(&w, 2);
    w8(&w, o->alpn_h3 ? 'h' : 'x');
    w8(&w, o->alpn_h3 ? '3' : 'y');

    if (o->quic_tp)
    {
        w16(&w, TLS_EXT_QUIC_TRANSPORT_PARAMS);
        w16(&w, (uint16_t)tp_len);
        wn(&w, tp, tp_len);
    }

    size_t ext_len = w.pos - ext_at - 2;
    out[ext_at] = (uint8_t)(ext_len >> 8);
    out[ext_at + 1] = (uint8_t)ext_len;
    size_t hs_len = w.pos - hs_at - 3;
    out[hs_at] = (uint8_t)(hs_len >> 16);
    out[hs_at + 1] = (uint8_t)(hs_len >> 8);
    out[hs_at + 2] = (uint8_t)hs_len;
    return w.pos;
}

// A server configured with fixed ephemeral inputs, so a run is reproducible.
static void server_start(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.cert_der = CERT_DER;
    g_cfg.cert_len = sizeof(CERT_DER);
    memset(g_cfg.ed25519_seed, 0x42, sizeof(g_cfg.ed25519_seed));
    memset(g_cfg.ephemeral_priv, 0x77, sizeof(g_cfg.ephemeral_priv));
    memset(g_cfg.random, 0x5A, sizeof(g_cfg.random));
    protocore_quic_tp_defaults(&g_cfg.params);
    g_cfg.params.initial_max_data = 65536;
    protocore_quic_tls_server_init(&g_qt, &g_cfg);

    memset(g_client_priv, 0x33, sizeof(g_client_priv));
    protocore_x25519_base(g_client_pub, g_client_priv);
}

static size_t feed_client_hello(const ChOpts *o)
{
    server_start();
    g_ch_len = build_client_hello(g_ch, o);
    return protocore_quic_tls_recv_crypto(&g_qt, QUIC_ENC_INITIAL, g_ch, g_ch_len);
}

// The length of the handshake message at @p p (4-octet header + 24-bit body length).
static size_t msg_len(const uint8_t *p)
{
    return 4u + (((size_t)p[1] << 16) | ((size_t)p[2] << 8) | p[3]);
}

// RFC 8446 sec 4.4 fixes the server's flight order after the ServerHello: EncryptedExtensions,
// [CertificateRequest], Certificate, CertificateVerify, Finished. RFC 9001 sec 4.1 puts the
// ServerHello at the Initial encryption level and the rest at Handshake.
void test_rfc8446_server_flight_order(void)
{
    TEST_ASSERT_EQUAL_UINT(g_ch_len, feed_client_hello(&CH_FULL));
    TEST_ASSERT_EQUAL_INT(QTLS_WAIT_FINISHED, g_qt.state);

    size_t n = 0;
    const uint8_t *initial = protocore_quic_tls_flight(&g_qt, QUIC_ENC_INITIAL, &n);
    TEST_ASSERT_TRUE(n > 4);
    TEST_ASSERT_EQUAL_HEX8(TLS_HS_SERVER_HELLO, initial[0]);
    TEST_ASSERT_EQUAL_UINT(n, msg_len(initial)); // exactly one message at this level

    const uint8_t *hs = protocore_quic_tls_flight(&g_qt, QUIC_ENC_HANDSHAKE, &n);
    static const uint8_t ORDER[4] = {TLS_HS_ENCRYPTED_EXTENSIONS, TLS_HS_CERTIFICATE, TLS_HS_CERTIFICATE_VERIFY,
                                     TLS_HS_FINISHED};
    size_t off = 0;
    for (size_t i = 0; i < 4; i++)
    {
        TEST_ASSERT_TRUE(off + 4 <= n);
        TEST_ASSERT_EQUAL_HEX8(ORDER[i], hs[off]);
        off += msg_len(hs + off);
        TEST_ASSERT_TRUE(off <= n);
    }
    TEST_ASSERT_EQUAL_UINT(n, off); // and nothing after the Finished

    // sec 4.4.4: verify_data is Hash.length octets, 32 for SHA-256
    TEST_ASSERT_EQUAL_UINT(4u + 32u, msg_len(hs + off - 36));

    // RFC 9001 sec 4.1: Initial keys are not derived here; Handshake and 1-RTT keys are, both ways
    TEST_ASSERT_NULL(protocore_quic_tls_keys(&g_qt, QUIC_ENC_INITIAL, PROTO_TRUE));
    TEST_ASSERT_NOT_NULL(protocore_quic_tls_keys(&g_qt, QUIC_ENC_HANDSHAKE, PROTO_TRUE));
    TEST_ASSERT_NOT_NULL(protocore_quic_tls_keys(&g_qt, QUIC_ENC_HANDSHAKE, PROTO_FALSE));
    TEST_ASSERT_NOT_NULL(protocore_quic_tls_keys(&g_qt, QUIC_ENC_APP, PROTO_TRUE));
    TEST_ASSERT_NOT_NULL(protocore_quic_tls_keys(&g_qt, QUIC_ENC_APP, PROTO_FALSE));
}

// RFC 8446 sec 4.1.3: the ServerHello names the suite and the key_share group the server selected,
// and supported_versions carries 0x0304. The server's X25519 share sits at a fixed offset once the
// echoed session id is empty: 4 header + 2 legacy_version + 32 random + 1 session id length
// + 2 cipher suite + 1 compression + 2 extensions length + 8 key_share extension header = 52.
#define SH_SHARE_OFFSET 52

void test_rfc8446_server_hello_fields(void)
{
    TEST_ASSERT_EQUAL_UINT(g_ch_len, feed_client_hello(&CH_FULL));
    size_t n = 0;
    const uint8_t *sh = protocore_quic_tls_flight(&g_qt, QUIC_ENC_INITIAL, &n);
    TEST_ASSERT_EQUAL_UINT(90u, n);
    TEST_ASSERT_EQUAL_HEX8(0x03, sh[4]); // legacy_version 0x0303
    TEST_ASSERT_EQUAL_HEX8(0x03, sh[5]);
    TEST_ASSERT_EQUAL_MEMORY(g_cfg.random, sh + 6, 32);
    TEST_ASSERT_EQUAL_HEX8(0x00, sh[38]); // legacy_session_id_echo: the client sent none
    TEST_ASSERT_EQUAL_HEX8(0x13, sh[39]); // TLS_AES_128_GCM_SHA256
    TEST_ASSERT_EQUAL_HEX8(0x01, sh[40]);
    TEST_ASSERT_EQUAL_HEX8(0x00, sh[41]); // legacy_compression_method
    static const uint8_t KS_HDR[8] = {0x00, 0x33, 0x00, 0x24, 0x00, 0x1D, 0x00, 0x20};
    TEST_ASSERT_EQUAL_MEMORY(KS_HDR, sh + 44, 8);
    static const uint8_t SV[6] = {0x00, 0x2B, 0x00, 0x02, 0x03, 0x04};
    TEST_ASSERT_EQUAL_MEMORY(SV, sh + SH_SHARE_OFFSET + 32, 6);

    // the share is the public half of the configured ephemeral private key
    uint8_t want_pub[32];
    protocore_x25519_base(want_pub, g_cfg.ephemeral_priv);
    TEST_ASSERT_EQUAL_MEMORY(want_pub, sh + SH_SHARE_OFFSET, 32);
}

// The whole handshake, with the client half computed here.
//
// RFC 8446 sec 7.1: the Handshake Secret is HKDF-Extract over the (EC)DHE shared secret, and each
// traffic secret is Derive-Secret of it over a Transcript-Hash the two ends compute independently.
// If the server hashed the same messages in the same order and used the same shared secret, the
// client's packet keys equal the server's - RFC 9001 sec 5.1 makes those keys a pure function of the
// traffic secret, so comparing the nonce bases compares the secrets.
void test_handshake_interop_round_trip(void)
{
    static uint8_t ks_store[PROTOCORE_TLS13_KS_BORROW];
    static uint8_t hash_work[PROTOCORE_SHA256_BORROW];
    static uint8_t keys_work[PROTOCORE_QUIC_KEYS_BORROW];
    Tls13KeySchedule ks;
    protocore_sha256_ctx tr;
    uint8_t hash[32];

    TEST_ASSERT_EQUAL_UINT(g_ch_len, feed_client_hello(&CH_FULL));
    TEST_ASSERT_EQUAL_INT(QTLS_WAIT_FINISHED, g_qt.state);

    size_t sh_len = 0;
    const uint8_t *sh = protocore_quic_tls_flight(&g_qt, QUIC_ENC_INITIAL, &sh_len);
    size_t hs_len = 0;
    const uint8_t *hs = protocore_quic_tls_flight(&g_qt, QUIC_ENC_HANDSHAKE, &hs_len);

    // the client's (EC)DHE secret, from its own private key and the server's share
    uint8_t ecdhe[32];
    protocore_x25519(ecdhe, g_client_priv, sh + SH_SHARE_OFFSET);

    // Transcript-Hash(ClientHello .. ServerHello)
    memset(ks_store, 0, sizeof(ks_store));
    protocore_sha256_init(&tr, hash_work);
    protocore_sha256_update(&tr, g_ch, g_ch_len);
    protocore_sha256_update(&tr, sh, sh_len);
    {
        protocore_sha256_ctx snap = tr;
        protocore_sha256_final(&snap, hash);
    }

    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &ks;
    Tls13Ks.bind.s = ks_store;
    Tls13Ks.early(Tls13Ks.internal);
    TEST_ASSERT_TRUE(Tls13Ks.ok);
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = sizeof(ecdhe);
    Tls13Ks.step.ch_sh_hash = hash;
    Tls13Ks.handshake(Tls13Ks.internal);

    // the Handshake-level packet keys both ends derive from those secrets must agree
    QuicPacketKeys mine;
    protocore_quic_keys_from_secret(keys_work, ks.s + TLS13_KS_CLIENT_HS, &mine);
    TEST_ASSERT_EQUAL_MEMORY(protocore_quic_tls_keys(&g_qt, QUIC_ENC_HANDSHAKE, PROTO_FALSE)->iv, mine.iv, 12);
    protocore_quic_keys_from_secret(keys_work, ks.s + TLS13_KS_SERVER_HS, &mine);
    TEST_ASSERT_EQUAL_MEMORY(protocore_quic_tls_keys(&g_qt, QUIC_ENC_HANDSHAKE, PROTO_TRUE)->iv, mine.iv, 12);

    // the server's Finished, checked against a verify_data computed here over
    // Transcript-Hash(ClientHello .. CertificateVerify) - RFC 8446 sec 4.4.4
    size_t fin_at = 0;
    for (size_t i = 0; i < 3; i++)
    {
        protocore_sha256_update(&tr, hs + fin_at, msg_len(hs + fin_at));
        fin_at += msg_len(hs + fin_at);
    }
    {
        protocore_sha256_ctx snap = tr;
        protocore_sha256_final(&snap, hash);
    }
    uint8_t verify[32];
    Tls13Ks.bind.ks = &ks;
    Tls13Ks.bind.s = ks_store;
    Tls13Ks.finished_args.base_secret = ks.s + TLS13_KS_SERVER_HS;
    Tls13Ks.finished_args.transcript_hash = hash;
    Tls13Ks.finished_args.out = verify;
    Tls13Ks.finished_mac(Tls13Ks.internal);
    TEST_ASSERT_EQUAL_HEX8(TLS_HS_FINISHED, hs[fin_at]);
    TEST_ASSERT_EQUAL_UINT(4u + 32u, msg_len(hs + fin_at));
    TEST_ASSERT_EQUAL_MEMORY(verify, hs + fin_at + 4, 32);

    // Transcript-Hash(ClientHello .. server Finished) keys the application secrets
    protocore_sha256_update(&tr, hs + fin_at, msg_len(hs + fin_at));
    {
        protocore_sha256_ctx snap = tr;
        protocore_sha256_final(&snap, hash);
    }
    Tls13Ks.bind.ks = &ks;
    Tls13Ks.bind.s = ks_store;
    Tls13Ks.step.ch_sfin_hash = hash;
    Tls13Ks.master(Tls13Ks.internal);
    protocore_quic_keys_from_secret(keys_work, ks.s + TLS13_KS_CLIENT_AP, &mine);
    TEST_ASSERT_EQUAL_MEMORY(protocore_quic_tls_keys(&g_qt, QUIC_ENC_APP, PROTO_FALSE)->iv, mine.iv, 12);
    protocore_quic_keys_from_secret(keys_work, ks.s + TLS13_KS_SERVER_AP, &mine);
    TEST_ASSERT_EQUAL_MEMORY(protocore_quic_tls_keys(&g_qt, QUIC_ENC_APP, PROTO_TRUE)->iv, mine.iv, 12);

    // the client Finished is taken over the same transcript under the client's handshake secret
    uint8_t cfin[36];
    Tls13Ks.bind.ks = &ks;
    Tls13Ks.bind.s = ks_store;
    Tls13Ks.finished_args.base_secret = ks.s + TLS13_KS_CLIENT_HS;
    Tls13Ks.finished_args.transcript_hash = hash;
    Tls13Ks.finished_args.out = cfin + 4;
    Tls13Ks.finished_mac(Tls13Ks.internal);
    cfin[0] = TLS_HS_FINISHED;
    cfin[1] = 0;
    cfin[2] = 0;
    cfin[3] = 32;
    TEST_ASSERT_EQUAL_UINT(sizeof(cfin), protocore_quic_tls_recv_crypto(&g_qt, QUIC_ENC_HANDSHAKE, cfin, sizeof(cfin)));
    TEST_ASSERT_EQUAL_INT(QTLS_DONE, g_qt.state);
    TEST_ASSERT_TRUE(g_qt.complete);

    // and one flipped octet in it is a decrypt_error rather than a completed handshake
    server_start();
    (void)protocore_quic_tls_recv_crypto(&g_qt, QUIC_ENC_INITIAL, g_ch, g_ch_len);
    cfin[4] ^= 0x01;
    (void)protocore_quic_tls_recv_crypto(&g_qt, QUIC_ENC_HANDSHAKE, cfin, sizeof(cfin));
    TEST_ASSERT_EQUAL_INT(QTLS_FAILED, g_qt.state);
    TEST_ASSERT_EQUAL_UINT8(51, g_qt.alert); // decrypt_error, RFC 8446 sec 6.2
}

// The client's transport parameters ride in the RFC 9001 sec 8.2 extension and reach the caller.
void test_rfc9001_peer_transport_parameters(void)
{
    TEST_ASSERT_EQUAL_UINT(g_ch_len, feed_client_hello(&CH_FULL));
    const QuicTransportParams *p = protocore_quic_tls_peer_params(&g_qt);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT64(1048576u, p->initial_max_data);
    TEST_ASSERT_EQUAL_UINT64(8u, p->initial_max_streams_bidi);
    // RFC 9000 sec 18.2 defaults for what the client did not send
    TEST_ASSERT_EQUAL_UINT64(3u, p->ack_delay_exponent);
    TEST_ASSERT_EQUAL_UINT64(25u, p->max_ack_delay);
    TEST_ASSERT_EQUAL_UINT64(2u, p->active_connection_id_limit);
}

// Each capability the profile requires, removed one at a time, with the alert RFC 8446 sec 6.2 (and
// RFC 7301 sec 3.2 for ALPN) assigns to that failure.
void test_negotiation_failures(void)
{
    struct
    {
        ChOpts o;
        uint8_t alert;
    } CASES[6];
    static const char *const WHY[6] = {"no TLS 1.3",       "no AES-128-GCM-SHA256", "no ed25519",
                                       "no x25519 group",  "no key_share",          "no h3 ALPN"};
    for (size_t i = 0; i < 6; i++)
    {
        CASES[i].o = CH_FULL;
    }
    CASES[0].o.tls13 = PROTO_FALSE;
    CASES[0].alert = 70; // protocol_version
    CASES[1].o.suite = PROTO_FALSE;
    CASES[1].alert = 40; // handshake_failure
    CASES[2].o.ed25519 = PROTO_FALSE;
    CASES[2].alert = 40;
    CASES[3].o.group = PROTO_FALSE;
    CASES[3].alert = 40;
    CASES[4].o.share = PROTO_FALSE;
    CASES[4].alert = 40;
    CASES[5].o.alpn_h3 = PROTO_FALSE;
    CASES[5].alert = 120; // no_application_protocol, RFC 7301 sec 3.2

    for (size_t i = 0; i < 6; i++)
    {
        (void)feed_client_hello(&CASES[i].o);
        TEST_ASSERT_EQUAL_INT_MESSAGE(QTLS_FAILED, g_qt.state, WHY[i]);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(CASES[i].alert, g_qt.alert, WHY[i]);
        size_t n = 1;
        (void)protocore_quic_tls_flight(&g_qt, QUIC_ENC_HANDSHAKE, &n);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, n, WHY[i]); // nothing is sent after a failure
    }
}

// RFC 9001 sec 8.2: "endpoints that receive ClientHello or EncryptedExtensions messages without the
// quic_transport_parameters extension MUST close the connection with an error of type 0x016d
// (equivalent to a fatal TLS missing_extension alert)".
void test_rfc9001_missing_transport_parameters(void)
{
    ChOpts o = CH_FULL;
    o.quic_tp = PROTO_FALSE;
    (void)feed_client_hello(&o);
    TEST_ASSERT_EQUAL_INT(QTLS_FAILED, g_qt.state);
    TEST_ASSERT_EQUAL_UINT8(109, g_qt.alert); // missing_extension
}

// The CRYPTO stream is a byte stream, so a run that stops inside a handshake message is left for the
// next call rather than parsed from whatever follows it in memory.
void test_partial_crypto_is_not_consumed(void)
{
    server_start();
    g_ch_len = build_client_hello(g_ch, &CH_FULL);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_quic_tls_recv_crypto(&g_qt, QUIC_ENC_INITIAL, g_ch, 3));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_quic_tls_recv_crypto(&g_qt, QUIC_ENC_INITIAL, g_ch, g_ch_len - 1));
    TEST_ASSERT_EQUAL_INT(QTLS_START, g_qt.state);
    TEST_ASSERT_EQUAL_UINT(g_ch_len, protocore_quic_tls_recv_crypto(&g_qt, QUIC_ENC_INITIAL, g_ch, g_ch_len));
    TEST_ASSERT_EQUAL_INT(QTLS_WAIT_FINISHED, g_qt.state);
}

// RFC 9001 sec 4.1.3 pairs each handshake message with the encryption level it belongs at, so a
// ClientHello arriving at the Handshake level, or a second one after the flight went out, is an
// unexpected_message.
void test_message_at_the_wrong_level_or_state(void)
{
    server_start();
    g_ch_len = build_client_hello(g_ch, &CH_FULL);
    (void)protocore_quic_tls_recv_crypto(&g_qt, QUIC_ENC_HANDSHAKE, g_ch, g_ch_len);
    TEST_ASSERT_EQUAL_INT(QTLS_FAILED, g_qt.state);
    TEST_ASSERT_EQUAL_UINT8(10, g_qt.alert); // unexpected_message

    TEST_ASSERT_EQUAL_UINT(g_ch_len, feed_client_hello(&CH_FULL));
    (void)protocore_quic_tls_recv_crypto(&g_qt, QUIC_ENC_INITIAL, g_ch, g_ch_len);
    TEST_ASSERT_EQUAL_INT(QTLS_FAILED, g_qt.state);
    TEST_ASSERT_EQUAL_UINT8(10, g_qt.alert);
}

// A message that is not a ClientHello at all is a decode_error, not a partially applied handshake.
void test_malformed_client_hello(void)
{
    static const uint8_t TRUNCATED[8] = {TLS_HS_CLIENT_HELLO, 0x00, 0x00, 0x04, 0x03, 0x03, 0x00, 0x00};
    server_start();
    TEST_ASSERT_EQUAL_UINT(8u, protocore_quic_tls_recv_crypto(&g_qt, QUIC_ENC_INITIAL, TRUNCATED, sizeof(TRUNCATED)));
    TEST_ASSERT_EQUAL_INT(QTLS_FAILED, g_qt.state);
    TEST_ASSERT_EQUAL_UINT8(50, g_qt.alert); // decode_error

    // and once failed, later bytes are drained rather than reprocessed
    TEST_ASSERT_EQUAL_UINT(4u, protocore_quic_tls_recv_crypto(&g_qt, QUIC_ENC_INITIAL, TRUNCATED, 4));
}
