// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// CoAP-over-DTLS server front-end (coaps_server.h): the per-peer DtlsConn pool + ingest/poll seam.
// An in-test DTLS 1.3 client drives the server entirely through pc_coaps_server_ingest() (datagrams in)
// and an output sink (datagrams out) - never touching the server's DtlsConn directly - so it exercises
// exactly the front-end's jobs: routing a datagram to the right connection by peer address, running the
// handshake + CoAP exchange through pc_coaps_process(), driving the retransmission timer from the poll, and
// reaping idle connections. The DTLS/CoAP crypto correctness itself is covered by test_dtls_conn and
// test_coaps; here the client is just a vehicle to reach and use an established connection.

#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ed25519.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/presentation/security/dtls/dtls_conn.h"
#include "network_drivers/presentation/security/dtls/dtls_handshake.h"
#include "network_drivers/presentation/security/dtls/dtls_record.h"
#include "network_drivers/tls/tls13_kdf.h"
#include "server/clock/clock.h"
#include "services/iot/coap/coap.h"
#include "services/iot/coap/coaps_server.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

// ---- fixed test key material (deterministic; matches test_coaps / test_dtls_conn) ----
static const uint8_t SERVER_ED_SEED[32] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                           17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
static const uint8_t CLIENT_X25519_PRIV[32] = {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                                               0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                                               0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22};
static const uint8_t SERVER_COOKIE_KEY[32] = {0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
                                              0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71,
                                              0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b};

// A GET /temp resource that replies 2.05 Content "hi".
static void h_temp(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    resp->code = (uint8_t)COAP_RSP_CONTENT;
    memcpy(resp->payload, "hi", 2);
    resp->payload_len = 2;
    resp->content_format = COAP_CF_TEXT;
}

// ---- simulation clock (drives the DTLS PTO + idle reaping deterministically) ----
static uint32_t g_ms = 0;
static uint32_t test_clock()
{
    return g_ms;
}

// ---- server randomness: a counter, so each connection's ephemeral/random differ ----
static uint8_t g_rng_ctr = 0;
static void test_rng(uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        out[i] = g_rng_ctr++;
    }
}

// ---- outbound-datagram capture queue (the host stand-in for the wire) ----
typedef struct
{
    uint8_t buf[2048];
    size_t len;
    char ip[16];
    uint16_t port;
} OutDg;
static OutDg g_out[16];
static int g_out_n = 0;
static void out_reset()
{
    g_out_n = 0;
}
static void out_sink(void *, const uint8_t *dg, size_t len, const char *ip, uint16_t port)
{
    if (g_out_n >= (int)(sizeof g_out / sizeof g_out[0]) || len > sizeof g_out[0].buf)
    {
        return;
    }
    OutDg *o = &g_out[g_out_n++];
    memcpy(o->buf, dg, len);
    o->len = len;
    size_t k = 0;
    while (ip[k] && k < sizeof o->ip - 1)
    {
        o->ip[k] = ip[k];
        k++;
    }
    o->ip[k] = 0;
    o->port = port;
}
// Pull the first captured datagram addressed to ip:port out of the queue.
static proto_bool take_out_for(const char *ip, uint16_t port, OutDg *dst)
{
    for (int i = 0; i < g_out_n; i++)
    {
        if (g_out[i].port == port && strcmp(g_out[i].ip, ip) == 0)
        {
            *dst = g_out[i];
            for (int j = i; j + 1 < g_out_n; j++)
            {
                g_out[j] = g_out[j + 1];
            }
            g_out_n--;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

static uint8_t g_server_cert[32]; // Ed25519 public key used as the raw leaf "certificate" (as in test_coaps)

void setUp()
{
    pc_coap_server_reset();
    pc_coap_server_add_resource("/temp", COAP_ALLOW_GET, h_temp);
    pc_set_clock(test_clock, 1000);
    g_ms = 0;
    g_rng_ctr = 0;
    out_reset();

    pc_ed25519_pubkey(g_server_cert, SERVER_ED_SEED);
    CoapsServerConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.cert_der = g_server_cert;
    cfg.cert_len = 32;
    memcpy(cfg.ed25519_seed, SERVER_ED_SEED, 32);
    memcpy(cfg.cookie_key, SERVER_COOKIE_KEY, 32);
    cfg.rng = test_rng;
    TEST_ASSERT_TRUE(pc_coaps_server_begin(PC_COAPS_PORT, &cfg));
    pc_coaps_server_set_out_sink_cb(out_sink, NULL);
}
void tearDown()
{
    pc_coaps_server_stop();
    pc_set_clock(NULL, 0);
}

// ---- minimal DTLS 1.3 client (offers X25519 directly, no HRR) ----
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

static size_t build_client_hello(uint8_t *out, const uint8_t client_pub[32], const uint8_t *cid, size_t cid_len)
{
    Buf b = {out, 0};
    b8(&b, 0x01); // client_hello
    size_t len_at = b.n;
    b8(&b, 0);
    b8(&b, 0);
    b8(&b, 0);       // 24-bit body length (patched)
    b16(&b, 0x0303); // legacy_version
    for (int i = 0; i < 32; i++)
    {
        b8(&b, (uint8_t)(0x30 + i)); // random
    }
    b8(&b, 0x00); // session_id: empty
    b8(&b, 0x00); // legacy_cookie: empty (DTLS)
    b16(&b, 0x0002);
    b16(&b, 0x1301); // cipher_suites: TLS_AES_128_GCM_SHA256
    b8(&b, 0x01);
    b8(&b, 0x00); // compression: null
    size_t ext_len_at = b.n;
    b16(&b, 0);
    b16(&b, 0x002b); // supported_versions: DTLS 1.3
    b16(&b, 0x0003);
    b8(&b, 0x02);
    b16(&b, 0xFEFC);
    b16(&b, 0x000a); // supported_groups: x25519
    b16(&b, 0x0004);
    b16(&b, 0x0002);
    b16(&b, 0x001d);
    b16(&b, 0x000d); // signature_algorithms: ed25519
    b16(&b, 0x0004);
    b16(&b, 0x0002);
    b16(&b, 0x0807);
    b16(&b, 0x0033); // key_share: x25519
    b16(&b, 0x0026);
    b16(&b, 0x0024);
    b16(&b, 0x001d);
    b16(&b, 0x0020);
    bmem(&b, client_pub, 32);
    if (cid)
    {
        b16(&b, 0x0036); // connection_id (RFC 9146): the CID the server places in records it sends us
        b16(&b, (uint16_t)(1 + cid_len));
        b8(&b, (uint8_t)cid_len);
        bmem(&b, cid, cid_len);
    }
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
    if (!DtlsHandshake.header_parse(payload, plen, &hh) || hh.frag_offset != 0 || hh.frag_length != hh.length)
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

static size_t ct_record_len(const uint8_t *rec, size_t avail, size_t cid_len)
{
    size_t pre = 1 + ((rec[0] & 0x10) ? cid_len : 0);
    size_t seq_len = (rec[0] & 0x08) ? 2 : 1;
    size_t len_off = pre + seq_len;
    size_t o = len_off + 2;
    size_t enc = ((size_t)rec[len_off] << 8) | rec[len_off + 1];
    return (o + enc <= avail) ? o + enc : 0;
}

// Pull the server's connection_id (extension 0x0036, RFC 9146) out of a ServerHello.
static size_t sh_conn_id(const uint8_t *sh, size_t len, uint8_t *cid_out)
{
    if (len < 44)
    {
        return 0;
    }
    size_t o = 4 + 2 + 32;
    uint8_t sid = sh[o++];
    o += sid;
    o += 2 + 1;
    if (o + 2 > len)
    {
        return 0;
    }
    size_t ext_end = o + 2 + ((sh[o] << 8) | sh[o + 1]);
    o += 2;
    while (o + 4 <= ext_end && ext_end <= len)
    {
        uint16_t et = (uint16_t)((sh[o] << 8) | sh[o + 1]);
        uint16_t el = (uint16_t)((sh[o + 2] << 8) | sh[o + 3]);
        o += 4;
        if (et == 0x0036 && el >= 1)
        {
            size_t cl = sh[o];
            if (1 + cl > el || o + 1 + cl > len)
            {
                return 0;
            }
            memcpy(cid_out, sh + o + 1, cl);
            return cl;
        }
        o += el;
    }
    return 0;
}

// Complete the handshake for peer ip:port through the front-end seam and hand back the client's
// application-traffic keys. Asserts each step so a failure pinpoints the stage.
static void client_handshake(const char *ip, uint16_t port, DtlsRecordKeys *cli_app_write, DtlsRecordKeys *cli_app_read,
                             const uint8_t *client_cid, size_t client_cid_len, uint8_t *scid_out, size_t *scid_len_out)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub, client_cid, client_cid_len);
    pc_sha256_ctx tr;
    pc_sha256_init(&tr);
    pc_sha256_update(&tr, ch, ch_len);
    uint8_t ch_frag[300];
    size_t ch_fl = DtlsHandshake.frag_build(ch[0], 0, (uint32_t)(ch_len - 4), 0, ch + 4, (uint32_t)(ch_len - 4),
                                            ch_frag, sizeof(ch_frag));
    uint8_t ch_rec[320];
    size_t ch_rl = DtlsRecord.plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, ch_frag, ch_fl, ch_rec, sizeof(ch_rec));
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(ch_rec, ch_rl, ip, port));
    pc_coaps_server_poll();

    OutDg fldg;
    TEST_ASSERT_TRUE(take_out_for(ip, port, &fldg)); // the server flight
    const uint8_t *flight = fldg.buf;
    size_t fl = fldg.len;

    size_t off = 0;
    DtlsPlaintext pt;
    size_t rl = DtlsRecord.plaintext_parse(flight, fl, &pt);
    TEST_ASSERT_TRUE(rl > 0);
    off += rl;
    uint8_t sh[512];
    size_t sh_len = frag_to_tls(pt.fragment, pt.frag_len, sh);
    TEST_ASSERT_TRUE(sh_len > 0);
    pc_sha256_update(&tr, sh, sh_len);
    uint8_t server_pub[32];
    TEST_ASSERT_TRUE(sh_keyshare(sh, sh_len, server_pub));

    // When we offered a CID, the ServerHello carries the server's CID (which we place in the records we
    // send); its epoch-2 flight carries our CID (@p client_cid).
    uint8_t scid[PC_DTLS_CID_MAX];
    size_t scid_len = client_cid_len ? sh_conn_id(sh, sh_len, scid) : 0;
    if (client_cid_len)
    {
        TEST_ASSERT_TRUE(scid_len > 0);
    }
    if (scid_out && scid_len_out)
    {
        memcpy(scid_out, scid, scid_len);
        *scid_len_out = scid_len;
    }

    uint8_t ecdhe[32];
    pc_x25519(ecdhe, CLIENT_X25519_PRIV, server_pub);
    Tls13KeySchedule cks;
    uint8_t hh[32];
    pc_sha256_ctx tmp = tr;
    pc_sha256_final(&tmp, hh);
    pc_tls13_ks_early(&DTLS13_KDF, &cks);
    pc_tls13_ks_handshake(&cks, ecdhe, hh, 32);
    DtlsRecordKeys srv_read;
    DtlsRecord.keys_derive(&srv_read, DTLS_CIPHER_AES_128_GCM_SHA256, 2, cks.server_hs_traffic);

    uint64_t exp_seq = 0;
    while (off < fl)
    {
        size_t crl = ct_record_len(flight + off, fl - off, client_cid_len);
        TEST_ASSERT_TRUE(crl > 0);
        uint8_t inner[512];
        DtlsCiphertext info;
        TEST_ASSERT_TRUE(DtlsRecord.unprotect(&srv_read, exp_seq, flight + off, crl, inner, sizeof(inner), &info,
                                              client_cid, client_cid_len));
        exp_seq = info.seq + 1;
        off += crl;
        uint8_t msg[512];
        size_t mlen = frag_to_tls(inner, info.pt_len, msg);
        TEST_ASSERT_TRUE(mlen > 0);
        pc_sha256_update(&tr, msg, mlen);
    }

    uint8_t h_sfin[32];
    pc_sha256_ctx s2 = tr;
    pc_sha256_final(&s2, h_sfin);
    pc_tls13_ks_master(&cks, h_sfin);
    uint8_t cfin_verify[32];
    pc_tls13_finished_mac(&DTLS13_KDF, cks.client_hs_traffic, h_sfin, cfin_verify);
    uint8_t cfin[64];
    size_t cfin_len = pc_tls13_build_finished(cfin, sizeof(cfin), cfin_verify);
    DtlsRecordKeys cli_write;
    DtlsRecord.keys_derive(&cli_write, DTLS_CIPHER_AES_128_GCM_SHA256, 2, cks.client_hs_traffic);
    uint8_t cfin_frag[80];
    size_t cff = DtlsHandshake.frag_build(cfin[0], 1, (uint32_t)(cfin_len - 4), 0, cfin + 4, (uint32_t)(cfin_len - 4),
                                          cfin_frag, sizeof(cfin_frag));
    uint8_t cfin_rec[128];
    size_t cfr = DtlsRecord.protect(&cli_write, 0, PC_DTLS_CT_HANDSHAKE, cfin_frag, cff, cfin_rec, sizeof(cfin_rec),
                                    scid_len ? scid : NULL, scid_len);
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(cfin_rec, cfr, ip, port));
    pc_coaps_server_poll();

    // The server acknowledges the client Finished with an epoch-3 ACK (seq 0); pull and discard it so
    // the CoAP response that follows is epoch-3 seq 1 on the wire.
    OutDg ackdg;
    take_out_for(ip, port, &ackdg);

    DtlsRecord.keys_derive(cli_app_read, DTLS_CIPHER_AES_128_GCM_SHA256, 3, cks.server_ap_traffic);
    DtlsRecord.keys_derive(cli_app_write, DTLS_CIPHER_AES_128_GCM_SHA256, 3, cks.client_ap_traffic);
}

// Seal a CoAP CON GET /temp as one epoch-3 client application record (client send-seq @p cseq).
static size_t client_get_temp(DtlsRecordKeys *w, uint64_t cseq, uint8_t *out, size_t cap, const uint8_t *cid,
                              size_t cid_len)
{
    const uint8_t coap_get[] = {0x40, 0x01, 0x12, 0x34, 0xB4, 't', 'e', 'm', 'p'};
    return DtlsRecord.protect(&w, cseq, PC_DTLS_CT_APPLICATION_DATA, coap_get, sizeof(coap_get), out, cap, cid,
                              cid_len);
}

// Decrypt the server's response record (epoch-3 send-seq 1, after the completion ACK) and assert it is
// the piggybacked 2.05 Content "hi".
static void assert_coap_205(DtlsRecordKeys *r, const OutDg *dg, const uint8_t *cid, size_t cid_len)
{
    uint8_t coap_resp[256];
    DtlsCiphertext info;
    TEST_ASSERT_TRUE(DtlsRecord.unprotect(&r, 1, dg->buf, dg->len, coap_resp, sizeof(coap_resp), &info, cid, cid_len));
    TEST_ASSERT_EQUAL_UINT8(PC_DTLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_TRUE(info.pt_len >= 6);
    TEST_ASSERT_EQUAL_UINT8(0x60, coap_resp[0] & 0xF0); // Ver 1, Type ACK
    TEST_ASSERT_EQUAL_UINT8(0x45, coap_resp[1]);        // 2.05 Content
    TEST_ASSERT_EQUAL_UINT8(0x12, coap_resp[2]);        // MID hi
    TEST_ASSERT_EQUAL_UINT8(0x34, coap_resp[3]);        // MID lo
    TEST_ASSERT_EQUAL_MEMORY("hi", coap_resp + info.pt_len - 2, 2);
}

// A single peer: handshake through the pool, then a CoAP GET is decrypted, answered, and re-sealed.
static void test_server_single_peer(void)
{
    DtlsRecordKeys w, r;
    client_handshake("10.0.0.5", 40001, &w, &r, NULL, 0, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT8(1, pc_coaps_server_active_conns());

    uint8_t rec[128];
    size_t n = client_get_temp(&w, 0, rec, sizeof(rec), NULL, 0);
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(rec, n, "10.0.0.5", 40001));
    pc_coaps_server_poll();

    OutDg dg;
    TEST_ASSERT_TRUE(take_out_for("10.0.0.5", 40001, &dg));
    assert_coap_205(&r, &dg, NULL, 0);
}

// Two peers at different addresses each get their own connection; requests injected in the same poll are
// routed to the right slot (a mis-route would decrypt under the wrong keys and fail the AEAD open).
static void test_two_peers_routing(void)
{
    DtlsRecordKeys wA, rA, wB, rB;
    client_handshake("10.0.0.5", 40001, &wA, &rA, NULL, 0, NULL, NULL);
    client_handshake("10.0.0.6", 40002, &wB, &rB, NULL, 0, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT8(2, pc_coaps_server_active_conns());

    uint8_t recA[128], recB[128];
    size_t nA = client_get_temp(&wA, 0, recA, sizeof(recA), NULL, 0);
    size_t nB = client_get_temp(&wB, 0, recB, sizeof(recB), NULL, 0);
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(recB, nB, "10.0.0.6", 40002));
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(recA, nA, "10.0.0.5", 40001));
    pc_coaps_server_poll();

    OutDg dgA, dgB;
    TEST_ASSERT_TRUE(take_out_for("10.0.0.5", 40001, &dgA));
    TEST_ASSERT_TRUE(take_out_for("10.0.0.6", 40002, &dgB));
    assert_coap_205(&rA, &dgA, NULL, 0);
    assert_coap_205(&rB, &dgB, NULL, 0);
}

// A connection with no inbound datagram for PC_COAPS_IDLE_MS is reclaimed by the poll.
static void test_idle_reap(void)
{
    DtlsRecordKeys w, r;
    client_handshake("10.0.0.5", 40001, &w, &r, NULL, 0, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT8(1, pc_coaps_server_active_conns());

    g_ms += PC_COAPS_IDLE_MS + 1;
    pc_coaps_server_poll();
    TEST_ASSERT_EQUAL_UINT8(0, pc_coaps_server_active_conns());
}

// The poll drives the DTLS retransmission timer: a lost client reply leaves the server flight
// outstanding, and once the PTO elapses the poll re-sends it (RFC 9147 §5.8) - not before.
static void test_pto_retransmit_driven_by_poll(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub, NULL, 0);
    uint8_t ch_frag[300];
    size_t ch_fl = DtlsHandshake.frag_build(ch[0], 0, (uint32_t)(ch_len - 4), 0, ch + 4, (uint32_t)(ch_len - 4),
                                            ch_frag, sizeof(ch_frag));
    uint8_t ch_rec[320];
    size_t ch_rl = DtlsRecord.plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, ch_frag, ch_fl, ch_rec, sizeof(ch_rec));
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(ch_rec, ch_rl, "10.0.0.7", 40003));
    pc_coaps_server_poll();

    OutDg f1;
    TEST_ASSERT_TRUE(take_out_for("10.0.0.7", 40003, &f1)); // original flight, PTO timer now armed

    // Before the PTO elapses a poll must not retransmit.
    g_ms += PC_DTLS_PTO_INITIAL_MS - 1;
    pc_coaps_server_poll();
    OutDg none;
    TEST_ASSERT_FALSE(take_out_for("10.0.0.7", 40003, &none));

    // At the PTO the poll fires the timer and the flight is re-sent (fresh record sequence numbers).
    g_ms += 1;
    pc_coaps_server_poll();
    OutDg f2;
    TEST_ASSERT_TRUE(take_out_for("10.0.0.7", 40003, &f2));
    TEST_ASSERT_TRUE(f2.len > 0);
    TEST_ASSERT_EQUAL_UINT8(1, pc_coaps_server_active_conns()); // still handshaking, not reaped
}

// HttpRoute-by-CID + address migration (RFC 9146 / RFC 9147 §9): a connection that negotiated a connection id
// is found by that id even after the peer's source address changes, and the reply follows to the new
// address - the NAT-rebinding survival the CID is for.
static void test_cid_address_migration(void)
{
    const uint8_t client_cid[3] = {0xC1, 0xC2, 0xC3};
    uint8_t scid[PC_DTLS_CID_MAX];
    size_t scid_len = 0;
    DtlsRecordKeys w, r;
    client_handshake("10.0.0.5", 40001, &w, &r, client_cid, sizeof(client_cid), scid, &scid_len);
    TEST_ASSERT_TRUE(scid_len > 0); // the server chose a connection id
    TEST_ASSERT_EQUAL_UINT8(1, pc_coaps_server_active_conns());

    // The peer roams to a new address and sends a CoAP GET protected with the server's CID.
    uint8_t rec[128];
    size_t n = client_get_temp(&w, 0, rec, sizeof(rec), scid, scid_len);
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(rec, n, "10.9.9.9", 55555)); // a different ip:port
    pc_coaps_server_poll();

    // The response is routed to the connection by its CID and sent to the NEW address, not the old one.
    OutDg dg;
    TEST_ASSERT_TRUE(take_out_for("10.9.9.9", 55555, &dg));
    OutDg stale;
    TEST_ASSERT_FALSE(take_out_for("10.0.0.5", 40001, &stale));
    assert_coap_205(&r, &dg, client_cid, sizeof(client_cid));   // the response carries our CID
    TEST_ASSERT_EQUAL_UINT8(1, pc_coaps_server_active_conns()); // migrated the existing connection, not a new one
}

// ---- helpers for the coverage tests below ----

// Ingest a well-formed first-flight ClientHello for ip:port (opens a slot, drives to WAIT_FINISHED).
static void ingest_real_client_hello(const char *ip, uint16_t port)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub, NULL, 0);
    uint8_t ch_frag[300];
    size_t ch_fl = DtlsHandshake.frag_build(ch[0], 0, (uint32_t)(ch_len - 4), 0, ch + 4, (uint32_t)(ch_len - 4),
                                            ch_frag, sizeof(ch_frag));
    uint8_t ch_rec[320];
    size_t ch_rl = DtlsRecord.plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, ch_frag, ch_fl, ch_rec, sizeof(ch_rec));
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(ch_rec, ch_rl, ip, port));
}

// Ingest a handshake record carrying a "ClientHello" whose body is too short to parse: the server rejects
// it fatally (ALERT_DECODE_ERROR), which the front-end turns into a freed slot.
static void ingest_bad_client_hello(const char *ip, uint16_t port)
{
    uint8_t garbage[8] = {0};
    uint8_t frag[64];
    size_t fl = DtlsHandshake.frag_build(0x01, 0, (uint32_t)sizeof(garbage), 0, garbage, (uint32_t)sizeof(garbage),
                                         frag, sizeof(frag));
    uint8_t rec[128];
    size_t rl = DtlsRecord.plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, frag, fl, rec, sizeof(rec));
    pc_coaps_server_ingest(rec, rl, ip, port);
}

// A one-byte record too short to parse: routed to its peer slot it is a no-op (pc_coaps_process returns 0),
// but the front-end still refreshes the slot's idle clock - a keepalive that never advances the handshake.
static void ingest_noop(const char *ip, uint16_t port)
{
    uint8_t junk[1] = {0x16};
    pc_coaps_server_ingest(junk, sizeof(junk), ip, port);
}

// pc_coaps_server_begin rejects a null config and each missing required field, and port 0 selects the default.
static void test_begin_rejects_invalid_cfg(void)
{
    CoapsServerConfig c;
    TEST_ASSERT_FALSE(pc_coaps_server_begin(PC_COAPS_PORT, NULL)); // null cfg

    memset(&c, 0, sizeof c); // rng missing
    c.cert_der = g_server_cert;
    c.cert_len = 32;
    c.rng = NULL;
    TEST_ASSERT_FALSE(pc_coaps_server_begin(PC_COAPS_PORT, &c));

    memset(&c, 0, sizeof c); // cert_der missing
    c.rng = test_rng;
    c.cert_der = NULL;
    c.cert_len = 32;
    TEST_ASSERT_FALSE(pc_coaps_server_begin(PC_COAPS_PORT, &c));

    memset(&c, 0, sizeof c); // cert_len 0
    c.rng = test_rng;
    c.cert_der = g_server_cert;
    c.cert_len = 0;
    TEST_ASSERT_FALSE(pc_coaps_server_begin(PC_COAPS_PORT, &c));

    memset(&c, 0, sizeof c); // valid; port 0 -> PC_COAPS_PORT
    c.cert_der = g_server_cert;
    c.cert_len = 32;
    c.rng = test_rng;
    memcpy(c.ed25519_seed, SERVER_ED_SEED, 32);
    memcpy(c.cookie_key, SERVER_COOKIE_KEY, 32);
    TEST_ASSERT_TRUE(pc_coaps_server_begin(0, &c));
}

// A poll on a stopped server is a no-op (does not touch the pool).
static void test_poll_when_stopped(void)
{
    pc_coaps_server_stop();
    pc_coaps_server_poll();
    TEST_ASSERT_EQUAL_UINT8(0, pc_coaps_server_active_conns());
}

// ingest rejects a zero-length and an over-sized datagram.
static void test_ingest_rejects_bad_len(void)
{
    uint8_t d[8] = {0};
    TEST_ASSERT_FALSE(pc_coaps_server_ingest(d, 0, "10.0.0.5", 1)); // zero length
    static uint8_t big[2000];                                       // > PC_COAPS_MAX_DATAGRAM (1500)
    memset(big, 0, sizeof big);
    TEST_ASSERT_FALSE(pc_coaps_server_ingest(big, sizeof big, "10.0.0.5", 1));
}

// The SPSC ingest ring drops datagrams once full (PC_COAPS_INGEST_RING - 1 usable entries).
static void test_ingest_ring_full(void)
{
    uint8_t d[8] = {0x16, 0, 0, 0, 0, 0, 0, 0};
    int pushed = 0;
    for (int i = 0; i < PC_COAPS_INGEST_RING + 3; i++)
    {
        if (pc_coaps_server_ingest(d, sizeof d, "10.0.0.5", 1))
        {
            pushed++;
        }
    }
    TEST_ASSERT_EQUAL_INT(PC_COAPS_INGEST_RING - 1, pushed);
}

// The peer-address copy tolerates a null ip (stored empty) and truncates an over-long one to the field.
static void test_ingest_addr_copy_edges(void)
{
    uint8_t d[8] = {0x16, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(d, sizeof d, NULL, 1));
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(d, sizeof d, "111.111.111.111.111.111", 2));
}

// Every malformed peer address fails serialize_peer (no HRR cookie binding); the connection still opens and
// the garbage ClientHello then frees the slot. Covers each serialize_peer reject branch.
static void test_malformed_peer_addr(void)
{
    const char *bad[] = {
        "999.0.0.1",  // octet > 255
        "10..0.1",    // empty octet
        "10.0.x.1",   // invalid character
        "1.2.3",      // too few octets
        "0001.0.0.1", // digit count > 3 within an octet whose value stays <= 255 (leading zeros)
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        ingest_bad_client_hello(bad[i], (uint16_t)(50000 + i));
        pc_coaps_server_poll();
        TEST_ASSERT_EQUAL_UINT8(0, pc_coaps_server_active_conns());
    }
}

// A fatal handshake error frees the slot and sends nothing.
static void test_fatal_handshake_frees_slot(void)
{
    ingest_bad_client_hello("10.0.0.5", 40001);
    pc_coaps_server_poll();
    TEST_ASSERT_EQUAL_UINT8(0, pc_coaps_server_active_conns());
    OutDg dg;
    TEST_ASSERT_FALSE(take_out_for("10.0.0.5", 40001, &dg));
}

// When the pool is full a new peer's datagram is dropped: no slot is opened, no reply, no eviction.
static void test_pool_full_rejects_new_peer(void)
{
    for (uint8_t i = 0; i < PC_COAPS_MAX_CONNS; i++)
    {
        char ip[16] = "10.0.1.0";
        ip[7] = (char)('1' + i);
        ingest_real_client_hello(ip, (uint16_t)(1000 + i));
        pc_coaps_server_poll();
    }
    TEST_ASSERT_EQUAL_UINT8(PC_COAPS_MAX_CONNS, pc_coaps_server_active_conns());

    out_reset();
    ingest_real_client_hello("10.0.1.9", 1099); // one peer too many
    pc_coaps_server_poll();
    TEST_ASSERT_EQUAL_UINT8(PC_COAPS_MAX_CONNS, pc_coaps_server_active_conns());
    OutDg dg;
    TEST_ASSERT_FALSE(take_out_for("10.0.1.9", 1099, &dg));
}

// The PTO retransmission ceiling abandons a handshake: with the client Finished never arriving but the peer
// dribbling keepalive traffic (so the idle reaper does not fire first), the flight is re-sent up to
// PC_DTLS_MAX_RETRANSMITS times and then the connection is dropped (RFC 9147 §5.8.1).
static void test_pto_ceiling_frees_slot(void)
{
    ingest_real_client_hello("10.0.0.7", 40003);
    pc_coaps_server_poll();
    TEST_ASSERT_EQUAL_UINT8(1, pc_coaps_server_active_conns());

    for (uint8_t i = 0; i < PC_DTLS_MAX_RETRANSMITS; i++)
    {
        g_ms += PC_DTLS_PTO_MAX_MS + 1; // past any PTO deadline
        out_reset();
        ingest_noop("10.0.0.7", 40003); // refresh the idle clock so the reaper does not preempt the PTO
        pc_coaps_server_poll();
        TEST_ASSERT_EQUAL_UINT8(1, pc_coaps_server_active_conns()); // retransmitted, still handshaking
    }

    g_ms += PC_DTLS_PTO_MAX_MS + 1;
    out_reset();
    ingest_noop("10.0.0.7", 40003);
    pc_coaps_server_poll();
    TEST_ASSERT_EQUAL_UINT8(0, pc_coaps_server_active_conns()); // ceiling hit: handshake abandoned
}

// A CID record whose connection id matches no connection is dropped: slot_by_cid skips the unused slot and
// finds no match, and a CID record never opens a new slot.
static void test_unknown_cid_dropped(void)
{
    const uint8_t client_cid[3] = {0xC1, 0xC2, 0xC3};
    uint8_t scid[PC_DTLS_CID_MAX];
    size_t scid_len = 0;
    DtlsRecordKeys w, r;
    client_handshake("10.0.0.5", 40001, &w, &r, client_cid, sizeof(client_cid), scid, &scid_len);
    TEST_ASSERT_TRUE(scid_len > 0);
    TEST_ASSERT_EQUAL_UINT8(1, pc_coaps_server_active_conns());

    uint8_t unknown[PC_DTLS_CID_MAX];
    memcpy(unknown, scid, scid_len);
    unknown[0] = (uint8_t)(unknown[0] ^ 0xFF); // differs from the negotiated server CID
    uint8_t rec[128];
    size_t n = client_get_temp(&w, 0, rec, sizeof(rec), unknown, scid_len);
    TEST_ASSERT_TRUE(n > 0);

    out_reset();
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(rec, n, "10.9.9.9", 55555));
    pc_coaps_server_poll();
    OutDg dg;
    TEST_ASSERT_FALSE(take_out_for("10.9.9.9", 55555, &dg));    // dropped
    TEST_ASSERT_EQUAL_UINT8(1, pc_coaps_server_active_conns()); // existing connection untouched
}

// server_send is a no-op when no output sink is configured (the host build's sink-unset branch): the
// handshake still proceeds server-side, but nothing is captured.
static void test_server_send_without_sink(void)
{
    pc_coaps_server_set_out_sink_cb(NULL, NULL);
    ingest_real_client_hello("10.0.0.5", 40001);
    pc_coaps_server_poll();
    TEST_ASSERT_EQUAL_UINT8(1, pc_coaps_server_active_conns()); // handshake still opened the slot
    OutDg dg;
    TEST_ASSERT_FALSE(take_out_for("10.0.0.5", 40001, &dg)); // nothing captured: sink was unset
}

// slot_by_peer must find a connection by ip when another used slot shares its port but not its address
// (the scan's port-matches/ip-differs step, not exercised when every peer in a test also differs by port).
static void test_slot_lookup_same_port_different_ip(void)
{
    DtlsRecordKeys wA, rA, wB, rB;
    client_handshake("10.0.2.5", 41000, &wA, &rA, NULL, 0, NULL, NULL);
    client_handshake("10.0.2.6", 41000, &wB, &rB, NULL, 0, NULL, NULL); // same port, different ip
    TEST_ASSERT_EQUAL_UINT8(2, pc_coaps_server_active_conns());

    uint8_t recA[128], recB[128];
    size_t nA = client_get_temp(&wA, 0, recA, sizeof(recA), NULL, 0);
    size_t nB = client_get_temp(&wB, 0, recB, sizeof(recB), NULL, 0);
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(recA, nA, "10.0.2.5", 41000));
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(recB, nB, "10.0.2.6", 41000));
    pc_coaps_server_poll();

    OutDg dgA, dgB;
    TEST_ASSERT_TRUE(take_out_for("10.0.2.5", 41000, &dgA));
    TEST_ASSERT_TRUE(take_out_for("10.0.2.6", 41000, &dgB));
    assert_coap_205(&rA, &dgA, NULL, 0);
    assert_coap_205(&rB, &dgB, NULL, 0);
}

// slot_by_cid must skip a connection with no negotiated CID (sl == 0) while scanning for the one that
// matches, and must reject a CID-tagged datagram too short to carry the negotiated length (sl > avail)
// rather than matching it.
static void test_slot_by_cid_skips_and_bounds(void)
{
    // A plain connection (no CID offered) occupies a slot whose local_cid_len is 0.
    DtlsRecordKeys wPlain, rPlain;
    client_handshake("10.0.3.1", 42001, &wPlain, &rPlain, NULL, 0, NULL, NULL);

    // A second connection negotiates a CID.
    const uint8_t client_cid[3] = {0xA1, 0xA2, 0xA3};
    uint8_t scid[PC_DTLS_CID_MAX];
    size_t scid_len = 0;
    DtlsRecordKeys w, r;
    client_handshake("10.0.3.2", 42002, &w, &r, client_cid, sizeof(client_cid), scid, &scid_len);
    TEST_ASSERT_TRUE(scid_len > 0);
    TEST_ASSERT_EQUAL_UINT8(2, pc_coaps_server_active_conns());

    // A GET protected under the CID connection is routed by its cid, skipping the plain (sl == 0) slot.
    uint8_t rec[128];
    size_t n = client_get_temp(&w, 0, rec, sizeof(rec), scid, scid_len);
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(rec, n, "10.0.3.2", 42002));
    pc_coaps_server_poll();
    OutDg dg;
    TEST_ASSERT_TRUE(take_out_for("10.0.3.2", 42002, &dg));
    assert_coap_205(&r, &dg, client_cid, sizeof(client_cid));

    // A CID-tagged datagram too short to carry any negotiated CID (avail < sl) is dropped, not matched.
    uint8_t tiny[1] = {0x30}; // header byte only: (b0 & 0xE0) == 0x20 && (b0 & 0x10), 0 bytes of cid follow
    out_reset();
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(tiny, sizeof(tiny), "10.9.9.8", 60000));
    pc_coaps_server_poll();
    OutDg none;
    TEST_ASSERT_FALSE(take_out_for("10.9.9.8", 60000, &none));
    TEST_ASSERT_EQUAL_UINT8(2, pc_coaps_server_active_conns()); // unchanged: dropped, no new slot opened
}

// A CID-tagged request from the address the connection already migrated to (or was established from)
// takes the migration check's "no change" path: cid_rec is true but peer_port/peer_ip already match, so
// neither is rewritten.
static void test_cid_no_migration_when_address_unchanged(void)
{
    const uint8_t client_cid[3] = {0xB1, 0xB2, 0xB3};
    uint8_t scid[PC_DTLS_CID_MAX];
    size_t scid_len = 0;
    DtlsRecordKeys w, r;
    client_handshake("10.0.4.1", 43001, &w, &r, client_cid, sizeof(client_cid), scid, &scid_len);
    TEST_ASSERT_TRUE(scid_len > 0);

    uint8_t rec1[128];
    size_t n1 = client_get_temp(&w, 0, rec1, sizeof(rec1), scid, scid_len);
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(rec1, n1, "10.0.4.1", 43001)); // same address as the handshake
    pc_coaps_server_poll();
    OutDg dg1;
    TEST_ASSERT_TRUE(take_out_for("10.0.4.1", 43001, &dg1));
    assert_coap_205(&r, &dg1, client_cid, sizeof(client_cid));
    TEST_ASSERT_EQUAL_UINT8(1, pc_coaps_server_active_conns());
}

// A partial migration - only the ip changes, the port stays the same - still updates peer_ip and routes
// the reply to the new address (the migration check's ip-differs-but-port-matches path).
static void test_cid_migration_same_port_different_ip(void)
{
    const uint8_t client_cid[3] = {0xD1, 0xD2, 0xD3};
    uint8_t scid[PC_DTLS_CID_MAX];
    size_t scid_len = 0;
    DtlsRecordKeys w, r;
    client_handshake("10.0.5.1", 44001, &w, &r, client_cid, sizeof(client_cid), scid, &scid_len);
    TEST_ASSERT_TRUE(scid_len > 0);

    uint8_t rec[128];
    size_t n = client_get_temp(&w, 0, rec, sizeof(rec), scid, scid_len);
    TEST_ASSERT_TRUE(pc_coaps_server_ingest(rec, n, "10.0.5.2", 44001)); // same port, different ip
    pc_coaps_server_poll();

    OutDg dg;
    TEST_ASSERT_TRUE(take_out_for("10.0.5.2", 44001, &dg));
    OutDg stale;
    TEST_ASSERT_FALSE(take_out_for("10.0.5.1", 44001, &stale));
    assert_coap_205(&r, &dg, client_cid, sizeof(client_cid));
    TEST_ASSERT_EQUAL_UINT8(1, pc_coaps_server_active_conns());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_server_single_peer);
    RUN_TEST(test_two_peers_routing);
    RUN_TEST(test_idle_reap);
    RUN_TEST(test_pto_retransmit_driven_by_poll);
    RUN_TEST(test_cid_address_migration);
    RUN_TEST(test_begin_rejects_invalid_cfg);
    RUN_TEST(test_poll_when_stopped);
    RUN_TEST(test_ingest_rejects_bad_len);
    RUN_TEST(test_ingest_ring_full);
    RUN_TEST(test_ingest_addr_copy_edges);
    RUN_TEST(test_malformed_peer_addr);
    RUN_TEST(test_fatal_handshake_frees_slot);
    RUN_TEST(test_pool_full_rejects_new_peer);
    RUN_TEST(test_pto_ceiling_frees_slot);
    RUN_TEST(test_unknown_cid_dropped);
    RUN_TEST(test_server_send_without_sink);
    RUN_TEST(test_slot_lookup_same_port_different_ip);
    RUN_TEST(test_slot_by_cid_skips_and_bounds);
    RUN_TEST(test_cid_no_migration_when_address_unchanged);
    RUN_TEST(test_cid_migration_same_port_different_ip);
    return UNITY_END();
}
