// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/asymmetric/curve25519/curve25519.h"
#include "crypto/hash/sha256/sha256.h"
#include "network_drivers/presentation/http/http3/h3_conn/h3_conn.h"
#include "network_drivers/presentation/http/http3/h3_frame/h3_frame.h"
#include "network_drivers/presentation/http/http3/qpack/qpack.h"
#include "network_drivers/presentation/http/http3/quic_crypto/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_frame/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_server/quic_server.h"
#include "network_drivers/presentation/http/http3/quic_tp/quic_tp.h"
#include "network_drivers/presentation/http/http3/quic_varint/quic_varint.h"
#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include "network_drivers/transport/udp/server/server.h"
#include "network_drivers/transport/udp/udp.h"
#include "protocore_net_host.h"
#include <string.h>

#include <unity.h>

static uint8_t qpack_work[16]; // the borrow an entry takes; Qpack never reads it

static uint8_t quic_crypto_work[16]; // the borrow an entry takes; QuicCrypto never reads it

static uint8_t quic_packet_work[16]; // the borrow an entry takes; QuicPacket never reads it

static uint8_t quic_frame_work[16]; // the borrow an entry takes; QuicFrame never reads it

static uint8_t quic_tp_work[16]; // the borrow an entry takes; QuicTp never reads it

static uint8_t h3_frame_work[16]; // the borrow an entry takes; H3Frame never reads it

static uint8_t quic_varint_work[16]; // the borrow an entry takes; QuicVarint never reads it

static uint8_t tw[4096];
static uint8_t tw_t[4096];

void setUp()
{
    protocore_net_host_reset();
}
void tearDown()
{
}

#define H3_PORT 443

static const uint8_t CERT[48] = {0x30, 0x2e, 0x02, 0x01, 0x02};
static uint8_t SERVER_PRIV[32], SERVER_SEED[32], SERVER_RANDOM[32], CLIENT_PRIV[32];
static const uint8_t ODCID[8] = {0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8};
static const uint8_t CLIENT_SCID[4] = {0xc1, 0xc2, 0xc3, 0xc4};
static const uint8_t SERVER_SCID[8] = {0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58};

static int g_rng_call;
static void test_rng(uint8_t *out, size_t len)
{
    if (g_rng_call == 0)
    {
        memcpy(out, SERVER_PRIV, len);
    }
    else if (g_rng_call == 1)
    {
        memcpy(out, SERVER_RANDOM, len);
    }
    else
    {
        memcpy(out, SERVER_SCID, len);
    }
    g_rng_call++;
}

static uint8_t g_out[16][1500];
static size_t g_out_len[16];
static int g_out_n;

static void harvest(void)
{
    g_out_n = 0;
    for (size_t i = 0; i < protocore_net_host_udp_count() && g_out_n < 16; i++)
    {
        const protocore_net_host_dgram *d = protocore_net_host_udp_at(i);
        if (d->len <= sizeof(g_out[0]))
        {
            memcpy(g_out[g_out_n], d->data, d->len);
            g_out_len[g_out_n] = d->len;
            g_out_n++;
        }
    }
}

static void deliver(const uint8_t *dg, size_t len, const char *ip, uint16_t port)
{
    TEST_ASSERT_TRUE(protocore_net_host_udp_deliver(H3_PORT, ip, port, (void *)dg, (uint16_t)len));
}

static void run(uint32_t now_ms)
{
    UdpListener.poll(protocore_udp_listener_span());
    QuicServer.now_ms = now_ms;
    QuicServer.poll(protocore_quic_server_span());
    UdpListener.poll(protocore_udp_listener_span());
    harvest();
}

static void feed(const uint8_t *dg, size_t len, const char *ip, uint16_t port, uint32_t now_ms)
{
    protocore_net_host_udp_reset();
    deliver(dg, len, ip, port);
    run(now_ms);
}

static char g_method[16], g_path[64];
static void app_request(void *, uint32_t conn_id, uint64_t sid, const char *method, const char *path, const char *,
                        const uint8_t *, size_t)
{
    strncpy(g_method, method, sizeof(g_method) - 1);
    strncpy(g_path, path, sizeof(g_path) - 1);
    QuicServer.stream.conn_id = conn_id;
    QuicServer.stream.stream_id = sid;
    QuicServer.resp.status = 200;
    QuicServer.resp.content_type = "text/plain";
    QuicServer.resp.body = (const uint8_t *)"hello h3";
    QuicServer.resp.body_len = 8;
    QuicServer.respond(protocore_quic_server_span());
}

static void fill()
{
    for (int i = 0; i < 32; i++)
    {
        SERVER_PRIV[i] = (uint8_t)(0x40 + i);
        SERVER_SEED[i] = (uint8_t)(0x80 + i);
        SERVER_RANDOM[i] = (uint8_t)(0xA0 + i);
        CLIENT_PRIV[i] = (uint8_t)(0x01 + i);
    }
    g_method[0] = g_path[0] = '\0';
    g_rng_call = 0;
    g_out_n = 0;
}

static void config(QuicServerConfig *scfg, void (*rng)(uint8_t *, size_t))
{
    memset(scfg, 0, sizeof(*scfg));
    scfg->cert_der = CERT;
    scfg->cert_len = sizeof(CERT);
    memcpy(scfg->ed25519_seed, SERVER_SEED, 32);
    scfg->rng = rng;
}

static void wr_pn(uint8_t *o, uint64_t pn, uint8_t pn_len)
{
    for (uint8_t i = 0; i < pn_len; i++)
    {
        o[i] = (uint8_t)(pn >> (8 * (pn_len - 1 - i)));
    }
}
static size_t build_long(uint8_t *out, size_t cap, uint8_t type, const uint8_t *dcid, uint8_t dcl, const uint8_t *scid,
                         uint8_t scl, uint64_t pn, QuicPacketKeys *keys, const uint8_t *frames, size_t frame_len)
{
    QuicPacket.pn_length_args.full_pn = pn;
    QuicPacket.pn_length_args.largest_acked = -1;
    QuicPacket.pn_length(quic_packet_work);
    uint8_t pn_len = QuicPacket.u8;
    QuicPacket.build_long_header_args.out = out;
    QuicPacket.build_long_header_args.cap = cap;
    QuicPacket.build_long_header_args.type = type;
    QuicPacket.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacket.build_long_header_args.dcid = dcid;
    QuicPacket.build_long_header_args.dcid_len = dcl;
    QuicPacket.build_long_header_args.scid = scid;
    QuicPacket.build_long_header_args.scid_len = scl;
    QuicPacket.build_long_header_args.pn_len = pn_len;
    QuicPacket.build_long_header(quic_packet_work);
    size_t p = QuicPacket.n;
    if (type == QUIC_LP_INITIAL)
    {
        QuicVarint.encode_args.out = out + p;
        QuicVarint.encode_args.cap = cap - p;
        QuicVarint.encode_args.value = 0;
        QuicVarint.encode(quic_varint_work);
        p += QuicVarint.n;
    }
    QuicVarint.encode_args.out = out + p;
    QuicVarint.encode_args.cap = cap - p;
    QuicVarint.encode_args.value = (uint64_t)pn_len + frame_len + 16;
    QuicVarint.encode(quic_varint_work);
    p += QuicVarint.n;
    size_t pn_off = p;
    wr_pn(out + p, pn, pn_len);
    p += pn_len;
    memcpy(out + p, frames, frame_len);
    QuicCrypto.packet_protect_args.pkt = out;
    QuicCrypto.packet_protect_args.cap = cap;
    QuicCrypto.packet_protect_args.pn_offset = pn_off;
    QuicCrypto.packet_protect_args.pn_len = pn_len;
    QuicCrypto.packet_protect_args.full_pn = pn;
    QuicCrypto.packet_protect_args.payload_len = frame_len;
    QuicCrypto.packet_protect_args.keys = keys;
    QuicCrypto.packet_protect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_protect(quic_crypto_work);
    return QuicCrypto.n;
}
static size_t build_short(uint8_t *out, size_t cap, const uint8_t *dcid, uint8_t dcl, uint64_t pn, QuicPacketKeys *keys,
                          const uint8_t *frames, size_t frame_len)
{
    QuicPacket.pn_length_args.full_pn = pn;
    QuicPacket.pn_length_args.largest_acked = -1;
    QuicPacket.pn_length(quic_packet_work);
    uint8_t pn_len = QuicPacket.u8;
    out[0] = (uint8_t)(0x40 | (pn_len - 1));
    memcpy(out + 1, dcid, dcl);
    size_t pn_off = 1 + dcl;
    wr_pn(out + pn_off, pn, pn_len);
    memcpy(out + pn_off + pn_len, frames, frame_len);
    QuicCrypto.packet_protect_args.pkt = out;
    QuicCrypto.packet_protect_args.cap = cap;
    QuicCrypto.packet_protect_args.pn_offset = pn_off;
    QuicCrypto.packet_protect_args.pn_len = pn_len;
    QuicCrypto.packet_protect_args.full_pn = pn;
    QuicCrypto.packet_protect_args.payload_len = frame_len;
    QuicCrypto.packet_protect_args.keys = keys;
    QuicCrypto.packet_protect_args.is_long = PROTO_FALSE;
    QuicCrypto.packet_protect(quic_crypto_work);
    return QuicCrypto.n;
}
static size_t open_long(const uint8_t *dg, size_t len, QuicPacketKeys *keys, uint8_t *plain, size_t *wire,
                        uint8_t *type)
{
    QuicLongHeader h;
    QuicPacket.parse_long_header_args.buf = dg;
    QuicPacket.parse_long_header_args.len = len;
    QuicPacket.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacket.ok);
    *type = h.type;
    size_t off = h.hdr_len;
    if (h.type == QUIC_LP_INITIAL)
    {
        uint64_t tl = 0;
        size_t c = 0;
        QuicVarint.decode_args.in = dg + off;
        QuicVarint.decode_args.len = len - off;
        QuicVarint.decode_args.value = &tl;
        QuicVarint.decode_args.consumed = &c;
        QuicVarint.decode(quic_varint_work);
        off += c + (size_t)tl;
    }
    uint64_t length = 0;
    size_t c = 0;
    QuicVarint.decode_args.in = dg + off;
    QuicVarint.decode_args.len = len - off;
    QuicVarint.decode_args.value = &length;
    QuicVarint.decode_args.consumed = &c;
    QuicVarint.decode(quic_varint_work);
    off += c;
    *wire = off + (size_t)length;
    static uint8_t work[2048];
    memcpy(work, dg, *wire);
    uint64_t pn = 0;
    QuicCrypto.packet_unprotect_args.pkt = work;
    QuicCrypto.packet_unprotect_args.pn_offset = off;
    QuicCrypto.packet_unprotect_args.length = (size_t)length;
    QuicCrypto.packet_unprotect_args.largest_pn = 0;
    QuicCrypto.packet_unprotect_args.keys = keys;
    QuicCrypto.packet_unprotect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_unprotect_args.out = plain;
    QuicCrypto.packet_unprotect_args.out_pn = &pn;
    QuicCrypto.packet_unprotect(quic_crypto_work);
    return QuicCrypto.n;
}
static size_t open_short(const uint8_t *dg, size_t len, uint8_t dcl, QuicPacketKeys *keys, uint8_t *plain)
{
    static uint8_t work[2048];
    memcpy(work, dg, len);
    uint64_t pn = 0;
    QuicCrypto.packet_unprotect_args.pkt = work;
    QuicCrypto.packet_unprotect_args.pn_offset = 1 + dcl;
    QuicCrypto.packet_unprotect_args.length = len - (1 + dcl);
    QuicCrypto.packet_unprotect_args.largest_pn = 0;
    QuicCrypto.packet_unprotect_args.keys = keys;
    QuicCrypto.packet_unprotect_args.is_long = PROTO_FALSE;
    QuicCrypto.packet_unprotect_args.out = plain;
    QuicCrypto.packet_unprotect_args.out_pn = &pn;
    QuicCrypto.packet_unprotect(quic_crypto_work);
    return QuicCrypto.n;
}
static size_t extract_crypto(const uint8_t *p, size_t len, uint8_t *out)
{
    size_t off = 0, got = 0;
    while (off < len)
    {
        if (p[off] == QUIC_FT_PADDING)
        {
            off++;
            continue;
        }
        QuicFrameHeader f;
        QuicFrame.parse_args.buf = p + off;
        QuicFrame.parse_args.len = len - off;
        QuicFrame.parse_args.out = &f;
        QuicFrame.parse(quic_frame_work);
        size_t n = QuicFrame.n;
        if (!n)
        {
            break;
        }
        off += n;
        if (f.type == QUIC_FT_CRYPTO)
        {
            memcpy(out + got, f.crypto.data, (size_t)f.crypto.length);
            got += (size_t)f.crypto.length;
        }
    }
    return got;
}
static size_t build_client_hello(uint8_t *out, const uint8_t client_pub[32], const uint8_t *tp, size_t tp_len)
{
    size_t p = 0;
    out[p++] = TLS_HS_CLIENT_HELLO;
    size_t hs = p;
    p += 3;
    out[p++] = 0x03;
    out[p++] = 0x03;
    for (int i = 0; i < 32; i++)
    {
        out[p++] = (uint8_t)i;
    }
    out[p++] = 0x00;
    out[p++] = 0x00;
    out[p++] = 0x02;
    out[p++] = 0x13;
    out[p++] = 0x01;
    out[p++] = 0x01;
    out[p++] = 0x00;
    size_t ext = p;
    p += 2;
    static const uint8_t sv[] = {0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04};
    static const uint8_t sg[] = {0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d};
    static const uint8_t sa[] = {0x00, 0x0d, 0x00, 0x04, 0x00, 0x02, 0x08, 0x07};
    static const uint8_t ks[] = {0x00, 0x33, 0x00, 0x26, 0x00, 0x24, 0x00, 0x1d, 0x00, 0x20};
    static const uint8_t al[] = {0x00, 0x10, 0x00, 0x05, 0x00, 0x03, 0x02, 'h', '3'};
    memcpy(out + p, sv, sizeof(sv));
    p += sizeof(sv);
    memcpy(out + p, sg, sizeof(sg));
    p += sizeof(sg);
    memcpy(out + p, sa, sizeof(sa));
    p += sizeof(sa);
    memcpy(out + p, ks, sizeof(ks));
    p += sizeof(ks);
    memcpy(out + p, client_pub, 32);
    p += 32;
    memcpy(out + p, al, sizeof(al));
    p += sizeof(al);
    out[p++] = 0x00;
    out[p++] = 0x39;
    out[p++] = (uint8_t)(tp_len >> 8);
    out[p++] = (uint8_t)tp_len;
    memcpy(out + p, tp, tp_len);
    p += tp_len;
    uint16_t el = (uint16_t)(p - ext - 2);
    out[ext] = (uint8_t)(el >> 8);
    out[ext + 1] = (uint8_t)el;
    uint32_t hl = (uint32_t)(p - hs - 3);
    out[hs] = (uint8_t)(hl >> 16);
    out[hs + 1] = (uint8_t)(hl >> 8);
    out[hs + 2] = (uint8_t)hl;
    return p;
}

static size_t make_client_initial(uint8_t *dg, size_t cap)
{
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    QuicTransportParams ctp;
    QuicTp.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctpe[128];
    QuicTp.encode_args.tp = &ctp;
    QuicTp.encode_args.out = ctpe;
    QuicTp.encode_args.cap = sizeof(ctpe);
    QuicTp.encode(quic_tp_work);
    size_t ctpl = QuicTp.n;
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t ch[512];
    size_t chl = build_client_hello(ch, client_pub, ctpe, ctpl);
    uint8_t frames[1200];
    QuicFrame.build_crypto_args.out = frames;
    QuicFrame.build_crypto_args.cap = sizeof(frames);
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = ch;
    QuicFrame.build_crypto_args.len = chl;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrame.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    return build_long(dg, cap, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0, &init.client,
                      frames, fl);
}

typedef struct
{
    char *s;
} StatusCapture;

static proto_bool capture_status(void *c, const char *nm, size_t nl, const char *v, size_t vl)
{
    if (nl == 7 && memcmp(nm, ":status", 7) == 0)
    {
        memcpy(((StatusCapture *)c)->s, v, vl);
        ((StatusCapture *)c)->s[vl] = 0;
    }
    return PROTO_TRUE;
}

static proto_bool response_ok(QuicPacketKeys *ap_s)
{
    uint8_t plain[2048];
    for (int d = 0; d < g_out_n; d++)
    {
        const uint8_t *dg = g_out[d];
        size_t len = g_out_len[d];
        QuicPacket.is_long_header_args.first = dg[0];
        QuicPacket.is_long_header(quic_packet_work);
        if (QuicPacket.ok)
        {
            continue;
        }
        size_t p2 = open_short(dg, len, sizeof(CLIENT_SCID), ap_s, plain);
        if (p2 == SIZE_MAX)
        {
            continue;
        }
        size_t fo = 0;
        while (fo < p2)
        {
            if (plain[fo] == QUIC_FT_PADDING)
            {
                fo++;
                continue;
            }
            QuicFrameHeader f;
            QuicFrame.parse_args.buf = plain + fo;
            QuicFrame.parse_args.len = p2 - fo;
            QuicFrame.parse_args.out = &f;
            QuicFrame.parse(quic_frame_work);
            size_t n = QuicFrame.n;
            if (!n)
            {
                break;
            }
            fo += n;
            if (!(f.type >= QUIC_FT_STREAM && f.type <= QUIC_FT_STREAM + 7 && f.stream.id == 0))
            {
                continue;
            }
            const uint8_t *sp = f.stream.data;
            size_t so = 0, sn = (size_t)f.stream.length;
            char status[8] = {0};
            proto_bool data_ok = PROTO_FALSE;
            while (so < sn)
            {
                H3FrameHeader hf;
                H3Frame.parse_header_args.buf = sp + so;
                H3Frame.parse_header_args.len = sn - so;
                H3Frame.parse_header_args.out = &hf;
                H3Frame.parse_header(h3_frame_work);
                if (!H3Frame.ok)
                {
                    break;
                }
                const uint8_t *hp = sp + so + hf.header_len;
                if (hf.type == H3_HEADERS)
                {
                    char sc[128];
                    StatusCapture e = {status};
                    Qpack.decode_args.block = hp;
                    Qpack.decode_args.len = (size_t)hf.length;
                    Qpack.decode_args.scratch = sc;
                    Qpack.decode_args.scratch_cap = sizeof(sc);
                    Qpack.decode_args.emit = capture_status;
                    Qpack.decode_args.ctx = &e;
                    Qpack.decode(qpack_work);
                    (void)Qpack.ok;
                }
                else if (hf.type == H3_DATA)
                {
                    if (hf.length == 8 && memcmp(hp, "hello h3", 8) == 0)
                    {
                        data_ok = PROTO_TRUE;
                    }
                }
                so += hf.header_len + (size_t)hf.length;
            }
            if (strcmp(status, "200") == 0 && data_ok)
            {
                return PROTO_TRUE;
            }
        }
    }
    return PROTO_FALSE;
}

void test_quic_server_http3_get()
{
    fill();

    QuicServerConfig scfg;
    config(&scfg, test_rng);
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);

    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    QuicTransportParams ctp;
    QuicTp.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctpe[128];
    QuicTp.encode_args.tp = &ctp;
    QuicTp.encode_args.out = ctpe;
    QuicTp.encode_args.cap = sizeof(ctpe);
    QuicTp.encode(quic_tp_work);
    size_t ctpl = QuicTp.n;
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t ch[512];
    size_t chl = build_client_hello(ch, client_pub, ctpe, ctpl);
    uint8_t frames[1200];
    QuicFrame.build_crypto_args.out = frames;
    QuicFrame.build_crypto_args.cap = sizeof(frames);
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = ch;
    QuicFrame.build_crypto_args.len = chl;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrame.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);

    feed(dg, dl, "192.0.2.10", 40000, 0);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, QuicServer.u8);
    TEST_ASSERT_GREATER_THAN(0, g_out_n);

    uint8_t plain[2048], sh[512], hsf[1024];
    size_t wire = 0;
    uint8_t ty = 0;
    size_t pt = open_long(g_out[0], g_out_len[0], &init.server, plain, &wire, &ty);
    size_t shl = extract_crypto(plain, pt, sh);
    uint8_t server_pub[32], ecdhe[32];
    Curve25519.x25519_base_args.out = server_pub;
    Curve25519.x25519_base_args.scalar = SERVER_PRIV;
    Curve25519.x25519_base(tw);
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    uint8_t *t;
    uint8_t chsh[32], chsf[32];
    t = tw_t;
    Sha256.init(t);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = chl;
    Sha256.update(t);
    Sha256.update_args.data = sh;
    Sha256.update_args.len = shl;
    Sha256.update(t);
    {
        Sha256.final_args.out = chsh;
        Sha256.final(t);
    }
    Tls13KeySchedule cks;
    static uint8_t ks_store_464[PROTOCORE_TLS13_KS_BORROW];
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.bind.s = ks_store_464;
    Tls13Ks.early(NULL);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = sizeof(ecdhe);
    Tls13Ks.step.ch_sh_hash = chsh;
    Tls13Ks.handshake(NULL);
    QuicPacketKeys hs_s, hs_c, ap_s, ap_c;
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_HS;
    QuicCrypto.keys_from_secret_args.out = &hs_s;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_HS;
    QuicCrypto.keys_from_secret_args.out = &hs_c;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    size_t hw = 0;
    uint8_t hty = 0;
    size_t hpt = open_long(g_out[0] + wire, g_out_len[0] - wire, &hs_s, plain, &hw, &hty);
    size_t hsfl = extract_crypto(plain, hpt, hsf);
    Sha256.update_args.data = hsf;
    Sha256.update_args.len = hsfl;
    Sha256.update(t);
    Sha256.final_args.out = chsf;
    Sha256.final(t);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ch_sfin_hash = chsf;
    Tls13Ks.master(NULL);
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_AP;
    QuicCrypto.keys_from_secret_args.out = &ap_s;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_AP;
    QuicCrypto.keys_from_secret_args.out = &ap_c;
    QuicCrypto.keys_from_secret(quic_crypto_work);

    uint8_t ifr[64];
    QuicFrame.build_ack_args.out = ifr;
    QuicFrame.build_ack_args.cap = sizeof(ifr);
    QuicFrame.build_ack_args.largest = 0;
    QuicFrame.build_ack_args.delay = 0;
    QuicFrame.build_ack_args.first_range = 0;
    QuicFrame.build_ack(quic_frame_work);
    size_t ifl = QuicFrame.n;
    uint8_t idg[256];
    size_t idl = build_long(idg, sizeof(idg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init.client, ifr, ifl);
    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.finished_args.base_secret = cks.s + TLS13_KS_CLIENT_HS;
    Tls13Ks.finished_args.transcript_hash = chsf;
    Tls13Ks.finished_args.out = cfin + 4;
    Tls13Ks.finished_mac(NULL);
    uint8_t hfr[64];
    QuicFrame.build_ack_args.out = hfr;
    QuicFrame.build_ack_args.cap = sizeof(hfr);
    QuicFrame.build_ack_args.largest = 0;
    QuicFrame.build_ack_args.delay = 0;
    QuicFrame.build_ack_args.first_range = 0;
    QuicFrame.build_ack(quic_frame_work);
    size_t hfl = QuicFrame.n;
    QuicFrame.build_crypto_args.out = hfr + hfl;
    QuicFrame.build_crypto_args.cap = sizeof(hfr) - hfl;
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = cfin;
    QuicFrame.build_crypto_args.len = sizeof(cfin);
    QuicFrame.build_crypto(quic_frame_work);
    hfl += QuicFrame.n;
    size_t hdl = build_long(idg + idl, sizeof(idg) - idl, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID,
                            sizeof(CLIENT_SCID), 0, &hs_c, hfr, hfl);
    feed(idg, idl + hdl, "192.0.2.10", 40000, 0);

    uint8_t block[128];
    Qpack.encode_prefix_args.out = block;
    Qpack.encode_prefix_args.cap = sizeof(block);
    Qpack.encode_prefix(qpack_work);
    size_t bp = Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":method";
    Qpack.encode_header_args.name_len = 7;
    Qpack.encode_header_args.value = "GET";
    Qpack.encode_header_args.value_len = 3;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":path";
    Qpack.encode_header_args.name_len = 5;
    Qpack.encode_header_args.value = "/hello";
    Qpack.encode_header_args.value_len = 6;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":authority";
    Qpack.encode_header_args.name_len = 10;
    Qpack.encode_header_args.value = "h3.test";
    Qpack.encode_header_args.value_len = 7;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    uint8_t h3req[256];
    H3Frame.build_headers_args.out = h3req;
    H3Frame.build_headers_args.cap = sizeof(h3req);
    H3Frame.build_headers_args.block = block;
    H3Frame.build_headers_args.len = bp;
    H3Frame.build_headers(h3_frame_work);
    size_t h3l = H3Frame.n;
    uint8_t sfr[300];
    QuicFrame.build_stream_args.out = sfr;
    QuicFrame.build_stream_args.cap = sizeof(sfr);
    QuicFrame.build_stream_args.id = 0;
    QuicFrame.build_stream_args.offset = 0;
    QuicFrame.build_stream_args.data = h3req;
    QuicFrame.build_stream_args.len = h3l;
    QuicFrame.build_stream_args.fin = PROTO_TRUE;
    QuicFrame.build_stream(quic_frame_work);
    size_t sfrl = QuicFrame.n;
    uint8_t s1[512];
    size_t s1l = build_short(s1, sizeof(s1), SERVER_SCID, sizeof(SERVER_SCID), 0, &ap_c, sfr, sfrl);

    feed(s1, s1l, "192.0.2.10", 40000, 0);

    TEST_ASSERT_EQUAL_STRING("GET", g_method);
    TEST_ASSERT_EQUAL_STRING("/hello", g_path);

    TEST_ASSERT_TRUE(response_ok(&ap_s));

    QuicServer.stop(protocore_quic_server_span());
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(0, QuicServer.u8);
}

void test_idle_connection_reaped()
{
    fill();
    uint32_t now = 100000;

    QuicServerConfig scfg;
    config(&scfg, test_rng);
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);

    uint8_t dg[1500];
    size_t dl = make_client_initial(dg, sizeof(dg));
    feed(dg, dl, "192.0.2.10", 40000, now);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, QuicServer.u8);

    now += PROTOCORE_QUIC_IDLE_MS - 1;
    run(now);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, QuicServer.u8);
    now += 2;
    run(now);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(0, QuicServer.u8);

    QuicServer.stop(protocore_quic_server_span());
}

static void bulk_rng(uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        out[i] = (uint8_t)(i * 7 + 1);
    }
}

static size_t make_min_initial(uint8_t *dg, size_t cap, const uint8_t *dcid, uint8_t dcl)
{
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = dcid;
    QuicCrypto.derive_initial_secrets_args.dcid_len = dcl;
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t frames[64];
    QuicFrame.build_ack_args.out = frames;
    QuicFrame.build_ack_args.cap = sizeof(frames);
    QuicFrame.build_ack_args.largest = 0;
    QuicFrame.build_ack_args.delay = 0;
    QuicFrame.build_ack_args.first_range = 0;
    QuicFrame.build_ack(quic_frame_work);
    size_t fl = QuicFrame.n;
    return build_long(dg, cap, QUIC_LP_INITIAL, dcid, dcl, CLIENT_SCID, sizeof(CLIENT_SCID), 0, &init.client, frames,
                      fl);
}

void test_quic_server_input_guards()
{
    fill();

    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = NULL;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_FALSE(QuicServer.ok);
    QuicServerConfig scfg;
    config(&scfg, NULL);
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_FALSE(QuicServer.ok);

    QuicServer.stop(protocore_quic_server_span());
    QuicServer.now_ms = 0;
    QuicServer.poll(protocore_quic_server_span());

    scfg.rng = test_rng;
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);

    uint8_t one[1] = {0x40};
    feed(one, 0, "192.0.2.1", 1, 0);
    static uint8_t huge[PROTOCORE_QUIC_MAX_DATAGRAM + 1];
    feed(huge, sizeof(huge), "192.0.2.1", 1, 0);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(0, QuicServer.u8);
    TEST_ASSERT_EQUAL_INT(0, (int)protocore_net_host_udp_sent());

    QuicServer.stream.conn_id = 999999;
    QuicServer.stream.stream_id = 0;
    QuicServer.resp.status = 200;
    QuicServer.resp.content_type = "text/plain";
    QuicServer.resp.body = NULL;
    QuicServer.resp.body_len = 0;
    QuicServer.respond(protocore_quic_server_span());
    TEST_ASSERT_FALSE(QuicServer.ok);

    uint8_t bad_long[1] = {0xC0};
    feed(bad_long, sizeof(bad_long), "192.0.2.1", 1, 0);
    uint8_t short_tiny[2] = {0x40, 0x00};
    feed(short_tiny, sizeof(short_tiny), "192.0.2.1", 1, 0);
    uint8_t short_unknown[1 + PROTOCORE_QUIC_SCID_LEN] = {0x40, 9, 9, 9, 9, 9, 9, 9, 9};
    feed(short_unknown, sizeof(short_unknown), "192.0.2.1", 1, 0);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(0, QuicServer.u8);

    QuicServer.stop(protocore_quic_server_span());
}

void test_ingest_ring_drops_past_capacity()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, bulk_rng);
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);

    uint8_t dcid_a[8] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7};
    uint8_t dcid_b[8] = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7};
    uint8_t a[256], b[256], junk[1] = {0x40};
    size_t al = make_min_initial(a, sizeof(a), dcid_a, sizeof(dcid_a));
    size_t bl = make_min_initial(b, sizeof(b), dcid_b, sizeof(dcid_b));

    deliver(a, al, "192.0.2.1", 1);
    for (int i = 0; i < PROTOCORE_QUIC_INGEST_RING - 3; i++)
    {
        deliver(junk, sizeof(junk), "192.0.2.1", 1);
    }
    deliver(b, bl, "192.0.2.1", 1);
    run(0);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(2, QuicServer.u8);

    QuicServer.stop(protocore_quic_server_span());
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);

    deliver(a, al, "192.0.2.1", 1);
    for (int i = 0; i < PROTOCORE_QUIC_INGEST_RING; i++)
    {
        deliver(junk, sizeof(junk), "192.0.2.1", 1);
    }
    deliver(b, bl, "192.0.2.1", 1);
    run(0);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, QuicServer.u8);

    QuicServer.stop(protocore_quic_server_span());
}

void test_quic_server_pool_full()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, bulk_rng);
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);

    for (int i = 0; i <= PROTOCORE_QUIC_MAX_CONNS; i++)
    {
        uint8_t dcid[8] = {0xA0, (uint8_t)i, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5};
        uint8_t dg[256];
        size_t dl = make_min_initial(dg, sizeof(dg), dcid, sizeof(dcid));
        deliver(dg, dl, "192.0.2.10", 40000);
    }
    run(0);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_QUIC_MAX_CONNS, QuicServer.u8);
    QuicServer.stop(protocore_quic_server_span());
}

void test_quic_server_replies_to_the_captured_peer()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, test_rng);
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);

    uint8_t dg[1500];
    size_t dl = make_client_initial(dg, sizeof(dg));
    feed(dg, dl, "255.255.255.255", 40009, 0);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, QuicServer.u8);

    TEST_ASSERT_GREATER_THAN(0, (int)protocore_net_host_udp_count());
    const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_NET_TYPE_V4, d->type);
    TEST_ASSERT_EQUAL_UINT8(255, d->addr[0]);
    TEST_ASSERT_EQUAL_UINT8(255, d->addr[3]);
    TEST_ASSERT_EQUAL_UINT16(40009, d->dst_port);
    TEST_ASSERT_EQUAL_UINT16(H3_PORT, d->src_port);

    QuicServer.stop(protocore_quic_server_span());
}

void test_quic_server_unrenderable_peer_dropped()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, test_rng);
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);

    uint8_t dg[1500];
    size_t dl = make_client_initial(dg, sizeof(dg));
    feed(dg, dl, NULL, 40000, 0);

    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(0, QuicServer.u8);
    TEST_ASSERT_EQUAL_INT(0, (int)protocore_net_host_udp_sent());

    QuicServer.stop(protocore_quic_server_span());
}

void test_quic_server_respond_unknown_id_with_active_conn()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, test_rng);
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);

    uint8_t dg0[256];
    size_t dl0 = make_min_initial(dg0, sizeof(dg0), ODCID, sizeof(ODCID));
    feed(dg0, dl0, "192.0.2.10", 40000, 0);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, QuicServer.u8);

    QuicServer.stream.conn_id = 2;
    QuicServer.stream.stream_id = 0;
    QuicServer.resp.status = 200;
    QuicServer.resp.content_type = "text/plain";
    QuicServer.resp.body = NULL;
    QuicServer.resp.body_len = 0;
    QuicServer.respond(protocore_quic_server_span());
    TEST_ASSERT_FALSE(QuicServer.ok);

    QuicServer.stop(protocore_quic_server_span());
}

void test_quic_server_begin_default_port()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, test_rng);
    QuicServer.begin_args.port = 0;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);
    TEST_ASSERT_NOT_NULL(protocore_net_host_udp_pcb(PROTOCORE_HTTP3_PORT));
    QuicServer.stop(protocore_quic_server_span());
}

void test_quic_server_route_header_edges()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, test_rng);
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);

    uint8_t dg0[256];
    size_t dl0 = make_min_initial(dg0, sizeof(dg0), ODCID, sizeof(ODCID));
    feed(dg0, dl0, "192.0.2.10", 40000, 0);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, QuicServer.u8);

    uint8_t short_dcid[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t hs_hdr[64];
    QuicPacket.build_long_header_args.out = hs_hdr;
    QuicPacket.build_long_header_args.cap = sizeof(hs_hdr);
    QuicPacket.build_long_header_args.type = QUIC_LP_HANDSHAKE;
    QuicPacket.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacket.build_long_header_args.dcid = short_dcid;
    QuicPacket.build_long_header_args.dcid_len = sizeof(short_dcid);
    QuicPacket.build_long_header_args.scid = CLIENT_SCID;
    QuicPacket.build_long_header_args.scid_len = sizeof(CLIENT_SCID);
    QuicPacket.build_long_header_args.pn_len = 1;
    QuicPacket.build_long_header(quic_packet_work);
    size_t hs_len = QuicPacket.n;
    deliver(hs_hdr, hs_len, "192.0.2.11", 40001);

    uint8_t other_dcid[8] = {0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78};
    uint8_t ver_hdr[64];
    QuicPacket.build_long_header_args.out = ver_hdr;
    QuicPacket.build_long_header_args.cap = sizeof(ver_hdr);
    QuicPacket.build_long_header_args.type = QUIC_LP_INITIAL;
    QuicPacket.build_long_header_args.version = 0xAABBCCDDu;
    QuicPacket.build_long_header_args.dcid = other_dcid;
    QuicPacket.build_long_header_args.dcid_len = sizeof(other_dcid);
    QuicPacket.build_long_header_args.scid = CLIENT_SCID;
    QuicPacket.build_long_header_args.scid_len = sizeof(CLIENT_SCID);
    QuicPacket.build_long_header_args.pn_len = 1;
    QuicPacket.build_long_header(quic_packet_work);
    size_t ver_len = QuicPacket.n;
    deliver(ver_hdr, ver_len, "192.0.2.12", 40002);

    uint8_t scid_hdr[64];
    QuicPacket.build_long_header_args.out = scid_hdr;
    QuicPacket.build_long_header_args.cap = sizeof(scid_hdr);
    QuicPacket.build_long_header_args.type = QUIC_LP_HANDSHAKE;
    QuicPacket.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacket.build_long_header_args.dcid = SERVER_SCID;
    QuicPacket.build_long_header_args.dcid_len = sizeof(SERVER_SCID);
    QuicPacket.build_long_header_args.scid = CLIENT_SCID;
    QuicPacket.build_long_header_args.scid_len = sizeof(CLIENT_SCID);
    QuicPacket.build_long_header_args.pn_len = 1;
    QuicPacket.build_long_header(quic_packet_work);
    size_t scid_hdr_len = QuicPacket.n;
    deliver(scid_hdr, scid_hdr_len, "192.0.2.13", 40003);

    run(0);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, QuicServer.u8);

    uint8_t wrong_scid[1 + PROTOCORE_QUIC_SCID_LEN];
    wrong_scid[0] = 0x40;
    for (int i = 0; i < PROTOCORE_QUIC_SCID_LEN; i++)
    {
        wrong_scid[1 + i] = (uint8_t)(0xEE + i);
    }
    feed(wrong_scid, sizeof(wrong_scid), "192.0.2.14", 40004, 0);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, QuicServer.u8);

    QuicServer.stop(protocore_quic_server_span());
}

void test_quic_server_close_reaped_before_idle()
{
    fill();
    uint32_t now = 5000;

    QuicServerConfig scfg;
    config(&scfg, test_rng);
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = app_request;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);

    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    uint8_t frames0[64];
    QuicFrame.build_ack_args.out = frames0;
    QuicFrame.build_ack_args.cap = sizeof(frames0);
    QuicFrame.build_ack_args.largest = 0;
    QuicFrame.build_ack_args.delay = 0;
    QuicFrame.build_ack_args.first_range = 0;
    QuicFrame.build_ack(quic_frame_work);
    size_t fl0 = QuicFrame.n;
    uint8_t dg0[256];
    size_t dl0 = build_long(dg0, sizeof(dg0), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            0, &init.client, frames0, fl0);
    feed(dg0, dl0, "192.0.2.20", 40010, now);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, QuicServer.u8);

    uint8_t cc_frames[64];
    QuicFrame.build_connection_close_args.out = cc_frames;
    QuicFrame.build_connection_close_args.cap = sizeof(cc_frames);
    QuicFrame.build_connection_close_args.app = PROTO_FALSE;
    QuicFrame.build_connection_close_args.error_code = 0;
    QuicFrame.build_connection_close_args.frame_type = 0;
    QuicFrame.build_connection_close_args.reason = NULL;
    QuicFrame.build_connection_close_args.reason_len = 0;
    QuicFrame.build_connection_close(quic_frame_work);
    size_t cc_len = QuicFrame.n;
    uint8_t dg1[256];
    size_t dl1 = build_long(dg1, sizeof(dg1), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init.client, cc_frames, cc_len);
    now += 5;
    feed(dg1, dl1, "192.0.2.20", 40010, now);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(0, QuicServer.u8);

    QuicServer.stop(protocore_quic_server_span());
}

void test_quic_server_on_request_null()
{
    fill();

    QuicServerConfig scfg;
    config(&scfg, test_rng);
    QuicServer.begin_args.port = H3_PORT;
    QuicServer.begin_args.cfg = &scfg;
    QuicServer.begin_args.on_request = NULL;
    QuicServer.begin_args.app = NULL;
    QuicServer.begin(protocore_quic_server_span());
    TEST_ASSERT_TRUE(QuicServer.ok);

    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    QuicTransportParams ctp;
    QuicTp.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctpe[128];
    QuicTp.encode_args.tp = &ctp;
    QuicTp.encode_args.out = ctpe;
    QuicTp.encode_args.cap = sizeof(ctpe);
    QuicTp.encode(quic_tp_work);
    size_t ctpl = QuicTp.n;
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t ch[512];
    size_t chl = build_client_hello(ch, client_pub, ctpe, ctpl);
    uint8_t frames[1200];
    QuicFrame.build_crypto_args.out = frames;
    QuicFrame.build_crypto_args.cap = sizeof(frames);
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = ch;
    QuicFrame.build_crypto_args.len = chl;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrame.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);

    feed(dg, dl, "192.0.2.10", 40000, 0);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, QuicServer.u8);
    TEST_ASSERT_GREATER_THAN(0, g_out_n);

    uint8_t plain[2048], sh[512], hsf[1024];
    size_t wire = 0;
    uint8_t ty = 0;
    size_t pt = open_long(g_out[0], g_out_len[0], &init.server, plain, &wire, &ty);
    size_t shl = extract_crypto(plain, pt, sh);
    uint8_t server_pub[32], ecdhe[32];
    Curve25519.x25519_base_args.out = server_pub;
    Curve25519.x25519_base_args.scalar = SERVER_PRIV;
    Curve25519.x25519_base(tw);
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    uint8_t *t;
    uint8_t chsh[32], chsf[32];
    t = tw_t;
    Sha256.init(t);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = chl;
    Sha256.update(t);
    Sha256.update_args.data = sh;
    Sha256.update_args.len = shl;
    Sha256.update(t);
    {
        Sha256.final_args.out = chsh;
        Sha256.final(t);
    }
    Tls13KeySchedule cks;
    static uint8_t ks_store_900[PROTOCORE_TLS13_KS_BORROW];
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.bind.s = ks_store_900;
    Tls13Ks.early(NULL);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = sizeof(ecdhe);
    Tls13Ks.step.ch_sh_hash = chsh;
    Tls13Ks.handshake(NULL);
    QuicPacketKeys hs_s, hs_c, ap_c;
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_HS;
    QuicCrypto.keys_from_secret_args.out = &hs_s;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_HS;
    QuicCrypto.keys_from_secret_args.out = &hs_c;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    size_t hw = 0;
    uint8_t hty = 0;
    size_t hpt = open_long(g_out[0] + wire, g_out_len[0] - wire, &hs_s, plain, &hw, &hty);
    size_t hsfl = extract_crypto(plain, hpt, hsf);
    Sha256.update_args.data = hsf;
    Sha256.update_args.len = hsfl;
    Sha256.update(t);
    Sha256.final_args.out = chsf;
    Sha256.final(t);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ch_sfin_hash = chsf;
    Tls13Ks.master(NULL);
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_AP;
    QuicCrypto.keys_from_secret_args.out = &ap_c;
    QuicCrypto.keys_from_secret(quic_crypto_work);

    uint8_t ifr[64];
    QuicFrame.build_ack_args.out = ifr;
    QuicFrame.build_ack_args.cap = sizeof(ifr);
    QuicFrame.build_ack_args.largest = 0;
    QuicFrame.build_ack_args.delay = 0;
    QuicFrame.build_ack_args.first_range = 0;
    QuicFrame.build_ack(quic_frame_work);
    size_t ifl = QuicFrame.n;
    uint8_t idg[256];
    size_t idl = build_long(idg, sizeof(idg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init.client, ifr, ifl);
    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.finished_args.base_secret = cks.s + TLS13_KS_CLIENT_HS;
    Tls13Ks.finished_args.transcript_hash = chsf;
    Tls13Ks.finished_args.out = cfin + 4;
    Tls13Ks.finished_mac(NULL);
    uint8_t hfr[64];
    QuicFrame.build_ack_args.out = hfr;
    QuicFrame.build_ack_args.cap = sizeof(hfr);
    QuicFrame.build_ack_args.largest = 0;
    QuicFrame.build_ack_args.delay = 0;
    QuicFrame.build_ack_args.first_range = 0;
    QuicFrame.build_ack(quic_frame_work);
    size_t hfl = QuicFrame.n;
    QuicFrame.build_crypto_args.out = hfr + hfl;
    QuicFrame.build_crypto_args.cap = sizeof(hfr) - hfl;
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = cfin;
    QuicFrame.build_crypto_args.len = sizeof(cfin);
    QuicFrame.build_crypto(quic_frame_work);
    hfl += QuicFrame.n;
    size_t hdl = build_long(idg + idl, sizeof(idg) - idl, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID,
                            sizeof(CLIENT_SCID), 0, &hs_c, hfr, hfl);
    feed(idg, idl + hdl, "192.0.2.10", 40000, 0);

    uint8_t block[128];
    Qpack.encode_prefix_args.out = block;
    Qpack.encode_prefix_args.cap = sizeof(block);
    Qpack.encode_prefix(qpack_work);
    size_t bp = Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":method";
    Qpack.encode_header_args.name_len = 7;
    Qpack.encode_header_args.value = "GET";
    Qpack.encode_header_args.value_len = 3;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":path";
    Qpack.encode_header_args.name_len = 5;
    Qpack.encode_header_args.value = "/hello";
    Qpack.encode_header_args.value_len = 6;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = sizeof(block) - bp;
    Qpack.encode_header_args.name = ":authority";
    Qpack.encode_header_args.name_len = 10;
    Qpack.encode_header_args.value = "h3.test";
    Qpack.encode_header_args.value_len = 7;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    uint8_t h3req[256];
    H3Frame.build_headers_args.out = h3req;
    H3Frame.build_headers_args.cap = sizeof(h3req);
    H3Frame.build_headers_args.block = block;
    H3Frame.build_headers_args.len = bp;
    H3Frame.build_headers(h3_frame_work);
    size_t h3l = H3Frame.n;
    uint8_t sfr[300];
    QuicFrame.build_stream_args.out = sfr;
    QuicFrame.build_stream_args.cap = sizeof(sfr);
    QuicFrame.build_stream_args.id = 0;
    QuicFrame.build_stream_args.offset = 0;
    QuicFrame.build_stream_args.data = h3req;
    QuicFrame.build_stream_args.len = h3l;
    QuicFrame.build_stream_args.fin = PROTO_TRUE;
    QuicFrame.build_stream(quic_frame_work);
    size_t sfrl = QuicFrame.n;
    uint8_t s1[512];
    size_t s1l = build_short(s1, sizeof(s1), SERVER_SCID, sizeof(SERVER_SCID), 0, &ap_c, sfr, sfrl);

    feed(s1, s1l, "192.0.2.10", 40000, 0);

    TEST_ASSERT_EQUAL_STRING("", g_method);
    QuicServer.active_conns(protocore_quic_server_span());
    TEST_ASSERT_EQUAL_UINT8(1, QuicServer.u8);

    QuicServer.stop(protocore_quic_server_span());
}

