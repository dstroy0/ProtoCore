// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/asymmetric/curve25519/curve25519.h"
#include "crypto/asymmetric/ed25519/ed25519.h"
#include "crypto/hash/sha256/sha256.h"
#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"
#include "network_drivers/presentation/security/dtls/dtls_conn/dtls_conn.h"
#include "network_drivers/presentation/security/dtls/dtls_handshake/dtls_handshake.h"
#include "network_drivers/presentation/security/dtls/dtls_record/dtls_record.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include "network_drivers/transport/udp/server/server.h" // UdpListener: what the server binds and what drains it
#include "server/clock/clock.h"
#include "services/iot/coap/coap/coap.h"
#include "services/iot/coap/coaps_server/coaps_server.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

static uint8_t dtls_handshake_work[16]; // the borrow an entry takes; DtlsHandshake never reads it

static uint8_t dtls_record_work[16]; // the borrow an entry takes; DtlsRecord never reads it

static uint8_t tw[4096];
static uint8_t tw_tr[4096];

static const uint8_t SERVER_ED_SEED[32] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                           17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
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

static uint32_t g_ms = 0;
static uint32_t test_clock()
{
    return g_ms;
}

static uint8_t g_rng_ctr = 0;
static void test_rng(uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        out[i] = g_rng_ctr++;
    }
}

typedef struct
{
    uint8_t buf[2048];
    size_t len;
    char ip[16];
    uint16_t port;
} OutDg;
// Three seams, one per arm, so every case below reads the same either way: reset the outbound
// capture, take what the server sent to a peer, put one datagram in, and turn the loop once.

#if PROTOCORE_HAS_NET_STACK

// The server binds a UdpListener, so a datagram goes in through the host UDP driver - which calls
// the recv callback the core armed - and a reply is read out of that driver's send capture. Both
// halves are the path the target runs.
static void out_reset()
{
    protocore_net_host_udp_reset();
}

// The datagram the server sent to ip:port, oldest first, taken out of the capture.
static proto_bool take_out_for(const char *ip, uint16_t port, OutDg *dst)
{
    protocore_net_ip want;
    memset(&want, 0, sizeof want);
    protocore_net_ip_parse(ip, &want);
    for (size_t i = 0; i < protocore_net_host_dgram_n; i++)
    {
        const protocore_net_host_dgram *d = &protocore_net_host_dgrams[i];
        if (d->dst_port != port || memcmp(d->addr, want.bytes, sizeof d->addr) != 0)
        {
            continue;
        }
        memcpy(dst->buf, d->data, d->len);
        dst->len = d->len;
        dst->port = port;
        size_t k = 0;
        while (ip[k] && k < sizeof dst->ip - 1)
        {
            dst->ip[k] = ip[k];
            k++;
        }
        dst->ip[k] = 0;
        for (size_t j = i; j + 1 < protocore_net_host_dgram_n; j++)
        {
            protocore_net_host_dgrams[j] = protocore_net_host_dgrams[j + 1];
        }
        protocore_net_host_dgram_n--;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// One datagram from ip:port into the port the server bound.
static proto_bool ingest_dgram(const uint8_t *data, size_t len, const char *ip, uint16_t port)
{
    return protocore_net_host_udp_deliver(CoapsServer.bind.port, ip, port, (void *)(uintptr_t)data, (uint16_t)len)
               ? PROTO_TRUE
               : PROTO_FALSE;
}

// One turn of the loop, in the order the loop takes it: handle() takes the clock reading its whole
// pass compares against (Clock.ms is that stamp, not a read of the source), then the listener's ring
// is drained into the server's, then the server services what that left it.
static void pump(void)
{
    Clock.millis(Clock.internal);
    UdpListener.poll(protocore_udp_listener_span());
    CoapsServer.poll(protocore_coaps_server_span());
}

#else

// There is no receive path, so a datagram goes in through ingest and a reply comes back through the
// sink this suite installs. The sink is where ingest's own refusals become observable.
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

static proto_bool ingest_dgram(const uint8_t *data, size_t len, const char *ip, uint16_t port)
{
    CoapsServer.dgram.data = data;
    CoapsServer.dgram.len = len;
    CoapsServer.dgram.ip = ip;
    CoapsServer.dgram.port = port;
    CoapsServer.ingest(protocore_coaps_server_span());
    return CoapsServer.ok;
}

static void pump(void)
{
    Clock.millis(Clock.internal);
    CoapsServer.poll(protocore_coaps_server_span());
}

#endif

static uint8_t g_server_cert[32];

void setUp()
{
    Coap.reset(protocore_coap_span());
    Coap.resource.path = "/temp";
    Coap.resource.methods = COAP_ALLOW_GET;
    Coap.resource.handler = h_temp;
    Coap.add_resource(protocore_coap_span());
    Clock.src.fn = test_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    g_ms = 0;
    g_rng_ctr = 0;
    out_reset();

    Ed25519.pubkey_args.pub = g_server_cert;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    memset(&CoapsServer.identity, 0, sizeof CoapsServer.identity);
    CoapsServer.identity.cert_der = g_server_cert;
    CoapsServer.identity.cert_len = 32;
    memcpy(CoapsServer.identity.ed25519_seed, SERVER_ED_SEED, 32);
    memcpy(CoapsServer.identity.cookie_key, SERVER_COOKIE_KEY, 32);
    CoapsServer.identity.rng = test_rng;
    CoapsServer.bind.port = PROTOCORE_COAPS_PORT;
    CoapsServer.begin(protocore_coaps_server_span());
    TEST_ASSERT_TRUE(CoapsServer.ok);
#if !PROTOCORE_HAS_NET_STACK
    CoapsServer.sink.fn = out_sink;
    CoapsServer.sink.ctx = NULL;
    CoapsServer.set_out_sink(protocore_coaps_server_span());
#endif
}
void tearDown()
{
    CoapsServer.stop(protocore_coaps_server_span());
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 0;
    Clock.set_ms(Clock.internal);
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

static size_t build_client_hello(uint8_t *out, const uint8_t client_pub[32], const uint8_t *cid, size_t cid_len)
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
    if (cid)
    {
        b16(&b, 0x0036);
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
    DtlsHandshake.header_parse_args.p = payload;
    DtlsHandshake.header_parse_args.len = plen;
    DtlsHandshake.header_parse_args.out = &hh;
    DtlsHandshake.header_parse(dtls_handshake_work);
    if (!DtlsHandshake.n || hh.frag_offset != 0 || hh.frag_length != hh.length)
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

static void client_handshake(const char *ip, uint16_t port, DtlsRecordKeys *cli_app_write, DtlsRecordKeys *cli_app_read,
                             const uint8_t *client_cid, size_t client_cid_len, uint8_t *scid_out, size_t *scid_len_out)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub, client_cid, client_cid_len);
    uint8_t *tr;
    tr = tw_tr;
    Sha256.init(tr);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = ch_len;
    Sha256.update(tr);
    uint8_t ch_frag[300];
    DtlsHandshake.frag_build_args.msg_type = ch[0];
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.out = ch_frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(ch_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t ch_fl = DtlsHandshake.n;
    uint8_t ch_rec[320];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = ch_frag;
    DtlsRecord.plaintext_build_args.frag_len = ch_fl;
    DtlsRecord.plaintext_build_args.out = ch_rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(ch_rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t ch_rl = DtlsRecord.n;
    TEST_ASSERT_TRUE(ingest_dgram(ch_rec, ch_rl, ip, port));
    pump();

    OutDg fldg;
    TEST_ASSERT_TRUE(take_out_for(ip, port, &fldg));
    const uint8_t *flight = fldg.buf;
    size_t fl = fldg.len;

    size_t off = 0;
    DtlsPlaintext pt;
    DtlsRecord.plaintext_parse_args.rec = flight;
    DtlsRecord.plaintext_parse_args.rec_len = fl;
    DtlsRecord.plaintext_parse_args.out = &pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    size_t rl = DtlsRecord.n;
    TEST_ASSERT_TRUE(rl > 0);
    off += rl;
    uint8_t sh[512];
    size_t sh_len = frag_to_tls(pt.fragment, pt.frag_len, sh);
    TEST_ASSERT_TRUE(sh_len > 0);
    Sha256.update_args.data = sh;
    Sha256.update_args.len = sh_len;
    Sha256.update(tr);
    uint8_t server_pub[32];
    TEST_ASSERT_TRUE(sh_keyshare(sh, sh_len, server_pub));

    uint8_t scid[PROTOCORE_DTLS_CID_MAX];
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
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    Tls13KeySchedule cks;
    uint8_t hh[32];
    Sha256.final_args.out = hh;
    Sha256.final(tr);
    static uint8_t ks_store_372[PROTOCORE_TLS13_KS_BORROW];
    Tls13Ks.bind.kdf = &DTLS13_KDF;
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.bind.s = ks_store_372;
    Tls13Ks.early(NULL);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = 32;
    Tls13Ks.step.ch_sh_hash = hh;
    Tls13Ks.handshake(NULL);
    DtlsRecordKeys srv_read;
    DtlsRecord.keys_derive_args.out = &srv_read;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 2;
    DtlsRecord.keys_derive_args.secret = cks.s + TLS13_KS_SERVER_HS;
    DtlsRecord.keys_derive(dtls_record_work);

    uint64_t exp_seq = 0;
    while (off < fl)
    {
        size_t crl = ct_record_len(flight + off, fl - off, client_cid_len);
        TEST_ASSERT_TRUE(crl > 0);
        uint8_t inner[512];
        DtlsCiphertext info;
        DtlsRecord.unprotect_args.keys = &srv_read;
        DtlsRecord.unprotect_args.next_seq = exp_seq;
        DtlsRecord.unprotect_args.rec = flight + off;
        DtlsRecord.unprotect_args.rec_len = crl;
        DtlsRecord.unprotect_args.out = inner;
        DtlsRecord.unprotect_args.out_cap = sizeof(inner);
        DtlsRecord.unprotect_args.info = &info;
        DtlsRecord.unprotect_args.expected_cid = client_cid;
        DtlsRecord.unprotect_args.expected_cid_len = client_cid_len;
        DtlsRecord.unprotect(dtls_record_work);
        TEST_ASSERT_TRUE(DtlsRecord.ok);
        exp_seq = info.seq + 1;
        off += crl;
        uint8_t msg[512];
        size_t mlen = frag_to_tls(inner, info.pt_len, msg);
        TEST_ASSERT_TRUE(mlen > 0);
        Sha256.update_args.data = msg;
        Sha256.update_args.len = mlen;
        Sha256.update(tr);
    }

    uint8_t h_sfin[32];
    Sha256.final_args.out = h_sfin;
    Sha256.final(tr);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ch_sfin_hash = h_sfin;
    Tls13Ks.master(NULL);
    uint8_t cfin_verify[32];
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.finished_args.base_secret = cks.s + TLS13_KS_CLIENT_HS;
    Tls13Ks.finished_args.transcript_hash = h_sfin;
    Tls13Ks.finished_args.out = cfin_verify;
    Tls13Ks.finished_mac(NULL);
    uint8_t cfin[64];
    size_t cfin_len = protocore_tls13_build_finished(cfin, sizeof(cfin), cfin_verify, 32);
    DtlsRecordKeys cli_write;
    DtlsRecord.keys_derive_args.out = &cli_write;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 2;
    DtlsRecord.keys_derive_args.secret = cks.s + TLS13_KS_CLIENT_HS;
    DtlsRecord.keys_derive(dtls_record_work);
    uint8_t cfin_frag[80];
    DtlsHandshake.frag_build_args.msg_type = cfin[0];
    DtlsHandshake.frag_build_args.msg_seq = 1;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(cfin_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = cfin + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(cfin_len - 4);
    DtlsHandshake.frag_build_args.out = cfin_frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(cfin_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t cff = DtlsHandshake.n;
    uint8_t cfin_rec[128];
    DtlsRecord.protect_args.keys = &cli_write;
    DtlsRecord.protect_args.seq = 0;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.protect_args.plaintext = cfin_frag;
    DtlsRecord.protect_args.pt_len = cff;
    DtlsRecord.protect_args.out = cfin_rec;
    DtlsRecord.protect_args.out_cap = sizeof(cfin_rec);
    DtlsRecord.protect_args.cid = scid_len ? scid : NULL;
    DtlsRecord.protect_args.cid_len = scid_len;
    DtlsRecord.protect(dtls_record_work);
    size_t cfr = DtlsRecord.n;
    TEST_ASSERT_TRUE(ingest_dgram(cfin_rec, cfr, ip, port));
    pump();

    OutDg ackdg;
    take_out_for(ip, port, &ackdg);

    DtlsRecord.keys_derive_args.out = cli_app_read;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 3;
    DtlsRecord.keys_derive_args.secret = cks.s + TLS13_KS_SERVER_AP;
    DtlsRecord.keys_derive(dtls_record_work);
    DtlsRecord.keys_derive_args.out = cli_app_write;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 3;
    DtlsRecord.keys_derive_args.secret = cks.s + TLS13_KS_CLIENT_AP;
    DtlsRecord.keys_derive(dtls_record_work);
}

static size_t client_get_temp(DtlsRecordKeys *w, uint64_t cseq, uint8_t *out, size_t cap, const uint8_t *cid,
                              size_t cid_len)
{
    const uint8_t coap_get[] = {0x40, 0x01, 0x12, 0x34, 0xB4, 't', 'e', 'm', 'p'};
    DtlsRecord.protect_args.keys = w;
    DtlsRecord.protect_args.seq = cseq;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = coap_get;
    DtlsRecord.protect_args.pt_len = sizeof(coap_get);
    DtlsRecord.protect_args.out = out;
    DtlsRecord.protect_args.out_cap = cap;
    DtlsRecord.protect_args.cid = cid;
    DtlsRecord.protect_args.cid_len = cid_len;
    DtlsRecord.protect(dtls_record_work);
    return DtlsRecord.n;
}

static void assert_coap_205(DtlsRecordKeys *r, const OutDg *dg, const uint8_t *cid, size_t cid_len)
{
    uint8_t coap_resp[256];
    DtlsCiphertext info;
    DtlsRecord.unprotect_args.keys = r;
    DtlsRecord.unprotect_args.next_seq = 1;
    DtlsRecord.unprotect_args.rec = dg->buf;
    DtlsRecord.unprotect_args.rec_len = dg->len;
    DtlsRecord.unprotect_args.out = coap_resp;
    DtlsRecord.unprotect_args.out_cap = sizeof(coap_resp);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = cid;
    DtlsRecord.unprotect_args.expected_cid_len = cid_len;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DTLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_TRUE(info.pt_len >= 6);
    TEST_ASSERT_EQUAL_UINT8(0x60, coap_resp[0] & 0xF0);
    TEST_ASSERT_EQUAL_UINT8(0x45, coap_resp[1]);
    TEST_ASSERT_EQUAL_UINT8(0x12, coap_resp[2]);
    TEST_ASSERT_EQUAL_UINT8(0x34, coap_resp[3]);
    TEST_ASSERT_EQUAL_MEMORY("hi", coap_resp + info.pt_len - 2, 2);
}

static void test_server_single_peer(void)
{
    DtlsRecordKeys w, r;
    client_handshake("10.0.0.5", 40001, &w, &r, NULL, 0, NULL, NULL);
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, CoapsServer.u8);

    uint8_t rec[128];
    size_t n = client_get_temp(&w, 0, rec, sizeof(rec), NULL, 0);
    TEST_ASSERT_TRUE(ingest_dgram(rec, n, "10.0.0.5", 40001));
    pump();

    OutDg dg;
    TEST_ASSERT_TRUE(take_out_for("10.0.0.5", 40001, &dg));
    assert_coap_205(&r, &dg, NULL, 0);
}

static void test_two_peers_routing(void)
{
    DtlsRecordKeys wA, rA, wB, rB;
    client_handshake("10.0.0.5", 40001, &wA, &rA, NULL, 0, NULL, NULL);
    client_handshake("10.0.0.6", 40002, &wB, &rB, NULL, 0, NULL, NULL);
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(2, CoapsServer.u8);

    uint8_t recA[128], recB[128];
    size_t nA = client_get_temp(&wA, 0, recA, sizeof(recA), NULL, 0);
    size_t nB = client_get_temp(&wB, 0, recB, sizeof(recB), NULL, 0);
    TEST_ASSERT_TRUE(ingest_dgram(recB, nB, "10.0.0.6", 40002));
    TEST_ASSERT_TRUE(ingest_dgram(recA, nA, "10.0.0.5", 40001));
    pump();

    OutDg dgA, dgB;
    TEST_ASSERT_TRUE(take_out_for("10.0.0.5", 40001, &dgA));
    TEST_ASSERT_TRUE(take_out_for("10.0.0.6", 40002, &dgB));
    assert_coap_205(&rA, &dgA, NULL, 0);
    assert_coap_205(&rB, &dgB, NULL, 0);
}

static void test_idle_reap(void)
{
    DtlsRecordKeys w, r;
    client_handshake("10.0.0.5", 40001, &w, &r, NULL, 0, NULL, NULL);
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, CoapsServer.u8);

    g_ms += PROTOCORE_COAPS_IDLE_MS + 1;
    pump();
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(0, CoapsServer.u8);
}

static void test_pto_retransmit_driven_by_poll(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub, NULL, 0);
    uint8_t ch_frag[300];
    DtlsHandshake.frag_build_args.msg_type = ch[0];
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.out = ch_frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(ch_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t ch_fl = DtlsHandshake.n;
    uint8_t ch_rec[320];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = ch_frag;
    DtlsRecord.plaintext_build_args.frag_len = ch_fl;
    DtlsRecord.plaintext_build_args.out = ch_rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(ch_rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t ch_rl = DtlsRecord.n;
    TEST_ASSERT_TRUE(ingest_dgram(ch_rec, ch_rl, "10.0.0.7", 40003));
    pump();

    OutDg f1;
    TEST_ASSERT_TRUE(take_out_for("10.0.0.7", 40003, &f1));

    g_ms += PROTOCORE_DTLS_PTO_INITIAL_MS - 1;
    pump();
    OutDg none;
    TEST_ASSERT_FALSE(take_out_for("10.0.0.7", 40003, &none));

    g_ms += 1;
    pump();
    OutDg f2;
    TEST_ASSERT_TRUE(take_out_for("10.0.0.7", 40003, &f2));
    TEST_ASSERT_TRUE(f2.len > 0);
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, CoapsServer.u8);
}

static void test_cid_address_migration(void)
{
    const uint8_t client_cid[3] = {0xC1, 0xC2, 0xC3};
    uint8_t scid[PROTOCORE_DTLS_CID_MAX];
    size_t scid_len = 0;
    DtlsRecordKeys w, r;
    client_handshake("10.0.0.5", 40001, &w, &r, client_cid, sizeof(client_cid), scid, &scid_len);
    TEST_ASSERT_TRUE(scid_len > 0);
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, CoapsServer.u8);

    uint8_t rec[128];
    size_t n = client_get_temp(&w, 0, rec, sizeof(rec), scid, scid_len);
    TEST_ASSERT_TRUE(ingest_dgram(rec, n, "10.9.9.9", 55555));
    pump();

    OutDg dg;
    TEST_ASSERT_TRUE(take_out_for("10.9.9.9", 55555, &dg));
    OutDg stale;
    TEST_ASSERT_FALSE(take_out_for("10.0.0.5", 40001, &stale));
    assert_coap_205(&r, &dg, client_cid, sizeof(client_cid));
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, CoapsServer.u8);
}

static void ingest_real_client_hello(const char *ip, uint16_t port)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub, NULL, 0);
    uint8_t ch_frag[300];
    DtlsHandshake.frag_build_args.msg_type = ch[0];
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.out = ch_frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(ch_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t ch_fl = DtlsHandshake.n;
    uint8_t ch_rec[320];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = ch_frag;
    DtlsRecord.plaintext_build_args.frag_len = ch_fl;
    DtlsRecord.plaintext_build_args.out = ch_rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(ch_rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t ch_rl = DtlsRecord.n;
    TEST_ASSERT_TRUE(ingest_dgram(ch_rec, ch_rl, ip, port));
}

static void ingest_bad_client_hello(const char *ip, uint16_t port)
{
    uint8_t garbage[8] = {0};
    uint8_t frag[64];
    DtlsHandshake.frag_build_args.msg_type = 0x01;
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)sizeof(garbage);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = garbage;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)sizeof(garbage);
    DtlsHandshake.frag_build_args.out = frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t fl = DtlsHandshake.n;
    uint8_t rec[128];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = frag;
    DtlsRecord.plaintext_build_args.frag_len = fl;
    DtlsRecord.plaintext_build_args.out = rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t rl = DtlsRecord.n;
    ingest_dgram(rec, rl, ip, port);
}

static void ingest_noop(const char *ip, uint16_t port)
{
    uint8_t junk[1] = {0x16};
    ingest_dgram(junk, sizeof(junk), ip, port);
}

static void test_begin_rejects_invalid_cfg(void)
{
    CoapsServer.bind.port = PROTOCORE_COAPS_PORT;

    memset(&CoapsServer.identity, 0, sizeof CoapsServer.identity);
    CoapsServer.begin(protocore_coaps_server_span());
    TEST_ASSERT_FALSE(CoapsServer.ok);

    memset(&CoapsServer.identity, 0, sizeof CoapsServer.identity);
    CoapsServer.identity.cert_der = g_server_cert;
    CoapsServer.identity.cert_len = 32;
    CoapsServer.identity.rng = NULL;
    CoapsServer.begin(protocore_coaps_server_span());
    TEST_ASSERT_FALSE(CoapsServer.ok);

    memset(&CoapsServer.identity, 0, sizeof CoapsServer.identity);
    CoapsServer.identity.rng = test_rng;
    CoapsServer.identity.cert_der = NULL;
    CoapsServer.identity.cert_len = 32;
    CoapsServer.begin(protocore_coaps_server_span());
    TEST_ASSERT_FALSE(CoapsServer.ok);

    memset(&CoapsServer.identity, 0, sizeof CoapsServer.identity);
    CoapsServer.identity.rng = test_rng;
    CoapsServer.identity.cert_der = g_server_cert;
    CoapsServer.identity.cert_len = 0;
    CoapsServer.begin(protocore_coaps_server_span());
    TEST_ASSERT_FALSE(CoapsServer.ok);

    memset(&CoapsServer.identity, 0, sizeof CoapsServer.identity);
    CoapsServer.identity.cert_der = g_server_cert;
    CoapsServer.identity.cert_len = 32;
    CoapsServer.identity.rng = test_rng;
    memcpy(CoapsServer.identity.ed25519_seed, SERVER_ED_SEED, 32);
    memcpy(CoapsServer.identity.cookie_key, SERVER_COOKIE_KEY, 32);
    CoapsServer.bind.port = 0;
    CoapsServer.begin(protocore_coaps_server_span());
    TEST_ASSERT_TRUE(CoapsServer.ok);
}

static void test_poll_when_stopped(void)
{
    CoapsServer.stop(protocore_coaps_server_span());
    pump();
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(0, CoapsServer.u8);
}

#if PROTOCORE_HAS_NET_STACK

// ring_push refuses a zero-length datagram on both arms, but here its answer is discarded in
// udp_ingest_cb, so what is asserted is the consequence: nothing was queued, so no slot opens and
// the peer is never written to.
//
// Only the zero-length half. The over-long half cannot arise on this arm: the stack stages a
// datagram into PROTOCORE_UDP_RX_BUF_SIZE (1472) bytes, which is already under the server's own
// PROTOCORE_COAPS_MAX_DATAGRAM (1500), so nothing over-long ever reaches ring_push. That refusal is
// covered on the arm where the caller states the length, in native_coaps_server_nostack.
static void test_a_zero_length_datagram_is_dropped(void)
{
    uint8_t d[8] = {0};
    ingest_dgram(d, 0, "10.0.0.5", 1);
    pump();

    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(0, CoapsServer.u8);
    OutDg dg;
    TEST_ASSERT_FALSE(take_out_for("10.0.0.5", 1, &dg));
}

#else

// The refusals ring_push makes, read off the return the injection seam reports. Only this arm has
// that return: with a network stack the datagram arrives through udp_ingest_cb, which discards it.
static void test_ingest_rejects_bad_len(void)
{
    uint8_t d[8] = {0};
    TEST_ASSERT_FALSE(ingest_dgram(d, 0, "10.0.0.5", 1));
    static uint8_t big[2000];
    memset(big, 0, sizeof big);
    TEST_ASSERT_FALSE(ingest_dgram(big, sizeof big, "10.0.0.5", 1));
}

static void test_ingest_ring_full(void)
{
    uint8_t d[8] = {0x16, 0, 0, 0, 0, 0, 0, 0};
    int pushed = 0;
    for (int i = 0; i < PROTOCORE_COAPS_INGEST_RING + 3; i++)
    {
        ingest_dgram(d, sizeof d, "10.0.0.5", 1);
        if (CoapsServer.ok)
        {
            pushed++;
        }
    }
    TEST_ASSERT_EQUAL_INT(PROTOCORE_COAPS_INGEST_RING - 1, pushed);
}

// The two ends of the address ring_push copies: no address at all is refused rather than copied,
// and one longer than the entry holds is taken truncated rather than refused.
static void test_ingest_addr_copy_edges(void)
{
    uint8_t d[8] = {0x16, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(ingest_dgram(d, sizeof d, NULL, 1));
    TEST_ASSERT_TRUE(ingest_dgram(d, sizeof d, "111.111.111.111.111.111", 2));
}

static void test_malformed_peer_addr(void)
{
    const char *bad[] = {
        "999.0.0.1", "10..0.1", "10.0.x.1", "1.2.3", "0001.0.0.1",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        ingest_bad_client_hello(bad[i], (uint16_t)(50000 + i));
        pump();
        CoapsServer.active_conns(protocore_coaps_server_span());
        TEST_ASSERT_EQUAL_UINT8(0, CoapsServer.u8);
    }
}

#endif

static void test_fatal_handshake_frees_slot(void)
{
    ingest_bad_client_hello("10.0.0.5", 40001);
    pump();
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(0, CoapsServer.u8);
    OutDg dg;
    TEST_ASSERT_FALSE(take_out_for("10.0.0.5", 40001, &dg));
}

static void test_pool_full_rejects_new_peer(void)
{
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        char ip[16] = "10.0.1.0";
        ip[7] = (char)('1' + i);
        ingest_real_client_hello(ip, (uint16_t)(1000 + i));
        pump();
    }
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_COAPS_MAX_CONNS, CoapsServer.u8);

    out_reset();
    ingest_real_client_hello("10.0.1.9", 1099);
    pump();
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_COAPS_MAX_CONNS, CoapsServer.u8);
    OutDg dg;
    TEST_ASSERT_FALSE(take_out_for("10.0.1.9", 1099, &dg));
}

static void test_pto_ceiling_frees_slot(void)
{
    ingest_real_client_hello("10.0.0.7", 40003);
    pump();
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, CoapsServer.u8);

    for (uint8_t i = 0; i < PROTOCORE_DTLS_MAX_RETRANSMITS; i++)
    {
        g_ms += PROTOCORE_DTLS_PTO_MAX_MS + 1;
        out_reset();
        ingest_noop("10.0.0.7", 40003);
        pump();
        CoapsServer.active_conns(protocore_coaps_server_span());
        TEST_ASSERT_EQUAL_UINT8(1, CoapsServer.u8);
    }

    g_ms += PROTOCORE_DTLS_PTO_MAX_MS + 1;
    out_reset();
    ingest_noop("10.0.0.7", 40003);
    pump();
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(0, CoapsServer.u8);
}

static void test_unknown_cid_dropped(void)
{
    const uint8_t client_cid[3] = {0xC1, 0xC2, 0xC3};
    uint8_t scid[PROTOCORE_DTLS_CID_MAX];
    size_t scid_len = 0;
    DtlsRecordKeys w, r;
    client_handshake("10.0.0.5", 40001, &w, &r, client_cid, sizeof(client_cid), scid, &scid_len);
    TEST_ASSERT_TRUE(scid_len > 0);
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, CoapsServer.u8);

    uint8_t unknown[PROTOCORE_DTLS_CID_MAX];
    memcpy(unknown, scid, scid_len);
    unknown[0] = (uint8_t)(unknown[0] ^ 0xFF);
    uint8_t rec[128];
    size_t n = client_get_temp(&w, 0, rec, sizeof(rec), unknown, scid_len);
    TEST_ASSERT_TRUE(n > 0);

    out_reset();
    TEST_ASSERT_TRUE(ingest_dgram(rec, n, "10.9.9.9", 55555));
    pump();
    OutDg dg;
    TEST_ASSERT_FALSE(take_out_for("10.9.9.9", 55555, &dg));
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, CoapsServer.u8);
}

#if PROTOCORE_HAS_NET_STACK
// A send the stack refuses leaves the slot where it was: the handshake is still up and the peer just
// sees nothing, which is what a lost datagram looks like to it.
static void test_a_refused_send_keeps_the_slot(void)
{
    mock_udp_send_fail_after(0);
    ingest_real_client_hello("10.0.0.5", 40001);
    pump();
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, CoapsServer.u8);
    OutDg dg;
    TEST_ASSERT_FALSE(take_out_for("10.0.0.5", 40001, &dg));
}
#endif

static void test_slot_lookup_same_port_different_ip(void)
{
    DtlsRecordKeys wA, rA, wB, rB;
    client_handshake("10.0.2.5", 41000, &wA, &rA, NULL, 0, NULL, NULL);
    client_handshake("10.0.2.6", 41000, &wB, &rB, NULL, 0, NULL, NULL);
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(2, CoapsServer.u8);

    uint8_t recA[128], recB[128];
    size_t nA = client_get_temp(&wA, 0, recA, sizeof(recA), NULL, 0);
    size_t nB = client_get_temp(&wB, 0, recB, sizeof(recB), NULL, 0);
    TEST_ASSERT_TRUE(ingest_dgram(recA, nA, "10.0.2.5", 41000));
    TEST_ASSERT_TRUE(ingest_dgram(recB, nB, "10.0.2.6", 41000));
    pump();

    OutDg dgA, dgB;
    TEST_ASSERT_TRUE(take_out_for("10.0.2.5", 41000, &dgA));
    TEST_ASSERT_TRUE(take_out_for("10.0.2.6", 41000, &dgB));
    assert_coap_205(&rA, &dgA, NULL, 0);
    assert_coap_205(&rB, &dgB, NULL, 0);
}

static void test_slot_by_cid_skips_and_bounds(void)
{

    DtlsRecordKeys wPlain, rPlain;
    client_handshake("10.0.3.1", 42001, &wPlain, &rPlain, NULL, 0, NULL, NULL);

    const uint8_t client_cid[3] = {0xA1, 0xA2, 0xA3};
    uint8_t scid[PROTOCORE_DTLS_CID_MAX];
    size_t scid_len = 0;
    DtlsRecordKeys w, r;
    client_handshake("10.0.3.2", 42002, &w, &r, client_cid, sizeof(client_cid), scid, &scid_len);
    TEST_ASSERT_TRUE(scid_len > 0);
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(2, CoapsServer.u8);

    uint8_t rec[128];
    size_t n = client_get_temp(&w, 0, rec, sizeof(rec), scid, scid_len);
    TEST_ASSERT_TRUE(ingest_dgram(rec, n, "10.0.3.2", 42002));
    pump();
    OutDg dg;
    TEST_ASSERT_TRUE(take_out_for("10.0.3.2", 42002, &dg));
    assert_coap_205(&r, &dg, client_cid, sizeof(client_cid));

    uint8_t tiny[1] = {0x30};
    out_reset();
    TEST_ASSERT_TRUE(ingest_dgram(tiny, sizeof(tiny), "10.9.9.8", 60000));
    pump();
    OutDg none;
    TEST_ASSERT_FALSE(take_out_for("10.9.9.8", 60000, &none));
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(2, CoapsServer.u8);
}

static void test_cid_no_migration_when_address_unchanged(void)
{
    const uint8_t client_cid[3] = {0xB1, 0xB2, 0xB3};
    uint8_t scid[PROTOCORE_DTLS_CID_MAX];
    size_t scid_len = 0;
    DtlsRecordKeys w, r;
    client_handshake("10.0.4.1", 43001, &w, &r, client_cid, sizeof(client_cid), scid, &scid_len);
    TEST_ASSERT_TRUE(scid_len > 0);

    uint8_t rec1[128];
    size_t n1 = client_get_temp(&w, 0, rec1, sizeof(rec1), scid, scid_len);
    TEST_ASSERT_TRUE(ingest_dgram(rec1, n1, "10.0.4.1", 43001));
    pump();
    OutDg dg1;
    TEST_ASSERT_TRUE(take_out_for("10.0.4.1", 43001, &dg1));
    assert_coap_205(&r, &dg1, client_cid, sizeof(client_cid));
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, CoapsServer.u8);
}

static void test_cid_migration_same_port_different_ip(void)
{
    const uint8_t client_cid[3] = {0xD1, 0xD2, 0xD3};
    uint8_t scid[PROTOCORE_DTLS_CID_MAX];
    size_t scid_len = 0;
    DtlsRecordKeys w, r;
    client_handshake("10.0.5.1", 44001, &w, &r, client_cid, sizeof(client_cid), scid, &scid_len);
    TEST_ASSERT_TRUE(scid_len > 0);

    uint8_t rec[128];
    size_t n = client_get_temp(&w, 0, rec, sizeof(rec), scid, scid_len);
    TEST_ASSERT_TRUE(ingest_dgram(rec, n, "10.0.5.2", 44001));
    pump();

    OutDg dg;
    TEST_ASSERT_TRUE(take_out_for("10.0.5.2", 44001, &dg));
    OutDg stale;
    TEST_ASSERT_FALSE(take_out_for("10.0.5.1", 44001, &stale));
    assert_coap_205(&r, &dg, client_cid, sizeof(client_cid));
    CoapsServer.active_conns(protocore_coaps_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, CoapsServer.u8);
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
#if PROTOCORE_HAS_NET_STACK
    RUN_TEST(test_a_zero_length_datagram_is_dropped);
#else
    RUN_TEST(test_ingest_rejects_bad_len);
    RUN_TEST(test_ingest_ring_full);
    RUN_TEST(test_ingest_addr_copy_edges);
    RUN_TEST(test_malformed_peer_addr);
#endif
    RUN_TEST(test_fatal_handshake_frees_slot);
    RUN_TEST(test_pool_full_rejects_new_peer);
    RUN_TEST(test_pto_ceiling_frees_slot);
    RUN_TEST(test_unknown_cid_dropped);
#if PROTOCORE_HAS_NET_STACK
    RUN_TEST(test_a_refused_send_keeps_the_slot);
#endif
    RUN_TEST(test_slot_lookup_same_port_different_ip);
    RUN_TEST(test_slot_by_cid_skips_and_bounds);
    RUN_TEST(test_cid_no_migration_when_address_unchanged);
    RUN_TEST(test_cid_migration_same_port_different_ip);
    return UNITY_END();
}
