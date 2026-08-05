// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the TLS 1.3 server handshake state machine (network_drivers/presentation/http/http3/
// pc_quic_tls; RFC 9001 / RFC 8446). The main test drives the server with a hand-built ClientHello and
// then runs the *client* half of the key schedule independently in the test, proving both sides
// derive identical handshake + application secrets and that the server accepts the client Finished
// (a full interop round-trip of the state machine). Plus: capability-negotiation rejections.

#include "crypto/asymmetric/curve25519.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/quic_tls.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/tls/tls13_kdf.h"
#if PC_ENABLE_PQC_KEX
#include "../test_pqc_mlkem/mlkem_kat.h" // kat_ek, kat_dk (a valid ML-KEM key pair)
#include "../test_ssh_pqc/mlkem_ref.h"   // pc_mlkem768_decaps_ref (the client side)
#include "crypto/pqc/mlkem.h"            // MLKEM768_EK_BYTES / MLKEM768_CT_BYTES
#endif

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// A dummy leaf certificate blob (the state machine embeds it verbatim; the test does not verify the
// X.509 signature - that path is covered by test_tls13_msg's Ed25519 sign/verify).
static const uint8_t CERT[48] = {0x30, 0x2e, 0x02, 0x01, 0x02};
static uint8_t SERVER_PRIV[32];
static uint8_t SERVER_SEED[32];
static uint8_t SERVER_RANDOM[32];
static uint8_t CLIENT_PRIV[32];

static void fill_test_material()
{
    for (int i = 0; i < 32; i++)
    {
        SERVER_PRIV[i] = (uint8_t)(0x40 + i);
        SERVER_SEED[i] = (uint8_t)(0x80 + i);
        SERVER_RANDOM[i] = (uint8_t)(0xA0 + i);
        CLIENT_PRIV[i] = (uint8_t)(0x01 + i);
    }
}

// Which ClientHello extensions to include (omitting one exercises the matching server rejection).
enum
{
    INC_SV = 1,    // supported_versions (TLS 1.3)
    INC_SG = 2,    // supported_groups (x25519)
    INC_SA = 4,    // signature_algorithms (ed25519)
    INC_KS = 8,    // key_share (x25519 + pub)
    INC_ALPN = 16, // ALPN h3
    INC_TP = 32,   // pc_quic_transport_parameters
    INC_ALL = 63,
};

// Build a TLS 1.3 ClientHello (QUIC) including exactly the extensions selected by @p inc. Returns the
// length of the whole handshake message.
static size_t build_ch_ext(uint8_t *out, const uint8_t client_pub[32], const uint8_t *tp, size_t tp_len, unsigned inc)
{
    size_t p = 0;
    out[p++] = TLS_HS_CLIENT_HELLO;
    size_t hs_len_at = p;
    p += 3; // handshake length, patched below
    out[p++] = 0x03;
    out[p++] = 0x03; // legacy_version
    for (int i = 0; i < 32; i++)
    {
        out[p++] = (uint8_t)i; // random
    }
    out[p++] = 0x00; // session id length 0
    // cipher_suites
    out[p++] = 0x00;
    out[p++] = 0x02;
    out[p++] = 0x13;
    out[p++] = 0x01;
    // compression
    out[p++] = 0x01;
    out[p++] = 0x00;
    // extensions
    size_t ext_len_at = p;
    p += 2;
    if (inc & INC_SV) // supported_versions
    {
        static const uint8_t sv[] = {0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04};
        memcpy(out + p, sv, sizeof(sv));
        p += sizeof(sv);
    }
    if (inc & INC_SG) // supported_groups (x25519)
    {
        static const uint8_t sg[] = {0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d};
        memcpy(out + p, sg, sizeof(sg));
        p += sizeof(sg);
    }
    if (inc & INC_SA) // signature_algorithms (ed25519)
    {
        static const uint8_t sa[] = {0x00, 0x0d, 0x00, 0x04, 0x00, 0x02, 0x08, 0x07};
        memcpy(out + p, sa, sizeof(sa));
        p += sizeof(sa);
    }
    if (inc & INC_KS) // key_share (x25519 + 32-byte pub)
    {
        static const uint8_t ks[] = {0x00, 0x33, 0x00, 0x26, 0x00, 0x24, 0x00, 0x1d, 0x00, 0x20};
        memcpy(out + p, ks, sizeof(ks));
        p += sizeof(ks);
        memcpy(out + p, client_pub, 32);
        p += 32;
    }
    if (inc & INC_ALPN) // ALPN h3
    {
        static const uint8_t alpn[] = {0x00, 0x10, 0x00, 0x05, 0x00, 0x03, 0x02, 'h', '3'};
        memcpy(out + p, alpn, sizeof(alpn));
        p += sizeof(alpn);
    }
    if (inc & INC_TP) // pc_quic_transport_parameters
    {
        out[p++] = 0x00;
        out[p++] = 0x39;
        out[p++] = (uint8_t)(tp_len >> 8);
        out[p++] = (uint8_t)tp_len;
        memcpy(out + p, tp, tp_len);
        p += tp_len;
    }
    // patch ext_len and hs_len
    uint16_t ext_len = (uint16_t)(p - ext_len_at - 2);
    out[ext_len_at] = (uint8_t)(ext_len >> 8);
    out[ext_len_at + 1] = (uint8_t)ext_len;
    uint32_t hs_len = (uint32_t)(p - hs_len_at - 3);
    out[hs_len_at] = (uint8_t)(hs_len >> 16);
    out[hs_len_at + 1] = (uint8_t)(hs_len >> 8);
    out[hs_len_at + 2] = (uint8_t)hs_len;
    return p;
}

// Build a full, valid ClientHello (all required extensions).
static size_t build_client_hello(uint8_t *out, const uint8_t client_pub[32], const uint8_t *tp, size_t tp_len)
{
    return build_ch_ext(out, client_pub, tp, tp_len, INC_ALL);
}

static void make_server_config(QuicTlsConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->cert_der = CERT;
    cfg->cert_len = sizeof(CERT);
    memcpy(cfg->ed25519_seed, SERVER_SEED, 32);
    memcpy(cfg->ephemeral_priv, SERVER_PRIV, 32);
    memcpy(cfg->random, SERVER_RANDOM, 32);
    pc_quic_tp_defaults(&cfg->params);
    cfg->params.has_initial_scid = PROTO_TRUE;
    cfg->params.initial_scid_len = 4;
    memcpy(cfg->params.initial_scid, "\x11\x22\x33\x44", 4);
    cfg->params.initial_max_data = 1048576;
    cfg->params.initial_max_streams_bidi = 8;
}

void test_full_handshake_roundtrip()
{
    fill_test_material();
    QuicTlsConfig cfg;
    make_server_config(&cfg);
    QuicTls qt;
    pc_quic_tls_server_init(&qt, &cfg);

    // Client transport params.
    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    ctp.has_initial_scid = PROTO_TRUE;
    ctp.initial_scid_len = 4;
    memcpy(ctp.initial_scid, "\xaa\xbb\xcc\xdd", 4);
    ctp.initial_max_data = 524288;
    uint8_t ctp_enc[128];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));

    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);

    // 1) Feed the ClientHello. The server should build its flights and derive keys.
    size_t used = pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len);
    TEST_ASSERT_EQUAL_UINT(ch_len, used);
    TEST_ASSERT_EQUAL_UINT8(QTLS_WAIT_FINISHED, qt.state);
    TEST_ASSERT_TRUE(qt.hs_keys_ready);
    TEST_ASSERT_TRUE(qt.ap_keys_ready);

    size_t si_len = 0, sh_flight_len = 0;
    const uint8_t *si = pc_quic_tls_flight(&qt, QUIC_ENC_INITIAL, &si_len);
    const uint8_t *sh_flight = pc_quic_tls_flight(&qt, QUIC_ENC_HANDSHAKE, &sh_flight_len);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_SERVER_HELLO, si[0]);                // Initial flight = ServerHello
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_ENCRYPTED_EXTENSIONS, sh_flight[0]); // then EE..Finished

    // Server parsed our transport params.
    const QuicTransportParams *peer = pc_quic_tls_peer_params(&qt);
    TEST_ASSERT_NOT_NULL(peer);
    TEST_ASSERT_EQUAL_UINT64(524288, peer->initial_max_data);

    // 2) Independently run the CLIENT key schedule and confirm the secrets match the server's.
    uint8_t server_pub[32], ecdhe[32];
    pc_x25519_base(server_pub, SERVER_PRIV);
    pc_x25519(ecdhe, CLIENT_PRIV, server_pub);

    pc_sha256_ctx t;
    uint8_t ch_sh[32], ch_sf[32];
    pc_sha256_init(&t);
    pc_sha256_update(&t, ch, ch_len);
    pc_sha256_update(&t, si, si_len);
    { // snapshot H(CH..SH)
        pc_sha256_ctx tmp = t;
        pc_sha256_final(&tmp, ch_sh);
    }
    pc_sha256_update(&t, sh_flight, sh_flight_len);
    pc_sha256_final(&t, ch_sf); // H(CH..server Finished)

    Tls13KeySchedule cks;
    pc_tls13_ks_early(&TLS13_KDF, &cks);
    pc_tls13_ks_handshake(&cks, ecdhe, ch_sh, sizeof(ecdhe));
    pc_tls13_ks_master(&cks, ch_sf);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(qt.ks.handshake_secret, cks.handshake_secret, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(qt.ks.client_hs_traffic, cks.client_hs_traffic, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(qt.ks.server_hs_traffic, cks.server_hs_traffic, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(qt.ks.client_ap_traffic, cks.client_ap_traffic, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(qt.ks.server_ap_traffic, cks.server_ap_traffic, 32);

    // The server Finished (tail of the handshake flight) must verify under the server hs secret.
    uint8_t ch_cv[32]; // H(CH..CertificateVerify) = everything but the trailing 36-byte Finished
    pc_sha256_init(&t);
    pc_sha256_update(&t, ch, ch_len);
    pc_sha256_update(&t, si, si_len);
    pc_sha256_update(&t, sh_flight, sh_flight_len - 36);
    pc_sha256_final(&t, ch_cv);
    uint8_t sfin_expected[32];
    pc_tls13_finished_mac(&TLS13_KDF, cks.server_hs_traffic, ch_cv, sfin_expected);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(sfin_expected, sh_flight + sh_flight_len - 32, 32);

    // 3) Client builds its Finished and the server accepts it.
    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    pc_tls13_finished_mac(&TLS13_KDF, cks.client_hs_traffic, ch_sf, cfin + 4);
    used = pc_quic_tls_recv_crypto(&qt, QUIC_ENC_HANDSHAKE, cfin, sizeof(cfin));
    TEST_ASSERT_EQUAL_UINT(sizeof(cfin), used);
    TEST_ASSERT_EQUAL_UINT8(QTLS_DONE, qt.state);
    TEST_ASSERT_TRUE(qt.complete);

    // Keys are exposed for both directions at both levels.
    TEST_ASSERT_NOT_NULL(pc_quic_tls_keys(&qt, QUIC_ENC_HANDSHAKE, PROTO_TRUE));
    TEST_ASSERT_NOT_NULL(pc_quic_tls_keys(&qt, QUIC_ENC_APP, PROTO_FALSE));
}

void test_reject_bad_client_finished()
{
    fill_test_material();
    QuicTlsConfig cfg;
    make_server_config(&cfg);
    QuicTls qt;
    pc_quic_tls_server_init(&qt, &cfg);

    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[64];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len);
    TEST_ASSERT_EQUAL_UINT8(QTLS_WAIT_FINISHED, qt.state);

    // A Finished with the wrong verify_data must be rejected (decrypt_error).
    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    memset(cfin + 4, 0x99, 32);
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_HANDSHAKE, cfin, sizeof(cfin));
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    TEST_ASSERT_EQUAL_UINT8(51, qt.alert); // decrypt_error
}

// A ClientHello whose ALPN lacks "h3" is rejected with no_application_protocol.
void test_reject_no_h3_alpn()
{
    fill_test_material();
    QuicTlsConfig cfg;
    make_server_config(&cfg);
    QuicTls qt;
    pc_quic_tls_server_init(&qt, &cfg);

    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[64];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    // Corrupt the ALPN protocol name "h3" -> "h9" so it no longer offers h3.
    for (size_t i = 0; i + 1 < ch_len; i++)
    {
        if (ch[i] == 'h' && ch[i + 1] == '3')
        {
            ch[i + 1] = '9';
            break;
        }
    }
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len);
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    TEST_ASSERT_EQUAL_UINT8(120, qt.alert); // no_application_protocol
}

// Incomplete CRYPTO (a partial ClientHello) consumes nothing and stays in START.
void test_partial_client_hello()
{
    fill_test_material();
    QuicTlsConfig cfg;
    make_server_config(&cfg);
    QuicTls qt;
    pc_quic_tls_server_init(&qt, &cfg);

    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[64];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);

    size_t used = pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len - 10);
    TEST_ASSERT_EQUAL_UINT(0, used);
    TEST_ASSERT_EQUAL_UINT8(QTLS_START, qt.state);
    // Delivering the whole message now completes it.
    used = pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len);
    TEST_ASSERT_EQUAL_UINT(ch_len, used);
    TEST_ASSERT_EQUAL_UINT8(QTLS_WAIT_FINISHED, qt.state);
}

// Feed a ClientHello with extensions @p inc (and optional malformed transport params) and return the
// alert the server failed with. Asserts the handshake ended in QTLS_FAILED.
static uint8_t reject_alert(unsigned inc, const uint8_t *bad_tp, size_t bad_tp_len)
{
    fill_test_material();
    QuicTlsConfig cfg;
    make_server_config(&cfg);
    QuicTls qt;
    pc_quic_tls_server_init(&qt, &cfg);

    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[64];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    const uint8_t *tp = bad_tp ? bad_tp : ctp_enc;
    size_t tpl = bad_tp ? bad_tp_len : ctp_len;

    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_ch_ext(ch, client_pub, tp, tpl, inc);
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len);
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    return qt.alert;
}

// No supported_versions (TLS 1.3) -> protocol_version(70).
void test_reject_no_tls13()
{
    TEST_ASSERT_EQUAL_UINT8(70, reject_alert(INC_ALL & ~INC_SV, NULL, 0));
}

// Missing key_share, x25519, or ed25519 -> handshake_failure(40) (no usable ECDHE / signature).
void test_reject_no_key_share()
{
    TEST_ASSERT_EQUAL_UINT8(40, reject_alert(INC_ALL & ~INC_KS, NULL, 0));
}
void test_reject_no_x25519_group()
{
    TEST_ASSERT_EQUAL_UINT8(40, reject_alert(INC_ALL & ~INC_SG, NULL, 0));
}
void test_reject_no_ed25519()
{
    TEST_ASSERT_EQUAL_UINT8(40, reject_alert(INC_ALL & ~INC_SA, NULL, 0));
}

// No pc_quic_transport_parameters extension -> missing_extension(109).
void test_reject_no_transport_params()
{
    TEST_ASSERT_EQUAL_UINT8(109, reject_alert(INC_ALL & ~INC_TP, NULL, 0));
}

// Malformed transport params (an oversize connection ID, 21 > QUIC_MAX_CID_LEN) -> illegal_parameter(47).
void test_reject_bad_transport_params()
{
    uint8_t bad_tp[2 + 21] = {0x0f, 0x15}; // initial_scid, length 21, then 21 zero bytes
    TEST_ASSERT_EQUAL_UINT8(47, reject_alert(INC_ALL, bad_tp, sizeof(bad_tp)));
}

// A structurally invalid ClientHello (declared length too small to hold the fields) -> decode_error(50).
void test_reject_malformed_client_hello()
{
    fill_test_material();
    QuicTlsConfig cfg;
    make_server_config(&cfg);
    QuicTls qt;
    pc_quic_tls_server_init(&qt, &cfg);
    // ClientHello, handshake length 2, body = legacy_version only (no random / ciphers / extensions).
    uint8_t bad[] = {TLS_HS_CLIENT_HELLO, 0x00, 0x00, 0x02, 0x03, 0x03};
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, bad, sizeof(bad));
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    TEST_ASSERT_EQUAL_UINT8(50, qt.alert); // decode_error
}

// Drive a fresh server to QTLS_WAIT_FINISHED with a well-formed ClientHello.
static void drive_to_wait_finished(QuicTls *qt, QuicTlsConfig *cfg)
{
    fill_test_material();
    make_server_config(cfg);
    pc_quic_tls_server_init(qt, cfg);
    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[64];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    pc_quic_tls_recv_crypto(qt, QUIC_ENC_INITIAL, ch, ch_len);
}

// Drive a fresh server through a ClientHello with the given config; return the resulting state.
static QtlsState run_handshake(const QuicTlsConfig *cfg)
{
    QuicTls qt;
    pc_quic_tls_server_init(&qt, cfg);
    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[64];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len);
    return qt.state;
}

// A certificate sized so the Certificate message fits the flight buffer but the following
// CertificateVerify (then, separately, the Finished) does not - exercising each later emit()
// overflow guard. Sizes are measured at runtime so this stays correct if a buffer/builder changes.
void test_quic_tls_cert_size_boundary_emit_fails()
{
    fill_test_material();
    QuicTlsConfig cfg;
    make_server_config(&cfg);

    QuicTls dummy;
    const size_t buf = sizeof(dummy.flight_hs);
    uint8_t scratch[4096];
    uint8_t tp_enc[512];
    size_t tp_len = pc_quic_tp_encode(&cfg.params, tp_enc, sizeof(tp_enc));
    size_t ee =
        pc_tls13_build_encrypted_extensions(scratch, sizeof(scratch), tp_enc, tp_len, /*rpk_server_cert=*/PROTO_FALSE);
    size_t cert_overhead = pc_tls13_build_certificate(scratch, sizeof(scratch), CERT, 0); // fixed part only
    uint8_t z[32] = {0};
    size_t cv = pc_tls13_build_cert_verify(scratch, sizeof(scratch), z, cfg.ed25519_seed);
    size_t fin = pc_tls13_build_finished(scratch, sizeof(scratch), z);
    TEST_ASSERT_TRUE(ee > 0 && cert_overhead > 0 && cv > 4 && fin > 4);

    static uint8_t big_cert[4096];
    memset(big_cert, 0x30, sizeof(big_cert));
    cfg.cert_der = big_cert;

    // (a) Certificate fits but leaves fewer than cv bytes -> CertificateVerify emit fails.
    size_t leave_a = 8; // 0 < leave_a < cv
    TEST_ASSERT_TRUE(buf > ee + cert_overhead + leave_a);
    cfg.cert_len = buf - ee - cert_overhead - leave_a;
    TEST_ASSERT_TRUE(cfg.cert_len < sizeof(big_cert));
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, run_handshake(&cfg));

    // (b) Certificate + CertificateVerify fit but leave fewer than fin bytes -> Finished emit fails.
    size_t leave_b = cv + (fin - 4); // CertVerify fits; the fin-4 left is too small for Finished
    TEST_ASSERT_TRUE(buf > ee + cert_overhead + leave_b);
    cfg.cert_len = buf - ee - cert_overhead - leave_b;
    TEST_ASSERT_TRUE(cfg.cert_len < sizeof(big_cert));
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, run_handshake(&cfg));
}

void test_quic_tls_more_guards()
{
    QuicTlsConfig cfg;
    QuicTls qt;

    // A Finished-typed message of the wrong length -> DECODE_ERROR inside process_client_finished.
    drive_to_wait_finished(&qt, &cfg);
    TEST_ASSERT_EQUAL_UINT8(QTLS_WAIT_FINISHED, qt.state);
    uint8_t fin_badlen[8] = {TLS_HS_FINISHED, 0x00, 0x00, 0x04, 1, 2, 3, 4}; // 4-byte verify, not 32
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_HANDSHAKE, fin_badlen, sizeof(fin_badlen));
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    TEST_ASSERT_EQUAL_UINT8(50, qt.alert); // decode_error

    // A non-Finished message while awaiting the client Finished -> UNEXPECTED_MESSAGE.
    drive_to_wait_finished(&qt, &cfg);
    uint8_t wrong[36] = {TLS_HS_CLIENT_HELLO, 0x00, 0x00, 0x20};
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_HANDSHAKE, wrong, sizeof(wrong));
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    TEST_ASSERT_EQUAL_UINT8(10, qt.alert); // unexpected_message

    // recv_crypto on a FAILED handshake drains (returns the whole length, changes nothing).
    uint8_t more[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT(sizeof(more), pc_quic_tls_recv_crypto(&qt, QUIC_ENC_HANDSHAKE, more, sizeof(more)));

    // flight() for a level that is neither Initial nor Handshake -> NULL / zero length.
    size_t l = 123;
    TEST_ASSERT_NULL(pc_quic_tls_flight(&qt, QUIC_ENC_APP, &l));
    TEST_ASSERT_EQUAL_UINT(0, l);

    // keys() before the handshake has derived them -> NULL (both levels + an unknown level).
    QuicTls fresh;
    pc_quic_tls_server_init(&fresh, &cfg);
    TEST_ASSERT_NULL(pc_quic_tls_keys(&fresh, QUIC_ENC_HANDSHAKE, PROTO_TRUE));
    TEST_ASSERT_NULL(pc_quic_tls_keys(&fresh, QUIC_ENC_APP, PROTO_FALSE));
    TEST_ASSERT_NULL(pc_quic_tls_keys(&fresh, 999, PROTO_TRUE));

    // An oversized certificate overruns the handshake flight buffer, so the emit() flight-bound guard
    // fails the handshake with INTERNAL_ERROR (cert_der/cert_len are caller-supplied and unbounded).
    fill_test_material();
    make_server_config(&cfg);
    cfg.cert_len = 100000; // far larger than PC_H3_CRYPTO_BUF; the Certificate builder returns 0
    pc_quic_tls_server_init(&qt, &cfg);
    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[64];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len);
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    TEST_ASSERT_EQUAL_UINT8(80, qt.alert); // internal_error
}

#if PC_ENABLE_PQC_KEX
// Build a ClientHello offering X25519MLKEM768: supported_groups {0x11ec, x25519} and a key_share
// carrying the client's ML-KEM ek (@p ek, normally kat_ek) || X25519 pub (1216 B), plus the other
// required extensions. With @p offer_hybrid_group false the hybrid share is sent but supported_groups
// advertises only x25519, so the server must not select the hybrid.
static size_t build_client_hello_hybrid(uint8_t *out, const uint8_t client_pub[32], const uint8_t *tp, size_t tp_len,
                                        const uint8_t *ek, proto_bool offer_hybrid_group)
{
    size_t p = 0;
    out[p++] = TLS_HS_CLIENT_HELLO;
    size_t hs_len_at = p;
    p += 3;
    out[p++] = 0x03;
    out[p++] = 0x03;
    for (int i = 0; i < 32; i++)
    {
        out[p++] = (uint8_t)i; // random
    }
    out[p++] = 0x00; // session id length 0
    out[p++] = 0x00; // cipher_suites
    out[p++] = 0x02;
    out[p++] = 0x13;
    out[p++] = 0x01;
    out[p++] = 0x01; // compression
    out[p++] = 0x00;
    size_t ext_len_at = p;
    p += 2;
    {
        static const uint8_t sv[] = {0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04}; // supported_versions
        memcpy(out + p, sv, sizeof(sv));
        p += sizeof(sv);
    }
    if (offer_hybrid_group)
    {
        static const uint8_t sg[] = {0x00, 0x0a, 0x00, 0x06, 0x00,
                                     0x04, 0x11, 0xec, 0x00, 0x1d}; // supported_groups {X25519MLKEM768, x25519}
        memcpy(out + p, sg, sizeof(sg));
        p += sizeof(sg);
    }
    else
    {
        static const uint8_t sg1[] = {0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d}; // supported_groups {x25519}
        memcpy(out + p, sg1, sizeof(sg1));
        p += sizeof(sg1);
    }
    {
        static const uint8_t sa[] = {0x00, 0x0d, 0x00, 0x04, 0x00, 0x02, 0x08, 0x07}; // sig_algs ed25519
        memcpy(out + p, sa, sizeof(sa));
        p += sizeof(sa);
    }
    { // key_share: one X25519MLKEM768 entry = ek(1184) || x25519(32)
        out[p++] = 0x00;
        out[p++] = 0x33;
        size_t el_at = p;
        p += 2; // ext_len
        size_t cs_at = p;
        p += 2; // client_shares length
        out[p++] = 0x11;
        out[p++] = 0xec; // group
        out[p++] = (uint8_t)((MLKEM768_EK_BYTES + 32) >> 8);
        out[p++] = (uint8_t)((MLKEM768_EK_BYTES + 32) & 0xff);
        memcpy(out + p, ek, MLKEM768_EK_BYTES);
        p += MLKEM768_EK_BYTES;
        memcpy(out + p, client_pub, 32);
        p += 32;
        uint16_t cs = (uint16_t)(p - cs_at - 2);
        out[cs_at] = (uint8_t)(cs >> 8);
        out[cs_at + 1] = (uint8_t)cs;
        uint16_t el = (uint16_t)(p - el_at - 2);
        out[el_at] = (uint8_t)(el >> 8);
        out[el_at + 1] = (uint8_t)el;
    }
    {
        static const uint8_t alpn[] = {0x00, 0x10, 0x00, 0x05, 0x00, 0x03, 0x02, 'h', '3'};
        memcpy(out + p, alpn, sizeof(alpn));
        p += sizeof(alpn);
    }
    {
        out[p++] = 0x00; // pc_quic_transport_parameters
        out[p++] = 0x39;
        out[p++] = (uint8_t)(tp_len >> 8);
        out[p++] = (uint8_t)tp_len;
        memcpy(out + p, tp, tp_len);
        p += tp_len;
    }
    uint16_t ext_len = (uint16_t)(p - ext_len_at - 2);
    out[ext_len_at] = (uint8_t)(ext_len >> 8);
    out[ext_len_at + 1] = (uint8_t)ext_len;
    uint32_t hs_len = (uint32_t)(p - hs_len_at - 3);
    out[hs_len_at] = (uint8_t)(hs_len >> 16);
    out[hs_len_at + 1] = (uint8_t)(hs_len >> 8);
    out[hs_len_at + 2] = (uint8_t)hs_len;
    return p;
}

// A ClientHello that offers X25519MLKEM768 in supported_groups but sends only a classical X25519
// key_share - the HelloRetryRequest trigger (the server should ask it to retry with the hybrid share).
static size_t build_client_hello_hybrid_classical(uint8_t *out, const uint8_t client_pub[32], const uint8_t *tp,
                                                  size_t tp_len)
{
    size_t p = 0;
    out[p++] = TLS_HS_CLIENT_HELLO;
    size_t hs_len_at = p;
    p += 3;
    out[p++] = 0x03;
    out[p++] = 0x03;
    for (int i = 0; i < 32; i++)
    {
        out[p++] = (uint8_t)i;
    }
    out[p++] = 0x00; // session id length 0
    out[p++] = 0x00; // cipher_suites
    out[p++] = 0x02;
    out[p++] = 0x13;
    out[p++] = 0x01;
    out[p++] = 0x01; // compression
    out[p++] = 0x00;
    size_t ext_len_at = p;
    p += 2;
    {
        static const uint8_t sv[] = {0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04}; // supported_versions
        memcpy(out + p, sv, sizeof(sv));
        p += sizeof(sv);
    }
    {
        static const uint8_t sg[] = {0x00, 0x0a, 0x00, 0x06, 0x00,
                                     0x04, 0x11, 0xec, 0x00, 0x1d}; // supported_groups {X25519MLKEM768, x25519}
        memcpy(out + p, sg, sizeof(sg));
        p += sizeof(sg);
    }
    {
        static const uint8_t sa[] = {0x00, 0x0d, 0x00, 0x04, 0x00, 0x02, 0x08, 0x07}; // sig_algs ed25519
        memcpy(out + p, sa, sizeof(sa));
        p += sizeof(sa);
    }
    { // key_share: a single CLASSICAL x25519 entry (group 0x001d, 32-byte pub) - no hybrid share
        static const uint8_t ks[] = {0x00, 0x33, 0x00, 0x26, 0x00, 0x24, 0x00, 0x1d, 0x00, 0x20};
        memcpy(out + p, ks, sizeof(ks));
        p += sizeof(ks);
        memcpy(out + p, client_pub, 32);
        p += 32;
    }
    {
        static const uint8_t alpn[] = {0x00, 0x10, 0x00, 0x05, 0x00, 0x03, 0x02, 'h', '3'};
        memcpy(out + p, alpn, sizeof(alpn));
        p += sizeof(alpn);
    }
    {
        out[p++] = 0x00; // pc_quic_transport_parameters
        out[p++] = 0x39;
        out[p++] = (uint8_t)(tp_len >> 8);
        out[p++] = (uint8_t)tp_len;
        memcpy(out + p, tp, tp_len);
        p += tp_len;
    }
    uint16_t ext_len = (uint16_t)(p - ext_len_at - 2);
    out[ext_len_at] = (uint8_t)(ext_len >> 8);
    out[ext_len_at + 1] = (uint8_t)ext_len;
    uint32_t hs_len = (uint32_t)(p - hs_len_at - 3);
    out[hs_len_at] = (uint8_t)(hs_len >> 16);
    out[hs_len_at + 1] = (uint8_t)(hs_len >> 8);
    out[hs_len_at + 2] = (uint8_t)hs_len;
    return p;
}

// HelloRetryRequest round trip (RFC 8446 §4.1.4 / §4.4.1): ClientHello1 offers X25519MLKEM768 but sends
// only a classical key_share, so the server answers with an HRR selecting X25519MLKEM768 (instead of
// downgrading to X25519); ClientHello2 carries the hybrid share and the handshake completes over the
// message_hash || HRR || ClientHello2 || ServerHello transcript. Verified as a conforming client - the
// server Finished MAC only checks out if the server restarted the transcript exactly right.
void test_hybrid_hrr_roundtrip()
{
    fill_test_material();
    QuicTlsConfig cfg;
    make_server_config(&cfg);
    for (int i = 0; i < 32; i++)
    {
        cfg.mlkem_m[i] = (uint8_t)(0x5a ^ i);
    }
    QuicTls qt;
    pc_quic_tls_server_init(&qt, &cfg);

    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    ctp.has_initial_scid = PROTO_TRUE;
    ctp.initial_scid_len = 4;
    memcpy(ctp.initial_scid, "\xaa\xbb\xcc\xdd", 4);
    uint8_t ctp_enc[128];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);

    // --- ClientHello1: hybrid group offered, classical share only -> HelloRetryRequest ---
    static uint8_t ch1[2048];
    size_t ch1_len = build_client_hello_hybrid_classical(ch1, client_pub, ctp_enc, ctp_len);
    size_t used = pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch1, ch1_len);
    TEST_ASSERT_EQUAL_UINT(ch1_len, used);
    TEST_ASSERT_EQUAL_UINT8(QTLS_START, qt.state); // awaiting the retry, not WAIT_FINISHED
    TEST_ASSERT_FALSE(qt.hs_keys_ready);
    TEST_ASSERT_TRUE(qt.hrr_sent);

    size_t hrr_len = 0;
    const uint8_t *hrr = pc_quic_tls_flight(&qt, QUIC_ENC_INITIAL, &hrr_len);
    TEST_ASSERT_TRUE(hrr_len > 0);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_SERVER_HELLO, hrr[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pc_tls13_hrr_random, hrr + 6, 32); // the HRR magic random
    static uint8_t hrr_saved[256];
    memcpy(hrr_saved, hrr, hrr_len);

    // --- ClientHello2: the hybrid ClientHello -> completes ---
    static uint8_t ch2[2048];
    size_t ch2_len = build_client_hello_hybrid(ch2, client_pub, ctp_enc, ctp_len, kat_ek, PROTO_TRUE);
    used = pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch2, ch2_len);
    TEST_ASSERT_EQUAL_UINT(ch2_len, used);
    TEST_ASSERT_EQUAL_UINT8(QTLS_WAIT_FINISHED, qt.state);

    size_t si_len = 0, sh_flight_len = 0;
    const uint8_t *si = pc_quic_tls_flight(&qt, QUIC_ENC_INITIAL, &si_len); // HRR || ServerHello
    const uint8_t *sh_flight = pc_quic_tls_flight(&qt, QUIC_ENC_HANDSHAKE, &sh_flight_len);
    TEST_ASSERT_TRUE(si_len > hrr_len);
    const uint8_t *sh = si + hrr_len; // the ServerHello is appended after the HRR
    size_t sh_len = si_len - hrr_len;
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_SERVER_HELLO, sh[0]);

    // Pull the hybrid server key_share out of the (appended) ServerHello.
    size_t o = 44;
    const uint8_t *server_ct = NULL;
    const uint8_t *server_x25519 = NULL;
    while (o + 4 <= sh_len)
    {
        uint16_t et = (uint16_t)((sh[o] << 8) | sh[o + 1]);
        uint16_t el = (uint16_t)((sh[o + 2] << 8) | sh[o + 3]);
        o += 4;
        if (et == 0x0033)
        {
            server_ct = sh + o + 4;
            server_x25519 = sh + o + 4 + MLKEM768_CT_BYTES;
        }
        o += el;
    }
    TEST_ASSERT_NOT_NULL(server_ct);

    uint8_t ml_ss[32];
    pc_mlkem768_decaps_ref(kat_dk, server_ct, ml_ss);
    uint8_t x_ss[32];
    pc_x25519(x_ss, CLIENT_PRIV, server_x25519);
    uint8_t ecdhe[64];
    memcpy(ecdhe, ml_ss, 32);
    memcpy(ecdhe + 32, x_ss, 32);

    // Client transcript: message_hash(Hash(CH1)) || HRR || CH2 || ServerHello || ...
    uint8_t ch1_hash[32];
    {
        pc_sha256_ctx h;
        pc_sha256_init(&h);
        pc_sha256_update(&h, ch1, ch1_len);
        pc_sha256_final(&h, ch1_hash);
    }
    uint8_t mh[40];
    size_t mhn = pc_tls13_build_message_hash(mh, sizeof(mh), ch1_hash);
    pc_sha256_ctx t;
    uint8_t ch_sh[32], ch_sf[32];
    pc_sha256_init(&t);
    pc_sha256_update(&t, mh, mhn);
    pc_sha256_update(&t, hrr_saved, hrr_len);
    pc_sha256_update(&t, ch2, ch2_len);
    pc_sha256_update(&t, sh, sh_len);
    {
        pc_sha256_ctx tmp = t;
        pc_sha256_final(&tmp, ch_sh); // H(..ServerHello)
    }
    pc_sha256_update(&t, sh_flight, sh_flight_len);
    pc_sha256_final(&t, ch_sf); // H(..server Finished)

    Tls13KeySchedule cks;
    pc_tls13_ks_early(&TLS13_KDF, &cks);
    pc_tls13_ks_handshake(&cks, ecdhe, ch_sh, 64);
    pc_tls13_ks_master(&cks, ch_sf);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(qt.ks.server_hs_traffic, cks.server_hs_traffic, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(qt.ks.client_hs_traffic, cks.client_hs_traffic, 32);

    // Server Finished verifies over the HRR transcript through CertificateVerify.
    uint8_t ch_cv[32];
    {
        pc_sha256_ctx tt;
        pc_sha256_init(&tt);
        pc_sha256_update(&tt, mh, mhn);
        pc_sha256_update(&tt, hrr_saved, hrr_len);
        pc_sha256_update(&tt, ch2, ch2_len);
        pc_sha256_update(&tt, sh, sh_len);
        pc_sha256_update(&tt, sh_flight, sh_flight_len - 36);
        pc_sha256_final(&tt, ch_cv);
    }
    uint8_t sfin_expected[32];
    pc_tls13_finished_mac(&TLS13_KDF, cks.server_hs_traffic, ch_cv, sfin_expected);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(sfin_expected, sh_flight + sh_flight_len - 32, 32);

    // The server accepts the client Finished -> DONE.
    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    pc_tls13_finished_mac(&TLS13_KDF, cks.client_hs_traffic, ch_sf, cfin + 4);
    used = pc_quic_tls_recv_crypto(&qt, QUIC_ENC_HANDSHAKE, cfin, sizeof(cfin));
    TEST_ASSERT_EQUAL_UINT(sizeof(cfin), used);
    TEST_ASSERT_EQUAL_UINT8(QTLS_DONE, qt.state);
}

// Full X25519MLKEM768 hybrid handshake, verified as a conforming client: decapsulate the server's
// ML-KEM ciphertext, redo X25519, form the 64-byte ML-KEM||X25519 secret, and confirm both sides derive
// identical handshake/application secrets and that the server's Finished + our Finished both verify.
void test_hybrid_handshake_roundtrip()
{
    fill_test_material();
    QuicTlsConfig cfg;
    make_server_config(&cfg);
    for (int i = 0; i < 32; i++)
    {
        cfg.mlkem_m[i] = (uint8_t)(0x5a ^ i); // fixed Encaps randomness for a repeatable test
    }
    QuicTls qt;
    pc_quic_tls_server_init(&qt, &cfg);

    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    ctp.has_initial_scid = PROTO_TRUE;
    ctp.initial_scid_len = 4;
    memcpy(ctp.initial_scid, "\xaa\xbb\xcc\xdd", 4);
    uint8_t ctp_enc[128];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));

    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    static uint8_t ch[2048];
    size_t ch_len = build_client_hello_hybrid(ch, client_pub, ctp_enc, ctp_len, kat_ek, PROTO_TRUE);

    size_t used = pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len);
    TEST_ASSERT_EQUAL_UINT(ch_len, used);
    TEST_ASSERT_EQUAL_UINT8(QTLS_WAIT_FINISHED, qt.state);

    size_t si_len = 0, sh_flight_len = 0;
    const uint8_t *si = pc_quic_tls_flight(&qt, QUIC_ENC_INITIAL, &si_len);
    const uint8_t *sh_flight = pc_quic_tls_flight(&qt, QUIC_ENC_HANDSHAKE, &sh_flight_len);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_SERVER_HELLO, si[0]);

    // Walk the ServerHello extensions (start = 4+2+32+1+2+1+2 = 44 with a 0-length session id) to the
    // key_share and confirm the hybrid group + 1120-byte share.
    size_t o = 44;
    const uint8_t *server_ct = NULL;
    const uint8_t *server_x25519 = NULL;
    while (o + 4 <= si_len)
    {
        uint16_t et = (uint16_t)((si[o] << 8) | si[o + 1]);
        uint16_t el = (uint16_t)((si[o + 2] << 8) | si[o + 3]);
        o += 4;
        if (et == 0x0033)
        {
            uint16_t g = (uint16_t)((si[o] << 8) | si[o + 1]);
            uint16_t sl = (uint16_t)((si[o + 2] << 8) | si[o + 3]);
            TEST_ASSERT_EQUAL_UINT16(TLS_GROUP_X25519MLKEM768, g);
            TEST_ASSERT_EQUAL_UINT16(MLKEM768_CT_BYTES + 32, sl);
            server_ct = si + o + 4;
            server_x25519 = si + o + 4 + MLKEM768_CT_BYTES;
        }
        o += el;
    }
    TEST_ASSERT_NOT_NULL(server_ct);

    // Client combiner: ML-KEM secret first, then X25519.
    uint8_t ml_ss[32];
    pc_mlkem768_decaps_ref(kat_dk, server_ct, ml_ss);
    uint8_t x_ss[32];
    pc_x25519(x_ss, CLIENT_PRIV, server_x25519);
    uint8_t ecdhe[64];
    memcpy(ecdhe, ml_ss, 32);
    memcpy(ecdhe + 32, x_ss, 32);

    pc_sha256_ctx t;
    uint8_t ch_sh[32], ch_sf[32];
    pc_sha256_init(&t);
    pc_sha256_update(&t, ch, ch_len);
    pc_sha256_update(&t, si, si_len);
    {
        pc_sha256_ctx tmp = t;
        pc_sha256_final(&tmp, ch_sh);
    }
    pc_sha256_update(&t, sh_flight, sh_flight_len);
    pc_sha256_final(&t, ch_sf);

    Tls13KeySchedule cks;
    pc_tls13_ks_early(&TLS13_KDF, &cks);
    pc_tls13_ks_handshake(&cks, ecdhe, ch_sh, 64); // 64-byte hybrid secret
    pc_tls13_ks_master(&cks, ch_sf);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(qt.ks.handshake_secret, cks.handshake_secret, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(qt.ks.client_hs_traffic, cks.client_hs_traffic, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(qt.ks.server_hs_traffic, cks.server_hs_traffic, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(qt.ks.server_ap_traffic, cks.server_ap_traffic, 32);

    // Server Finished verifies, then the server accepts the client Finished.
    uint8_t ch_cv[32];
    pc_sha256_init(&t);
    pc_sha256_update(&t, ch, ch_len);
    pc_sha256_update(&t, si, si_len);
    pc_sha256_update(&t, sh_flight, sh_flight_len - 36);
    pc_sha256_final(&t, ch_cv);
    uint8_t sfin_expected[32];
    pc_tls13_finished_mac(&TLS13_KDF, cks.server_hs_traffic, ch_cv, sfin_expected);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(sfin_expected, sh_flight + sh_flight_len - 32, 32);

    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    pc_tls13_finished_mac(&TLS13_KDF, cks.client_hs_traffic, ch_sf, cfin + 4);
    used = pc_quic_tls_recv_crypto(&qt, QUIC_ENC_HANDSHAKE, cfin, sizeof(cfin));
    TEST_ASSERT_EQUAL_UINT(sizeof(cfin), used);
    TEST_ASSERT_EQUAL_UINT8(QTLS_DONE, qt.state);
}
// A hybrid key_share offered WITHOUT X25519MLKEM768 in supported_groups is not usable: the server
// refuses to select a group the client did not advertise, and (having no classical share either)
// fails the handshake rather than silently using the share.
void test_hybrid_share_without_group_offer()
{
    fill_test_material();
    QuicTlsConfig cfg;
    make_server_config(&cfg);
    for (int i = 0; i < 32; i++)
    {
        cfg.mlkem_m[i] = (uint8_t)(0x5a ^ i);
    }
    QuicTls qt;
    pc_quic_tls_server_init(&qt, &cfg);

    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[128];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    static uint8_t ch[2048];
    size_t ch_len =
        build_client_hello_hybrid(ch, client_pub, ctp_enc, ctp_len, kat_ek, /*offer_hybrid_group=*/PROTO_FALSE);

    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len);
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    TEST_ASSERT_EQUAL_UINT8(40, qt.alert); // handshake_failure: no usable key_share
    TEST_ASSERT_FALSE(qt.hs_keys_ready);
}

// One HelloRetryRequest only: a retry that STILL sends no X25519MLKEM768 key_share is fatal, so a
// client cannot loop the server through the retry path (RFC 8446 sec 4.1.4).
void test_hybrid_hrr_retry_without_share_is_fatal()
{
    fill_test_material();
    QuicTlsConfig cfg;
    make_server_config(&cfg);
    for (int i = 0; i < 32; i++)
    {
        cfg.mlkem_m[i] = (uint8_t)(0x5a ^ i);
    }
    QuicTls qt;
    pc_quic_tls_server_init(&qt, &cfg);

    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[128];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    static uint8_t ch[2048];
    size_t ch_len = build_client_hello_hybrid_classical(ch, client_pub, ctp_enc, ctp_len);

    // ClientHello1: hybrid group offered, classical share only -> HelloRetryRequest.
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len);
    TEST_ASSERT_TRUE(qt.hrr_sent);
    TEST_ASSERT_EQUAL_UINT8(QTLS_START, qt.state);

    // ClientHello2 is identical - still no hybrid share. A second HRR is not sent; it is fatal.
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len);
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    TEST_ASSERT_EQUAL_UINT8(40, qt.alert); // handshake_failure
}

// A malformed ML-KEM-768 encapsulation key (a coefficient at or above q, which FIPS 203's modulus
// check rejects) fails the handshake instead of encapsulating against a bad key.
void test_hybrid_bad_mlkem_key_rejected()
{
    fill_test_material();
    QuicTlsConfig cfg;
    make_server_config(&cfg);
    for (int i = 0; i < 32; i++)
    {
        cfg.mlkem_m[i] = (uint8_t)(0x5a ^ i);
    }
    QuicTls qt;
    pc_quic_tls_server_init(&qt, &cfg);

    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[128];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);

    static uint8_t bad_ek[MLKEM768_EK_BYTES];
    memcpy(bad_ek, kat_ek, sizeof(bad_ek));
    bad_ek[0] = 0xFF; // first 12-bit coefficient becomes 0xFFF = 4095 >= q (3329)
    bad_ek[1] = 0xFF;

    static uint8_t ch[2048];
    size_t ch_len = build_client_hello_hybrid(ch, client_pub, ctp_enc, ctp_len, bad_ek, PROTO_TRUE);
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, ch_len);
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    TEST_ASSERT_EQUAL_UINT8(40, qt.alert); // handshake_failure
    TEST_ASSERT_FALSE(qt.hs_keys_ready);   // no keys were installed from a bad encapsulation
}
// The key_share walk skips entries it cannot use and keeps going: a group it does not implement, and
// an X25519MLKEM768 entry whose key_exchange is the wrong length, are both stepped over so a usable
// hybrid entry later in the same list is still found (RFC 8446 sec 4.2.8).
void test_hybrid_key_share_entry_skipping()
{
    fill_test_material();
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[128];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));

    // Start from the valid hybrid ClientHello, then rebuild its key_share with two unusable entries
    // in front of the good one.
    static uint8_t ch[4096];
    size_t p = 0;
    ch[p++] = TLS_HS_CLIENT_HELLO;
    size_t hs_len_at = p;
    p += 3;
    ch[p++] = 0x03;
    ch[p++] = 0x03;
    for (int i = 0; i < 32; i++)
    {
        ch[p++] = (uint8_t)i;
    }
    ch[p++] = 0x00; // session id
    ch[p++] = 0x00; // cipher suites
    ch[p++] = 0x02;
    ch[p++] = 0x13;
    ch[p++] = 0x01;
    ch[p++] = 0x01; // compression
    ch[p++] = 0x00;
    size_t ext_len_at = p;
    p += 2;
    {
        static const uint8_t sv[] = {0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04};
        memcpy(ch + p, sv, sizeof(sv));
        p += sizeof(sv);
        static const uint8_t sg[] = {0x00, 0x0a, 0x00, 0x06, 0x00, 0x04, 0x11, 0xec, 0x00, 0x1d};
        memcpy(ch + p, sg, sizeof(sg));
        p += sizeof(sg);
        static const uint8_t sa[] = {0x00, 0x0d, 0x00, 0x04, 0x00, 0x02, 0x08, 0x07};
        memcpy(ch + p, sa, sizeof(sa));
        p += sizeof(sa);
    }
    { // key_share: [ secp256r1(65B) , X25519MLKEM768 with a 100-byte share , valid X25519MLKEM768 ]
        ch[p++] = 0x00;
        ch[p++] = 0x33;
        size_t el_at = p;
        p += 2;
        size_t cs_at = p;
        p += 2;
        ch[p++] = 0x00; // group secp256r1 - not implemented here
        ch[p++] = 0x17;
        ch[p++] = 0x00;
        ch[p++] = 0x41; // 65 bytes
        memset(ch + p, 0x04, 65);
        p += 65;
        ch[p++] = 0x11; // group X25519MLKEM768, but the wrong key_exchange length
        ch[p++] = 0xec;
        ch[p++] = 0x00;
        ch[p++] = 0x64; // 100 bytes
        memset(ch + p, 0x5c, 100);
        p += 100;
        ch[p++] = 0x11; // group X25519MLKEM768, correct ek(1184) || x25519(32)
        ch[p++] = 0xec;
        ch[p++] = (uint8_t)((MLKEM768_EK_BYTES + 32) >> 8);
        ch[p++] = (uint8_t)((MLKEM768_EK_BYTES + 32) & 0xff);
        memcpy(ch + p, kat_ek, MLKEM768_EK_BYTES);
        p += MLKEM768_EK_BYTES;
        memcpy(ch + p, client_pub, 32);
        p += 32;
        uint16_t cs = (uint16_t)(p - cs_at - 2);
        ch[cs_at] = (uint8_t)(cs >> 8);
        ch[cs_at + 1] = (uint8_t)cs;
        uint16_t el = (uint16_t)(p - el_at - 2);
        ch[el_at] = (uint8_t)(el >> 8);
        ch[el_at + 1] = (uint8_t)el;
    }
    {
        static const uint8_t alpn[] = {0x00, 0x10, 0x00, 0x05, 0x00, 0x03, 0x02, 'h', '3'};
        memcpy(ch + p, alpn, sizeof(alpn));
        p += sizeof(alpn);
        ch[p++] = 0x00;
        ch[p++] = 0x39;
        ch[p++] = (uint8_t)(ctp_len >> 8);
        ch[p++] = (uint8_t)ctp_len;
        memcpy(ch + p, ctp_enc, ctp_len);
        p += ctp_len;
    }
    uint16_t ext_len = (uint16_t)(p - ext_len_at - 2);
    ch[ext_len_at] = (uint8_t)(ext_len >> 8);
    ch[ext_len_at + 1] = (uint8_t)ext_len;
    uint32_t hs_len = (uint32_t)(p - hs_len_at - 3);
    ch[hs_len_at] = (uint8_t)(hs_len >> 16);
    ch[hs_len_at + 1] = (uint8_t)(hs_len >> 8);
    ch[hs_len_at + 2] = (uint8_t)hs_len;

    Tls13ClientHello parsed;
    TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(ch, p, &parsed, /*dtls=*/PROTO_FALSE));
    TEST_ASSERT_TRUE(parsed.has_hybrid_share); // the third entry was picked up
    TEST_ASSERT_FALSE(parsed.has_key_share);   // neither unusable entry was mistaken for X25519
    TEST_ASSERT_TRUE(parsed.offers_x25519mlkem768);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kat_ek, parsed.client_mlkem_ek, MLKEM768_EK_BYTES);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(client_pub, parsed.client_x25519, 32);

    // And the server really can complete a handshake from it.
    QuicTlsConfig cfg;
    make_server_config(&cfg);
    for (int i = 0; i < 32; i++)
    {
        cfg.mlkem_m[i] = (uint8_t)(0x5a ^ i);
    }
    QuicTls qt;
    pc_quic_tls_server_init(&qt, &cfg);
    TEST_ASSERT_EQUAL_UINT(p, pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch, p));
    TEST_ASSERT_EQUAL_UINT8(QTLS_WAIT_FINISHED, qt.state);
}
#endif // PC_ENABLE_PQC_KEX

// process_message's level/state/type dispatch: a handshake message that matches neither arm is an
// unexpected_message, whichever of the three conditions is the one that does not hold.
void test_quic_tls_message_dispatch_guards()
{
    QuicTlsConfig cfg;
    QuicTls qt;

    // (a) A ClientHello at the Initial level once the handshake has moved past START.
    drive_to_wait_finished(&qt, &cfg);
    TEST_ASSERT_EQUAL_UINT8(QTLS_WAIT_FINISHED, qt.state);
    uint8_t ch2[36] = {TLS_HS_CLIENT_HELLO, 0x00, 0x00, 0x20};
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, ch2, sizeof(ch2));
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    TEST_ASSERT_EQUAL_UINT8(10, qt.alert); // unexpected_message

    // (b) A Finished at the Initial level on a fresh server: the right level and state, wrong type.
    fill_test_material();
    make_server_config(&cfg);
    uint8_t fin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    pc_quic_tls_server_init(&qt, &cfg);
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_INITIAL, fin, sizeof(fin));
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    TEST_ASSERT_EQUAL_UINT8(10, qt.alert);

    // (c) A Finished at the Handshake level before the ServerHello: the right level and type, but
    // the state is still START.
    pc_quic_tls_server_init(&qt, &cfg);
    pc_quic_tls_recv_crypto(&qt, QUIC_ENC_HANDSHAKE, fin, sizeof(fin));
    TEST_ASSERT_EQUAL_UINT8(QTLS_FAILED, qt.state);
    TEST_ASSERT_EQUAL_UINT8(10, qt.alert);

    // (d) The peer's transport parameters are unavailable until a ClientHello has been parsed.
    pc_quic_tls_server_init(&qt, &cfg);
    TEST_ASSERT_NULL(pc_quic_tls_peer_params(&qt));
    drive_to_wait_finished(&qt, &cfg);
    TEST_ASSERT_NOT_NULL(pc_quic_tls_peer_params(&qt));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_quic_tls_message_dispatch_guards);
    RUN_TEST(test_full_handshake_roundtrip);
    RUN_TEST(test_reject_bad_client_finished);
    RUN_TEST(test_reject_no_h3_alpn);
    RUN_TEST(test_partial_client_hello);
    RUN_TEST(test_reject_no_tls13);
    RUN_TEST(test_reject_no_key_share);
    RUN_TEST(test_reject_no_x25519_group);
    RUN_TEST(test_reject_no_ed25519);
    RUN_TEST(test_reject_no_transport_params);
    RUN_TEST(test_reject_bad_transport_params);
    RUN_TEST(test_reject_malformed_client_hello);
    RUN_TEST(test_quic_tls_more_guards);
    RUN_TEST(test_quic_tls_cert_size_boundary_emit_fails);
#if PC_ENABLE_PQC_KEX
    RUN_TEST(test_hybrid_handshake_roundtrip);
    RUN_TEST(test_hybrid_hrr_roundtrip);
    RUN_TEST(test_hybrid_share_without_group_offer);
    RUN_TEST(test_hybrid_hrr_retry_without_share_is_fatal);
    RUN_TEST(test_hybrid_bad_mlkem_key_rejected);
    RUN_TEST(test_hybrid_key_share_entry_skipping);
#endif
    return UNITY_END();
}
