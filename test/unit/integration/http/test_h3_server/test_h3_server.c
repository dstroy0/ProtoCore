// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/asymmetric/curve25519/curve25519.h"
#include "crypto/hash/sha256/sha256.h"
#include "network_drivers/presentation/http/http3/h3_frame/h3_frame.h"
#include "network_drivers/presentation/http/http3/h3_server/h3_server.h"
#include "network_drivers/presentation/http/http3/qpack/qpack.h"
#include "network_drivers/presentation/http/http3/quic_crypto/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_frame/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_server/quic_server.h"
#include "network_drivers/presentation/http/http3/quic_tp/quic_tp.h"
#include "network_drivers/presentation/http/http3/quic_varint/quic_varint.h"
#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"
#include "network_drivers/session/session.h" // http_h3 / http_resp_sink: HTTP's per-slot state
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/udp/server/server.h"
#include "protocore.h"
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "network_drivers/transport/udp/udp.h"
#include "protocore_net_host.h"
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

#define H3_PORT 443

void setUp()
{
    // Every case starts with no listener and no HTTP/3 certificate, so what one begins does not
    // decide what the next one sees. test_h3_begin_edges asks proto_begin for NO_LISTENERS, which
    // is only the answer from that state.
    protocore_server_reset();
}
void tearDown()
{
}

static const uint8_t CERT[48] = {0x30, 0x2e, 0x02, 0x01, 0x02};
static uint8_t SERVER_SEED[32], CLIENT_PRIV[32];
static const uint8_t ODCID[8] = {0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8};
static const uint8_t CLIENT_SCID[4] = {0xc1, 0xc2, 0xc3, 0xc4};

typedef struct
{
    char *s;
} H3StatusCtx;

static proto_bool h3_take_status(void *c, const char *nm, size_t nl, const char *v, size_t vl)
{
    if (nl == 7 && memcmp(nm, ":status", 7) == 0)
    {
        memcpy(((H3StatusCtx *)c)->s, v, vl);
        ((H3StatusCtx *)c)->s[vl] = 0;
    }
    return PROTO_TRUE;
}

static proto_bool g_handler_ran = PROTO_FALSE;
static void h_hello(uint8_t slot, HttpReq *req)
{
    g_handler_ran = PROTO_TRUE;
    TEST_ASSERT_EQUAL_STRING("/hello", req->path);
    send_text(slot, 200, "text/plain", "bridged h3");
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

static void feed(const uint8_t *dg, size_t len, const char *ip, uint16_t port)
{
    protocore_net_host_udp_reset();
    TEST_ASSERT_TRUE(protocore_net_host_udp_deliver(H3_PORT, ip, port, (void *)dg, (uint16_t)len));
    service_once(0);
    UdpListener.poll(protocore_udp_listener_span());
    harvest();
}

static void fill()
{
    for (int i = 0; i < 32; i++)
    {
        SERVER_SEED[i] = (uint8_t)(0x80 + i);
        CLIENT_PRIV[i] = (uint8_t)(0x01 + i);
    }
    g_handler_ran = PROTO_FALSE;
    g_out_n = 0;
}

static void wr_pn(uint8_t *o, uint64_t pn, uint8_t pn_len)
{
    for (uint8_t i = 0; i < pn_len; i++)
    {
        o[i] = (uint8_t)(pn >> (8 * (pn_len - 1 - i)));
    }
}
static size_t build_long(uint8_t *out, size_t cap, uint8_t type, const uint8_t *dcid, uint8_t dcl, const uint8_t *scid,
                         uint8_t scl, uint64_t pn, const QuicPacketKeys *keys, const uint8_t *frames, size_t frame_len)
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
        QuicVarintV.encode_args.out = out + p;
        QuicVarintV.encode_args.cap = cap - p;
        QuicVarintV.encode_args.value = 0;
        QuicVarint.encode(quic_varint_work);
        p += QuicVarintV.n;
    }
    QuicVarintV.encode_args.out = out + p;
    QuicVarintV.encode_args.cap = cap - p;
    QuicVarintV.encode_args.value = (uint64_t)pn_len + frame_len + 16;
    QuicVarint.encode(quic_varint_work);
    p += QuicVarintV.n;
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
static size_t build_short(uint8_t *out, size_t cap, const uint8_t *dcid, uint8_t dcl, uint64_t pn,
                          const QuicPacketKeys *keys, const uint8_t *frames, size_t frame_len)
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
static size_t open_long(const uint8_t *dg, size_t len, const QuicPacketKeys *keys, uint8_t *plain, size_t *wire,
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
        QuicVarintV.decode_args.in = dg + off;
        QuicVarintV.decode_args.len = len - off;
        QuicVarintV.decode_args.value = &tl;
        QuicVarintV.decode_args.consumed = &c;
        QuicVarint.decode(quic_varint_work);
        off += c + (size_t)tl;
    }
    uint64_t length = 0;
    size_t c = 0;
    QuicVarintV.decode_args.in = dg + off;
    QuicVarintV.decode_args.len = len - off;
    QuicVarintV.decode_args.value = &length;
    QuicVarintV.decode_args.consumed = &c;
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
static size_t open_short(const uint8_t *dg, size_t len, uint8_t dcl, const QuicPacketKeys *keys, uint8_t *plain)
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

static proto_bool server_pub_from_sh(const uint8_t *sh, size_t shl, uint8_t out[32])
{
    for (size_t i = 0; i + 4 + 32 <= shl; i++)
    {
        if (sh[i] == 0x00 && sh[i + 1] == 0x1d && sh[i + 2] == 0x00 && sh[i + 3] == 0x20)
        {
            memcpy(out, sh + i + 4, 32);
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
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

static proto_bool response_ok(const QuicPacketKeys *ap_s)
{
    uint8_t plain[2048];
    for (int d = 0; d < g_out_n; d++)
    {
        QuicPacket.is_long_header_args.first = g_out[d][0];
        QuicPacket.is_long_header(quic_packet_work);
        if (QuicPacket.ok)
        {
            continue;
        }
        size_t p2 = open_short(g_out[d], g_out_len[d], sizeof(CLIENT_SCID), ap_s, plain);
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
                    H3StatusCtx e = {status};
                    Qpack.decode_args.block = hp;
                    Qpack.decode_args.len = (size_t)hf.length;
                    Qpack.decode_args.scratch = sc;
                    Qpack.decode_args.scratch_cap = sizeof(sc);
                    Qpack.decode_args.emit = h3_take_status;
                    Qpack.decode_args.ctx = &e;
                    Qpack.decode(qpack_work);
                }
                else if (hf.type == H3_DATA)
                {
                    if (hf.length == 10 && memcmp(hp, "bridged h3", 10) == 0)
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

void test_h3_request_served_by_route()
{
    fill();

    on_http("/hello", HTTP_GET, h_hello);
    TEST_ASSERT_TRUE(protocore_h3_cert(CERT, sizeof(CERT), SERVER_SEED, 443));
    TEST_ASSERT_EQUAL_INT32(PROTOCORE_OK, proto_begin(NULL));

    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    QuicTransportParams ctp;
    QuicTpV.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctpe[128];
    QuicTpV.encode_args.tp = &ctp;
    QuicTpV.encode_args.out = ctpe;
    QuicTpV.encode_args.cap = sizeof(ctpe);
    QuicTp.encode(quic_tp_work);
    size_t ctpl = QuicTpV.n;
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
    feed(dg, dl, "192.0.2.10", 40000);
    TEST_ASSERT_GREATER_THAN(0, g_out_n);

    QuicLongHeader sh_hdr;
    QuicPacket.parse_long_header_args.buf = g_out[0];
    QuicPacket.parse_long_header_args.len = g_out_len[0];
    QuicPacket.parse_long_header_args.out = &sh_hdr;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacket.ok);
    uint8_t server_scid[QUIC_MAX_CID_LEN];
    uint8_t server_scid_len = sh_hdr.scid_len;
    memcpy(server_scid, sh_hdr.scid, server_scid_len);

    uint8_t plain[2048], sh[512], hsf[1024];
    size_t wire = 0;
    uint8_t ty = 0;
    size_t pt = open_long(g_out[0], g_out_len[0], &init.server, plain, &wire, &ty);
    size_t shl = extract_crypto(plain, pt, sh);
    uint8_t server_pub[32], ecdhe[32];
    TEST_ASSERT_TRUE(server_pub_from_sh(sh, shl, server_pub));
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    uint8_t *t;
    uint8_t chsh[32], chsf[32];
    t = tw_t;
    Sha256.init(t);
    Sha256V.update_args.data = ch;
    Sha256V.update_args.len = chl;
    Sha256.update(t);
    Sha256V.update_args.data = sh;
    Sha256V.update_args.len = shl;
    Sha256.update(t);
    {
        Sha256V.final_args.out = chsh;
        Sha256.final(t);
    }
    Tls13KeySchedule cks;
    static uint8_t ks_store_408[PROTOCORE_TLS13_KS_BORROW];
    Tls13KsV.bind.kdf = &TLS13_KDF;
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.bind.s = ks_store_408;
    Tls13Ks.early(NULL);
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.step.ecdhe = ecdhe;
    Tls13KsV.step.ecdhe_len = 32;
    Tls13KsV.step.ch_sh_hash = chsh;
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
    Sha256V.update_args.data = hsf;
    Sha256V.update_args.len = hsfl;
    Sha256.update(t);
    Sha256V.final_args.out = chsf;
    Sha256.final(t);
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.step.ch_sfin_hash = chsf;
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
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.finished_args.base_secret = cks.s + TLS13_KS_CLIENT_HS;
    Tls13KsV.finished_args.transcript_hash = chsf;
    Tls13KsV.finished_args.out = cfin + 4;
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
    feed(idg, idl + hdl, "192.0.2.10", 40000);

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
    size_t s1l = build_short(s1, sizeof(s1), server_scid, server_scid_len, 0, &ap_c, sfr, sfrl);

    feed(s1, s1l, "192.0.2.10", 40000);

    TEST_ASSERT_TRUE(g_handler_ran);
    TEST_ASSERT_TRUE(response_ok(&ap_s));
    TEST_ASSERT_EQUAL_UINT8(0, http_h3[PROTOCORE_H3_DISPATCH_SLOT]);

    QuicServer.stop(protocore_quic_server_span());
}

void test_h3_begin_edges()
{

    TEST_ASSERT_EQUAL_INT32((int32_t)PROTOCORE_ERR_NO_LISTENERS, proto_begin(NULL));

    TEST_ASSERT_EQUAL_INT32((int32_t)PROTOCORE_OK, begin_http(8080, NULL));

    service_once(0);
    service_once(1);
}

static proto_bool g_empty_ran = PROTO_FALSE;
static void h_empty(uint8_t slot, HttpReq *)
{
    g_empty_ran = PROTO_TRUE;
    send_empty(slot, 204);
}

void test_h3_dispatch_edges()
{
    fill();
    on_http("/hello", HTTP_GET, h_hello);
    on_http("/empty", HTTP_GET, h_empty);

    const uint32_t CID = 0xDEADBEEF;

    TEST_ASSERT_FALSE(protocore_h3_cert(NULL, sizeof(CERT), SERVER_SEED, 443));
    TEST_ASSERT_FALSE(protocore_h3_cert(CERT, 0, SERVER_SEED, 443));
    TEST_ASSERT_FALSE(protocore_h3_cert(CERT, sizeof(CERT), NULL, 443));

    char long_method[32];
    memset(long_method, 'A', sizeof(long_method) - 1);
    long_method[sizeof(long_method) - 1] = 0;
    protocore_h3_server_request(NULL, CID, 0, long_method, "/hello", "h3.test", NULL, 0);

    g_handler_ran = PROTO_FALSE;
    protocore_h3_server_request(NULL, CID, 0, "GET", "/hello?a=1&b=2", "h3.test", NULL, 0);
    TEST_ASSERT_TRUE(g_handler_ran);

    char long_query[256];
    long_query[0] = '/';
    memcpy(long_query + 1, "hello?", 6);
    memset(long_query + 7, 'q', sizeof(long_query) - 8);
    long_query[sizeof(long_query) - 1] = 0;
    protocore_h3_server_request(NULL, CID, 0, "GET", long_query, "h3.test", NULL, 0);

    char long_path[256];
    long_path[0] = '/';
    memset(long_path + 1, 'p', sizeof(long_path) - 2);
    long_path[sizeof(long_path) - 1] = 0;
    protocore_h3_server_request(NULL, CID, 0, "GET", long_path, "h3.test", NULL, 0);

    char long_auth[128];
    memset(long_auth, 'h', sizeof(long_auth) - 1);
    long_auth[sizeof(long_auth) - 1] = 0;
    protocore_h3_server_request(NULL, CID, 0, "GET", "/hello", long_auth, NULL, 0);

    protocore_h3_server_request(NULL, CID, 0, "GET", "/hello", NULL, NULL, 0);
    protocore_h3_server_request(NULL, CID, 0, "GET", "/hello", "", NULL, 0);

    static const uint8_t BODY[16] = {'h', 'e', 'l', 'l', 'o', '-', 'b', 'o', 'd', 'y', '!', '!', '!', '!', '!', '!'};
    protocore_h3_server_request(NULL, CID, 0, "GET", "/hello", "h3.test", BODY, sizeof(BODY));

    protocore_h3_server_request(NULL, CID, 0, "GET", "/hello", "h3.test", BODY, 0);

    g_empty_ran = PROTO_FALSE;
    protocore_h3_server_request(NULL, CID, 0, "GET", "/empty", "h3.test", NULL, 0);
    TEST_ASSERT_TRUE(g_empty_ran);

    http_resp_sink[0] = NULL;
    conn_pool[0].pcb = NULL;
    send_text(0, 200, "text/plain", "x");
    send_empty(0, 204);
}
