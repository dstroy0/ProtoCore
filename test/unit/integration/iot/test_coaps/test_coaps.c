// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/aead/aes128gcm/aes128gcm.h"
#include "crypto/asymmetric/curve25519/curve25519.h"
#include "crypto/asymmetric/ed25519/ed25519.h"
#include "crypto/cipher/aes_block/aes_block.h"
#include "crypto/hash/sha256/sha256.h"
#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"
#include "network_drivers/presentation/security/dtls/dtls_conn/dtls_conn.h"
#include "network_drivers/presentation/security/dtls/dtls_handshake/dtls_handshake.h"
#include "network_drivers/presentation/security/dtls/dtls_record/dtls_record.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include "services/iot/coap/coap/coap.h"
#include "services/iot/coap/coaps/coaps.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

static uint8_t tls13_msg_work[16]; // the borrow an entry takes; Tls13Msg never reads it

static uint8_t dtls_server_work[16]; // the borrow an entry takes; DtlsServer never reads it

static uint8_t dtls_handshake_work[16]; // the borrow an entry takes; DtlsHandshake never reads it

static uint8_t dtls_record_work[16]; // the borrow an entry takes; DtlsRecord never reads it

static uint8_t coaps_work[16]; // the borrow an entry takes; Coaps never reads it

static uint8_t tw[4096];
static uint8_t tw_tr[4096];

static const uint8_t SERVER_ED_SEED[32] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                           17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
static const uint8_t SERVER_X25519_PRIV[32] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                                               0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                                               0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
static const uint8_t SERVER_RANDOM[32] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA,
                                          0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5,
                                          0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF};
static const uint8_t CLIENT_X25519_PRIV[32] = {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                                               0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                                               0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22};
static const uint8_t SERVER_COOKIE_KEY[32] = {0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
                                              0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71,
                                              0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b};

static void h_temp(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    resp->code = (uint8_t)COAP_RSP_CONTENT;
    memcpy(resp->payload, "hi", 2);
    resp->payload_len = 2;
    resp->content_format = COAP_CF_TEXT;
}

void setUp()
{
    Coap.reset(protocore_coap_span());
    Coap.resource.path = "/temp";
    Coap.resource.methods = COAP_ALLOW_GET;
    Coap.resource.handler = h_temp;
    Coap.add_resource(protocore_coap_span());
}
void tearDown()
{
}

typedef struct
{
    uint8_t *p;
    size_t n;
} Buf;
static void b8(Buf *b, uint8_t v)
{
    b->p[b->n++] = v;
}
static void b16(Buf *b, uint16_t v)
{
    b8(b, (uint8_t)(v >> 8));
    b8(b, (uint8_t)v);
}
static void bmem(Buf *b, const uint8_t *m, size_t k)
{
    memcpy(b->p + b->n, m, k);
    b->n += k;
}

static size_t build_client_hello(uint8_t *out, const uint8_t client_pub[32])
{
    Buf b = {out, 0};
    b8(&b, 0x01);
    size_t len_at = b.n;
    b8(&b, 0);
    b8(&b, 0);
    b8(&b, 0);
    b16(&b, 0x0303);
    for (int i = 0; i < 32; i++)
    {
        b8(&b, (uint8_t)(0x30 + i));
    }
    b8(&b, 0x00);
    b8(&b, 0x00);
    b16(&b, 0x0002);
    b16(&b, 0x1301);
    b8(&b, 0x01);
    b8(&b, 0x00);
    size_t ext_len_at = b.n;
    b16(&b, 0);
    b16(&b, 0x002b);
    b16(&b, 0x0003);
    b8(&b, 0x02);
    b16(&b, 0xFEFC);
    b16(&b, 0x000a);
    b16(&b, 0x0004);
    b16(&b, 0x0002);
    b16(&b, 0x001d);
    b16(&b, 0x000d);
    b16(&b, 0x0004);
    b16(&b, 0x0002);
    b16(&b, 0x0807);
    b16(&b, 0x0033);
    b16(&b, 0x0026);
    b16(&b, 0x0024);
    b16(&b, 0x001d);
    b16(&b, 0x0020);
    bmem(&b, client_pub, 32);
    uint16_t ext_len = (uint16_t)(b.n - ext_len_at - 2);
    out[ext_len_at] = (uint8_t)(ext_len >> 8);
    out[ext_len_at + 1] = (uint8_t)ext_len;
    uint32_t body = (uint32_t)(b.n - len_at - 3);
    out[len_at] = (uint8_t)(body >> 16);
    out[len_at + 1] = (uint8_t)(body >> 8);
    out[len_at + 2] = (uint8_t)body;
    return b.n;
}

static proto_bool sh_keyshare(const uint8_t *sh, size_t len, uint8_t pub[32])
{
    if (len < 44)
    {
        return PROTO_FALSE;
    }
    size_t o = 4 + 2 + 32;
    uint8_t sid = sh[o++];
    o += sid;
    o += 2 + 1;
    if (o + 2 > len)
    {
        return PROTO_FALSE;
    }
    size_t ext_end = o + 2 + ((sh[o] << 8) | sh[o + 1]);
    o += 2;
    while (o + 4 <= ext_end && ext_end <= len)
    {
        uint16_t et = (uint16_t)((sh[o] << 8) | sh[o + 1]);
        uint16_t el = (uint16_t)((sh[o + 2] << 8) | sh[o + 3]);
        o += 4;
        if (et == 0x0033 && el >= 4 + 32)
        {
            memcpy(pub, sh + o + 4, 32);
            return PROTO_TRUE;
        }
        o += el;
    }
    return PROTO_FALSE;
}

static size_t frag_to_tls(const uint8_t *payload, size_t plen, uint8_t *tls_out)
{
    DtlsHsHeader hh;
    DtlsHandshakeV.header_parse_args.p = payload;
    DtlsHandshakeV.header_parse_args.len = plen;
    DtlsHandshakeV.header_parse_args.out = &hh;
    DtlsHandshake.header_parse(dtls_handshake_work);
    if (!DtlsHandshakeV.n || hh.frag_offset != 0 || hh.frag_length != hh.length)
    {
        return 0;
    }
    tls_out[0] = hh.msg_type;
    tls_out[1] = (uint8_t)(hh.length >> 16);
    tls_out[2] = (uint8_t)(hh.length >> 8);
    tls_out[3] = (uint8_t)hh.length;
    memcpy(tls_out + 4, hh.fragment, hh.length);
    return 4 + hh.length;
}

static size_t ct_record_len(const uint8_t *rec, size_t avail)
{
    size_t seq_len = (rec[0] & 0x08) ? 2 : 1;
    size_t o = 1 + seq_len + 2;
    size_t enc = ((size_t)rec[1 + seq_len] << 8) | rec[1 + seq_len + 1];
    return (o + enc <= avail) ? o + enc : 0;
}

static DtlsConn g_dtls;
static DtlsConn g_dtls2;

static void handshake(DtlsConn *conn, DtlsRecordKeys *cli_app_write, DtlsRecordKeys *cli_app_read)
{
    uint8_t client_pub[32];
    Curve25519V.x25519_base_args.out = client_pub;
    Curve25519V.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519V.pubkey_args.pub = server_ed_pub;
    Ed25519V.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);

    DtlsServerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cert_der = server_ed_pub;
    cfg.cert_len = 32;
    cfg.ed25519_seed = SERVER_ED_SEED;
    cfg.ephemeral_priv = SERVER_X25519_PRIV;
    cfg.server_random = SERVER_RANDOM;
    cfg.cookie_key = SERVER_COOKIE_KEY;
    DtlsServerV.init_args.c = conn;
    DtlsServerV.init_args.cfg = &cfg;
    DtlsServerV.init_args.peer_addr = NULL;
    DtlsServerV.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t *tr;
    tr = tw_tr;
    Sha256.init(tr);
    Sha256V.update_args.data = ch;
    Sha256V.update_args.len = ch_len;
    Sha256.update(tr);
    uint8_t ch_frag[300];
    DtlsHandshakeV.frag_build_args.msg_type = ch[0];
    DtlsHandshakeV.frag_build_args.msg_seq = 0;
    DtlsHandshakeV.frag_build_args.full_len = (uint32_t)(ch_len - 4);
    DtlsHandshakeV.frag_build_args.frag_offset = 0;
    DtlsHandshakeV.frag_build_args.frag = ch + 4;
    DtlsHandshakeV.frag_build_args.frag_len = (uint32_t)(ch_len - 4);
    DtlsHandshakeV.frag_build_args.out = ch_frag;
    DtlsHandshakeV.frag_build_args.out_cap = sizeof(ch_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t ch_fl = DtlsHandshakeV.n;
    uint8_t ch_rec[320];
    DtlsRecordV.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecordV.plaintext_build_args.epoch = 0;
    DtlsRecordV.plaintext_build_args.seq = 0;
    DtlsRecordV.plaintext_build_args.fragment = ch_frag;
    DtlsRecordV.plaintext_build_args.frag_len = ch_fl;
    DtlsRecordV.plaintext_build_args.out = ch_rec;
    DtlsRecordV.plaintext_build_args.out_cap = sizeof(ch_rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t ch_rl = DtlsRecordV.n;

    uint8_t flight[2048];
    DtlsServerV.process_args.c = conn;
    DtlsServerV.process_args.dgram = ch_rec;
    DtlsServerV.process_args.len = ch_rl;
    DtlsServerV.process_args.out = flight;
    DtlsServerV.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    int fl = DtlsServerV.n;
    TEST_ASSERT_TRUE(fl > 0);

    size_t off = 0;
    DtlsPlaintext pt;
    DtlsRecordV.plaintext_parse_args.rec = flight;
    DtlsRecordV.plaintext_parse_args.rec_len = (size_t)fl;
    DtlsRecordV.plaintext_parse_args.out = &pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    size_t rl = DtlsRecordV.n;
    TEST_ASSERT_TRUE(rl > 0);
    off += rl;
    uint8_t sh[512];
    size_t sh_len = frag_to_tls(pt.fragment, pt.frag_len, sh);
    TEST_ASSERT_TRUE(sh_len > 0);
    Sha256V.update_args.data = sh;
    Sha256V.update_args.len = sh_len;
    Sha256.update(tr);
    uint8_t server_pub[32];
    TEST_ASSERT_TRUE(sh_keyshare(sh, sh_len, server_pub));

    uint8_t ecdhe[32];
    Curve25519V.x25519_args.out = ecdhe;
    Curve25519V.x25519_args.scalar = CLIENT_X25519_PRIV;
    Curve25519V.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    Tls13KeySchedule cks;
    uint8_t h[32];
    Sha256V.final_args.out = h;
    Sha256.final(tr);
    static uint8_t ks_store_236[PROTOCORE_TLS13_KS_BORROW];
    Tls13KsV.bind.kdf = &DTLS13_KDF;
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.bind.s = ks_store_236;
    Tls13Ks.early(NULL);
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.step.ecdhe = ecdhe;
    Tls13KsV.step.ecdhe_len = 32;
    Tls13KsV.step.ch_sh_hash = h;
    Tls13Ks.handshake(NULL);
    DtlsRecordKeys srv_read;
    DtlsRecordV.keys_derive_args.out = &srv_read;
    DtlsRecordV.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecordV.keys_derive_args.epoch = 2;
    DtlsRecordV.keys_derive_args.secret = cks.s + TLS13_KS_SERVER_HS;
    DtlsRecord.keys_derive(dtls_record_work);

    uint64_t exp_seq = 0;
    while (off < (size_t)fl)
    {
        size_t crl = ct_record_len(flight + off, (size_t)fl - off);
        TEST_ASSERT_TRUE(crl > 0);
        uint8_t inner[512];
        DtlsCiphertext info;
        DtlsRecordV.unprotect_args.keys = &srv_read;
        DtlsRecordV.unprotect_args.next_seq = exp_seq;
        DtlsRecordV.unprotect_args.rec = flight + off;
        DtlsRecordV.unprotect_args.rec_len = crl;
        DtlsRecordV.unprotect_args.out = inner;
        DtlsRecordV.unprotect_args.out_cap = sizeof(inner);
        DtlsRecordV.unprotect_args.info = &info;
        DtlsRecordV.unprotect_args.expected_cid = NULL;
        DtlsRecordV.unprotect_args.expected_cid_len = 0;
        DtlsRecord.unprotect(dtls_record_work);
        TEST_ASSERT_TRUE(DtlsRecordV.ok);
        exp_seq = info.seq + 1;
        off += crl;
        uint8_t msg[512];
        size_t mlen = frag_to_tls(inner, info.pt_len, msg);
        TEST_ASSERT_TRUE(mlen > 0);
        Sha256V.update_args.data = msg;
        Sha256V.update_args.len = mlen;
        Sha256.update(tr);
    }

    uint8_t h_sfin[32];
    Sha256V.final_args.out = h_sfin;
    Sha256.final(tr);
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.step.ch_sfin_hash = h_sfin;
    Tls13Ks.master(NULL);
    uint8_t cfin_verify[32];
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.finished_args.base_secret = cks.s + TLS13_KS_CLIENT_HS;
    Tls13KsV.finished_args.transcript_hash = h_sfin;
    Tls13KsV.finished_args.out = cfin_verify;
    Tls13Ks.finished_mac(NULL);
    uint8_t cfin[64];
    Tls13MsgV.build_finished_args.out = cfin;
    Tls13MsgV.build_finished_args.cap = sizeof(cfin);
    Tls13MsgV.build_finished_args.verify_data = cfin_verify;
    Tls13MsgV.build_finished_args.verify_len = 32;
    Tls13Msg.build_finished(tls13_msg_work);
    size_t cfin_len = Tls13MsgV.n;
    DtlsRecordKeys cli_write;
    DtlsRecordV.keys_derive_args.out = &cli_write;
    DtlsRecordV.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecordV.keys_derive_args.epoch = 2;
    DtlsRecordV.keys_derive_args.secret = cks.s + TLS13_KS_CLIENT_HS;
    DtlsRecord.keys_derive(dtls_record_work);
    uint8_t cfin_frag[80];
    DtlsHandshakeV.frag_build_args.msg_type = cfin[0];
    DtlsHandshakeV.frag_build_args.msg_seq = 1;
    DtlsHandshakeV.frag_build_args.full_len = (uint32_t)(cfin_len - 4);
    DtlsHandshakeV.frag_build_args.frag_offset = 0;
    DtlsHandshakeV.frag_build_args.frag = cfin + 4;
    DtlsHandshakeV.frag_build_args.frag_len = (uint32_t)(cfin_len - 4);
    DtlsHandshakeV.frag_build_args.out = cfin_frag;
    DtlsHandshakeV.frag_build_args.out_cap = sizeof(cfin_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t cff = DtlsHandshakeV.n;
    uint8_t cfin_rec[128];
    DtlsRecordV.protect_args.keys = &cli_write;
    DtlsRecordV.protect_args.seq = 0;
    DtlsRecordV.protect_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecordV.protect_args.plaintext = cfin_frag;
    DtlsRecordV.protect_args.pt_len = cff;
    DtlsRecordV.protect_args.out = cfin_rec;
    DtlsRecordV.protect_args.out_cap = sizeof(cfin_rec);
    DtlsRecordV.protect_args.cid = NULL;
    DtlsRecordV.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t cfr = DtlsRecordV.n;
    uint8_t out2[64];
    DtlsServerV.process_args.c = conn;
    DtlsServerV.process_args.dgram = cfin_rec;
    DtlsServerV.process_args.len = cfr;
    DtlsServerV.process_args.out = out2;
    DtlsServerV.process_args.out_cap = sizeof(out2);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServerV.n > 0);
    DtlsServerV.established_args.c = conn;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServerV.ok);

    DtlsRecordV.keys_derive_args.out = cli_app_read;
    DtlsRecordV.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecordV.keys_derive_args.epoch = 3;
    DtlsRecordV.keys_derive_args.secret = cks.s + TLS13_KS_SERVER_AP;
    DtlsRecord.keys_derive(dtls_record_work);
    DtlsRecordV.keys_derive_args.out = cli_app_write;
    DtlsRecordV.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecordV.keys_derive_args.epoch = 3;
    DtlsRecordV.keys_derive_args.secret = cks.s + TLS13_KS_CLIENT_AP;
    DtlsRecord.keys_derive(dtls_record_work);
}

void test_coap_over_dtls(void)
{
    DtlsRecordKeys cli_app_write, cli_app_read;
    handshake(&g_dtls, &cli_app_write, &cli_app_read);

    const uint8_t coap_get[] = {0x40, 0x01, 0x12, 0x34, 0xB4, 't', 'e', 'm', 'p'};
    uint8_t app_rec[128];
    DtlsRecordV.protect_args.keys = &cli_app_write;
    DtlsRecordV.protect_args.seq = 0;
    DtlsRecordV.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecordV.protect_args.plaintext = coap_get;
    DtlsRecordV.protect_args.pt_len = sizeof(coap_get);
    DtlsRecordV.protect_args.out = app_rec;
    DtlsRecordV.protect_args.out_cap = sizeof(app_rec);
    DtlsRecordV.protect_args.cid = NULL;
    DtlsRecordV.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t ar = DtlsRecordV.n;
    TEST_ASSERT_TRUE(ar > 0);

    uint8_t out[256];
    Coaps.conn = &g_dtls;
    Coaps.dgram.data = app_rec;
    Coaps.dgram.len = ar;
    Coaps.dgram.out = out;
    Coaps.dgram.out_cap = sizeof(out);
    Coaps.process(coaps_work);
    int on = Coaps.i32;
    TEST_ASSERT_TRUE(on > 0);

    uint8_t coap_resp[256];
    DtlsCiphertext info;
    DtlsRecordV.unprotect_args.keys = &cli_app_read;
    DtlsRecordV.unprotect_args.next_seq = 1;
    DtlsRecordV.unprotect_args.rec = out;
    DtlsRecordV.unprotect_args.rec_len = (size_t)on;
    DtlsRecordV.unprotect_args.out = coap_resp;
    DtlsRecordV.unprotect_args.out_cap = sizeof(coap_resp);
    DtlsRecordV.unprotect_args.info = &info;
    DtlsRecordV.unprotect_args.expected_cid = NULL;
    DtlsRecordV.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecordV.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DTLS_CT_APPLICATION_DATA, info.content_type);

    TEST_ASSERT_TRUE(info.pt_len >= 6);
    TEST_ASSERT_EQUAL_UINT8(0x60, coap_resp[0] & 0xF0);
    TEST_ASSERT_EQUAL_UINT8(0x45, coap_resp[1]);
    TEST_ASSERT_EQUAL_UINT8(0x12, coap_resp[2]);
    TEST_ASSERT_EQUAL_UINT8(0x34, coap_resp[3]);

    TEST_ASSERT_EQUAL_MEMORY("hi", coap_resp + info.pt_len - 2, 2);
}

void test_coap_over_dtls_replay_dropped(void)
{
    DtlsRecordKeys cli_app_write, cli_app_read;
    handshake(&g_dtls, &cli_app_write, &cli_app_read);

    const uint8_t coap_get[] = {0x40, 0x01, 0x12, 0x34, 0xB4, 't', 'e', 'm', 'p'};
    uint8_t app_rec[128];
    DtlsRecordV.protect_args.keys = &cli_app_write;
    DtlsRecordV.protect_args.seq = 0;
    DtlsRecordV.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecordV.protect_args.plaintext = coap_get;
    DtlsRecordV.protect_args.pt_len = sizeof(coap_get);
    DtlsRecordV.protect_args.out = app_rec;
    DtlsRecordV.protect_args.out_cap = sizeof(app_rec);
    DtlsRecordV.protect_args.cid = NULL;
    DtlsRecordV.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t ar = DtlsRecordV.n;
    uint8_t out[256];
    Coaps.conn = &g_dtls;
    Coaps.dgram.data = app_rec;
    Coaps.dgram.len = ar;
    Coaps.dgram.out = out;
    Coaps.dgram.out_cap = sizeof(out);
    Coaps.process(coaps_work);
    TEST_ASSERT_TRUE(Coaps.i32 > 0);
    Coaps.conn = &g_dtls;
    Coaps.dgram.data = app_rec;
    Coaps.dgram.len = ar;
    Coaps.dgram.out = out;
    Coaps.dgram.out_cap = sizeof(out);
    Coaps.process(coaps_work);
    TEST_ASSERT_EQUAL_INT(0, Coaps.i32);
}

void test_coaps_no_coap_response(void)
{
    DtlsRecordKeys cli_app_write, cli_app_read;
    handshake(&g_dtls, &cli_app_write, &cli_app_read);

    const uint8_t coap_ack[] = {0x60, 0x00, 0x12, 0x34};
    uint8_t app_rec[128];
    DtlsRecordV.protect_args.keys = &cli_app_write;
    DtlsRecordV.protect_args.seq = 0;
    DtlsRecordV.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecordV.protect_args.plaintext = coap_ack;
    DtlsRecordV.protect_args.pt_len = sizeof(coap_ack);
    DtlsRecordV.protect_args.out = app_rec;
    DtlsRecordV.protect_args.out_cap = sizeof(app_rec);
    DtlsRecordV.protect_args.cid = NULL;
    DtlsRecordV.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t ar = DtlsRecordV.n;
    TEST_ASSERT_TRUE(ar > 0);

    uint8_t out[256];
    Coaps.conn = &g_dtls;
    Coaps.dgram.data = app_rec;
    Coaps.dgram.len = ar;
    Coaps.dgram.out = out;
    Coaps.dgram.out_cap = sizeof(out);
    Coaps.process(coaps_work);
    TEST_ASSERT_EQUAL_INT(0, Coaps.i32);
}

void test_coaps_non_app_record(void)
{
    DtlsRecordKeys cli_app_write, cli_app_read;
    handshake(&g_dtls, &cli_app_write, &cli_app_read);

    uint8_t out[256];
    uint8_t byte[1] = {0x16};
    Coaps.conn = &g_dtls;
    Coaps.dgram.data = byte;
    Coaps.dgram.len = 0;
    Coaps.dgram.out = out;
    Coaps.dgram.out_cap = sizeof(out);
    Coaps.process(coaps_work);
    TEST_ASSERT_EQUAL_INT(0, Coaps.i32);
    Coaps.conn = &g_dtls;
    Coaps.dgram.data = byte;
    Coaps.dgram.len = 1;
    Coaps.dgram.out = out;
    Coaps.dgram.out_cap = sizeof(out);
    Coaps.process(coaps_work);
    TEST_ASSERT_EQUAL_INT(0, Coaps.i32);
    DtlsServerV.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServerV.ok);
}

void test_coaps_wrong_epoch_record(void)
{
    DtlsRecordKeys cli_app_write, cli_app_read;
    handshake(&g_dtls, &cli_app_write, &cli_app_read);

    uint8_t rec[24];
    memset(rec, 0, sizeof(rec));
    rec[0] = 0x22;

    uint8_t out[64];
    Coaps.conn = &g_dtls;
    Coaps.dgram.data = rec;
    Coaps.dgram.len = sizeof(rec);
    Coaps.dgram.out = out;
    Coaps.dgram.out_cap = sizeof(out);
    Coaps.process(coaps_work);
    TEST_ASSERT_EQUAL_INT(0, Coaps.i32);
}

void test_coaps_forwards_handshake(void)
{
    uint8_t client_pub[32];
    Curve25519V.x25519_base_args.out = client_pub;
    Curve25519V.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519V.pubkey_args.pub = server_ed_pub;
    Ed25519V.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);

    DtlsServerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cert_der = server_ed_pub;
    cfg.cert_len = 32;
    cfg.ed25519_seed = SERVER_ED_SEED;
    cfg.ephemeral_priv = SERVER_X25519_PRIV;
    cfg.server_random = SERVER_RANDOM;
    cfg.cookie_key = SERVER_COOKIE_KEY;
    DtlsServerV.init_args.c = &g_dtls;
    DtlsServerV.init_args.cfg = &cfg;
    DtlsServerV.init_args.peer_addr = NULL;
    DtlsServerV.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t ch_frag[300];
    DtlsHandshakeV.frag_build_args.msg_type = ch[0];
    DtlsHandshakeV.frag_build_args.msg_seq = 0;
    DtlsHandshakeV.frag_build_args.full_len = (uint32_t)(ch_len - 4);
    DtlsHandshakeV.frag_build_args.frag_offset = 0;
    DtlsHandshakeV.frag_build_args.frag = ch + 4;
    DtlsHandshakeV.frag_build_args.frag_len = (uint32_t)(ch_len - 4);
    DtlsHandshakeV.frag_build_args.out = ch_frag;
    DtlsHandshakeV.frag_build_args.out_cap = sizeof(ch_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t ch_fl = DtlsHandshakeV.n;
    uint8_t ch_rec[320];
    DtlsRecordV.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecordV.plaintext_build_args.epoch = 0;
    DtlsRecordV.plaintext_build_args.seq = 0;
    DtlsRecordV.plaintext_build_args.fragment = ch_frag;
    DtlsRecordV.plaintext_build_args.frag_len = ch_fl;
    DtlsRecordV.plaintext_build_args.out = ch_rec;
    DtlsRecordV.plaintext_build_args.out_cap = sizeof(ch_rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t ch_rl = DtlsRecordV.n;

    DtlsServerV.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_FALSE(DtlsServerV.ok);
    uint8_t flight[2048];
    Coaps.conn = &g_dtls;
    Coaps.dgram.data = ch_rec;
    Coaps.dgram.len = ch_rl;
    Coaps.dgram.out = flight;
    Coaps.dgram.out_cap = sizeof(flight);
    Coaps.process(coaps_work);
    int fl = Coaps.i32;
    TEST_ASSERT_TRUE(fl > 0);
}

void test_aes256_key_expand_kat(void)
{
    static const uint8_t key[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    static const uint8_t pt[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                   0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    static const uint8_t expect_ct[16] = {0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
                                          0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89};
    uint32_t rk[60];
    protocore_aes_key_expand(key, 8, rk);
    uint8_t ct[16];
    protocore_aes_encrypt_block(rk, 14, pt, ct);
    TEST_ASSERT_EQUAL_MEMORY(expect_ct, ct, 16);
}
