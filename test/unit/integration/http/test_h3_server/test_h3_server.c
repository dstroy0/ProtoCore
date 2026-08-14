// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/asymmetric/curve25519.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/udp/server/server.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/h3_frame.h"
#include "network_drivers/presentation/http/http3/h3_server.h"
#include "network_drivers/presentation/http/http3/qpack.h"
#include "network_drivers/presentation/http/http3/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_server.h"
#include "network_drivers/presentation/http/http3/quic_tp.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include "protocore.h"
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "network_drivers/transport/udp/udp.h"
#include "protocore_net_host.h"
#include <unity.h>

static uint8_t tw[4096];
static uint8_t tw_t[4096];

#define H3_PORT 443

void setUp()
{
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
    UdpListener.poll(UdpListener.internal);
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
    uint8_t pn_len = protocore_quic_pn_length(pn, -1);
    size_t p = protocore_quic_build_long_header(out, cap, type, QUIC_VERSION_1, dcid, dcl, scid, scl, pn_len);
    if (type == QUIC_LP_INITIAL)
    {
        p += protocore_quic_varint_encode(out + p, cap - p, 0);
    }
    p += protocore_quic_varint_encode(out + p, cap - p, (uint64_t)pn_len + frame_len + 16);
    size_t pn_off = p;
    wr_pn(out + p, pn, pn_len);
    p += pn_len;
    memcpy(out + p, frames, frame_len);
    return protocore_quic_packet_protect(out, cap, pn_off, pn_len, pn, frame_len, keys, PROTO_TRUE);
}
static size_t build_short(uint8_t *out, size_t cap, const uint8_t *dcid, uint8_t dcl, uint64_t pn,
                          const QuicPacketKeys *keys, const uint8_t *frames, size_t frame_len)
{
    uint8_t pn_len = protocore_quic_pn_length(pn, -1);
    out[0] = (uint8_t)(0x40 | (pn_len - 1));
    memcpy(out + 1, dcid, dcl);
    size_t pn_off = 1 + dcl;
    wr_pn(out + pn_off, pn, pn_len);
    memcpy(out + pn_off + pn_len, frames, frame_len);
    return protocore_quic_packet_protect(out, cap, pn_off, pn_len, pn, frame_len, keys, PROTO_FALSE);
}
static size_t open_long(const uint8_t *dg, size_t len, const QuicPacketKeys *keys, uint8_t *plain, size_t *wire,
                        uint8_t *type)
{
    QuicLongHeader h;
    TEST_ASSERT_TRUE(protocore_quic_parse_long_header(dg, len, &h));
    *type = h.type;
    size_t off = h.hdr_len;
    if (h.type == QUIC_LP_INITIAL)
    {
        uint64_t tl = 0;
        size_t c = 0;
        protocore_quic_varint_decode(dg + off, len - off, &tl, &c);
        off += c + (size_t)tl;
    }
    uint64_t length = 0;
    size_t c = 0;
    protocore_quic_varint_decode(dg + off, len - off, &length, &c);
    off += c;
    *wire = off + (size_t)length;
    static uint8_t work[2048];
    memcpy(work, dg, *wire);
    uint64_t pn = 0;
    return protocore_quic_packet_unprotect(work, off, (size_t)length, 0, keys, PROTO_TRUE, plain, &pn);
}
static size_t open_short(const uint8_t *dg, size_t len, uint8_t dcl, const QuicPacketKeys *keys, uint8_t *plain)
{
    static uint8_t work[2048];
    memcpy(work, dg, len);
    uint64_t pn = 0;
    return protocore_quic_packet_unprotect(work, 1 + dcl, len - (1 + dcl), 0, keys, PROTO_FALSE, plain, &pn);
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
        QuicFrame f;
        size_t n = protocore_quic_frame_parse(p + off, len - off, &f);
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
        if (protocore_quic_is_long_header(g_out[d][0]))
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
            QuicFrame f;
            size_t n = protocore_quic_frame_parse(plain + fo, p2 - fo, &f);
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
                H3Frame hf;
                if (!protocore_h3_frame_parse(sp + so, sn - so, &hf))
                {
                    break;
                }
                const uint8_t *hp = sp + so + hf.header_len;
                if (hf.type == H3_HEADERS)
                {
                    char sc[128];
                    H3StatusCtx e = {status};
                    protocore_qpack_decode(hp, (size_t)hf.length, sc, sizeof(sc), h3_take_status, &e);
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
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);

    QuicTransportParams ctp;
    protocore_quic_tp_defaults(&ctp);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctpe[128];
    size_t ctpl = protocore_quic_tp_encode(&ctp, ctpe, sizeof(ctpe));
    uint8_t client_pub[32];
    protocore_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t chl = build_client_hello(ch, client_pub, ctpe, ctpl);
    uint8_t frames[1200];
    size_t fl = protocore_quic_build_crypto(frames, sizeof(frames), 0, ch, chl);
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    feed(dg, dl, "192.0.2.10", 40000);
    TEST_ASSERT_GREATER_THAN(0, g_out_n);

    QuicLongHeader sh_hdr;
    TEST_ASSERT_TRUE(protocore_quic_parse_long_header(g_out[0], g_out_len[0], &sh_hdr));
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
    protocore_x25519(ecdhe, CLIENT_PRIV, server_pub);
    protocore_sha256_ctx t;
    uint8_t chsh[32], chsf[32];
    protocore_sha256_init(&t, tw_t);
    protocore_sha256_update(&t, ch, chl);
    protocore_sha256_update(&t, sh, shl);
    {
        protocore_sha256_final(&t, chsh);
    }
    Tls13KeySchedule cks;
    static uint8_t ks_store_408[PROTOCORE_TLS13_KS_BORROW];
    protocore_tls13_ks_early(&TLS13_KDF, &cks, ks_store_408);
    protocore_tls13_ks_handshake(&cks, ecdhe, chsh, 32);
    QuicPacketKeys hs_s, hs_c, ap_s, ap_c;
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_SERVER_HS, &hs_s);
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_CLIENT_HS, &hs_c);
    size_t hw = 0;
    uint8_t hty = 0;
    size_t hpt = open_long(g_out[0] + wire, g_out_len[0] - wire, &hs_s, plain, &hw, &hty);
    size_t hsfl = extract_crypto(plain, hpt, hsf);
    protocore_sha256_update(&t, hsf, hsfl);
    protocore_sha256_final(&t, chsf);
    protocore_tls13_ks_master(&cks, chsf);
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_SERVER_AP, &ap_s);
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_CLIENT_AP, &ap_c);

    uint8_t ifr[64];
    size_t ifl = protocore_quic_build_ack(ifr, sizeof(ifr), 0, 0, 0);
    uint8_t idg[256];
    size_t idl = build_long(idg, sizeof(idg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init.client, ifr, ifl);
    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    protocore_tls13_finished_mac(&cks, cks.s + TLS13_KS_CLIENT_HS, chsf, cfin + 4);
    uint8_t hfr[64];
    size_t hfl = protocore_quic_build_ack(hfr, sizeof(hfr), 0, 0, 0);
    hfl += protocore_quic_build_crypto(hfr + hfl, sizeof(hfr) - hfl, 0, cfin, sizeof(cfin));
    size_t hdl = build_long(idg + idl, sizeof(idg) - idl, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID,
                            sizeof(CLIENT_SCID), 0, &hs_c, hfr, hfl);
    feed(idg, idl + hdl, "192.0.2.10", 40000);

    uint8_t block[128];
    size_t bp = protocore_qpack_encode_prefix(block, sizeof(block));
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/hello", 6);
    bp += protocore_qpack_encode_header(block + bp, sizeof(block) - bp, ":authority", 10, "h3.test", 7);
    uint8_t h3req[256];
    size_t h3l = protocore_h3_build_headers(h3req, sizeof(h3req), block, bp);
    uint8_t sfr[300];
    size_t sfrl = protocore_quic_build_stream(sfr, sizeof(sfr), 0, 0, h3req, h3l, PROTO_TRUE);
    uint8_t s1[512];
    size_t s1l = build_short(s1, sizeof(s1), server_scid, server_scid_len, 0, &ap_c, sfr, sfrl);

    feed(s1, s1l, "192.0.2.10", 40000);

    TEST_ASSERT_TRUE(g_handler_ran);
    TEST_ASSERT_TRUE(response_ok(&ap_s));
    TEST_ASSERT_EQUAL_UINT8(0, conn_pool[PROTOCORE_H3_DISPATCH_SLOT].h3);

    protocore_quic_server_stop();
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

    conn_pool[0].protocore_resp_sink = NULL;
    conn_pool[0].pcb = NULL;
    send_text(0, 200, "text/plain", "x");
    send_empty(0, 204);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_h3_begin_edges);
    RUN_TEST(test_h3_request_served_by_route);
    RUN_TEST(test_h3_dispatch_edges);
    return UNITY_END();
}
