// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// End-to-end capstone for the whole HTTP/3 stack: a QUIC client (in the test) completes the TLS 1.3
// handshake against a server that is pc_quic_conn + pc_h3_conn wired together, then sends a real HTTP/3
// GET (QPACK HEADERS in a STREAM frame). The server's pc_h3_conn dispatches the request, the app
// responds through pc_h3_conn_respond, and the client decrypts the 1-RTT response and verifies the
// HEADERS (:status 200) + DATA body. Exercises QUIC packet crypto + framing, the TLS 1.3 handshake,
// QUIC streams, HTTP/3 framing, and QPACK, all composed.

#include "crypto/asymmetric/curve25519.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/h3_conn.h"
#include "network_drivers/presentation/http/http3/h3_frame.h"
#include "network_drivers/presentation/http/http3/qpack.h"
#include "network_drivers/presentation/http/http3/quic_conn.h"
#include "network_drivers/presentation/http/http3/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/tls/tls13_kdf.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// Where the QPACK emit callback puts the ":status" it finds. The callback captures nothing, so it
// is a plain function and the buffer travels in the ctx pointer the decoder already carries.
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

static const uint8_t CERT[48] = {0x30, 0x2e, 0x02, 0x01, 0x02};
static uint8_t SERVER_PRIV[32], SERVER_SEED[32], SERVER_RANDOM[32], CLIENT_PRIV[32];
static const uint8_t ODCID[8] = {0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8};
static const uint8_t CLIENT_SCID[4] = {0xc1, 0xc2, 0xc3, 0xc4};
static const uint8_t SERVER_SCID[4] = {0x51, 0x52, 0x53, 0x54};

// The app handler: capture the request and answer 200.
static char g_method[16], g_path[64];
static void app_request(void *app, H3Conn *h3, uint64_t sid, const char *method, const char *path, const char *,
                        const uint8_t *, size_t)
{
    (void)app;
    strncpy(g_method, method, sizeof(g_method) - 1);
    strncpy(g_path, path, sizeof(g_path) - 1);
    pc_h3_conn_respond(h3, sid, 200, "text/plain", (const uint8_t *)"hello h3", 8);
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
}

// --- packet helpers (client side), same construction the engine uses --------------------------
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
    uint8_t pn_len = pc_quic_pn_length(pn, -1);
    size_t p = pc_quic_build_long_header(out, cap, type, QUIC_VERSION_1, dcid, dcl, scid, scl, pn_len);
    if (type == QUIC_LP_INITIAL)
    {
        p += pc_quic_varint_encode(out + p, cap - p, 0);
    }
    p += pc_quic_varint_encode(out + p, cap - p, (uint64_t)pn_len + frame_len + 16);
    size_t pn_off = p;
    wr_pn(out + p, pn, pn_len);
    p += pn_len;
    memcpy(out + p, frames, frame_len);
    return pc_quic_packet_protect(out, cap, pn_off, pn_len, pn, frame_len, keys, PROTO_TRUE);
}
static size_t build_short(uint8_t *out, size_t cap, const uint8_t *dcid, uint8_t dcl, uint64_t pn,
                          const QuicPacketKeys *keys, const uint8_t *frames, size_t frame_len)
{
    uint8_t pn_len = pc_quic_pn_length(pn, -1);
    out[0] = (uint8_t)(0x40 | (pn_len - 1));
    memcpy(out + 1, dcid, dcl);
    size_t pn_off = 1 + dcl;
    wr_pn(out + pn_off, pn, pn_len);
    memcpy(out + pn_off + pn_len, frames, frame_len);
    return pc_quic_packet_protect(out, cap, pn_off, pn_len, pn, frame_len, keys, PROTO_FALSE);
}
static size_t open_long(const uint8_t *dg, size_t len, const QuicPacketKeys *keys, uint8_t *plain, size_t *wire,
                        uint8_t *type)
{
    QuicLongHeader h;
    TEST_ASSERT_TRUE(pc_quic_parse_long_header(dg, len, &h));
    *type = h.type;
    size_t off = h.hdr_len;
    if (h.type == QUIC_LP_INITIAL)
    {
        uint64_t tl = 0;
        size_t c = 0;
        pc_quic_varint_decode(dg + off, len - off, &tl, &c);
        off += c + (size_t)tl;
    }
    uint64_t length = 0;
    size_t c = 0;
    pc_quic_varint_decode(dg + off, len - off, &length, &c);
    off += c;
    *wire = off + (size_t)length;
    static uint8_t work[2048];
    memcpy(work, dg, *wire);
    uint64_t pn = 0;
    return pc_quic_packet_unprotect(work, off, (size_t)length, 0, keys, PROTO_TRUE, plain, &pn);
}
static size_t open_short(const uint8_t *dg, size_t len, uint8_t dcl, const QuicPacketKeys *keys, uint8_t *plain)
{
    static uint8_t work[2048];
    memcpy(work, dg, len);
    uint64_t pn = 0;
    return pc_quic_packet_unprotect(work, 1 + dcl, len - (1 + dcl), 0, keys, PROTO_FALSE, plain, &pn);
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
        size_t n = pc_quic_frame_parse(p + off, len - off, &f);
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

void test_http3_get_end_to_end()
{
    fill();
    // Server: pc_quic_conn + pc_h3_conn wired together.
    QuicTlsConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cert_der = CERT;
    cfg.cert_len = sizeof(CERT);
    memcpy(cfg.ed25519_seed, SERVER_SEED, 32);
    memcpy(cfg.ephemeral_priv, SERVER_PRIV, 32);
    memcpy(cfg.random, SERVER_RANDOM, 32);
    pc_quic_tp_defaults(&cfg.params);
    cfg.params.initial_max_data = 1048576;
    cfg.params.initial_max_sd_bidi_remote = 262144;
    cfg.params.initial_max_streams_bidi = 8;

    QuicConn qc;
    pc_quic_conn_init(&qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                      sizeof(SERVER_SCID), NULL);
    H3Conn h3;
    pc_h3_conn_init(&h3, &qc, app_request, NULL); // installs the QUIC callbacks

    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

    // Client Initial(ClientHello).
    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctpe[128];
    size_t ctpl = pc_quic_tp_encode(&ctp, ctpe, sizeof(ctpe));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t chl = build_client_hello(ch, client_pub, ctpe, ctpl);
    uint8_t frames[1200];
    size_t fl = pc_quic_build_crypto(frames, sizeof(frames), 0, ch, chl);
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    pc_quic_conn_recv(&qc, dg, dl);

    // Server flight -> derive client-side keys.
    uint8_t sdg[1500], plain[2048], sh[512], hsf[1024];
    size_t sl = pc_quic_conn_send(&qc, sdg, sizeof(sdg));
    size_t wire = 0;
    uint8_t ty = 0;
    size_t pt = open_long(sdg, sl, &init.server, plain, &wire, &ty);
    size_t shl = extract_crypto(plain, pt, sh);
    uint8_t server_pub[32], ecdhe[32];
    pc_x25519_base(server_pub, SERVER_PRIV);
    pc_x25519(ecdhe, CLIENT_PRIV, server_pub);
    pc_sha256_ctx t;
    uint8_t chsh[32], chsf[32];
    pc_sha256_init(&t);
    pc_sha256_update(&t, ch, chl);
    pc_sha256_update(&t, sh, shl);
    {
        pc_sha256_ctx tmp = t;
        pc_sha256_final(&tmp, chsh);
    }
    Tls13KeySchedule cks;
    pc_tls13_ks_early(&TLS13_KDF, &cks);
    pc_tls13_ks_handshake(&cks, ecdhe, chsh, 32);
    QuicPacketKeys hs_s, hs_c, ap_s, ap_c;
    pc_quic_keys_from_secret(cks.server_hs_traffic, &hs_s);
    pc_quic_keys_from_secret(cks.client_hs_traffic, &hs_c);
    size_t hw = 0;
    uint8_t hty = 0;
    size_t hpt = open_long(sdg + wire, sl - wire, &hs_s, plain, &hw, &hty);
    size_t hsfl = extract_crypto(plain, hpt, hsf);
    pc_sha256_update(&t, hsf, hsfl);
    pc_sha256_final(&t, chsf);
    pc_tls13_ks_master(&cks, chsf);
    pc_quic_keys_from_secret(cks.server_ap_traffic, &ap_s);
    pc_quic_keys_from_secret(cks.client_ap_traffic, &ap_c);

    // Client Initial(ACK) + Handshake(ACK + Finished) -> server completes + opens h3 streams.
    uint8_t ifr[64];
    size_t ifl = pc_quic_build_ack(ifr, sizeof(ifr), 0, 0, 0);
    uint8_t idg[256];
    size_t idl = build_long(idg, sizeof(idg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init.client, ifr, ifl);
    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    pc_tls13_finished_mac(&TLS13_KDF, cks.client_hs_traffic, chsf, cfin + 4);
    uint8_t hfr[64];
    size_t hfl = pc_quic_build_ack(hfr, sizeof(hfr), 0, 0, 0);
    hfl += pc_quic_build_crypto(hfr + hfl, sizeof(hfr) - hfl, 0, cfin, sizeof(cfin));
    size_t hdl = build_long(idg + idl, sizeof(idg) - idl, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID,
                            sizeof(CLIENT_SCID), 0, &hs_c, hfr, hfl);
    pc_quic_conn_recv(&qc, idg, idl + hdl);
    TEST_ASSERT_TRUE(pc_quic_conn_established(&qc));
    // Drain the server's HANDSHAKE_DONE + control-stream datagram(s).
    while ((sl = pc_quic_conn_send(&qc, sdg, sizeof(sdg))) > 0)
    {
    }

    // --- Client sends an HTTP/3 GET on request stream 0 (1-RTT) ---
    uint8_t block[128];
    size_t bp = pc_qpack_encode_prefix(block, sizeof(block));
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/hello", 6);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":authority", 10, "h3.test", 7);
    uint8_t h3req[256];
    size_t h3l = pc_h3_build_headers(h3req, sizeof(h3req), block, bp);
    uint8_t sfr[300];
    size_t sfrl = pc_quic_build_stream(sfr, sizeof(sfr), 0, 0, h3req, h3l, PROTO_TRUE);
    uint8_t s1[512];
    size_t s1l = build_short(s1, sizeof(s1), SERVER_SCID, sizeof(SERVER_SCID), 0, &ap_c, sfr, sfrl);
    pc_quic_conn_recv(&qc, s1, s1l);

    // pc_h3_conn dispatched the request to the app.
    TEST_ASSERT_EQUAL_STRING("GET", g_method);
    TEST_ASSERT_EQUAL_STRING("/hello", g_path);

    // --- Server response: 1-RTT STREAM with HTTP/3 HEADERS(200) + DATA("hello h3") ---
    proto_bool got = PROTO_FALSE;
    while ((sl = pc_quic_conn_send(&qc, sdg, sizeof(sdg))) > 0)
    {
        size_t off = 0;
        while (off < sl)
        {
            if (pc_quic_is_long_header(sdg[off]))
            {
                size_t w = 0;
                uint8_t tt = 0;
                open_long(sdg + off, sl - off, &hs_s, plain, &w, &tt);
                off += w;
                continue;
            }
            size_t p2 = open_short(sdg + off, sl - off, sizeof(SERVER_SCID), &ap_s, plain);
            if (p2 == SIZE_MAX)
            {
                break;
            }
            // Find the STREAM frame on stream 0 and parse its HTTP/3 frames.
            size_t fo = 0;
            while (fo < p2)
            {
                if (plain[fo] == QUIC_FT_PADDING)
                {
                    fo++;
                    continue;
                }
                QuicFrame f;
                size_t n = pc_quic_frame_parse(plain + fo, p2 - fo, &f);
                if (!n)
                {
                    break;
                }
                fo += n;
                if (f.type >= QUIC_FT_STREAM && f.type <= QUIC_FT_STREAM + 7 && f.stream.id == 0)
                {
                    const uint8_t *sp = f.stream.data;
                    size_t so = 0, sn = (size_t)f.stream.length;
                    char status[8] = {0};
                    proto_bool data_ok = PROTO_FALSE;
                    while (so < sn)
                    {
                        H3Frame hf;
                        if (!pc_h3_frame_parse(sp + so, sn - so, &hf))
                        {
                            break;
                        }
                        const uint8_t *hp = sp + so + hf.header_len;
                        if (hf.type == H3_HEADERS)
                        {
                            char sc[128];
                            H3StatusCtx e = {status};
                            pc_qpack_decode(hp, (size_t)hf.length, sc, sizeof(sc), h3_take_status, &e);
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
                        got = PROTO_TRUE;
                    }
                }
            }
            break;
        }
        if (got)
        {
            break;
        }
    }
    TEST_ASSERT_TRUE(got);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_http3_get_end_to_end);
    return UNITY_END();
}
