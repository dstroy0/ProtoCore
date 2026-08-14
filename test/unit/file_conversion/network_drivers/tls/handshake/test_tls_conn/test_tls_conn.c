// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ed25519.h"
#include "crypto/hash/sha256.h"
#include "crypto/hash/sha512.h"
#include "network_drivers/tls/handshake/handshake.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include "network_drivers/tls/record/record.h"
#include <string.h>

#include <unity.h>

static const uint8_t CLIENT_X25519_PRIV[32] = {0x49, 0xaf, 0x42, 0xba, 0x7f, 0x79, 0x94, 0x85, 0x2d, 0x71, 0x3e,
                                               0xf2, 0x78, 0x4b, 0xcb, 0xca, 0xa7, 0x91, 0x1d, 0xe2, 0x6a, 0xdc,
                                               0x56, 0x42, 0xcb, 0x63, 0x45, 0x40, 0xe7, 0xea, 0x50, 0x05};
static const uint8_t CLIENT_X25519_PUB[32] = {0x99, 0x38, 0x1d, 0xe5, 0x60, 0xe4, 0xbd, 0x43, 0xd2, 0x3d, 0x8e,
                                              0x43, 0x5a, 0x7d, 0xba, 0xfe, 0xb3, 0xc0, 0x6e, 0x51, 0xc1, 0x3c,
                                              0xae, 0x4d, 0x54, 0x13, 0x69, 0x1e, 0x52, 0x9a, 0xaf, 0x2c};

static const uint8_t SERVER_X25519_PRIV[32] = {0xb1, 0x58, 0x0e, 0xea, 0xdf, 0x6d, 0xd5, 0x89, 0xb8, 0xef, 0x4f,
                                               0x2d, 0x56, 0x52, 0x57, 0x8c, 0xc8, 0x10, 0xe9, 0x98, 0x01, 0x91,
                                               0xec, 0x8d, 0x05, 0x83, 0x08, 0xce, 0xa2, 0x16, 0xa2, 0x1e};

static const uint8_t SERVER_ED_SEED[32] = {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a,
                                           0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32,
                                           0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
static const uint8_t SERVER_ED_PUB[32] = {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
                                          0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
                                          0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
static const uint8_t SERVER_RANDOM[32] = {0xa6, 0xaf, 0x06, 0xa4, 0x12, 0x18, 0x60, 0xdc, 0x5e, 0x6e, 0x60,
                                          0x24, 0x9c, 0xd3, 0x4c, 0x95, 0x93, 0x0c, 0x8a, 0xc5, 0xcb, 0x14,
                                          0x34, 0xda, 0xc1, 0x55, 0x77, 0x2e, 0xd3, 0xe2, 0x69, 0x28};

static TlsConn g_conn;
static TlsConnConfig g_cfg;
static uint8_t g_srv_out[2048];

static uint8_t g_cli_ks_bytes[PROTOCORE_TLS13_KS_BORROW] __attribute__((aligned(8)));
static uint8_t g_cli_hash_work[PROTOCORE_SHA256_BORROW] __attribute__((aligned(4)));
static uint8_t g_sign_work[PROTOCORE_SHA512_BORROW] __attribute__((aligned(8)));
static Tls13KeySchedule g_cli_ks;
static protocore_sha256_ctx g_cli_transcript;
static TlsRecordKeys g_cli_hs_rx;
static TlsRecordKeys g_cli_hs_tx;
static TlsRecordKeys g_cli_ap_rx;
static TlsRecordKeys g_cli_ap_tx;

void setUp(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    memset(g_cli_ks_bytes, 0, sizeof(g_cli_ks_bytes));
    memset(&g_cli_ks, 0, sizeof(g_cli_ks));
    memset(&g_cli_hs_rx, 0, sizeof(g_cli_hs_rx));
    memset(&g_cli_hs_tx, 0, sizeof(g_cli_hs_tx));
    memset(&g_cli_ap_rx, 0, sizeof(g_cli_ap_rx));
    memset(&g_cli_ap_tx, 0, sizeof(g_cli_ap_tx));

    g_cfg.ed25519_seed = SERVER_ED_SEED;
    g_cfg.ed25519_pub = SERVER_ED_PUB;
    g_cfg.ephemeral_priv = SERVER_X25519_PRIV;
    g_cfg.random = SERVER_RANDOM;
}
void tearDown(void)
{
}

static size_t build_client_hello(uint8_t *out, proto_bool with_x25519_group, proto_bool with_share,
                                 proto_bool with_ed25519, uint16_t suite)
{
    size_t i = 0;
    out[i++] = TLS_HS_CLIENT_HELLO;
    size_t body_at = i;
    i += 3;

    out[i++] = 0x03;
    out[i++] = 0x03;
    for (size_t k = 0; k < 32; k++)
    {
        out[i++] = (uint8_t)(0xcb + k);
    }
    out[i++] = 0;

    out[i++] = 0x00;
    out[i++] = 0x02;
    out[i++] = (uint8_t)(suite >> 8);
    out[i++] = (uint8_t)suite;

    out[i++] = 0x01;
    out[i++] = 0x00;

    size_t ext_at = i;
    i += 2;

    out[i++] = 0x00;
    out[i++] = 0x2b;
    out[i++] = 0x00;
    out[i++] = 0x03;
    out[i++] = 0x02;
    out[i++] = 0x03;
    out[i++] = 0x04;

    if (with_x25519_group)
    {

        out[i++] = 0x00;
        out[i++] = 0x0a;
        out[i++] = 0x00;
        out[i++] = 0x04;
        out[i++] = 0x00;
        out[i++] = 0x02;
        out[i++] = 0x00;
        out[i++] = 0x1d;
    }
    if (with_ed25519)
    {

        out[i++] = 0x00;
        out[i++] = 0x0d;
        out[i++] = 0x00;
        out[i++] = 0x04;
        out[i++] = 0x00;
        out[i++] = 0x02;
        out[i++] = 0x08;
        out[i++] = 0x07;
    }
    if (with_share)
    {

        out[i++] = 0x00;
        out[i++] = 0x33;
        out[i++] = 0x00;
        out[i++] = 0x26;
        out[i++] = 0x00;
        out[i++] = 0x24;
        out[i++] = 0x00;
        out[i++] = 0x1d;
        out[i++] = 0x00;
        out[i++] = 0x20;
        memcpy(out + i, CLIENT_X25519_PUB, 32);
        i += 32;
    }

    const size_t ext_len = i - ext_at - 2;
    out[ext_at] = (uint8_t)(ext_len >> 8);
    out[ext_at + 1] = (uint8_t)ext_len;
    const size_t body_len = i - body_at - 3;
    out[body_at] = (uint8_t)(body_len >> 16);
    out[body_at + 1] = (uint8_t)(body_len >> 8);
    out[body_at + 2] = (uint8_t)body_len;
    return i;
}

static void init_server(void)
{
    TlsConnection.conn = &g_conn;
    TlsConnection.init_args.role = TLS_ROLE_SERVER;
    TlsConnection.init_args.cfg = &g_cfg;
    TlsConnection.init(TlsConnection.internal);
    TEST_ASSERT_TRUE(TlsConnection.ok);
}

static int feed(const uint8_t *rec, size_t rec_len)
{
    memcpy(g_conn.rx, rec, rec_len);
    TlsConnection.conn = &g_conn;
    TlsConnection.io.rx_len = rec_len;
    TlsConnection.out_args.out = g_srv_out;
    TlsConnection.out_args.out_cap = sizeof(g_srv_out);
    TlsConnection.process(TlsConnection.internal);
    return TlsConnection.i32;
}

static void cli_expand(const uint8_t *secret, const char *label, uint8_t *out, size_t out_len, uint8_t *work)
{
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.derive_args.work = work;
    Tls13Ks.derive_args.secret = secret;
    Tls13Ks.derive_args.label = label;
    Tls13Ks.derive_args.out = out;
    Tls13Ks.derive_args.out_len = out_len;
    Tls13Ks.expand_label(Tls13Ks.internal);
}

static void cli_keys(TlsRecordKeys *keys, const uint8_t *secret)
{
    TlsRecord.key.keys = keys;
    TlsRecord.key.cipher = TLS_CIPHER_AES_128_GCM_SHA256;
    TlsRecord.key.secret = secret;
    TlsRecord.keys_derive(TlsRecord.internal);
}

void test_full_handshake(void)
{
    uint8_t ch[512];
    uint8_t rec[1024];
    uint8_t pt[2048];

    uint8_t pub_check[32];
    protocore_x25519_base(pub_check, CLIENT_X25519_PRIV);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLIENT_X25519_PUB, pub_check, 32);
    protocore_ed25519_pubkey(g_sign_work, pub_check, SERVER_ED_SEED);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SERVER_ED_PUB, pub_check, 32);

    init_server();
    protocore_sha256_init(&g_cli_transcript, g_cli_hash_work);

    size_t ch_len = build_client_hello(ch, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE,
                                       PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256);
    protocore_sha256_update(&g_cli_transcript, ch, ch_len);

    TlsRecord.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecord.plain.fragment = ch;
    TlsRecord.plain.frag_len = ch_len;
    TlsRecord.out_args.out = rec;
    TlsRecord.out_args.out_cap = sizeof(rec);
    TlsRecord.plaintext_build(TlsRecord.internal);
    size_t rec_len = TlsRecord.n;
    TEST_ASSERT_TRUE(rec_len > 0);

    int wrote = feed(rec, rec_len);
    TEST_ASSERT_TRUE_MESSAGE(wrote > 0, "the server owed a flight");

    TlsPlaintext view;
    TlsRecord.sealed.rec = g_srv_out;
    TlsRecord.sealed.rec_len = (size_t)wrote;
    TlsRecord.plain.view = &view;
    TlsRecord.plaintext_parse(TlsRecord.internal);
    size_t off = TlsRecord.n;
    TEST_ASSERT_TRUE(off > 0);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_TLS_CT_HANDSHAKE, view.content_type);
    TEST_ASSERT_EQUAL_HEX8(TLS_HS_SERVER_HELLO, view.fragment[0]);
    protocore_sha256_update(&g_cli_transcript, view.fragment, view.frag_len);

    const uint8_t *sh = view.fragment;
    TEST_ASSERT_EQUAL_HEX16(0x0303, (uint16_t)((sh[4] << 8) | sh[5]));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SERVER_RANDOM, sh + 6, 32);
    TEST_ASSERT_EQUAL_UINT8(0u, sh[38]);
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, (uint16_t)((sh[39] << 8) | sh[40]));
    TEST_ASSERT_EQUAL_UINT8(0u, sh[41]);
    TEST_ASSERT_EQUAL_HEX16(0x0033, (uint16_t)((sh[44] << 8) | sh[45]));
    TEST_ASSERT_EQUAL_HEX16(TLS_GROUP_X25519, (uint16_t)((sh[48] << 8) | sh[49]));
    TEST_ASSERT_EQUAL_HEX16(32, (uint16_t)((sh[50] << 8) | sh[51]));
    const uint8_t *server_share = sh + 52;

    uint8_t want_share[32];
    protocore_x25519_base(want_share, SERVER_X25519_PRIV);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_share, server_share, 32);

    uint8_t ecdhe[32];
    protocore_x25519(ecdhe, CLIENT_X25519_PRIV, server_share);

    uint8_t ch_sh_hash[32];
    protocore_sha256_final(&g_cli_transcript, ch_sh_hash);

    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &g_cli_ks;
    Tls13Ks.bind.s = g_cli_ks_bytes;
    Tls13Ks.early(Tls13Ks.internal);
    TEST_ASSERT_TRUE(Tls13Ks.ok);
    Tls13Ks.bind.ks = &g_cli_ks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = sizeof(ecdhe);
    Tls13Ks.step.ch_sh_hash = ch_sh_hash;
    Tls13Ks.handshake(Tls13Ks.internal);

    cli_keys(&g_cli_hs_rx, g_cli_ks.s + TLS13_KS_SERVER_HS);
    cli_keys(&g_cli_hs_tx, g_cli_ks.s + TLS13_KS_CLIENT_HS);

    uint8_t cert_verify_hash[32];
    uint8_t peer_pub[32];
    proto_bool have_pub = PROTO_FALSE;
    uint8_t server_finished[32];
    proto_bool have_finished = PROTO_FALSE;
    uint8_t cv_sig[PROTOCORE_ED25519_SIG_LEN];
    proto_bool have_cv = PROTO_FALSE;
    int seen = 0;

    while (off < (size_t)wrote)
    {
        TlsCiphertext info;
        size_t body = ((size_t)g_srv_out[off + 3] << 8) | g_srv_out[off + 4];
        size_t whole = PROTOCORE_TLS_PLAINTEXT_HDR_LEN + body;
        TEST_ASSERT_TRUE(off + whole <= (size_t)wrote);
        TEST_ASSERT_EQUAL_HEX8(PROTOCORE_TLS_CT_APPLICATION_DATA, g_srv_out[off]);

        TlsRecord.key.keys = &g_cli_hs_rx;
        TlsRecord.sealed.rec = g_srv_out + off;
        TlsRecord.sealed.rec_len = whole;
        TlsRecord.sealed.info = &info;
        TlsRecord.out_args.out = pt;
        TlsRecord.out_args.out_cap = sizeof(pt);
        TlsRecord.unprotect(TlsRecord.internal);
        TEST_ASSERT_TRUE_MESSAGE(TlsRecord.ok, "the client could not open a server handshake record");
        TEST_ASSERT_EQUAL_UINT8(PROTOCORE_TLS_CT_HANDSHAKE, info.content_type);
        off += whole;
        seen++;

        const uint8_t *msg = pt;
        size_t body_len = ((size_t)msg[1] << 16) | ((size_t)msg[2] << 8) | msg[3];
        TEST_ASSERT_EQUAL_UINT(info.pt_len, 4u + body_len);

        if (msg[0] == TLS_HS_CERTIFICATE)
        {

            TEST_ASSERT_EQUAL_UINT8(0u, msg[4]);
            size_t entry_len = ((size_t)msg[8] << 16) | ((size_t)msg[9] << 8) | msg[10];
            TEST_ASSERT_EQUAL_UINT((size_t)PROTOCORE_TLS13_ED25519_SPKI_LEN, entry_len);
            static const uint8_t SPKI_PREFIX[12] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03,
                                                    0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};
            TEST_ASSERT_EQUAL_UINT8_ARRAY(SPKI_PREFIX, msg + 11, sizeof(SPKI_PREFIX));
            memcpy(peer_pub, msg + 11 + sizeof(SPKI_PREFIX), 32);
            have_pub = PROTO_TRUE;
        }
        if (msg[0] == TLS_HS_CERTIFICATE_VERIFY)
        {

            protocore_sha256_final(&g_cli_transcript, cert_verify_hash);
            TEST_ASSERT_EQUAL_HEX16(TLS_SIG_ED25519, (uint16_t)((msg[4] << 8) | msg[5]));
            TEST_ASSERT_EQUAL_HEX16(PROTOCORE_ED25519_SIG_LEN, (uint16_t)((msg[6] << 8) | msg[7]));
            memcpy(cv_sig, msg + 8, PROTOCORE_ED25519_SIG_LEN);
            have_cv = PROTO_TRUE;
        }
        if (msg[0] == TLS_HS_FINISHED)
        {

            uint8_t hash[32];
            protocore_sha256_final(&g_cli_transcript, hash);
            uint8_t expect[32];
            Tls13Ks.bind.ks = &g_cli_ks;
            Tls13Ks.finished_args.base_secret = g_cli_ks.s + TLS13_KS_SERVER_HS;
            Tls13Ks.finished_args.transcript_hash = hash;
            Tls13Ks.finished_args.out = expect;
            Tls13Ks.finished_mac(Tls13Ks.internal);
            TEST_ASSERT_EQUAL_UINT(32u, body_len);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, msg + 4, 32);
            memcpy(server_finished, msg + 4, 32);
            have_finished = PROTO_TRUE;
        }
        protocore_sha256_update(&g_cli_transcript, msg, info.pt_len);
    }

    TEST_ASSERT_EQUAL_INT(4, seen);
    TEST_ASSERT_TRUE(have_pub);
    TEST_ASSERT_TRUE(have_cv);
    TEST_ASSERT_TRUE(have_finished);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SERVER_ED_PUB, peer_pub, 32);

    uint8_t content[160];
    size_t clen = protocore_tls13_cert_verify_content(content, sizeof(content), cert_verify_hash, PROTO_TRUE);
    TEST_ASSERT_TRUE(clen > 0);
    TEST_ASSERT_TRUE_MESSAGE(protocore_ed25519_verify(g_sign_work, peer_pub, content, clen, cv_sig),
                             "the server's CertificateVerify did not verify under the key it presented");

    uint8_t ch_sfin_hash[32];
    protocore_sha256_final(&g_cli_transcript, ch_sfin_hash);
    Tls13Ks.bind.ks = &g_cli_ks;
    Tls13Ks.step.ch_sfin_hash = ch_sfin_hash;
    Tls13Ks.master(Tls13Ks.internal);
    cli_keys(&g_cli_ap_rx, g_cli_ks.s + TLS13_KS_SERVER_AP);
    cli_keys(&g_cli_ap_tx, g_cli_ks.s + TLS13_KS_CLIENT_AP);

    uint8_t verify[32];
    Tls13Ks.bind.ks = &g_cli_ks;
    Tls13Ks.finished_args.base_secret = g_cli_ks.s + TLS13_KS_CLIENT_HS;
    Tls13Ks.finished_args.transcript_hash = ch_sfin_hash;
    Tls13Ks.finished_args.out = verify;
    Tls13Ks.finished_mac(Tls13Ks.internal);

    uint8_t fin_msg[4 + 32];
    fin_msg[0] = TLS_HS_FINISHED;
    fin_msg[1] = 0;
    fin_msg[2] = 0;
    fin_msg[3] = 32;
    memcpy(fin_msg + 4, verify, 32);

    TlsRecord.key.keys = &g_cli_hs_tx;
    TlsRecord.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecord.sealed.pt = fin_msg;
    TlsRecord.sealed.pt_len = sizeof(fin_msg);
    TlsRecord.out_args.out = rec;
    TlsRecord.out_args.out_cap = sizeof(rec);
    TlsRecord.protect(TlsRecord.internal);
    rec_len = TlsRecord.n;
    TEST_ASSERT_TRUE(rec_len > 0);

    TEST_ASSERT_EQUAL_INT(0, feed(rec, rec_len));
    TlsConnection.conn = &g_conn;
    TlsConnection.established(TlsConnection.internal);
    TEST_ASSERT_TRUE_MESSAGE(TlsConnection.ok, "the handshake did not complete");
    TlsConnection.conn = &g_conn;
    TlsConnection.alert(TlsConnection.internal);
    TEST_ASSERT_EQUAL_UINT8(0u, TlsConnection.u8);

    static const uint8_t SERVER_MSG[13] = "hello, client";
    TlsConnection.conn = &g_conn;
    TlsConnection.io.data = SERVER_MSG;
    TlsConnection.io.len = sizeof(SERVER_MSG);
    TlsConnection.out_args.out = g_srv_out;
    TlsConnection.out_args.out_cap = sizeof(g_srv_out);
    TlsConnection.seal_app(TlsConnection.internal);
    size_t app_len = TlsConnection.n;
    TEST_ASSERT_TRUE(app_len > 0);

    TlsCiphertext info;
    TlsRecord.key.keys = &g_cli_ap_rx;
    TlsRecord.sealed.rec = g_srv_out;
    TlsRecord.sealed.rec_len = app_len;
    TlsRecord.sealed.info = &info;
    TlsRecord.out_args.out = pt;
    TlsRecord.out_args.out_cap = sizeof(pt);
    TlsRecord.unprotect(TlsRecord.internal);
    TEST_ASSERT_TRUE_MESSAGE(TlsRecord.ok, "the client could not open the server's application record");
    TEST_ASSERT_EQUAL_UINT(sizeof(SERVER_MSG), info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SERVER_MSG, pt, sizeof(SERVER_MSG));

    static const uint8_t CLIENT_MSG[13] = "hello, server";
    TlsRecord.key.keys = &g_cli_ap_tx;
    TlsRecord.content_type = PROTOCORE_TLS_CT_APPLICATION_DATA;
    TlsRecord.sealed.pt = CLIENT_MSG;
    TlsRecord.sealed.pt_len = sizeof(CLIENT_MSG);
    TlsRecord.out_args.out = rec;
    TlsRecord.out_args.out_cap = sizeof(rec);
    TlsRecord.protect(TlsRecord.internal);
    rec_len = TlsRecord.n;
    TEST_ASSERT_TRUE(rec_len > 0);

    size_t got = 0;
    TlsConnection.conn = &g_conn;
    TlsConnection.io.rec = rec;
    TlsConnection.io.rec_len = rec_len;
    TlsConnection.out_args.out = pt;
    TlsConnection.out_args.out_cap = sizeof(pt);
    TlsConnection.out_args.out_len = &got;
    TlsConnection.open_app(TlsConnection.internal);
    TEST_ASSERT_TRUE_MESSAGE(TlsConnection.ok, "the server could not open the client's application record");
    TEST_ASSERT_EQUAL_UINT(sizeof(CLIENT_MSG), got);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLIENT_MSG, pt, sizeof(CLIENT_MSG));
}

void test_a_client_outside_the_profile_is_refused(void)
{
    uint8_t ch[512];
    uint8_t rec[1024];

    struct
    {
        proto_bool group;
        proto_bool share;
        proto_bool ed;
        uint16_t suite;
        const char *why;
    } static const CASES[] = {
        {PROTO_FALSE, PROTO_TRUE, PROTO_TRUE, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, "no x25519 in supported_groups"},
        {PROTO_TRUE, PROTO_FALSE, PROTO_TRUE, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, "no key_share"},
        {PROTO_TRUE, PROTO_TRUE, PROTO_FALSE, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, "no ed25519"},
        {PROTO_TRUE, PROTO_TRUE, PROTO_TRUE, 0x1302, "a suite this stack does not implement"},
    };

    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        setUp();
        init_server();
        size_t ch_len = build_client_hello(ch, CASES[i].group, CASES[i].share, CASES[i].ed, CASES[i].suite);
        TlsRecord.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
        TlsRecord.plain.fragment = ch;
        TlsRecord.plain.frag_len = ch_len;
        TlsRecord.out_args.out = rec;
        TlsRecord.out_args.out_cap = sizeof(rec);
        TlsRecord.plaintext_build(TlsRecord.internal);

        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, feed(rec, TlsRecord.n), CASES[i].why);
        TlsConnection.conn = &g_conn;
        TlsConnection.alert(TlsConnection.internal);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(40, TlsConnection.u8, CASES[i].why);
        TlsConnection.conn = &g_conn;
        TlsConnection.established(TlsConnection.internal);
        TEST_ASSERT_FALSE(TlsConnection.ok);
    }
}

void test_malformed_and_out_of_order_messages_alert(void)
{
    uint8_t ch[512];
    uint8_t rec[1024];

    init_server();
    size_t ch_len = build_client_hello(ch, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE,
                                       PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256);
    ch[3] = (uint8_t)(ch[3] + 4);
    TlsRecord.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecord.plain.fragment = ch;
    TlsRecord.plain.frag_len = ch_len;
    TlsRecord.out_args.out = rec;
    TlsRecord.out_args.out_cap = sizeof(rec);
    TlsRecord.plaintext_build(TlsRecord.internal);
    TEST_ASSERT_EQUAL_INT(-1, feed(rec, TlsRecord.n));
    TlsConnection.conn = &g_conn;
    TlsConnection.alert(TlsConnection.internal);
    TEST_ASSERT_EQUAL_UINT8(47, TlsConnection.u8);

    setUp();
    init_server();
    uint8_t fin[4 + 32];
    memset(fin, 0, sizeof(fin));
    fin[0] = TLS_HS_FINISHED;
    fin[3] = 32;
    TlsRecord.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecord.plain.fragment = fin;
    TlsRecord.plain.frag_len = sizeof(fin);
    TlsRecord.out_args.out = rec;
    TlsRecord.out_args.out_cap = sizeof(rec);
    TlsRecord.plaintext_build(TlsRecord.internal);
    TEST_ASSERT_EQUAL_INT(-1, feed(rec, TlsRecord.n));
    TlsConnection.conn = &g_conn;
    TlsConnection.alert(TlsConnection.internal);
    TEST_ASSERT_EQUAL_UINT8(47, TlsConnection.u8);

    setUp();
    init_server();
    static const uint8_t GARBAGE[3] = {0x16, 0x03, 0x03};
    TEST_ASSERT_EQUAL_INT(-1, feed(GARBAGE, sizeof(GARBAGE)));
    TlsConnection.conn = &g_conn;
    TlsConnection.alert(TlsConnection.internal);
    TEST_ASSERT_EQUAL_UINT8(50, TlsConnection.u8);
}

void test_a_wrong_client_finished_is_decrypt_error(void)
{
    uint8_t ch[512];
    uint8_t rec[1024];
    uint8_t pt[2048];

    init_server();
    protocore_sha256_init(&g_cli_transcript, g_cli_hash_work);
    size_t ch_len = build_client_hello(ch, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE,
                                       PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256);
    protocore_sha256_update(&g_cli_transcript, ch, ch_len);
    TlsRecord.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecord.plain.fragment = ch;
    TlsRecord.plain.frag_len = ch_len;
    TlsRecord.out_args.out = rec;
    TlsRecord.out_args.out_cap = sizeof(rec);
    TlsRecord.plaintext_build(TlsRecord.internal);
    int wrote = feed(rec, TlsRecord.n);
    TEST_ASSERT_TRUE(wrote > 0);

    TlsPlaintext view;
    TlsRecord.sealed.rec = g_srv_out;
    TlsRecord.sealed.rec_len = (size_t)wrote;
    TlsRecord.plain.view = &view;
    TlsRecord.plaintext_parse(TlsRecord.internal);
    protocore_sha256_update(&g_cli_transcript, view.fragment, view.frag_len);
    const uint8_t *server_share = view.fragment + 52;

    uint8_t ecdhe[32];
    uint8_t ch_sh_hash[32];
    protocore_x25519(ecdhe, CLIENT_X25519_PRIV, server_share);
    protocore_sha256_final(&g_cli_transcript, ch_sh_hash);
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &g_cli_ks;
    Tls13Ks.bind.s = g_cli_ks_bytes;
    Tls13Ks.early(Tls13Ks.internal);
    Tls13Ks.bind.ks = &g_cli_ks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = sizeof(ecdhe);
    Tls13Ks.step.ch_sh_hash = ch_sh_hash;
    Tls13Ks.handshake(Tls13Ks.internal);
    cli_keys(&g_cli_hs_tx, g_cli_ks.s + TLS13_KS_CLIENT_HS);

    uint8_t fin_msg[4 + 32];
    memset(fin_msg, 0xA5, sizeof(fin_msg));
    fin_msg[0] = TLS_HS_FINISHED;
    fin_msg[1] = 0;
    fin_msg[2] = 0;
    fin_msg[3] = 32;

    TlsRecord.key.keys = &g_cli_hs_tx;
    TlsRecord.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecord.sealed.pt = fin_msg;
    TlsRecord.sealed.pt_len = sizeof(fin_msg);
    TlsRecord.out_args.out = rec;
    TlsRecord.out_args.out_cap = sizeof(rec);
    TlsRecord.protect(TlsRecord.internal);

    TEST_ASSERT_EQUAL_INT(-1, feed(rec, TlsRecord.n));
    TlsConnection.conn = &g_conn;
    TlsConnection.alert(TlsConnection.internal);
    TEST_ASSERT_EQUAL_UINT8(51, TlsConnection.u8);
    TlsConnection.conn = &g_conn;
    TlsConnection.established(TlsConnection.internal);
    TEST_ASSERT_FALSE(TlsConnection.ok);
    (void)pt;
}

void test_application_data_needs_the_handshake(void)
{
    uint8_t buf[256];
    size_t got = 0;
    static const uint8_t MSG[4] = {1, 2, 3, 4};

    init_server();
    TlsConnection.conn = &g_conn;
    TlsConnection.io.data = MSG;
    TlsConnection.io.len = sizeof(MSG);
    TlsConnection.out_args.out = buf;
    TlsConnection.out_args.out_cap = sizeof(buf);
    TlsConnection.seal_app(TlsConnection.internal);
    TEST_ASSERT_EQUAL_UINT(0u, TlsConnection.n);

    TlsConnection.conn = &g_conn;
    TlsConnection.io.rec = buf;
    TlsConnection.io.rec_len = sizeof(buf);
    TlsConnection.out_args.out = buf;
    TlsConnection.out_args.out_cap = sizeof(buf);
    TlsConnection.out_args.out_len = &got;
    TlsConnection.open_app(TlsConnection.internal);
    TEST_ASSERT_FALSE(TlsConnection.ok);

    TlsConnection.conn = &g_conn;
    TlsConnection.established(TlsConnection.internal);
    TEST_ASSERT_FALSE(TlsConnection.ok);

    static const uint8_t GARBAGE[3] = {0x16, 0x03, 0x03};
    TEST_ASSERT_EQUAL_INT(-1, feed(GARBAGE, sizeof(GARBAGE)));
    TEST_ASSERT_EQUAL_INT(-1, feed(GARBAGE, sizeof(GARBAGE)));
}

void test_the_client_role_refuses(void)
{
    uint8_t buf[256];
    TlsConnection.conn = &g_conn;
    TlsConnection.init_args.role = TLS_ROLE_CLIENT;
    TlsConnection.init_args.cfg = &g_cfg;
    TlsConnection.init(TlsConnection.internal);
    TEST_ASSERT_TRUE(TlsConnection.ok);

    TlsConnection.conn = &g_conn;
    TlsConnection.out_args.out = buf;
    TlsConnection.out_args.out_cap = sizeof(buf);
    TlsConnection.start(TlsConnection.internal);
    TEST_ASSERT_EQUAL_UINT(0u, TlsConnection.n);
    TlsConnection.conn = &g_conn;
    TlsConnection.alert(TlsConnection.internal);
    TEST_ASSERT_EQUAL_UINT8(80, TlsConnection.u8);
}
