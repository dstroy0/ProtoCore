// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/asymmetric/curve25519.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/h3_conn.h"
#include "network_drivers/presentation/http/http3/h3_frame.h"
#include "network_drivers/presentation/http/http3/qpack.h"
#include "network_drivers/presentation/http/http3/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_server.h"
#include "network_drivers/presentation/http/http3/quic_tp.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include "network_drivers/transport/udp/server/server.h"
#include "network_drivers/transport/udp/udp.h"
#include "protocore_net_host.h"
#include <string.h>

#include <unity.h>

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
    UdpListener.poll(UdpListener.internal);
    protocore_quic_server_poll(now_ms);
    UdpListener.poll(UdpListener.internal);
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
    protocore_quic_server_respond(conn_id, sid, 200, "text/plain", (const uint8_t *)"hello h3", 8);
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
static size_t build_short(uint8_t *out, size_t cap, const uint8_t *dcid, uint8_t dcl, uint64_t pn, QuicPacketKeys *keys,
                          const uint8_t *frames, size_t frame_len)
{
    uint8_t pn_len = protocore_quic_pn_length(pn, -1);
    out[0] = (uint8_t)(0x40 | (pn_len - 1));
    memcpy(out + 1, dcid, dcl);
    size_t pn_off = 1 + dcl;
    wr_pn(out + pn_off, pn, pn_len);
    memcpy(out + pn_off + pn_len, frames, frame_len);
    return protocore_quic_packet_protect(out, cap, pn_off, pn_len, pn, frame_len, keys, PROTO_FALSE);
}
static size_t open_long(const uint8_t *dg, size_t len, QuicPacketKeys *keys, uint8_t *plain, size_t *wire,
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
static size_t open_short(const uint8_t *dg, size_t len, uint8_t dcl, QuicPacketKeys *keys, uint8_t *plain)
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
        if (protocore_quic_is_long_header(dg[0]))
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
                    StatusCapture e = {status};
                    (void)protocore_qpack_decode(hp, (size_t)hf.length, sc, sizeof(sc), capture_status, &e);
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
    TEST_ASSERT_TRUE(protocore_quic_server_begin(H3_PORT, &scfg, app_request, NULL));

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

    feed(dg, dl, "192.0.2.10", 40000, 0);
    TEST_ASSERT_EQUAL_UINT8(1, protocore_quic_server_active_conns());
    TEST_ASSERT_GREATER_THAN(0, g_out_n);

    uint8_t plain[2048], sh[512], hsf[1024];
    size_t wire = 0;
    uint8_t ty = 0;
    size_t pt = open_long(g_out[0], g_out_len[0], &init.server, plain, &wire, &ty);
    size_t shl = extract_crypto(plain, pt, sh);
    uint8_t server_pub[32], ecdhe[32];
    protocore_x25519_base(server_pub, SERVER_PRIV);
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
    static uint8_t ks_store_464[PROTOCORE_TLS13_KS_BORROW];
    protocore_tls13_ks_early(&TLS13_KDF, &cks, ks_store_464);
    protocore_tls13_ks_handshake(&cks, ecdhe, chsh, sizeof(ecdhe));
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
    feed(idg, idl + hdl, "192.0.2.10", 40000, 0);

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
    size_t s1l = build_short(s1, sizeof(s1), SERVER_SCID, sizeof(SERVER_SCID), 0, &ap_c, sfr, sfrl);

    feed(s1, s1l, "192.0.2.10", 40000, 0);

    TEST_ASSERT_EQUAL_STRING("GET", g_method);
    TEST_ASSERT_EQUAL_STRING("/hello", g_path);

    TEST_ASSERT_TRUE(response_ok(&ap_s));

    protocore_quic_server_stop();
    TEST_ASSERT_EQUAL_UINT8(0, protocore_quic_server_active_conns());
}

void test_idle_connection_reaped()
{
    fill();
    uint32_t now = 100000;

    QuicServerConfig scfg;
    config(&scfg, test_rng);
    TEST_ASSERT_TRUE(protocore_quic_server_begin(H3_PORT, &scfg, app_request, NULL));

    uint8_t dg[1500];
    size_t dl = make_client_initial(dg, sizeof(dg));
    feed(dg, dl, "192.0.2.10", 40000, now);
    TEST_ASSERT_EQUAL_UINT8(1, protocore_quic_server_active_conns());

    now += PROTOCORE_QUIC_IDLE_MS - 1;
    run(now);
    TEST_ASSERT_EQUAL_UINT8(1, protocore_quic_server_active_conns());
    now += 2;
    run(now);
    TEST_ASSERT_EQUAL_UINT8(0, protocore_quic_server_active_conns());

    protocore_quic_server_stop();
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
    protocore_quic_derive_initial_secrets(tw, dcid, dcl, &init);
    uint8_t frames[64];
    size_t fl = protocore_quic_build_ack(frames, sizeof(frames), 0, 0, 0);
    return build_long(dg, cap, QUIC_LP_INITIAL, dcid, dcl, CLIENT_SCID, sizeof(CLIENT_SCID), 0, &init.client, frames,
                      fl);
}

void test_quic_server_input_guards()
{
    fill();

    TEST_ASSERT_FALSE(protocore_quic_server_begin(H3_PORT, NULL, app_request, NULL));
    QuicServerConfig scfg;
    config(&scfg, NULL);
    TEST_ASSERT_FALSE(protocore_quic_server_begin(H3_PORT, &scfg, app_request, NULL));

    protocore_quic_server_stop();
    protocore_quic_server_poll(0);

    scfg.rng = test_rng;
    TEST_ASSERT_TRUE(protocore_quic_server_begin(H3_PORT, &scfg, app_request, NULL));

    uint8_t one[1] = {0x40};
    feed(one, 0, "192.0.2.1", 1, 0);
    static uint8_t huge[PROTOCORE_QUIC_MAX_DATAGRAM + 1];
    feed(huge, sizeof(huge), "192.0.2.1", 1, 0);
    TEST_ASSERT_EQUAL_UINT8(0, protocore_quic_server_active_conns());
    TEST_ASSERT_EQUAL_INT(0, (int)protocore_net_host_udp_sent());

    TEST_ASSERT_FALSE(protocore_quic_server_respond(999999, 0, 200, "text/plain", NULL, 0));

    uint8_t bad_long[1] = {0xC0};
    feed(bad_long, sizeof(bad_long), "192.0.2.1", 1, 0);
    uint8_t short_tiny[2] = {0x40, 0x00};
    feed(short_tiny, sizeof(short_tiny), "192.0.2.1", 1, 0);
    uint8_t short_unknown[1 + PROTOCORE_QUIC_SCID_LEN] = {0x40, 9, 9, 9, 9, 9, 9, 9, 9};
    feed(short_unknown, sizeof(short_unknown), "192.0.2.1", 1, 0);
    TEST_ASSERT_EQUAL_UINT8(0, protocore_quic_server_active_conns());

    protocore_quic_server_stop();
}

void test_ingest_ring_drops_past_capacity()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, bulk_rng);
    TEST_ASSERT_TRUE(protocore_quic_server_begin(H3_PORT, &scfg, app_request, NULL));

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
    TEST_ASSERT_EQUAL_UINT8(2, protocore_quic_server_active_conns());

    protocore_quic_server_stop();
    TEST_ASSERT_TRUE(protocore_quic_server_begin(H3_PORT, &scfg, app_request, NULL));

    deliver(a, al, "192.0.2.1", 1);
    for (int i = 0; i < PROTOCORE_QUIC_INGEST_RING; i++)
    {
        deliver(junk, sizeof(junk), "192.0.2.1", 1);
    }
    deliver(b, bl, "192.0.2.1", 1);
    run(0);
    TEST_ASSERT_EQUAL_UINT8(1, protocore_quic_server_active_conns());

    protocore_quic_server_stop();
}

void test_quic_server_pool_full()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, bulk_rng);
    TEST_ASSERT_TRUE(protocore_quic_server_begin(H3_PORT, &scfg, app_request, NULL));

    for (int i = 0; i <= PROTOCORE_QUIC_MAX_CONNS; i++)
    {
        uint8_t dcid[8] = {0xA0, (uint8_t)i, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5};
        uint8_t dg[256];
        size_t dl = make_min_initial(dg, sizeof(dg), dcid, sizeof(dcid));
        deliver(dg, dl, "192.0.2.10", 40000);
    }
    run(0);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_QUIC_MAX_CONNS, protocore_quic_server_active_conns());
    protocore_quic_server_stop();
}

void test_quic_server_replies_to_the_captured_peer()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, test_rng);
    TEST_ASSERT_TRUE(protocore_quic_server_begin(H3_PORT, &scfg, app_request, NULL));

    uint8_t dg[1500];
    size_t dl = make_client_initial(dg, sizeof(dg));
    feed(dg, dl, "255.255.255.255", 40009, 0);
    TEST_ASSERT_EQUAL_UINT8(1, protocore_quic_server_active_conns());

    TEST_ASSERT_GREATER_THAN(0, (int)protocore_net_host_udp_count());
    const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_NET_TYPE_V4, d->type);
    TEST_ASSERT_EQUAL_UINT8(255, d->addr[0]);
    TEST_ASSERT_EQUAL_UINT8(255, d->addr[3]);
    TEST_ASSERT_EQUAL_UINT16(40009, d->dst_port);
    TEST_ASSERT_EQUAL_UINT16(H3_PORT, d->src_port);

    protocore_quic_server_stop();
}

void test_quic_server_unrenderable_peer_dropped()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, test_rng);
    TEST_ASSERT_TRUE(protocore_quic_server_begin(H3_PORT, &scfg, app_request, NULL));

    uint8_t dg[1500];
    size_t dl = make_client_initial(dg, sizeof(dg));
    feed(dg, dl, NULL, 40000, 0);

    TEST_ASSERT_EQUAL_UINT8(0, protocore_quic_server_active_conns());
    TEST_ASSERT_EQUAL_INT(0, (int)protocore_net_host_udp_sent());

    protocore_quic_server_stop();
}

void test_quic_server_respond_unknown_id_with_active_conn()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, test_rng);
    TEST_ASSERT_TRUE(protocore_quic_server_begin(H3_PORT, &scfg, app_request, NULL));

    uint8_t dg0[256];
    size_t dl0 = make_min_initial(dg0, sizeof(dg0), ODCID, sizeof(ODCID));
    feed(dg0, dl0, "192.0.2.10", 40000, 0);
    TEST_ASSERT_EQUAL_UINT8(1, protocore_quic_server_active_conns());

    TEST_ASSERT_FALSE(protocore_quic_server_respond(2, 0, 200, "text/plain", NULL, 0));

    protocore_quic_server_stop();
}

void test_quic_server_begin_default_port()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, test_rng);
    TEST_ASSERT_TRUE(protocore_quic_server_begin(0, &scfg, app_request, NULL));
    TEST_ASSERT_NOT_NULL(protocore_net_host_udp_pcb(PROTOCORE_HTTP3_PORT));
    protocore_quic_server_stop();
}

void test_quic_server_route_header_edges()
{
    fill();
    QuicServerConfig scfg;
    config(&scfg, test_rng);
    TEST_ASSERT_TRUE(protocore_quic_server_begin(H3_PORT, &scfg, app_request, NULL));

    uint8_t dg0[256];
    size_t dl0 = make_min_initial(dg0, sizeof(dg0), ODCID, sizeof(ODCID));
    feed(dg0, dl0, "192.0.2.10", 40000, 0);
    TEST_ASSERT_EQUAL_UINT8(1, protocore_quic_server_active_conns());

    uint8_t short_dcid[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t hs_hdr[64];
    size_t hs_len =
        protocore_quic_build_long_header(hs_hdr, sizeof(hs_hdr), QUIC_LP_HANDSHAKE, QUIC_VERSION_1, short_dcid,
                                         sizeof(short_dcid), CLIENT_SCID, sizeof(CLIENT_SCID), 1);
    deliver(hs_hdr, hs_len, "192.0.2.11", 40001);

    uint8_t other_dcid[8] = {0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78};
    uint8_t ver_hdr[64];
    size_t ver_len =
        protocore_quic_build_long_header(ver_hdr, sizeof(ver_hdr), QUIC_LP_INITIAL, 0xAABBCCDDu, other_dcid,
                                         sizeof(other_dcid), CLIENT_SCID, sizeof(CLIENT_SCID), 1);
    deliver(ver_hdr, ver_len, "192.0.2.12", 40002);

    uint8_t scid_hdr[64];
    size_t scid_hdr_len =
        protocore_quic_build_long_header(scid_hdr, sizeof(scid_hdr), QUIC_LP_HANDSHAKE, QUIC_VERSION_1, SERVER_SCID,
                                         sizeof(SERVER_SCID), CLIENT_SCID, sizeof(CLIENT_SCID), 1);
    deliver(scid_hdr, scid_hdr_len, "192.0.2.13", 40003);

    run(0);
    TEST_ASSERT_EQUAL_UINT8(1, protocore_quic_server_active_conns());

    uint8_t wrong_scid[1 + PROTOCORE_QUIC_SCID_LEN];
    wrong_scid[0] = 0x40;
    for (int i = 0; i < PROTOCORE_QUIC_SCID_LEN; i++)
    {
        wrong_scid[1 + i] = (uint8_t)(0xEE + i);
    }
    feed(wrong_scid, sizeof(wrong_scid), "192.0.2.14", 40004, 0);
    TEST_ASSERT_EQUAL_UINT8(1, protocore_quic_server_active_conns());

    protocore_quic_server_stop();
}

void test_quic_server_close_reaped_before_idle()
{
    fill();
    uint32_t now = 5000;

    QuicServerConfig scfg;
    config(&scfg, test_rng);
    TEST_ASSERT_TRUE(protocore_quic_server_begin(H3_PORT, &scfg, app_request, NULL));

    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);

    uint8_t frames0[64];
    size_t fl0 = protocore_quic_build_ack(frames0, sizeof(frames0), 0, 0, 0);
    uint8_t dg0[256];
    size_t dl0 = build_long(dg0, sizeof(dg0), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            0, &init.client, frames0, fl0);
    feed(dg0, dl0, "192.0.2.20", 40010, now);
    TEST_ASSERT_EQUAL_UINT8(1, protocore_quic_server_active_conns());

    uint8_t cc_frames[64];
    size_t cc_len = protocore_quic_build_connection_close(cc_frames, sizeof(cc_frames), PROTO_FALSE, 0, 0, NULL, 0);
    uint8_t dg1[256];
    size_t dl1 = build_long(dg1, sizeof(dg1), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init.client, cc_frames, cc_len);
    now += 5;
    feed(dg1, dl1, "192.0.2.20", 40010, now);
    TEST_ASSERT_EQUAL_UINT8(0, protocore_quic_server_active_conns());

    protocore_quic_server_stop();
}

void test_quic_server_on_request_null()
{
    fill();

    QuicServerConfig scfg;
    config(&scfg, test_rng);
    TEST_ASSERT_TRUE(protocore_quic_server_begin(H3_PORT, &scfg, NULL, NULL));

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

    feed(dg, dl, "192.0.2.10", 40000, 0);
    TEST_ASSERT_EQUAL_UINT8(1, protocore_quic_server_active_conns());
    TEST_ASSERT_GREATER_THAN(0, g_out_n);

    uint8_t plain[2048], sh[512], hsf[1024];
    size_t wire = 0;
    uint8_t ty = 0;
    size_t pt = open_long(g_out[0], g_out_len[0], &init.server, plain, &wire, &ty);
    size_t shl = extract_crypto(plain, pt, sh);
    uint8_t server_pub[32], ecdhe[32];
    protocore_x25519_base(server_pub, SERVER_PRIV);
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
    static uint8_t ks_store_900[PROTOCORE_TLS13_KS_BORROW];
    protocore_tls13_ks_early(&TLS13_KDF, &cks, ks_store_900);
    protocore_tls13_ks_handshake(&cks, ecdhe, chsh, sizeof(ecdhe));
    QuicPacketKeys hs_s, hs_c, ap_c;
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_SERVER_HS, &hs_s);
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_CLIENT_HS, &hs_c);
    size_t hw = 0;
    uint8_t hty = 0;
    size_t hpt = open_long(g_out[0] + wire, g_out_len[0] - wire, &hs_s, plain, &hw, &hty);
    size_t hsfl = extract_crypto(plain, hpt, hsf);
    protocore_sha256_update(&t, hsf, hsfl);
    protocore_sha256_final(&t, chsf);
    protocore_tls13_ks_master(&cks, chsf);
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
    feed(idg, idl + hdl, "192.0.2.10", 40000, 0);

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
    size_t s1l = build_short(s1, sizeof(s1), SERVER_SCID, sizeof(SERVER_SCID), 0, &ap_c, sfr, sfrl);

    feed(s1, s1l, "192.0.2.10", 40000, 0);

    TEST_ASSERT_EQUAL_STRING("", g_method);
    TEST_ASSERT_EQUAL_UINT8(1, protocore_quic_server_active_conns());

    protocore_quic_server_stop();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_quic_server_http3_get);
    RUN_TEST(test_idle_connection_reaped);
    RUN_TEST(test_quic_server_input_guards);
    RUN_TEST(test_ingest_ring_drops_past_capacity);
    RUN_TEST(test_quic_server_pool_full);
    RUN_TEST(test_quic_server_replies_to_the_captured_peer);
    RUN_TEST(test_quic_server_unrenderable_peer_dropped);
    RUN_TEST(test_quic_server_begin_default_port);
    RUN_TEST(test_quic_server_respond_unknown_id_with_active_conn);
    RUN_TEST(test_quic_server_route_header_edges);
    RUN_TEST(test_quic_server_close_reaped_before_idle);
    RUN_TEST(test_quic_server_on_request_null);
    return UNITY_END();
}
