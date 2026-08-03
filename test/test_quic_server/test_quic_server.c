// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// HTTP/3 server-glue test: the same end-to-end flow as test_h3_e2e (a QUIC client completes the
// TLS 1.3 handshake, sends an HTTP/3 GET, and verifies the 200 + body), but driven THROUGH the
// pc_quic_server module - pc_quic_server_begin / pc_quic_server_ingest / pc_quic_server_poll /
// pc_quic_server_respond with the outbound datagrams captured by the host output sink. It exercises what
// pc_quic_server adds over the raw engines: opening a connection from a client Initial, routing later datagrams by
// Destination Connection ID (odcid for the long-header handshake packets, our chosen SCID for the
// 1-RTT short header), the ingest ring, and the request/response callback seam.

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
#include "network_drivers/tls/tls13_kdf.h"
#include <string.h>
#include <unity.h>

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
static const uint8_t SERVER_SCID[8] = {0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58};

// The server's RNG is driven so the handshake is reproducible: the ephemeral X25519 key must be
// SERVER_PRIV (the client computes the shared secret against x25519_base(SERVER_PRIV)); the SCID is a
// known value so the client can address the 1-RTT short header to it.
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
        memcpy(out, SERVER_SCID, len); // PC_QUIC_SCID_LEN == sizeof(SERVER_SCID)
    }
    g_rng_call++;
}

// Captured outbound datagrams (server -> client) from the current poll.
static uint8_t g_out[16][1500];
static size_t g_out_len[16];
static int g_out_n;
static void out_sink(void *, const uint8_t *dg, size_t len, const char *, uint16_t)
{
    if (g_out_n < 16 && len <= sizeof(g_out[0]))
    {
        memcpy(g_out[g_out_n], dg, len);
        g_out_len[g_out_n] = len;
        g_out_n++;
    }
}

// The application handler: capture the request and answer 200 through the server API.
static char g_method[16], g_path[64];
static void app_request(void *, uint32_t conn_id, uint64_t sid, const char *method, const char *path, const char *,
                        const uint8_t *, size_t)
{
    strncpy(g_method, method, sizeof(g_method) - 1);
    strncpy(g_path, path, sizeof(g_path) - 1);
    pc_quic_server_respond(conn_id, sid, 200, "text/plain", (const uint8_t *)"hello h3", 8);
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

// --- packet helpers (client side), identical construction to the engine's -------------------
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
    return pc_quic_packet_protect(out, cap, pn_off, pn_len, pn, frame_len, keys, true);
}
static size_t build_short(uint8_t *out, size_t cap, const uint8_t *dcid, uint8_t dcl, uint64_t pn, QuicPacketKeys *keys,
                          const uint8_t *frames, size_t frame_len)
{
    uint8_t pn_len = pc_quic_pn_length(pn, -1);
    out[0] = (uint8_t)(0x40 | (pn_len - 1));
    memcpy(out + 1, dcid, dcl);
    size_t pn_off = 1 + dcl;
    wr_pn(out + pn_off, pn, pn_len);
    memcpy(out + pn_off + pn_len, frames, frame_len);
    return pc_quic_packet_protect(out, cap, pn_off, pn_len, pn, frame_len, keys, false);
}
static size_t open_long(const uint8_t *dg, size_t len, QuicPacketKeys *keys, uint8_t *plain, size_t *wire,
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
    return pc_quic_packet_unprotect(work, off, (size_t)length, 0, keys, true, plain, &pn);
}
static size_t open_short(const uint8_t *dg, size_t len, uint8_t dcl, QuicPacketKeys *keys, uint8_t *plain)
{
    static uint8_t work[2048];
    memcpy(work, dg, len);
    uint64_t pn = 0;
    return pc_quic_packet_unprotect(work, 1 + dcl, len - (1 + dcl), 0, keys, false, plain, &pn);
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

// Scan every captured datagram for a 1-RTT STREAM frame on stream 0 carrying HTTP/3
// HEADERS(:status 200) + DATA("hello h3"); returns true if found.
static bool response_ok(QuicPacketKeys *ap_s)
{
    uint8_t plain[2048];
    for (int d = 0; d < g_out_n; d++)
    {
        const uint8_t *dg = g_out[d];
        size_t len = g_out_len[d];
        if (pc_quic_is_long_header(dg[0]))
        {
            continue; // handshake / coalesced long-header packets: not the 1-RTT response
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
            size_t n = pc_quic_frame_parse(plain + fo, p2 - fo, &f);
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
            bool data_ok = false;
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
                    struct E
                    {
                        char *s;
                    } e = {status};
                    pc_qpack_decode(
                        hp, (size_t)hf.length, sc, sizeof(sc),
                        [](void *c, const char *nm, size_t nl, const char *v, size_t vl) -> bool {
                            if (nl == 7 && memcmp(nm, ":status", 7) == 0)
                            {
                                memcpy(((E *)c)->s, v, vl);
                                ((E *)c)->s[vl] = 0;
                            }
                            return true;
                        },
                        &e);
                }
                else if (hf.type == H3_DATA)
                {
                    if (hf.length == 8 && memcmp(hp, "hello h3", 8) == 0)
                    {
                        data_ok = true;
                    }
                }
                so += hf.header_len + (size_t)hf.length;
            }
            if (strcmp(status, "200") == 0 && data_ok)
            {
                return true;
            }
        }
    }
    return false;
}

void test_quic_server_http3_get()
{
    fill();

    // Start the server: it owns the QuicConn + H3Conn pool and routes by connection ID.
    QuicServerConfig scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.cert_der = CERT;
    scfg.cert_len = sizeof(CERT);
    memcpy(scfg.ed25519_seed, SERVER_SEED, 32);
    scfg.rng = test_rng;
    pc_quic_server_set_out_sink_cb(out_sink, NULL);
    TEST_ASSERT_TRUE(pc_quic_server_begin(443, &scfg, app_request, NULL));

    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

    // Client Initial(ClientHello), padded to the 1200-byte minimum.
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

    // Deliver it: pc_quic_server opens the connection and produces the server flight.
    g_out_n = 0;
    TEST_ASSERT_TRUE(pc_quic_server_ingest(dg, dl, "192.0.2.10", 40000));
    pc_quic_server_poll(0);
    TEST_ASSERT_EQUAL_UINT8(1, pc_quic_server_active_conns());
    TEST_ASSERT_GREATER_THAN(0, g_out_n); // the coalesced Initial(SH) + Handshake(EE..Finished) flight

    // Derive the client-side handshake + 1-RTT keys from the captured flight.
    uint8_t plain[2048], sh[512], hsf[1024];
    size_t wire = 0;
    uint8_t ty = 0;
    size_t pt = open_long(g_out[0], g_out_len[0], &init.server, plain, &wire, &ty);
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
    pc_tls13_ks_handshake(&cks, ecdhe, chsh);
    QuicPacketKeys hs_s, hs_c, ap_s, ap_c;
    pc_quic_keys_from_secret(cks.server_hs_traffic, &hs_s);
    pc_quic_keys_from_secret(cks.client_hs_traffic, &hs_c);
    size_t hw = 0;
    uint8_t hty = 0;
    size_t hpt = open_long(g_out[0] + wire, g_out_len[0] - wire, &hs_s, plain, &hw, &hty);
    size_t hsfl = extract_crypto(plain, hpt, hsf);
    pc_sha256_update(&t, hsf, hsfl);
    pc_sha256_final(&t, chsf);
    pc_tls13_ks_master(&cks, chsf);
    pc_quic_keys_from_secret(cks.server_ap_traffic, &ap_s);
    pc_quic_keys_from_secret(cks.client_ap_traffic, &ap_c);

    // Client Initial(ACK) + Handshake(ACK + Finished) -> server completes the handshake.
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
    g_out_n = 0;
    TEST_ASSERT_TRUE(pc_quic_server_ingest(idg, idl + hdl, "192.0.2.10", 40000));
    pc_quic_server_poll(0); // drains HANDSHAKE_DONE + the server's control/QPACK streams (ignored)

    // Client HTTP/3 GET on request stream 0 (1-RTT). The short-header DCID is the server's SCID.
    uint8_t block[128];
    size_t bp = pc_qpack_encode_prefix(block, sizeof(block));
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/hello", 6);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":authority", 10, "h3.test", 7);
    uint8_t h3req[256];
    size_t h3l = pc_h3_build_headers(h3req, sizeof(h3req), block, bp);
    uint8_t sfr[300];
    size_t sfrl = pc_quic_build_stream(sfr, sizeof(sfr), 0, 0, h3req, h3l, true);
    uint8_t s1[512];
    size_t s1l = build_short(s1, sizeof(s1), SERVER_SCID, sizeof(SERVER_SCID), 0, &ap_c, sfr, sfrl);

    g_out_n = 0;
    TEST_ASSERT_TRUE(pc_quic_server_ingest(s1, s1l, "192.0.2.10", 40000));
    pc_quic_server_poll(0); // dispatches the request to app_request, which responds; flushed here

    // The request reached the application through the server callback...
    TEST_ASSERT_EQUAL_STRING("GET", g_method);
    TEST_ASSERT_EQUAL_STRING("/hello", g_path);
    // ...and the 200 + body came back on the 1-RTT request stream.
    TEST_ASSERT_TRUE(response_ok(&ap_s));

    pc_quic_server_stop();
    TEST_ASSERT_EQUAL_UINT8(0, pc_quic_server_active_conns());
}

// A connection with no traffic past the idle timeout is reclaimed (the fixed pool cannot be leaked
// by a client that never closes). The caller-supplied clock is advanced by hand.
void test_idle_connection_reaped()
{
    fill();
    uint32_t now = 100000;

    QuicServerConfig scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.cert_der = CERT;
    scfg.cert_len = sizeof(CERT);
    memcpy(scfg.ed25519_seed, SERVER_SEED, 32);
    scfg.rng = test_rng;
    pc_quic_server_set_out_sink_cb(out_sink, NULL);
    TEST_ASSERT_TRUE(pc_quic_server_begin(443, &scfg, app_request, NULL));

    // Open a connection with a client Initial (the handshake need not complete to hold a slot).
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
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
    pc_quic_server_ingest(dg, dl, "192.0.2.10", 40000);
    pc_quic_server_poll(now);
    TEST_ASSERT_EQUAL_UINT8(1, pc_quic_server_active_conns());

    // Just before the timeout it is kept; just after, a poll reclaims it.
    now += PC_QUIC_IDLE_MS - 1;
    pc_quic_server_poll(now);
    TEST_ASSERT_EQUAL_UINT8(1, pc_quic_server_active_conns());
    now += 2;
    pc_quic_server_poll(now);
    TEST_ASSERT_EQUAL_UINT8(0, pc_quic_server_active_conns());

    pc_quic_server_stop();
}

// An RNG safe for any length / call count (test_rng only sources one handshake's fixed material).
static void bulk_rng(uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        out[i] = (uint8_t)(i * 7 + 1);
    }
}

// A minimal Initial (a single ACK frame) with the given DCID - enough for the router to mark it an
// unmatched Initial and for the engine to allocate a slot; no full handshake is needed.
static size_t make_min_initial(uint8_t *dg, size_t cap, const uint8_t *dcid, uint8_t dcl)
{
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(dcid, dcl, &init);
    uint8_t frames[64];
    size_t fl = pc_quic_build_ack(frames, sizeof(frames), 0, 0, 0);
    return build_long(dg, cap, QUIC_LP_INITIAL, dcid, dcl, CLIENT_SCID, sizeof(CLIENT_SCID), 0, &init.client, frames,
                      fl);
}

void test_quic_server_input_guards()
{
    fill();
    // begin() rejects a null config and a null RNG.
    TEST_ASSERT_FALSE(pc_quic_server_begin(443, NULL, app_request, NULL));
    QuicServerConfig scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.cert_der = CERT;
    scfg.cert_len = sizeof(CERT);
    memcpy(scfg.ed25519_seed, SERVER_SEED, 32);
    scfg.rng = NULL;
    TEST_ASSERT_FALSE(pc_quic_server_begin(443, &scfg, app_request, NULL));

    // poll() before a successful begin is a no-op (running == false).
    pc_quic_server_stop();
    pc_quic_server_poll(0);

    scfg.rng = test_rng;
    pc_quic_server_set_out_sink_cb(out_sink, NULL);
    TEST_ASSERT_TRUE(pc_quic_server_begin(443, &scfg, app_request, NULL));

    // ingest() rejects an empty or oversized datagram.
    uint8_t one[1] = {0x40};
    TEST_ASSERT_FALSE(pc_quic_server_ingest(one, 0, "192.0.2.1", 1));
    static uint8_t huge[PC_QUIC_MAX_DATAGRAM + 1];
    TEST_ASSERT_FALSE(pc_quic_server_ingest(huge, sizeof(huge), "192.0.2.1", 1));

    // respond() with an unknown connection id fails.
    TEST_ASSERT_FALSE(pc_quic_server_respond(999999, 0, 200, "text/plain", NULL, 0));

    // Route guards via poll: a malformed long header, a too-short short header, and a short header
    // with an unknown DCID each route to nothing and are dropped.
    uint8_t bad_long[1] = {0xC0}; // long-header bit set but truncated -> parse fails
    TEST_ASSERT_TRUE(pc_quic_server_ingest(bad_long, 1, "192.0.2.1", 1));
    uint8_t short_tiny[2] = {0x40, 0x00}; // short header shorter than 1 + SCID_LEN
    TEST_ASSERT_TRUE(pc_quic_server_ingest(short_tiny, 2, "192.0.2.1", 1));
    uint8_t short_unknown[1 + PC_QUIC_SCID_LEN] = {0x40, 9, 9, 9, 9, 9, 9, 9, 9}; // no matching conn
    TEST_ASSERT_TRUE(pc_quic_server_ingest(short_unknown, sizeof(short_unknown), "192.0.2.1", 1));
    pc_quic_server_poll(0);
    TEST_ASSERT_EQUAL_UINT8(0, pc_quic_server_active_conns());

    // Ring full: without polling, the ring holds PC_QUIC_INGEST_RING-1 entries, then drops.
    pc_quic_server_begin(443, &scfg, app_request, NULL); // reset the ring cursors
    int pushed = 0;
    for (int i = 0; i < PC_QUIC_INGEST_RING + 4; i++)
    {
        if (pc_quic_server_ingest(one, 1, "192.0.2.1", 1))
        {
            pushed++;
        }
    }
    TEST_ASSERT_EQUAL_INT(PC_QUIC_INGEST_RING - 1, pushed);
    pc_quic_server_stop();
}

void test_quic_server_pool_full()
{
    fill();
    QuicServerConfig scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.cert_der = CERT;
    scfg.cert_len = sizeof(CERT);
    memcpy(scfg.ed25519_seed, SERVER_SEED, 32);
    scfg.rng = bulk_rng; // several connections are opened, so the RNG must serve any call count
    pc_quic_server_set_out_sink_cb(out_sink, NULL);
    TEST_ASSERT_TRUE(pc_quic_server_begin(443, &scfg, app_request, NULL));

    // One more distinct-DCID Initial than the pool holds: the extra alloc_slot()/open_conn() fails.
    for (int i = 0; i <= PC_QUIC_MAX_CONNS; i++)
    {
        uint8_t dcid[8] = {0xA0, (uint8_t)i, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5};
        uint8_t dg[256];
        size_t dl = make_min_initial(dg, sizeof(dg), dcid, sizeof(dcid));
        TEST_ASSERT_TRUE(pc_quic_server_ingest(dg, dl, "192.0.2.10", 40000));
    }
    pc_quic_server_poll(0);
    TEST_ASSERT_EQUAL_UINT8(PC_QUIC_MAX_CONNS, pc_quic_server_active_conns()); // capped at the pool size
    pc_quic_server_stop();
}

// copy_str (peer-ip capture) is fed a null ip and an ip longer than the 16-byte peer_ip/ring ip
// fields: the "if (src)" guard and the "n + 1 < cap" capacity guard must each be exercised. The
// datagram content is irrelevant - a bare short header is enough since it never matches a connection.
void test_quic_server_copy_str_edges()
{
    fill();
    QuicServerConfig scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.cert_der = CERT;
    scfg.cert_len = sizeof(CERT);
    memcpy(scfg.ed25519_seed, SERVER_SEED, 32);
    scfg.rng = test_rng;
    pc_quic_server_set_out_sink_cb(out_sink, NULL);
    TEST_ASSERT_TRUE(pc_quic_server_begin(443, &scfg, app_request, NULL));

    uint8_t one[1] = {0x40};
    TEST_ASSERT_TRUE(pc_quic_server_ingest(one, sizeof(one), NULL, 1));
    TEST_ASSERT_TRUE(pc_quic_server_ingest(one, sizeof(one), "AAAAAAAAAAAAAAAAAAAA", 2)); // 20 chars > ip[16]

    pc_quic_server_poll(0);
    TEST_ASSERT_EQUAL_UINT8(0, pc_quic_server_active_conns()); // neither datagram matched or opened a connection

    pc_quic_server_stop();
}

// With no output sink registered (host build), server_send's "if (s_quic.out_sink)" guard must
// silently drop the handshake-response flight rather than call through a null pointer.
void test_quic_server_no_out_sink()
{
    fill();
    pc_quic_server_set_out_sink_cb(NULL, NULL); // guard against a previous test's sink lingering

    QuicServerConfig scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.cert_der = CERT;
    scfg.cert_len = sizeof(CERT);
    memcpy(scfg.ed25519_seed, SERVER_SEED, 32);
    scfg.rng = test_rng;
    TEST_ASSERT_TRUE(pc_quic_server_begin(443, &scfg, app_request, NULL));

    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
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

    TEST_ASSERT_TRUE(pc_quic_server_ingest(dg, dl, "192.0.2.10", 40000));
    pc_quic_server_poll(0); // generates a handshake-response flight that server_send must drop silently
    TEST_ASSERT_EQUAL_UINT8(1, pc_quic_server_active_conns());
    TEST_ASSERT_EQUAL_INT(0, g_out_n); // no sink registered -> nothing captured, but no crash either

    pc_quic_server_stop();
    pc_quic_server_set_out_sink_cb(out_sink, NULL); // restore for any test relying on the global sink
}

// pc_quic_server_respond() with an unknown conn_id while a real connection IS active: slot_by_id's
// loop must fall through the id mismatch (used == true, id != conn_id), not just the used == false
// case already covered by test_quic_server_input_guards.
void test_quic_server_respond_unknown_id_with_active_conn()
{
    fill();
    QuicServerConfig scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.cert_der = CERT;
    scfg.cert_len = sizeof(CERT);
    memcpy(scfg.ed25519_seed, SERVER_SEED, 32);
    scfg.rng = test_rng;
    pc_quic_server_set_out_sink_cb(out_sink, NULL);
    TEST_ASSERT_TRUE(pc_quic_server_begin(443, &scfg, app_request, NULL));

    uint8_t dg0[256];
    size_t dl0 = make_min_initial(dg0, sizeof(dg0), ODCID, sizeof(ODCID));
    TEST_ASSERT_TRUE(pc_quic_server_ingest(dg0, dl0, "192.0.2.10", 40000));
    pc_quic_server_poll(0);
    TEST_ASSERT_EQUAL_UINT8(1, pc_quic_server_active_conns()); // the one active conn gets id 1 (next_id starts at 1)

    TEST_ASSERT_FALSE(pc_quic_server_respond(2, 0, 200, "text/plain", NULL, 0)); // no slot has id 2

    pc_quic_server_stop();
}

// pc_quic_server_begin() with port == 0 must fall back to PC_HTTP3_PORT (every other test in this
// file passes a nonzero port, so the ternary's zero branch is otherwise never taken).
void test_quic_server_begin_default_port()
{
    fill();
    QuicServerConfig scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.cert_der = CERT;
    scfg.cert_len = sizeof(CERT);
    memcpy(scfg.ed25519_seed, SERVER_SEED, 32);
    scfg.rng = test_rng;
    pc_quic_server_set_out_sink_cb(out_sink, NULL);
    TEST_ASSERT_TRUE(pc_quic_server_begin(0, &scfg, app_request, NULL));
    pc_quic_server_stop();
}

// route()'s long-header cid_eq matching (against an open connection's SCID and ODCID) and its
// version/type guard for treating an unmatched packet as a new Initial, plus the short-header
// matching loop, each need their untaken branch exercised: a DCID whose LENGTH differs from the
// open connection's SCID/ODCID (cid_eq's alen==blen short-circuit), a DCID that exactly equals the
// open connection's SCID (cid_eq's memcmp short-circuit match), a non-INITIAL long header (the
// type != INITIAL branch), an unsupported version (the version != 1 branch), and a short header with
// the right length but the wrong SCID content (the memcmp mismatch branch).
void test_quic_server_route_header_edges()
{
    fill();
    QuicServerConfig scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.cert_der = CERT;
    scfg.cert_len = sizeof(CERT);
    memcpy(scfg.ed25519_seed, SERVER_SEED, 32);
    scfg.rng = test_rng; // 3rd rng call yields SERVER_SCID, a known value to address packets to
    pc_quic_server_set_out_sink_cb(out_sink, NULL);
    TEST_ASSERT_TRUE(pc_quic_server_begin(443, &scfg, app_request, NULL));

    // Open one connection so the loops below have a real "used" slot to compare against.
    uint8_t dg0[256];
    size_t dl0 = make_min_initial(dg0, sizeof(dg0), ODCID, sizeof(ODCID));
    TEST_ASSERT_TRUE(pc_quic_server_ingest(dg0, dl0, "192.0.2.10", 40000));
    pc_quic_server_poll(0);
    TEST_ASSERT_EQUAL_UINT8(1, pc_quic_server_active_conns());

    // A long-header HANDSHAKE packet (not INITIAL) with a DCID SHORTER than the open connection's
    // 8-byte SCID/ODCID: cid_eq's alen == blen short-circuits false against both, and the
    // version/type guard's "type != INITIAL" branch keeps it from opening a new connection.
    uint8_t short_dcid[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t hs_hdr[64];
    size_t hs_len = pc_quic_build_long_header(hs_hdr, sizeof(hs_hdr), QUIC_LP_HANDSHAKE, QUIC_VERSION_1, short_dcid,
                                              sizeof(short_dcid), CLIENT_SCID, sizeof(CLIENT_SCID), 1);
    TEST_ASSERT_TRUE(pc_quic_server_ingest(hs_hdr, hs_len, "192.0.2.11", 40001));

    // A long-header Initial with an unsupported version: the "version == 1" branch goes false and
    // short-circuits before the type check.
    uint8_t other_dcid[8] = {0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78};
    uint8_t ver_hdr[64];
    size_t ver_len = pc_quic_build_long_header(ver_hdr, sizeof(ver_hdr), QUIC_LP_INITIAL, 0xAABBCCDDu, other_dcid,
                                               sizeof(other_dcid), CLIENT_SCID, sizeof(CLIENT_SCID), 1);
    TEST_ASSERT_TRUE(pc_quic_server_ingest(ver_hdr, ver_len, "192.0.2.12", 40002));

    // A long-header HANDSHAKE packet whose DCID is exactly the open connection's SCID (SERVER_SCID):
    // cid_eq's alen == blen AND memcmp == 0 both go true, so it matches by SCID directly (the ODCID
    // compare is short-circuited away).
    uint8_t scid_hdr[64];
    size_t scid_hdr_len =
        pc_quic_build_long_header(scid_hdr, sizeof(scid_hdr), QUIC_LP_HANDSHAKE, QUIC_VERSION_1, SERVER_SCID,
                                  sizeof(SERVER_SCID), CLIENT_SCID, sizeof(CLIENT_SCID), 1);
    TEST_ASSERT_TRUE(pc_quic_server_ingest(scid_hdr, scid_hdr_len, "192.0.2.13", 40003));

    pc_quic_server_poll(0);
    TEST_ASSERT_EQUAL_UINT8(1, pc_quic_server_active_conns()); // still just the one connection

    // A short header (1-RTT) with the right length but a SCID that does not match: the memcmp
    // mismatch branch of the short-header routing loop.
    uint8_t wrong_scid[1 + PC_QUIC_SCID_LEN];
    wrong_scid[0] = 0x40;
    for (int i = 0; i < PC_QUIC_SCID_LEN; i++)
    {
        wrong_scid[1 + i] = (uint8_t)(0xEE + i);
    }
    TEST_ASSERT_TRUE(pc_quic_server_ingest(wrong_scid, sizeof(wrong_scid), "192.0.2.14", 40004));
    pc_quic_server_poll(0);
    TEST_ASSERT_EQUAL_UINT8(1, pc_quic_server_active_conns());

    pc_quic_server_stop();
}

// A client CONNECTION_CLOSE well before the idle timeout: pc_quic_conn_is_closed() must reap the
// slot on its own (flush_and_reap's "||" short-circuits before the idle-timeout comparison), not
// just via the idle path already covered by test_idle_connection_reaped.
void test_quic_server_close_reaped_before_idle()
{
    fill();
    uint32_t now = 5000;

    QuicServerConfig scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.cert_der = CERT;
    scfg.cert_len = sizeof(CERT);
    memcpy(scfg.ed25519_seed, SERVER_SEED, 32);
    scfg.rng = test_rng;
    pc_quic_server_set_out_sink_cb(out_sink, NULL);
    TEST_ASSERT_TRUE(pc_quic_server_begin(443, &scfg, app_request, NULL));

    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

    uint8_t frames0[64];
    size_t fl0 = pc_quic_build_ack(frames0, sizeof(frames0), 0, 0, 0);
    uint8_t dg0[256];
    size_t dl0 = build_long(dg0, sizeof(dg0), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            0, &init.client, frames0, fl0);
    TEST_ASSERT_TRUE(pc_quic_server_ingest(dg0, dl0, "192.0.2.20", 40010));
    pc_quic_server_poll(now);
    TEST_ASSERT_EQUAL_UINT8(1, pc_quic_server_active_conns());

    uint8_t cc_frames[64];
    size_t cc_len = pc_quic_build_connection_close(cc_frames, sizeof(cc_frames), 0, 0, NULL, 0);
    uint8_t dg1[256];
    size_t dl1 = build_long(dg1, sizeof(dg1), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init.client, cc_frames, cc_len);
    TEST_ASSERT_TRUE(pc_quic_server_ingest(dg1, dl1, "192.0.2.20", 40010));
    now += 5; // far short of PC_QUIC_IDLE_MS
    pc_quic_server_poll(now);
    TEST_ASSERT_EQUAL_UINT8(0, pc_quic_server_active_conns()); // reaped by is_closed(), not by idle

    pc_quic_server_stop();
}

// With on_request == NULL, pc_h3_on_request's "if (s_quic.on_request)" guard must skip the
// forwarding call instead of dereferencing a null function pointer, once a real HTTP/3 request
// completes on a fully-established connection.
void test_quic_server_on_request_null()
{
    fill();

    QuicServerConfig scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.cert_der = CERT;
    scfg.cert_len = sizeof(CERT);
    memcpy(scfg.ed25519_seed, SERVER_SEED, 32);
    scfg.rng = test_rng;
    pc_quic_server_set_out_sink_cb(out_sink, NULL);
    TEST_ASSERT_TRUE(pc_quic_server_begin(443, &scfg, NULL, NULL)); // no request callback

    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

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

    g_out_n = 0;
    TEST_ASSERT_TRUE(pc_quic_server_ingest(dg, dl, "192.0.2.10", 40000));
    pc_quic_server_poll(0);
    TEST_ASSERT_EQUAL_UINT8(1, pc_quic_server_active_conns());
    TEST_ASSERT_GREATER_THAN(0, g_out_n);

    uint8_t plain[2048], sh[512], hsf[1024];
    size_t wire = 0;
    uint8_t ty = 0;
    size_t pt = open_long(g_out[0], g_out_len[0], &init.server, plain, &wire, &ty);
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
    pc_tls13_ks_handshake(&cks, ecdhe, chsh);
    QuicPacketKeys hs_s, hs_c, ap_c;
    pc_quic_keys_from_secret(cks.server_hs_traffic, &hs_s);
    pc_quic_keys_from_secret(cks.client_hs_traffic, &hs_c);
    size_t hw = 0;
    uint8_t hty = 0;
    size_t hpt = open_long(g_out[0] + wire, g_out_len[0] - wire, &hs_s, plain, &hw, &hty);
    size_t hsfl = extract_crypto(plain, hpt, hsf);
    pc_sha256_update(&t, hsf, hsfl);
    pc_sha256_final(&t, chsf);
    pc_tls13_ks_master(&cks, chsf);
    pc_quic_keys_from_secret(cks.client_ap_traffic, &ap_c);

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
    g_out_n = 0;
    TEST_ASSERT_TRUE(pc_quic_server_ingest(idg, idl + hdl, "192.0.2.10", 40000));
    pc_quic_server_poll(0);

    uint8_t block[128];
    size_t bp = pc_qpack_encode_prefix(block, sizeof(block));
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/hello", 6);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":authority", 10, "h3.test", 7);
    uint8_t h3req[256];
    size_t h3l = pc_h3_build_headers(h3req, sizeof(h3req), block, bp);
    uint8_t sfr[300];
    size_t sfrl = pc_quic_build_stream(sfr, sizeof(sfr), 0, 0, h3req, h3l, true);
    uint8_t s1[512];
    size_t s1l = build_short(s1, sizeof(s1), SERVER_SCID, sizeof(SERVER_SCID), 0, &ap_c, sfr, sfrl);

    g_out_n = 0;
    TEST_ASSERT_TRUE(pc_quic_server_ingest(s1, s1l, "192.0.2.10", 40000));
    pc_quic_server_poll(0); // the H3 engine completes the request and calls pc_h3_on_request, which
                            // must see s_quic.on_request == NULL and skip forwarding, not crash

    TEST_ASSERT_EQUAL_STRING("", g_method); // never invoked: no crash and nothing forwarded
    TEST_ASSERT_EQUAL_UINT8(1, pc_quic_server_active_conns());

    pc_quic_server_stop();
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_quic_server_http3_get);
    RUN_TEST(test_idle_connection_reaped);
    RUN_TEST(test_quic_server_input_guards);
    RUN_TEST(test_quic_server_pool_full);
    RUN_TEST(test_quic_server_copy_str_edges);
    RUN_TEST(test_quic_server_no_out_sink);
    RUN_TEST(test_quic_server_begin_default_port);
    RUN_TEST(test_quic_server_respond_unknown_id_with_active_conn);
    RUN_TEST(test_quic_server_route_header_edges);
    RUN_TEST(test_quic_server_close_reaped_before_idle);
    RUN_TEST(test_quic_server_on_request_null);
    return UNITY_END();
}
