// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/asymmetric/curve25519.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/quic_conn.h"
#include "network_drivers/presentation/http/http3/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include <string.h>

#include <unity.h>

static QuicConn g_qc;
static QuicConn g_qc2;

static uint8_t tw[4096];
static uint8_t tw_t[4096];
static uint8_t tw_tctx[4096];

static void assert_ctx_match(uint8_t *a, uint8_t *b)
{
    uint8_t n12[12] = {0}, zpt[16] = {0}, c1[16], t1[16], c2[16], t2[16];
    (void)protocore_aes128gcm_seal((struct protocore_aes128gcm_key *)(a), n12, NULL, 0, zpt, sizeof zpt, c1, t1);
    (void)protocore_aes128gcm_seal((struct protocore_aes128gcm_key *)(b), n12, NULL, 0, zpt, sizeof zpt, c2, t2);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(c1, c2, sizeof c1);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(t1, t2, sizeof t1);
}

void setUp()
{
}
void tearDown()
{
}

static const uint8_t CERT[48] = {0x30, 0x2e, 0x02, 0x01, 0x02};
static uint8_t SERVER_PRIV[32], SERVER_SEED[32], SERVER_RANDOM[32], CLIENT_PRIV[32];
static const uint8_t ODCID[8] = {0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8};
static const uint8_t CLIENT_SCID[4] = {0xc1, 0xc2, 0xc3, 0xc4};
static const uint8_t SERVER_SCID[4] = {0x51, 0x52, 0x53, 0x54};

static proto_bool g_hs_done;
static uint8_t g_stream_data[256];
static size_t g_stream_len;
static uint64_t g_stream_id;
static proto_bool g_stream_fin;

static void on_hs_done(void *, struct QuicConn *)
{
    g_hs_done = PROTO_TRUE;
}
static void on_stream_data(void *, struct QuicConn *, uint64_t id, const uint8_t *data, size_t len, proto_bool fin)
{
    g_stream_id = id;
    if (len && g_stream_len + len <= sizeof(g_stream_data))
    {
        memcpy(g_stream_data + g_stream_len, data, len);
        g_stream_len += len;
    }
    g_stream_fin = fin;
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
    g_hs_done = PROTO_FALSE;
    g_stream_len = 0;
    g_stream_id = 0;
    g_stream_fin = PROTO_FALSE;
}

static size_t build_client_hello(uint8_t *out, const uint8_t client_pub[32], const uint8_t *tp, size_t tp_len)
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
    out[p++] = 0x00;
    out[p++] = 0x00;
    out[p++] = 0x02;
    out[p++] = 0x13;
    out[p++] = 0x01;
    out[p++] = 0x01;
    out[p++] = 0x00;
    size_t ext_at = p;
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
    uint16_t el = (uint16_t)(p - ext_at - 2);
    out[ext_at] = (uint8_t)(el >> 8);
    out[ext_at + 1] = (uint8_t)el;
    uint32_t hl = (uint32_t)(p - hs_len_at - 3);
    out[hs_len_at] = (uint8_t)(hl >> 16);
    out[hs_len_at + 1] = (uint8_t)(hl >> 8);
    out[hs_len_at + 2] = (uint8_t)hl;
    return p;
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
    uint64_t length = (uint64_t)pn_len + frame_len + 16;
    p += protocore_quic_varint_encode(out + p, cap - p, length);
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

static size_t open_long(const uint8_t *dg, size_t len, QuicPacketKeys *keys, uint8_t *plain, size_t *wire_len,
                        uint8_t *type_out)
{
    QuicLongHeader h;
    TEST_ASSERT_TRUE(protocore_quic_parse_long_header(dg, len, &h));
    *type_out = h.type;
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
    *wire_len = off + (size_t)length;
    static uint8_t work[2048];
    memcpy(work, dg, *wire_len);
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

static proto_bool has_frame(const uint8_t *p, size_t len, uint64_t want)
{
    size_t off = 0;
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
        if (f.type == want)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

static void make_cfg(QuicTlsConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->cert_der = CERT;
    cfg->cert_len = sizeof(CERT);
    memcpy(cfg->ed25519_seed, SERVER_SEED, 32);
    memcpy(cfg->ephemeral_priv, SERVER_PRIV, 32);
    memcpy(cfg->random, SERVER_RANDOM, 32);
    protocore_quic_tp_defaults(&cfg->params);
    cfg->params.initial_max_data = 1048576;
    cfg->params.initial_max_sd_bidi_remote = 262144;
    cfg->params.initial_max_streams_bidi = 8;
}

void test_full_handshake_and_stream()
{
    fill();
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    protocore_quic_conn_init(&g_qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                             sizeof(SERVER_SCID), &cb);

    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);

    QuicTransportParams ctp;
    protocore_quic_tp_defaults(&ctp);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctp_enc[128];
    size_t ctp_len = protocore_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    protocore_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);

    uint8_t frames[1200];
    size_t fl = protocore_quic_build_crypto(frames, sizeof(frames), 0, ch, ch_len);
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));

    uint8_t sdg[1500];
    size_t sl = protocore_quic_conn_send(&g_qc, sdg, sizeof(sdg));
    TEST_ASSERT_TRUE(sl > 0);

    uint8_t plain[2048], sh[512], hsflight[1024];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg, sl, &init.server, plain, &wire, &type);
    TEST_ASSERT_EQUAL_UINT8(QUIC_LP_INITIAL, type);
    size_t sh_len = extract_crypto(plain, pt, sh);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_SERVER_HELLO, sh[0]);

    uint8_t server_pub[32], ecdhe[32];
    protocore_x25519_base(server_pub, SERVER_PRIV);
    protocore_x25519(ecdhe, CLIENT_PRIV, server_pub);
    protocore_sha256_ctx t;
    uint8_t ch_sh[32], ch_sf[32];
    protocore_sha256_init(&t, tw_t);
    protocore_sha256_update(&t, ch, ch_len);
    protocore_sha256_update(&t, sh, sh_len);
    {
        protocore_sha256_final(&t, ch_sh);
    }
    Tls13KeySchedule cks;
    static uint8_t ks_store_340[PROTOCORE_TLS13_KS_BORROW];
    protocore_tls13_ks_early(&TLS13_KDF, &cks, ks_store_340);
    protocore_tls13_ks_handshake(&cks, ecdhe, ch_sh, sizeof(ecdhe));
    QuicPacketKeys hs_server_keys, hs_client_keys;
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_SERVER_HS, &hs_server_keys);
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_CLIENT_HS, &hs_client_keys);

    size_t hswire = 0;
    uint8_t hstype = 0;
    size_t hpt = open_long(sdg + wire, sl - wire, &hs_server_keys, plain, &hswire, &hstype);
    TEST_ASSERT_EQUAL_UINT8(QUIC_LP_HANDSHAKE, hstype);
    size_t hsflen = extract_crypto(plain, hpt, hsflight);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_ENCRYPTED_EXTENSIONS, hsflight[0]);

    protocore_sha256_update(&t, hsflight, hsflen);
    protocore_sha256_final(&t, ch_sf);
    protocore_tls13_ks_master(&cks, ch_sf);
    QuicPacketKeys ap_server_keys, ap_client_keys;
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_SERVER_AP, &ap_server_keys);
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_CLIENT_AP, &ap_client_keys);

    assert_ctx_match(g_qc.tls.hs_server.gcm, hs_server_keys.gcm);
    assert_ctx_match(g_qc.tls.ap_server.gcm, ap_server_keys.gcm);

    uint8_t ifr[64];
    size_t ifl = protocore_quic_build_ack(ifr, sizeof(ifr), 0, 0, 0);
    uint8_t idg[256];
    size_t idl = build_long(idg, sizeof(idg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init.client, ifr, ifl);

    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    protocore_tls13_finished_mac(&cks, cks.s + TLS13_KS_CLIENT_HS, ch_sf, cfin + 4);
    uint8_t hfr[64];
    size_t hfl = protocore_quic_build_ack(hfr, sizeof(hfr), 0, 0, 0);
    hfl += protocore_quic_build_crypto(hfr + hfl, sizeof(hfr) - hfl, 0, cfin, sizeof(cfin));
    size_t hdl = build_long(idg + idl, sizeof(idg) - idl, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID,
                            sizeof(CLIENT_SCID), 0, &hs_client_keys, hfr, hfl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, idg, idl + hdl));

    TEST_ASSERT_TRUE(protocore_quic_conn_established(&g_qc));
    TEST_ASSERT_TRUE(g_hs_done);

    sl = protocore_quic_conn_send(&g_qc, sdg, sizeof(sdg));
    TEST_ASSERT_TRUE(sl > 0);
    proto_bool saw_hs_done = PROTO_FALSE;
    size_t off = 0;
    while (off < sl)
    {
        if (protocore_quic_is_long_header(sdg[off]))
        {
            size_t w = 0;
            uint8_t tp2 = 0;
            open_long(sdg + off, sl - off, &hs_server_keys, plain, &w, &tp2);
            off += w;
        }
        else
        {
            size_t p2 = open_short(sdg + off, sl - off, sizeof(SERVER_SCID), &ap_server_keys, plain);
            TEST_ASSERT_NOT_EQUAL(SIZE_MAX, p2);
            if (has_frame(plain, p2, QUIC_FT_HANDSHAKE_DONE))
            {
                saw_hs_done = PROTO_TRUE;
            }
            break;
        }
    }
    TEST_ASSERT_TRUE(saw_hs_done);

    uint8_t sfr[64];
    size_t sfl = protocore_quic_build_stream(sfr, sizeof(sfr), 0, 0, (const uint8_t *)"GET", 3, PROTO_TRUE);
    uint8_t s1[256];
    size_t s1l = build_short(s1, sizeof(s1), SERVER_SCID, sizeof(SERVER_SCID), 0, &ap_client_keys, sfr, sfl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, s1, s1l));
    TEST_ASSERT_EQUAL_UINT64(0, g_stream_id);
    TEST_ASSERT_EQUAL_UINT(3, g_stream_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("GET", g_stream_data, 3);
    TEST_ASSERT_TRUE(g_stream_fin);

    protocore_quic_conn_stream_send(&g_qc, 0, (const uint8_t *)"OK", 2, PROTO_TRUE);
    sl = protocore_quic_conn_send(&g_qc, sdg, sizeof(sdg));
    TEST_ASSERT_TRUE(sl > 0);

    off = 0;
    proto_bool got_resp = PROTO_FALSE;
    while (off < sl)
    {
        if (protocore_quic_is_long_header(sdg[off]))
        {
            size_t w = 0;
            uint8_t tp2 = 0;
            open_long(sdg + off, sl - off, &hs_server_keys, plain, &w, &tp2);
            off += w;
            continue;
        }
        size_t p2 = open_short(sdg + off, sl - off, sizeof(SERVER_SCID), &ap_server_keys, plain);
        TEST_ASSERT_NOT_EQUAL(SIZE_MAX, p2);

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
            if (f.type >= QUIC_FT_STREAM && f.type <= QUIC_FT_STREAM + 7)
            {
                TEST_ASSERT_EQUAL_UINT(2, (size_t)f.stream.length);
                TEST_ASSERT_EQUAL_UINT8_ARRAY("OK", f.stream.data, 2);
                got_resp = PROTO_TRUE;
            }
        }
        break;
    }
    TEST_ASSERT_TRUE(got_resp);

    protocore_quic_conn_on_timeout(&g_qc, 5000);
    protocore_quic_conn_on_timeout(&g_qc, 5000 + PROTOCORE_QUIC_PTO_MS + 1);
    sl = protocore_quic_conn_send(&g_qc, sdg, sizeof(sdg));
    TEST_ASSERT_TRUE(sl > 0);
    proto_bool resent = PROTO_FALSE;
    off = 0;
    while (off < sl)
    {
        if (protocore_quic_is_long_header(sdg[off]))
        {
            size_t w = 0;
            uint8_t tp2 = 0;
            open_long(sdg + off, sl - off, &hs_server_keys, plain, &w, &tp2);
            off += w;
            continue;
        }
        size_t p2 = open_short(sdg + off, sl - off, sizeof(SERVER_SCID), &ap_server_keys, plain);
        TEST_ASSERT_NOT_EQUAL(SIZE_MAX, p2);
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
            if (f.type >= QUIC_FT_STREAM && f.type <= QUIC_FT_STREAM + 7 && f.stream.id == 0 && f.stream.length == 2 &&
                memcmp(f.stream.data, "OK", 2) == 0)
            {
                resent = PROTO_TRUE;
            }
        }
        break;
    }
    TEST_ASSERT_TRUE(resent);
}

void test_pto_retransmits_flight()
{
    fill();
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    protocore_quic_conn_init(&g_qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                             sizeof(SERVER_SCID), &cb);

    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);

    QuicTransportParams ctp;
    protocore_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[128];
    size_t ctp_len = protocore_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    protocore_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    size_t fl = protocore_quic_build_crypto(frames, sizeof(frames), 0, ch, ch_len);
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));

    uint8_t sdg[1500];
    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, sdg, sizeof(sdg)) > 0);

    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, sdg, sizeof(sdg)));
    protocore_quic_conn_on_timeout(&g_qc, 1000);
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, sdg, sizeof(sdg)));

    protocore_quic_conn_on_timeout(&g_qc, 1000 + PROTOCORE_QUIC_PTO_MS + 1);
    uint8_t sdg2[1500];
    size_t sl2 = protocore_quic_conn_send(&g_qc, sdg2, sizeof(sdg2));
    TEST_ASSERT_TRUE(sl2 > 0);
    uint8_t plain[2048], sh[512];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg2, sl2, &init.server, plain, &wire, &type);
    TEST_ASSERT_EQUAL_UINT8(QUIC_LP_INITIAL, type);
    size_t sh_len = extract_crypto(plain, pt, sh);
    TEST_ASSERT_TRUE(sh_len > 0);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_SERVER_HELLO, sh[0]);

    g_qc.space[QUIC_ENC_INITIAL].discarded = PROTO_TRUE;
    g_qc.space[QUIC_ENC_HANDSHAKE].largest_acked = g_qc.space[QUIC_ENC_HANDSHAKE].last_ae_pn;
    protocore_quic_conn_on_timeout(&g_qc, 1000 + 10 * PROTOCORE_QUIC_PTO_MS);
    TEST_ASSERT_FALSE(g_qc.pto_armed);
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, sdg2, sizeof(sdg2)));
}

static void feed_client_initial(struct QuicConn *qc, QuicConnCallbacks *cb, QuicInitialSecrets *init, uint8_t *ch,
                                size_t *ch_len)
{
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    protocore_quic_conn_init(qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                             sizeof(SERVER_SCID), cb);
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), init);
    QuicTransportParams ctp;
    protocore_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[128];
    size_t ctp_len = protocore_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    protocore_x25519_base(client_pub, CLIENT_PRIV);
    *ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    size_t fl = protocore_quic_build_crypto(frames, sizeof(frames), 0, ch, *ch_len);
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init->client, frames, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(qc, dg, dl));
}

void test_connection_close_api()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&g_qc, &cb, &init, ch, &ch_len);

    protocore_quic_conn_close(&g_qc, QUIC_ERR_NO_ERROR);
    uint8_t cdg[512];
    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, cdg, sizeof(cdg)) > 0);
    TEST_ASSERT_TRUE(protocore_quic_conn_is_closed(&g_qc));
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, cdg, sizeof(cdg)));
}

void test_connection_close_on_malformed_frame()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&g_qc, &cb, &init, ch, &ch_len);

    uint8_t sdg[1500];
    size_t sl = protocore_quic_conn_send(&g_qc, sdg, sizeof(sdg));
    TEST_ASSERT_TRUE(sl > 0);
    uint8_t plain[2048], sh[512];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg, sl, &init.server, plain, &wire, &type);
    size_t sh_len = extract_crypto(plain, pt, sh);
    uint8_t server_pub[32], ecdhe[32];
    protocore_x25519_base(server_pub, SERVER_PRIV);
    protocore_x25519(ecdhe, CLIENT_PRIV, server_pub);
    protocore_sha256_ctx tctx;
    uint8_t ch_sh[32];
    protocore_sha256_init(&tctx, tw_tctx);
    protocore_sha256_update(&tctx, ch, ch_len);
    protocore_sha256_update(&tctx, sh, sh_len);
    protocore_sha256_final(&tctx, ch_sh);
    Tls13KeySchedule cks;
    static uint8_t ks_store_652[PROTOCORE_TLS13_KS_BORROW];
    protocore_tls13_ks_early(&TLS13_KDF, &cks, ks_store_652);
    protocore_tls13_ks_handshake(&cks, ecdhe, ch_sh, sizeof(ecdhe));
    QuicPacketKeys hs_server_keys, hs_client_keys;
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_SERVER_HS, &hs_server_keys);
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_CLIENT_HS, &hs_client_keys);

    uint8_t bad[4] = {QUIC_FT_CRYPTO, 0x00, 0x7f, 0xff};
    uint8_t bdg[256];
    size_t bl = build_long(bdg, sizeof(bdg), QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                           0, &hs_client_keys, bad, sizeof(bad));
    protocore_quic_conn_recv(&g_qc, bdg, bl);

    uint8_t cdg[512];
    size_t cl = protocore_quic_conn_send(&g_qc, cdg, sizeof(cdg));
    TEST_ASSERT_TRUE(cl > 0);
    size_t cw = 0;
    uint8_t ctype = 0;
    size_t cpt = open_long(cdg, cl, &hs_server_keys, plain, &cw, &ctype);
    TEST_ASSERT_NOT_EQUAL(SIZE_MAX, cpt);
    proto_bool saw = PROTO_FALSE;
    size_t fo = 0;
    while (fo < cpt)
    {
        if (plain[fo] == QUIC_FT_PADDING)
        {
            fo++;
            continue;
        }
        QuicFrame f;
        size_t n = protocore_quic_frame_parse(plain + fo, cpt - fo, &f);
        if (!n)
        {
            break;
        }
        fo += n;
        if (f.type == QUIC_FT_CONNECTION_CLOSE)
        {
            saw = PROTO_TRUE;
            TEST_ASSERT_EQUAL_UINT64(QUIC_ERR_FRAME_ENCODING, f.close.error_code);
        }
    }
    TEST_ASSERT_TRUE(saw);
    TEST_ASSERT_TRUE(protocore_quic_conn_is_closed(&g_qc));
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, cdg, sizeof(cdg)));
}

static void init_conn(struct QuicConn *qc, QuicConnCallbacks *cb)
{
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    protocore_quic_conn_init(qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                             sizeof(SERVER_SCID), cb);
}

void test_quic_recv_connection_close()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);

    uint8_t fr[32];
    size_t fl = protocore_quic_build_connection_close(fr, sizeof(fr), PROTO_FALSE, QUIC_ERR_NO_ERROR, 0, NULL, 0);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    protocore_quic_conn_recv(&g_qc, dg, dl);
    TEST_ASSERT_TRUE(protocore_quic_conn_is_closed(&g_qc));

    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, dl));
}

void test_quic_recv_ping_and_max_data()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);

    uint8_t fr[16];
    size_t fl = protocore_quic_build_ping(fr, sizeof(fr));
    fl += protocore_quic_build_max_data(fr + fl, sizeof(fr) - fl, 1000000);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));
}

void test_quic_recv_bad_version()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    dg[1] = dg[2] = dg[3] = dg[4] = 0xAA;
    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, dl));
}

void test_quic_recv_unsupported_long_type()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_0RTT, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, dl));
}

void test_quic_recv_short_before_app_keys()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_short(dg, sizeof(dg), SERVER_SCID, sizeof(SERVER_SCID), 0, &init.client, fr, 1);
    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, dl));
}

void test_quic_recv_short_too_short()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    uint8_t dg[1] = {0x40};
    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, sizeof(dg)));
}

void test_quic_recv_unprotect_failure()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    dg[dl - 1] ^= 0xFF;
    protocore_quic_conn_recv(&g_qc, dg, dl);
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));
    TEST_ASSERT_FALSE(protocore_quic_conn_established(&g_qc));
}

void test_quic_recv_truncated_long_header()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    uint8_t dg[4] = {0xC0, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, sizeof(dg)));
}

void test_quic_send_amplification_limited()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    uint8_t out[256];
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, out, sizeof(out)));
}

void test_quic_crypto_out_of_order_and_dup()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t data[4] = {0x01, 0x00, 0x00, 0xFF};
    uint8_t fr[32], dg[256];

    size_t fl = protocore_quic_build_crypto(fr, sizeof(fr), 100, data, sizeof(data));
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));

    fl = protocore_quic_build_crypto(fr, sizeof(fr), 0, data, sizeof(data));
    dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 1,
                    &init.client, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    uint8_t dg2[256];
    size_t dl2 = build_long(dg2, sizeof(dg2), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            2, &init.client, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg2, dl2));
}

void test_quic_timeout_when_closed()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t fr[32];
    size_t fl = protocore_quic_build_connection_close(fr, sizeof(fr), PROTO_FALSE, QUIC_ERR_NO_ERROR, 0, NULL, 0);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    protocore_quic_conn_recv(&g_qc, dg, dl);
    TEST_ASSERT_TRUE(protocore_quic_conn_is_closed(&g_qc));
    protocore_quic_conn_on_timeout(&g_qc, 1000);
}

void test_quic_stream_send_table_full()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    for (int i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        TEST_ASSERT_EQUAL_UINT(
            2, protocore_quic_conn_stream_send(&g_qc, (uint64_t)(i * 4), (const uint8_t *)"hi", 2, PROTO_FALSE));
    }
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_stream_send(&g_qc, 999, (const uint8_t *)"x", 1, PROTO_FALSE));
}

void test_quic_recv_malformed_initial_headers()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    uint8_t dg[1500];

    size_t hn = protocore_quic_build_long_header(dg, sizeof dg, QUIC_LP_INITIAL, QUIC_VERSION_1, ODCID, sizeof(ODCID),
                                                 CLIENT_SCID, sizeof(CLIENT_SCID), 1);
    dg[hn] = 0xC0;
    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, hn + 1));

    dg[hn] = 0x40;
    dg[hn + 1] = 0xFF;
    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, hn + 2));

    dg[hn] = 0x00;
    dg[hn + 1] = 0xC0;
    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, hn + 2));

    dg[hn] = 0x00;
    dg[hn + 1] = 0x44;
    dg[hn + 2] = 0x00;
    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, hn + 8));

    dg[hn] = 0x00;
    size_t c = protocore_quic_varint_encode(dg + hn + 1, sizeof(dg) - hn - 1, 1400);
    memset(dg + hn + 1 + c, 0, 1450 - (hn + 1 + c));
    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, 1450));
}

void test_quic_recv_handshake_done_frame()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t hd[32];
    size_t hdl = protocore_quic_build_handshake_done(hd, sizeof hd);
    memset(hd + hdl, 0, 20);
    hdl += 20;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, hd, hdl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));
}

void test_quic_conn_stream_frames()
{
    fill();
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t dg[1500];

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        uint8_t data[4] = {1, 2, 3, 4};
        uint8_t fr[32];
        size_t fl = protocore_quic_build_stream(fr, sizeof fr, 0, 100, data, 4, PROTO_FALSE);
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
        g_stream_len = 0;
        protocore_quic_conn_recv(&g_qc, dg, dl);
        TEST_ASSERT_EQUAL_UINT(0, g_stream_len);
    }

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        uint8_t d0 = 0;
        uint8_t fr[16];
        size_t fl = protocore_quic_build_stream(fr, sizeof fr, 0, 0, &d0, 0, PROTO_TRUE);
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
        g_stream_fin = PROTO_FALSE;
        protocore_quic_conn_recv(&g_qc, dg, dl);
        TEST_ASSERT_TRUE(g_stream_fin);
    }

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        uint8_t d1 = 0x55;
        uint8_t fr[512];
        size_t fl = 0;
        for (int i = 0; i <= PROTOCORE_QUIC_MAX_STREAMS; i++)
        {
            fl += protocore_quic_build_stream(fr + fl, sizeof(fr) - fl, (uint64_t)(i * 4), 0, &d1, 1, PROTO_FALSE);
        }
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
        TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    }
}

void test_quic_conn_crypto_window_clamp()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t dg[1500];
    uint8_t chunk[1200];
    chunk[0] = 0x01;
    chunk[1] = 0x00;
    chunk[2] = 0xFF;
    chunk[3] = 0xFF;
    memset(chunk + 4, 0, sizeof(chunk) - 4);
    uint8_t fr[1300];
    size_t fl = protocore_quic_build_crypto(fr, sizeof fr, 0, chunk, sizeof chunk);
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));
    fl = protocore_quic_build_crypto(fr, sizeof fr, 1200, chunk, sizeof chunk);
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 1, &init.client, fr, fl);
    protocore_quic_conn_recv(&g_qc, dg, dl);
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));
}

void test_quic_conn_crypto_error_close()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t bad_ch[6] = {0x01, 0x00, 0x00, 0x02, 0x03, 0x03};
    uint8_t fr[32];
    size_t fl = protocore_quic_build_crypto(fr, sizeof fr, 0, bad_ch, sizeof bad_ch);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
    protocore_quic_conn_recv(&g_qc, dg, dl);

    protocore_quic_conn_close(&g_qc, 0);
    uint8_t out[256];
    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, out, sizeof out) > 0);
}

void test_quic_conn_no_keys_build()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t fr[32] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, sizeof fr);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    uint8_t out[256];

    (void)protocore_quic_conn_send(&g_qc, out, sizeof out);
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));
}

void test_quic_conn_pto_not_yet()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&g_qc, &cb, &init, ch, &ch_len);
    uint8_t out[2048];
    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, out, sizeof out) > 0);
    protocore_quic_conn_on_timeout(&g_qc, 0);
    protocore_quic_conn_on_timeout(&g_qc, 1);
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));
}

void test_quic_conn_send_tiny_cap()
{
    for (size_t cap = 1; cap <= 40; cap++)
    {
        fill();
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        QuicInitialSecrets init;
        uint8_t ch[512];
        size_t ch_len = 0;
        feed_client_initial(&g_qc, &cb, &init, ch, &ch_len);
        uint8_t out[64];
        (void)protocore_quic_conn_send(&g_qc, out, cap);
    }
}

static void complete_handshake(struct QuicConn *qc, QuicConnCallbacks *cb, QuicInitialSecrets *init,
                               QuicPacketKeys *ap_client, QuicPacketKeys *ap_server, uint8_t peer_scid_len)
{
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    protocore_quic_conn_init(qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, peer_scid_len, SERVER_SCID,
                             sizeof(SERVER_SCID), cb);
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), init);

    QuicTransportParams ctp;
    protocore_quic_tp_defaults(&ctp);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctp_enc[128];
    size_t ctp_len = protocore_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    protocore_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    size_t fl = protocore_quic_build_crypto(frames, sizeof(frames), 0, ch, ch_len);
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init->client, frames, fl);
    protocore_quic_conn_recv(qc, dg, dl);

    uint8_t sdg[1500];
    size_t sl = protocore_quic_conn_send(qc, sdg, sizeof(sdg));
    uint8_t plain[2048], sh[512], hsflight[1024];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg, sl, &init->server, plain, &wire, &type);
    size_t sh_len = extract_crypto(plain, pt, sh);

    uint8_t server_pub[32], ecdhe[32];
    protocore_x25519_base(server_pub, SERVER_PRIV);
    protocore_x25519(ecdhe, CLIENT_PRIV, server_pub);
    protocore_sha256_ctx t;
    uint8_t ch_sh[32], ch_sf[32];
    protocore_sha256_init(&t, tw_t);
    protocore_sha256_update(&t, ch, ch_len);
    protocore_sha256_update(&t, sh, sh_len);
    {
        protocore_sha256_final(&t, ch_sh);
    }
    Tls13KeySchedule cks;
    static uint8_t ks_store_1181[PROTOCORE_TLS13_KS_BORROW];
    protocore_tls13_ks_early(&TLS13_KDF, &cks, ks_store_1181);
    protocore_tls13_ks_handshake(&cks, ecdhe, ch_sh, sizeof(ecdhe));
    QuicPacketKeys hs_server_keys, hs_client_keys;
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_SERVER_HS, &hs_server_keys);
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_CLIENT_HS, &hs_client_keys);
    size_t hswire = 0;
    uint8_t hstype = 0;
    size_t hpt = open_long(sdg + wire, sl - wire, &hs_server_keys, plain, &hswire, &hstype);
    size_t hsflen = extract_crypto(plain, hpt, hsflight);
    protocore_sha256_update(&t, hsflight, hsflen);
    protocore_sha256_final(&t, ch_sf);
    protocore_tls13_ks_master(&cks, ch_sf);
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_CLIENT_AP, ap_client);
    protocore_quic_keys_from_secret(tw, cks.s + TLS13_KS_SERVER_AP, ap_server);

    uint8_t ifr[64];
    size_t ifl = protocore_quic_build_ack(ifr, sizeof(ifr), 0, 0, 0);
    uint8_t idg[256];
    size_t idl = build_long(idg, sizeof(idg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init->client, ifr, ifl);
    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    protocore_tls13_finished_mac(&cks, cks.s + TLS13_KS_CLIENT_HS, ch_sf, cfin + 4);
    uint8_t hfr[64];
    size_t hfl = protocore_quic_build_ack(hfr, sizeof(hfr), 0, 0, 0);
    hfl += protocore_quic_build_crypto(hfr + hfl, sizeof(hfr) - hfl, 0, cfin, sizeof(cfin));
    size_t hdl = build_long(idg + idl, sizeof(idg) - idl, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID,
                            sizeof(CLIENT_SCID), 0, &hs_client_keys, hfr, hfl);
    protocore_quic_conn_recv(qc, idg, idl + hdl);
    protocore_quic_conn_send(qc, sdg, sizeof(sdg));
}

void test_quic_conn_stream_nothing_to_send()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));
    uint8_t out[512];
    TEST_ASSERT_EQUAL_UINT(2, protocore_quic_conn_stream_send(&g_qc, 0, (const uint8_t *)"OK", 2, PROTO_TRUE));
    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, out, sizeof out) > 0);
    protocore_quic_conn_send(&g_qc, out, sizeof out);
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));
}

void test_quic_conn_short_header_tiny_cap()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));
    protocore_quic_conn_stream_send(&g_qc, 0, (const uint8_t *)"DATA", 4, PROTO_FALSE);
    uint8_t out[8];
    (void)protocore_quic_conn_send(&g_qc, out, 4);
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));
}

void test_quic_conn_close_level_fallback()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));

    uint8_t bad[20] = {0x06, 0x00, 0x44, 0x00};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 5,
                           &init.client, bad, sizeof bad);
    protocore_quic_conn_recv(&g_qc, dg, dl);
    uint8_t out[256];
    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, out, sizeof out) > 0);
    TEST_ASSERT_TRUE(protocore_quic_conn_is_closed(&g_qc));
}

void test_quic_conn_null_callbacks()
{
    fill();
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    protocore_quic_conn_init(&g_qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                             sizeof(SERVER_SCID), NULL);
    TEST_ASSERT_NULL(g_qc.cb.on_stream_data);
    TEST_ASSERT_NULL(g_qc.cb.on_handshake_done);

    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);

    uint8_t d[3] = {1, 2, 3};
    uint8_t fr[64];
    size_t fl = protocore_quic_build_stream(fr, sizeof fr, 0, 0, d, 3, PROTO_FALSE);
    fl += protocore_quic_build_stream(fr + fl, sizeof(fr) - fl, 0, 3, d, 0, PROTO_TRUE);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT(0, g_stream_len);
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));

    QuicPacketKeys apc, aps;
    QuicInitialSecrets init2;
    complete_handshake(&g_qc2, NULL, &init2, &apc, &aps, sizeof(CLIENT_SCID));
    TEST_ASSERT_TRUE(protocore_quic_conn_established(&g_qc2));
    TEST_ASSERT_FALSE(g_hs_done);
}

void test_quic_conn_stream_duplicate_and_stale_fin()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t d[4] = {0xA1, 0xA2, 0xA3, 0xA4};
    uint8_t fr[64], dg[256];
    uint64_t pn = 0;

    size_t fl = protocore_quic_build_stream(fr, sizeof fr, 0, 0, d, 4, PROTO_FALSE);
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT(4, g_stream_len);

    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT(4, g_stream_len);

    fl = protocore_quic_build_stream(fr, sizeof fr, 0, 0, d, 0, PROTO_TRUE);
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    g_stream_fin = PROTO_FALSE;
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_FALSE(g_stream_fin);

    fl = protocore_quic_build_stream(fr, sizeof fr, 0, 4, d, 0, PROTO_TRUE);
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_TRUE(g_stream_fin);

    g_stream_fin = PROTO_FALSE;
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_FALSE(g_stream_fin);
}

void test_quic_conn_frame_dispatch_variants()
{
    fill();
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t dg[512];

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);

        const uint8_t ack_ecn[8] = {0x03, 5, 0, 0, 0, 0, 0, 0};
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, ack_ecn,
                               sizeof ack_ecn);
        TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
        TEST_ASSERT_EQUAL_INT64(5, g_qc.space[QUIC_ENC_INITIAL].largest_acked);
        TEST_ASSERT_FALSE(g_qc.space[QUIC_ENC_INITIAL].ack_eliciting_rx);

        uint8_t older[8];
        size_t ol = protocore_quic_build_ack(older, sizeof older, 3, 0, 0);
        dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 1, &init.client, older, ol);
        TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
        TEST_ASSERT_EQUAL_INT64(5, g_qc.space[QUIC_ENC_INITIAL].largest_acked);
    }

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        const uint8_t app_close[3] = {0x1d, 0x05, 0x00};
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, app_close,
                               sizeof app_close);
        TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
        TEST_ASSERT_TRUE(protocore_quic_conn_is_closed(&g_qc));
        TEST_ASSERT_FALSE(g_qc.space[QUIC_ENC_INITIAL].ack_eliciting_rx);
    }

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        const uint8_t misc[6] = {QUIC_FT_RESET_STREAM, 0x00, 0x01, 0x02, QUIC_FT_MAX_STREAMS_BIDI, 0x08};
        size_t dl =
            build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, misc, sizeof misc);
        TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
        TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));
        TEST_ASSERT_EQUAL_UINT(0, g_stream_len);
    }
}

void test_quic_recv_zero_version()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    dg[1] = dg[2] = dg[3] = dg[4] = 0x00;
    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, dl));
}

void test_quic_recv_older_packet_number()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);
    uint8_t fr[24] = {QUIC_FT_PING};
    uint8_t dg[256];

    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 5, &init.client, fr, sizeof fr);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT64(5, g_qc.space[QUIC_ENC_INITIAL].largest_rx);

    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 1, &init.client, fr, sizeof fr);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT64(5, g_qc.space[QUIC_ENC_INITIAL].largest_rx);
}

void test_quic_recv_short_header_decrypt_failure()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));

    uint8_t fr[32];
    size_t fl = protocore_quic_build_ping(fr, sizeof fr);
    memset(fr + fl, 0, 20);
    fl += 20;
    uint8_t dg[256];
    size_t dl = build_short(dg, sizeof dg, SERVER_SCID, sizeof(SERVER_SCID), 0, &apc, fr, fl);
    dg[dl - 1] ^= 0xFF;
    TEST_ASSERT_FALSE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));
}

void test_quic_conn_crypto_after_handshake_done()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));
    TEST_ASSERT_TRUE(g_qc.handshake_done_sent);
    TEST_ASSERT_FALSE(g_qc.handshake_done_queued);

    QuicPacketKeys hs_client_keys;
    {

        hs_client_keys = g_qc.tls.hs_client;
    }
    uint64_t off = g_qc.space[QUIC_ENC_HANDSHAKE].crypto_rx_off;
    const uint8_t frag[2] = {0xFE, 0xFF};
    uint8_t fr[64];
    size_t fl = protocore_quic_build_crypto(fr, sizeof fr, off, frag, sizeof frag);
    memset(fr + fl, 0, 20);
    fl += 20;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 7,
                           &hs_client_keys, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT8(QTLS_DONE, g_qc.tls.state);
    TEST_ASSERT_FALSE(g_qc.handshake_done_queued);
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));

    g_qc.handshake_done_sent = PROTO_FALSE;
    g_qc.handshake_done_queued = PROTO_TRUE;
    g_hs_done = PROTO_FALSE;
    off = g_qc.space[QUIC_ENC_HANDSHAKE].crypto_rx_off;
    TEST_ASSERT_TRUE(off > 0);
    fl = protocore_quic_build_crypto(fr, sizeof fr, off, frag, sizeof frag);
    memset(fr + fl, 0, 20);
    fl += 20;
    dl = build_long(dg, sizeof dg, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 8,
                    &hs_client_keys, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT64(off + sizeof(frag), g_qc.space[QUIC_ENC_HANDSHAKE].crypto_rx_off);
    TEST_ASSERT_TRUE(g_qc.handshake_done_queued);
    TEST_ASSERT_FALSE(g_qc.handshake_done_sent);
    TEST_ASSERT_FALSE(g_hs_done);
}

void test_quic_conn_close_after_peer_close()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);

    uint8_t fr[32];
    size_t fl = protocore_quic_build_connection_close(fr, sizeof fr, PROTO_FALSE, QUIC_ERR_NO_ERROR, 0, NULL, 0);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    protocore_quic_conn_recv(&g_qc, dg, dl);
    TEST_ASSERT_TRUE(g_qc.closed);
    TEST_ASSERT_TRUE(g_qc.draining);

    protocore_quic_conn_close(&g_qc, QUIC_ERR_FRAME_ENCODING);
    TEST_ASSERT_FALSE(g_qc.close_queued);
    uint8_t out[512];
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, out, sizeof out));
}

void test_quic_conn_close_queued_then_peer_close()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&g_qc, &cb, &init, ch, &ch_len);

    g_qc.address_validated = PROTO_TRUE;
    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    for (int i = 0; i < 8 && protocore_quic_conn_send(&g_qc, out, sizeof out) > 0; i++)
    {
    }
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, out, sizeof out));

    protocore_quic_conn_close(&g_qc, QUIC_ERR_NO_ERROR);
    TEST_ASSERT_TRUE(g_qc.close_queued);

    uint8_t fr[32];
    size_t fl = protocore_quic_build_connection_close(fr, sizeof fr, PROTO_FALSE, QUIC_ERR_NO_ERROR, 0, NULL, 0);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 1,
                           &init.client, fr, fl);
    protocore_quic_conn_recv(&g_qc, dg, dl);
    TEST_ASSERT_TRUE(g_qc.draining);

    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, out, sizeof out) > 0);
    TEST_ASSERT_TRUE(g_qc.close_sent);

    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, out, sizeof out));
}

void test_quic_conn_close_send_no_room()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&g_qc, &cb, &init, ch, &ch_len);

    protocore_quic_conn_close(&g_qc, QUIC_ERR_NO_ERROR);
    uint8_t tiny[8];
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, tiny, sizeof tiny));
    TEST_ASSERT_FALSE(g_qc.close_sent);
    TEST_ASSERT_FALSE(g_qc.closed);

    uint8_t out[512];
    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, out, sizeof out) > 0);
    TEST_ASSERT_TRUE(g_qc.close_sent);
}

void test_quic_conn_close_level_out_of_range()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&g_qc, &cb, &init, ch, &ch_len);

    protocore_quic_conn_close(&g_qc, QUIC_ERR_NO_ERROR);
    g_qc.close_level = 200;
    uint8_t out[512];
    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, out, sizeof out) > 0);
    TEST_ASSERT_TRUE(protocore_quic_conn_is_closed(&g_qc));
}

void test_quic_conn_highest_sealed_level_fallback()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    for (int l = QUIC_ENC_INITIAL; l <= QUIC_ENC_APP; l++)
    {
        g_qc.space[l].discarded = PROTO_TRUE;
    }

    protocore_quic_conn_close(&g_qc, QUIC_ERR_NO_ERROR);
    TEST_ASSERT_TRUE(g_qc.close_queued);
    TEST_ASSERT_EQUAL_UINT8(QUIC_ENC_INITIAL, g_qc.close_level);
}

void test_quic_conn_crypto_flight_fragmented()
{
    fill();
    static uint8_t big_cert[1500];
    memset(big_cert, 0x30, sizeof(big_cert));
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    cfg.cert_der = big_cert;
    cfg.cert_len = sizeof(big_cert);

    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    protocore_quic_conn_init(&g_qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                             sizeof(SERVER_SCID), &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);

    QuicTransportParams ctp;
    protocore_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[128];
    size_t ctp_len = protocore_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    protocore_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t chb[512];
    size_t ch_len = build_client_hello(chb, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    size_t fl = protocore_quic_build_crypto(frames, sizeof(frames), 0, chb, ch_len);
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT8(QTLS_WAIT_FINISHED, g_qc.tls.state);

    size_t hs_flight_len = 0;
    (void)protocore_quic_tls_flight(&g_qc.tls, QUIC_ENC_HANDSHAKE, &hs_flight_len);
    TEST_ASSERT_TRUE(hs_flight_len > PROTOCORE_QUIC_MAX_DATAGRAM);

    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    size_t first = protocore_quic_conn_send(&g_qc, out, sizeof out);
    TEST_ASSERT_TRUE(first > 0);
    uint64_t sent_after_first = g_qc.space[QUIC_ENC_HANDSHAKE].crypto_tx_off;
    TEST_ASSERT_TRUE(sent_after_first > 0);
    TEST_ASSERT_TRUE(sent_after_first < hs_flight_len);

    g_qc.address_validated = PROTO_TRUE;
    size_t total = sent_after_first;
    for (int i = 0; i < 8 && total < hs_flight_len; i++)
    {
        TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, out, sizeof out) > 0);
        total = g_qc.space[QUIC_ENC_HANDSHAKE].crypto_tx_off;
    }
    TEST_ASSERT_EQUAL_UINT64(hs_flight_len, total);
}

void test_quic_conn_stream_tx_partitioning()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));

    uint8_t d[2] = {0x11, 0x22};
    uint8_t fr[64];
    size_t fl = protocore_quic_build_stream(fr, sizeof fr, 0, 0, d, 2, PROTO_FALSE);
    uint8_t sdg[256];
    size_t sl = build_short(sdg, sizeof sdg, SERVER_SCID, sizeof(SERVER_SCID), 1, &apc, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, sdg, sl));
    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    (void)protocore_quic_conn_send(&g_qc, out, sizeof out);

    static uint8_t big[PROTOCORE_QUIC_STREAM_TX - 64];
    memset(big, 0x5A, sizeof(big));
    size_t queued = protocore_quic_conn_stream_send(&g_qc, 0, big, sizeof(big), PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT(sizeof(big), queued);
    QuicStream *st = NULL;
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        if (g_qc.streams[i].id == 0)
        {
            st = &g_qc.streams[i];
        }
    }
    TEST_ASSERT_NOT_NULL(st);

    (void)protocore_quic_conn_send(&g_qc, out, sizeof out);
    TEST_ASSERT_TRUE(st->tx_sent > 0);
    TEST_ASSERT_TRUE(st->tx_sent < st->tx_have);
    TEST_ASSERT_EQUAL_UINT64(st->tx_sent, st->tx_off);
    TEST_ASSERT_FALSE(st->tx_fin_sent);

    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, out, sizeof out) > 0);
    TEST_ASSERT_EQUAL_UINT(st->tx_have, st->tx_sent);
    TEST_ASSERT_TRUE(st->tx_fin_sent);

    TEST_ASSERT_EQUAL_UINT(2, protocore_quic_conn_stream_send(&g_qc, 0, d, 2, PROTO_FALSE));
    size_t before = st->tx_sent;
    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, out, sizeof out) > 0);
    TEST_ASSERT_EQUAL_UINT(before + 2, st->tx_sent);
    TEST_ASSERT_TRUE(st->tx_fin_sent);
}

void test_quic_conn_stream_fin_only()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));

    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    TEST_ASSERT_EQUAL_UINT(4, protocore_quic_conn_stream_send(&g_qc, 0, (const uint8_t *)"BODY", 4, PROTO_FALSE));
    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, out, sizeof out) > 0);
    QuicStream *st = NULL;
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        if (g_qc.streams[i].id == 0)
        {
            st = &g_qc.streams[i];
        }
    }
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT(st->tx_have, st->tx_sent);
    TEST_ASSERT_FALSE(st->tx_fin_sent);

    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_stream_send(&g_qc, 0, (const uint8_t *)"", 0, PROTO_TRUE));
    TEST_ASSERT_TRUE(st->tx_fin);
    size_t n = protocore_quic_conn_send(&g_qc, out, sizeof out);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(st->tx_fin_sent);

    uint8_t plain[PROTOCORE_QUIC_MAX_DATAGRAM];
    size_t pt = open_short(out, n, sizeof(CLIENT_SCID), &aps, plain);
    TEST_ASSERT_NOT_EQUAL(SIZE_MAX, pt);
    QuicFrame f;
    size_t got = protocore_quic_frame_parse(plain, pt, &f);
    TEST_ASSERT_TRUE(got > 0);
    TEST_ASSERT_TRUE(f.type >= QUIC_FT_STREAM);
    TEST_ASSERT_EQUAL_UINT64(0, f.stream.length);
    TEST_ASSERT_TRUE(f.stream.fin);

    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, out, sizeof out));
}

void test_quic_conn_stream_tx_datagram_full()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));

    static uint8_t payload[500];
    memset(payload, 0x77, sizeof(payload));
    uint64_t ids[PROTOCORE_QUIC_MAX_STREAMS];
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        ids[i] = 0x3F00000000000000ull + (uint64_t)(i * 4);
        TEST_ASSERT_EQUAL_UINT(sizeof(payload),
                               protocore_quic_conn_stream_send(&g_qc, ids[i], payload, sizeof(payload), PROTO_FALSE));
    }

    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    (void)protocore_quic_conn_send(&g_qc, out, sizeof out);

    size_t untouched = 0, partial = 0;
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        for (size_t j = 0; j < PROTOCORE_QUIC_MAX_STREAMS; j++)
        {
            if (g_qc.streams[j].id == ids[i])
            {
                if (g_qc.streams[j].tx_sent == 0)
                {
                    untouched++;
                }
                else if (g_qc.streams[j].tx_sent < g_qc.streams[j].tx_have)
                {
                    partial++;
                }
            }
        }
    }
    TEST_ASSERT_TRUE(untouched > 0);
    TEST_ASSERT_TRUE(partial > 0);

    g_qc.address_validated = PROTO_TRUE;
    for (int round = 0; round < 20; round++)
    {
        (void)protocore_quic_conn_send(&g_qc, out, sizeof out);
    }
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        for (size_t j = 0; j < PROTOCORE_QUIC_MAX_STREAMS; j++)
        {
            if (g_qc.streams[j].id == ids[i])
            {
                TEST_ASSERT_EQUAL_UINT(g_qc.streams[j].tx_have, g_qc.streams[j].tx_sent);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, out, sizeof out));
}

void test_quic_conn_stream_send_clamped()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);

    static uint8_t huge[PROTOCORE_QUIC_STREAM_TX + 512];
    memset(huge, 0x2B, sizeof(huge));
    size_t took = protocore_quic_conn_stream_send(&g_qc, 0, huge, sizeof(huge), PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_QUIC_STREAM_TX, took);

    QuicStream *st = NULL;
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        if (g_qc.streams[i].id == 0)
        {
            st = &g_qc.streams[i];
        }
    }
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_QUIC_STREAM_TX, st->tx_have);
    TEST_ASSERT_FALSE(st->tx_fin);
}

void test_quic_conn_stream_send_sentinel_id()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));

    TEST_ASSERT_EQUAL_UINT(2, protocore_quic_conn_stream_send(&g_qc, UINT64_MAX, (const uint8_t *)"hi", 2, PROTO_TRUE));
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, g_qc.streams[i].id);
    }

    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, out, sizeof out));
}

void test_quic_conn_pto_backoff_ceiling()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&g_qc, &cb, &init, ch, &ch_len);
    uint8_t out[2048];
    TEST_ASSERT_TRUE(protocore_quic_conn_send(&g_qc, out, sizeof out) > 0);

    uint32_t now = 1000;
    protocore_quic_conn_on_timeout(&g_qc, now);
    TEST_ASSERT_TRUE(g_qc.pto_armed);

    for (int i = 0; i < 12; i++)
    {
        now = g_qc.pto_deadline_ms + 1;
        protocore_quic_conn_on_timeout(&g_qc, now);
    }
    TEST_ASSERT_EQUAL_UINT8(8, g_qc.pto_count);

    g_qc.pto_count = 40;
    g_qc.pto_armed = PROTO_FALSE;
    protocore_quic_conn_on_timeout(&g_qc, 0);
    TEST_ASSERT_TRUE(g_qc.pto_armed);
    TEST_ASSERT_EQUAL_UINT32(2097152000u, g_qc.pto_deadline_ms);
}

void test_quic_conn_ack_owed_without_rx()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));
    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, out, sizeof out));

    g_qc.space[QUIC_ENC_APP].ack_eliciting_rx = PROTO_TRUE;
    g_qc.space[QUIC_ENC_APP].have_rx = PROTO_FALSE;
    TEST_ASSERT_EQUAL_UINT(0, protocore_quic_conn_send(&g_qc, out, sizeof out));
    TEST_ASSERT_TRUE(g_qc.space[QUIC_ENC_APP].ack_eliciting_rx);
}

void test_quic_conn_close_level_without_keys()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    protocore_quic_derive_initial_secrets(tw, ODCID, sizeof(ODCID), &init);

    uint8_t fr[64] = {QUIC_FT_PING};
    uint8_t dg[512];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, sizeof fr);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT8(QTLS_START, g_qc.tls.state);

    protocore_quic_conn_close(&g_qc, QUIC_ERR_NO_ERROR);
    TEST_ASSERT_TRUE(g_qc.close_queued);
    g_qc.close_level = QUIC_ENC_APP;
    TEST_ASSERT_FALSE(g_qc.space[QUIC_ENC_APP].discarded);

    uint8_t out[512];
    size_t n = protocore_quic_conn_send(&g_qc, out, sizeof out);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(protocore_quic_conn_is_closed(&g_qc));

    uint8_t plain[512];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(out, n, &init.server, plain, &wire, &type);
    TEST_ASSERT_EQUAL_UINT8(QUIC_LP_INITIAL, type);
    TEST_ASSERT_NOT_EQUAL(SIZE_MAX, pt);
    TEST_ASSERT_TRUE(has_frame(plain, pt, QUIC_FT_CONNECTION_CLOSE));
}

void test_quic_conn_is_closed_draining_only()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));

    g_qc.draining = PROTO_TRUE;
    TEST_ASSERT_FALSE(g_qc.closed);
    TEST_ASSERT_TRUE(protocore_quic_conn_is_closed(&g_qc));
}

void test_quic_conn_pto_outstanding_per_space()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    static const int levels[3] = {QUIC_ENC_INITIAL, QUIC_ENC_HANDSHAKE, QUIC_ENC_APP};

    for (int i = 0; i < 3; i++)
    {
        init_conn(&g_qc, &cb);

        g_qc.space[levels[i]].last_ae_pn = 3;
        g_qc.space[levels[i]].largest_acked = 2;
        protocore_quic_conn_on_timeout(&g_qc, 1000);
        TEST_ASSERT_TRUE(g_qc.pto_armed);
        TEST_ASSERT_EQUAL_UINT32(1000 + PROTOCORE_QUIC_PTO_MS, g_qc.pto_deadline_ms);
    }

    init_conn(&g_qc, &cb);
    g_qc.pto_armed = PROTO_TRUE;
    g_qc.pto_count = 3;
    protocore_quic_conn_on_timeout(&g_qc, 1000);
    TEST_ASSERT_FALSE(g_qc.pto_armed);
    TEST_ASSERT_EQUAL_UINT8(0, g_qc.pto_count);

    init_conn(&g_qc, &cb);
    g_qc.space[QUIC_ENC_APP].last_ae_pn = 3;
    g_qc.space[QUIC_ENC_APP].largest_acked = 2;
    g_qc.space[QUIC_ENC_APP].discarded = PROTO_TRUE;
    protocore_quic_conn_on_timeout(&g_qc, 1000);
    TEST_ASSERT_FALSE(g_qc.pto_armed);
}

void test_quic_conn_pto_disarms_when_all_acked()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));

    protocore_quic_conn_on_timeout(&g_qc, 1000);
    TEST_ASSERT_TRUE(g_qc.pto_armed);

    uint8_t fr[32];
    size_t fl = protocore_quic_build_ack(fr, sizeof fr, 0, 0, 0);
    uint8_t dg[256];
    size_t dl = build_short(dg, sizeof dg, SERVER_SCID, sizeof(SERVER_SCID), 0, &apc, fr, fl);
    TEST_ASSERT_TRUE(protocore_quic_conn_recv(&g_qc, dg, dl));
    TEST_ASSERT_EQUAL_INT64(0, g_qc.space[QUIC_ENC_APP].largest_acked);

    g_qc.pto_armed = PROTO_TRUE;
    protocore_quic_conn_on_timeout(&g_qc, 2000);
    TEST_ASSERT_FALSE(g_qc.pto_armed);
    TEST_ASSERT_EQUAL_UINT8(0, g_qc.pto_count);
    TEST_ASSERT_FALSE(protocore_quic_conn_is_closed(&g_qc));
}

void test_quic_conn_pto_requeues_handshake_done_once()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));
    TEST_ASSERT_TRUE(g_qc.handshake_done_sent);

    protocore_quic_conn_on_timeout(&g_qc, 1000);
    protocore_quic_conn_on_timeout(&g_qc, 1000 + PROTOCORE_QUIC_PTO_MS + 1);
    TEST_ASSERT_TRUE(g_qc.handshake_done_queued);
    TEST_ASSERT_FALSE(g_qc.handshake_done_sent);

    protocore_quic_conn_on_timeout(&g_qc, g_qc.pto_deadline_ms + 1);
    TEST_ASSERT_TRUE(g_qc.handshake_done_queued);
    TEST_ASSERT_FALSE(g_qc.handshake_done_sent);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_handshake_and_stream);
    RUN_TEST(test_quic_conn_null_callbacks);
    RUN_TEST(test_quic_conn_stream_duplicate_and_stale_fin);
    RUN_TEST(test_quic_conn_frame_dispatch_variants);
    RUN_TEST(test_quic_recv_zero_version);
    RUN_TEST(test_quic_recv_older_packet_number);
    RUN_TEST(test_quic_recv_short_header_decrypt_failure);
    RUN_TEST(test_quic_conn_crypto_after_handshake_done);
    RUN_TEST(test_quic_conn_close_after_peer_close);
    RUN_TEST(test_quic_conn_close_queued_then_peer_close);
    RUN_TEST(test_quic_conn_close_send_no_room);
    RUN_TEST(test_quic_conn_close_level_out_of_range);
    RUN_TEST(test_quic_conn_highest_sealed_level_fallback);
    RUN_TEST(test_quic_conn_crypto_flight_fragmented);
    RUN_TEST(test_quic_conn_stream_tx_partitioning);
    RUN_TEST(test_quic_conn_stream_tx_datagram_full);
    RUN_TEST(test_quic_conn_stream_fin_only);
    RUN_TEST(test_quic_conn_stream_send_clamped);
    RUN_TEST(test_quic_conn_stream_send_sentinel_id);
    RUN_TEST(test_quic_conn_pto_backoff_ceiling);
    RUN_TEST(test_quic_conn_ack_owed_without_rx);
    RUN_TEST(test_quic_conn_close_level_without_keys);
    RUN_TEST(test_quic_conn_is_closed_draining_only);
    RUN_TEST(test_quic_conn_pto_outstanding_per_space);
    RUN_TEST(test_quic_conn_pto_disarms_when_all_acked);
    RUN_TEST(test_quic_conn_pto_requeues_handshake_done_once);
    RUN_TEST(test_pto_retransmits_flight);
    RUN_TEST(test_connection_close_api);
    RUN_TEST(test_connection_close_on_malformed_frame);
    RUN_TEST(test_quic_send_amplification_limited);
    RUN_TEST(test_quic_crypto_out_of_order_and_dup);
    RUN_TEST(test_quic_timeout_when_closed);
    RUN_TEST(test_quic_stream_send_table_full);
    RUN_TEST(test_quic_recv_connection_close);
    RUN_TEST(test_quic_recv_ping_and_max_data);
    RUN_TEST(test_quic_recv_bad_version);
    RUN_TEST(test_quic_recv_unsupported_long_type);
    RUN_TEST(test_quic_recv_short_before_app_keys);
    RUN_TEST(test_quic_recv_short_too_short);
    RUN_TEST(test_quic_recv_unprotect_failure);
    RUN_TEST(test_quic_recv_truncated_long_header);
    RUN_TEST(test_quic_recv_malformed_initial_headers);
    RUN_TEST(test_quic_recv_handshake_done_frame);
    RUN_TEST(test_quic_conn_stream_frames);
    RUN_TEST(test_quic_conn_crypto_window_clamp);
    RUN_TEST(test_quic_conn_crypto_error_close);
    RUN_TEST(test_quic_conn_no_keys_build);
    RUN_TEST(test_quic_conn_pto_not_yet);
    RUN_TEST(test_quic_conn_send_tiny_cap);
    RUN_TEST(test_quic_conn_stream_nothing_to_send);
    RUN_TEST(test_quic_conn_short_header_tiny_cap);
    RUN_TEST(test_quic_conn_close_level_fallback);
    return UNITY_END();
}
