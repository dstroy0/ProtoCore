// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the QUIC v1 server connection engine (network_drivers/presentation/http/http3/pc_quic_conn;
// RFC 9000 / RFC 9001). The test acts as a QUIC *client*: it builds real Initial / Handshake / 1-RTT
// packets, protects and opens them with the same crypto the engine uses, and drives a server QuicConn
// through a full handshake and a 1-RTT request/response stream exchange - proving the engine's packet
// parsing, per-level AEAD, CRYPTO reassembly, ACK generation, coalescing, HANDSHAKE_DONE, and stream
// plumbing all interoperate with an independent implementation.

#include "crypto/asymmetric/curve25519.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/quic_conn.h"
#include "network_drivers/presentation/http/http3/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/tls/tls13_kdf.h"
#include <string.h>
#include <unity.h>

// Two keyed AEAD contexts agree iff they seal the same input identically. The raw key is no longer
// stored, so this is how a derived-key equality check is expressed now.
static void assert_ctx_match(uint8_t *a, uint8_t *b)
{
    uint8_t n12[12] = {0}, zpt[16] = {0}, c1[16], t1[16], c2[16], t2[16];
    pc_aes128gcm_seal(reinterpret_cast<pc_aes128gcm_key *>(a), n12, NULL, 0, zpt, sizeof zpt, c1, t1);
    pc_aes128gcm_seal(reinterpret_cast<pc_aes128gcm_key *>(b), n12, NULL, 0, zpt, sizeof zpt, c2, t2);
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

// Callback capture.
static proto_bool g_hs_done;
static uint8_t g_stream_data[256];
static size_t g_stream_len;
static uint64_t g_stream_id;
static proto_bool g_stream_fin;

static void on_hs_done(void *, QuicConn *)
{
    g_hs_done = PROTO_TRUE;
}
static void on_stream_data(void *, QuicConn *, uint64_t id, const uint8_t *data, size_t len, proto_bool fin)
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
    out[p++] = 0x00; // session id
    out[p++] = 0x00;
    out[p++] = 0x02;
    out[p++] = 0x13;
    out[p++] = 0x01; // cipher suites
    out[p++] = 0x01;
    out[p++] = 0x00; // compression
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

// Build a protected long-header packet; returns total length.
static size_t build_long(uint8_t *out, size_t cap, uint8_t type, const uint8_t *dcid, uint8_t dcl, const uint8_t *scid,
                         uint8_t scl, uint64_t pn, QuicPacketKeys *keys, const uint8_t *frames, size_t frame_len)
{
    uint8_t pn_len = pc_quic_pn_length(pn, -1);
    size_t p = pc_quic_build_long_header(out, cap, type, QUIC_VERSION_1, dcid, dcl, scid, scl, pn_len);
    if (type == QUIC_LP_INITIAL)
    {
        p += pc_quic_varint_encode(out + p, cap - p, 0);
    }
    uint64_t length = (uint64_t)pn_len + frame_len + 16;
    p += pc_quic_varint_encode(out + p, cap - p, length);
    size_t pn_off = p;
    wr_pn(out + p, pn, pn_len);
    p += pn_len;
    memcpy(out + p, frames, frame_len);
    return pc_quic_packet_protect(out, cap, pn_off, pn_len, pn, frame_len, keys, PROTO_TRUE);
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
    return pc_quic_packet_protect(out, cap, pn_off, pn_len, pn, frame_len, keys, PROTO_FALSE);
}

// Open a long-header packet at dg; returns plaintext length (or SIZE_MAX) and the on-wire length.
static size_t open_long(const uint8_t *dg, size_t len, QuicPacketKeys *keys, uint8_t *plain, size_t *wire_len,
                        uint8_t *type_out)
{
    QuicLongHeader h;
    TEST_ASSERT_TRUE(pc_quic_parse_long_header(dg, len, &h));
    *type_out = h.type;
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
    *wire_len = off + (size_t)length;
    static uint8_t work[2048];
    memcpy(work, dg, *wire_len);
    uint64_t pn = 0;
    return pc_quic_packet_unprotect(work, off, (size_t)length, 0, keys, PROTO_TRUE, plain, &pn);
}

static size_t open_short(const uint8_t *dg, size_t len, uint8_t dcl, QuicPacketKeys *keys, uint8_t *plain)
{
    static uint8_t work[2048];
    memcpy(work, dg, len);
    uint64_t pn = 0;
    return pc_quic_packet_unprotect(work, 1 + dcl, len - (1 + dcl), 0, keys, PROTO_FALSE, plain, &pn);
}

// Scan a decrypted payload for a CRYPTO frame and copy its data out; returns bytes copied.
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
        size_t n = pc_quic_frame_parse(p + off, len - off, &f);
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
    pc_quic_tp_defaults(&cfg->params);
    cfg->params.initial_max_data = 1048576;
    cfg->params.initial_max_sd_bidi_remote = 262144;
    cfg->params.initial_max_streams_bidi = 8;
}

void test_full_handshake_and_stream()
{
    fill();
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    pc_quic_conn_init(&qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                      sizeof(SERVER_SCID), &cb);

    // Client Initial keys from the same DCID the server used.
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

    // --- Client -> server: Initial(CRYPTO ClientHello), padded ---
    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctp_enc[128];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);

    uint8_t frames[1200];
    size_t fl = pc_quic_build_crypto(frames, sizeof(frames), 0, ch, ch_len);
    memset(frames + fl, 0, 1100 - fl); // PADDING to give the server a comfortable amp budget
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));

    // --- Server -> client: Initial(SH) + Handshake(EE..Finished) ---
    uint8_t sdg[1500];
    size_t sl = pc_quic_conn_send(&qc, sdg, sizeof(sdg));
    TEST_ASSERT_TRUE(sl > 0);

    uint8_t plain[2048], sh[512], hsflight[1024];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg, sl, &init.server, plain, &wire, &type);
    TEST_ASSERT_EQUAL_UINT8(QUIC_LP_INITIAL, type);
    size_t sh_len = extract_crypto(plain, pt, sh);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_SERVER_HELLO, sh[0]);

    // Derive the handshake secrets (client side) and open the Handshake packet.
    uint8_t server_pub[32], ecdhe[32];
    pc_x25519_base(server_pub, SERVER_PRIV);
    pc_x25519(ecdhe, CLIENT_PRIV, server_pub);
    pc_sha256_ctx t;
    uint8_t ch_sh[32], ch_sf[32];
    pc_sha256_init(&t);
    pc_sha256_update(&t, ch, ch_len);
    pc_sha256_update(&t, sh, sh_len);
    {
        pc_sha256_ctx tmp = t;
        pc_sha256_final(&tmp, ch_sh);
    }
    Tls13KeySchedule cks;
    pc_tls13_ks_early(&TLS13_KDF, &cks);
    pc_tls13_ks_handshake(&cks, ecdhe, ch_sh);
    QuicPacketKeys hs_server_keys, hs_client_keys;
    pc_quic_keys_from_secret(cks.server_hs_traffic, &hs_server_keys);
    pc_quic_keys_from_secret(cks.client_hs_traffic, &hs_client_keys);

    size_t hswire = 0;
    uint8_t hstype = 0;
    size_t hpt = open_long(sdg + wire, sl - wire, &hs_server_keys, plain, &hswire, &hstype);
    TEST_ASSERT_EQUAL_UINT8(QUIC_LP_HANDSHAKE, hstype);
    size_t hsflen = extract_crypto(plain, hpt, hsflight);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_ENCRYPTED_EXTENSIONS, hsflight[0]);

    // Full transcript -> application keys.
    pc_sha256_update(&t, hsflight, hsflen);
    pc_sha256_final(&t, ch_sf);
    pc_tls13_ks_master(&cks, ch_sf);
    QuicPacketKeys ap_server_keys, ap_client_keys;
    pc_quic_keys_from_secret(cks.server_ap_traffic, &ap_server_keys);
    pc_quic_keys_from_secret(cks.client_ap_traffic, &ap_client_keys);
    // Diagnostic: the client-derived keys must equal the server's.
    assert_ctx_match(qc.tls.hs_server.gcm, hs_server_keys.gcm);
    assert_ctx_match(qc.tls.ap_server.gcm, ap_server_keys.gcm);

    // --- Client -> server: Initial(ACK) + Handshake(ACK + CRYPTO client Finished) ---
    uint8_t ifr[64];
    size_t ifl = pc_quic_build_ack(ifr, sizeof(ifr), 0, 0, 0);
    uint8_t idg[256];
    size_t idl = build_long(idg, sizeof(idg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init.client, ifr, ifl);

    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    pc_tls13_finished_mac(&TLS13_KDF, cks.client_hs_traffic, ch_sf, cfin + 4);
    uint8_t hfr[64];
    size_t hfl = pc_quic_build_ack(hfr, sizeof(hfr), 0, 0, 0);
    hfl += pc_quic_build_crypto(hfr + hfl, sizeof(hfr) - hfl, 0, cfin, sizeof(cfin));
    size_t hdl = build_long(idg + idl, sizeof(idg) - idl, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID,
                            sizeof(CLIENT_SCID), 0, &hs_client_keys, hfr, hfl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, idg, idl + hdl));

    TEST_ASSERT_TRUE(pc_quic_conn_established(&qc));
    TEST_ASSERT_TRUE(g_hs_done);

    // Server's next datagram carries HANDSHAKE_DONE at 1-RTT.
    sl = pc_quic_conn_send(&qc, sdg, sizeof(sdg));
    TEST_ASSERT_TRUE(sl > 0);
    proto_bool saw_hs_done = PROTO_FALSE;
    size_t off = 0;
    while (off < sl)
    {
        if (pc_quic_is_long_header(sdg[off]))
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

    // --- 1-RTT stream: client sends "GET" (fin) on stream 0; server echoes "OK" ---
    uint8_t sfr[64];
    size_t sfl = pc_quic_build_stream(sfr, sizeof(sfr), 0, 0, (const uint8_t *)"GET", 3, PROTO_TRUE);
    uint8_t s1[256];
    size_t s1l = build_short(s1, sizeof(s1), SERVER_SCID, sizeof(SERVER_SCID), 0, &ap_client_keys, sfr, sfl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, s1, s1l));
    TEST_ASSERT_EQUAL_UINT64(0, g_stream_id);
    TEST_ASSERT_EQUAL_UINT(3, g_stream_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("GET", g_stream_data, 3);
    TEST_ASSERT_TRUE(g_stream_fin);

    pc_quic_conn_stream_send(&qc, 0, (const uint8_t *)"OK", 2, PROTO_TRUE);
    sl = pc_quic_conn_send(&qc, sdg, sizeof(sdg));
    TEST_ASSERT_TRUE(sl > 0);
    // The 1-RTT packet is the last (short-header) packet in the datagram.
    off = 0;
    proto_bool got_resp = PROTO_FALSE;
    while (off < sl)
    {
        if (pc_quic_is_long_header(sdg[off]))
        {
            size_t w = 0;
            uint8_t tp2 = 0;
            open_long(sdg + off, sl - off, &hs_server_keys, plain, &w, &tp2);
            off += w;
            continue;
        }
        size_t p2 = open_short(sdg + off, sl - off, sizeof(SERVER_SCID), &ap_server_keys, plain);
        TEST_ASSERT_NOT_EQUAL(SIZE_MAX, p2);
        // Find the STREAM frame carrying "OK".
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

    // --- Loss recovery: the client never ACKed the 1-RTT response, so a PTO retransmits it ---
    pc_quic_conn_on_timeout(&qc, 5000);                      // arm (APP space has unacknowledged stream data)
    pc_quic_conn_on_timeout(&qc, 5000 + PC_QUIC_PTO_MS + 1); // fire -> rewind the response stream
    sl = pc_quic_conn_send(&qc, sdg, sizeof(sdg));
    TEST_ASSERT_TRUE(sl > 0);
    proto_bool resent = PROTO_FALSE;
    off = 0;
    while (off < sl)
    {
        if (pc_quic_is_long_header(sdg[off]))
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
            size_t n = pc_quic_frame_parse(plain + fo, p2 - fo, &f);
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
    TEST_ASSERT_TRUE(resent); // the response was retransmitted
}

// A lost server flight is retransmitted on the Probe Timeout, and stops once acknowledged (RFC 9002).
void test_pto_retransmits_flight()
{
    fill();
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    pc_quic_conn_init(&qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                      sizeof(SERVER_SCID), &cb);

    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

    // Client Initial(ClientHello), padded for the amplification budget.
    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[128];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    size_t fl = pc_quic_build_crypto(frames, sizeof(frames), 0, ch, ch_len);
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));

    // Server's first flight (pretend it is lost: capture it and never ACK it).
    uint8_t sdg[1500];
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, sdg, sizeof(sdg)) > 0);

    // With the flight already sent and no PTO fired, there is nothing to send.
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, sdg, sizeof(sdg)));
    pc_quic_conn_on_timeout(&qc, 1000);                                  // arm
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, sdg, sizeof(sdg))); // still nothing (not fired)

    // Advance past the PTO: the flight is marked for retransmission and re-sent.
    pc_quic_conn_on_timeout(&qc, 1000 + PC_QUIC_PTO_MS + 1);
    uint8_t sdg2[1500];
    size_t sl2 = pc_quic_conn_send(&qc, sdg2, sizeof(sdg2));
    TEST_ASSERT_TRUE(sl2 > 0);
    uint8_t plain[2048], sh[512];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg2, sl2, &init.server, plain, &wire, &type);
    TEST_ASSERT_EQUAL_UINT8(QUIC_LP_INITIAL, type);
    size_t sh_len = extract_crypto(plain, pt, sh);
    TEST_ASSERT_TRUE(sh_len > 0);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_SERVER_HELLO, sh[0]); // the ServerHello was retransmitted

    // Once the flight is acknowledged (Initial discarded as the client moves on; Handshake ACKed up
    // to the last packet we sent), the PTO disarms and does not retransmit again.
    qc.space[QUIC_ENC_INITIAL].discarded = PROTO_TRUE;
    qc.space[QUIC_ENC_HANDSHAKE].largest_acked = qc.space[QUIC_ENC_HANDSHAKE].last_ae_pn;
    pc_quic_conn_on_timeout(&qc, 1000 + 10 * PC_QUIC_PTO_MS);
    TEST_ASSERT_FALSE(qc.pto_armed);
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, sdg2, sizeof(sdg2)));
}

// Init a server conn and feed it a padded client Initial (ClientHello); the server flight is left
// pending. Returns the client's Initial secrets and the ClientHello bytes (for deriving later keys).
static void feed_client_initial(QuicConn *qc, QuicConnCallbacks *cb, QuicInitialSecrets *init, uint8_t *ch,
                                size_t *ch_len)
{
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    pc_quic_conn_init(qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                      sizeof(SERVER_SCID), cb);
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), init);
    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[128];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    *ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    size_t fl = pc_quic_build_crypto(frames, sizeof(frames), 0, ch, *ch_len);
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init->client, frames, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(qc, dg, dl));
}

// The public initiate-close API queues a transport CONNECTION_CLOSE the next send emits, after which
// the connection is closed and sends nothing more.
void test_connection_close_api()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&qc, &cb, &init, ch, &ch_len);

    pc_quic_conn_close(&qc, QUIC_ERR_NO_ERROR);
    uint8_t cdg[512];
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, cdg, sizeof(cdg)) > 0);
    TEST_ASSERT_TRUE(pc_quic_conn_is_closed(&qc));
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, cdg, sizeof(cdg))); // nothing more after the close
}

// A fatal transport error (an undecodable frame) makes the server report a CONNECTION_CLOSE with the
// right error code instead of leaving the peer to time out (RFC 9000 sec 10.2 / 19.19).
void test_connection_close_on_malformed_frame()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&qc, &cb, &init, ch, &ch_len);

    // Capture the server flight and derive the client-side Handshake keys (to send at + read that level).
    uint8_t sdg[1500];
    size_t sl = pc_quic_conn_send(&qc, sdg, sizeof(sdg));
    TEST_ASSERT_TRUE(sl > 0);
    uint8_t plain[2048], sh[512];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg, sl, &init.server, plain, &wire, &type);
    size_t sh_len = extract_crypto(plain, pt, sh);
    uint8_t server_pub[32], ecdhe[32];
    pc_x25519_base(server_pub, SERVER_PRIV);
    pc_x25519(ecdhe, CLIENT_PRIV, server_pub);
    pc_sha256_ctx tctx;
    uint8_t ch_sh[32];
    pc_sha256_init(&tctx);
    pc_sha256_update(&tctx, ch, ch_len);
    pc_sha256_update(&tctx, sh, sh_len);
    pc_sha256_final(&tctx, ch_sh);
    Tls13KeySchedule cks;
    pc_tls13_ks_early(&TLS13_KDF, &cks);
    pc_tls13_ks_handshake(&cks, ecdhe, ch_sh);
    QuicPacketKeys hs_server_keys, hs_client_keys;
    pc_quic_keys_from_secret(cks.server_hs_traffic, &hs_server_keys);
    pc_quic_keys_from_secret(cks.client_hs_traffic, &hs_client_keys);

    // Client -> server: a Handshake packet whose only frame is a malformed CRYPTO (its declared length
    // dwarfs the packet), which the server cannot decode.
    uint8_t bad[4] = {QUIC_FT_CRYPTO, 0x00, 0x7f, 0xff}; // CRYPTO off=0 len=16383, no data present
    uint8_t bdg[256];
    size_t bl = build_long(bdg, sizeof(bdg), QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                           0, &hs_client_keys, bad, sizeof(bad));
    pc_quic_conn_recv(&qc, bdg, bl); // fatal -> queues the close

    // The next server datagram is a CONNECTION_CLOSE (FRAME_ENCODING) at the Handshake level.
    uint8_t cdg[512];
    size_t cl = pc_quic_conn_send(&qc, cdg, sizeof(cdg));
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
        size_t n = pc_quic_frame_parse(plain + fo, cpt - fo, &f);
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
    TEST_ASSERT_TRUE(pc_quic_conn_is_closed(&qc));
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, cdg, sizeof(cdg))); // nothing more after the close
}

// Init a bare server conn (Initial keys ready, handshake not started). cb is owned
// by the caller (pc_quic_conn_init keeps the pointer).
static void init_conn(QuicConn *qc, QuicConnCallbacks *cb)
{
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    pc_quic_conn_init(qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                      sizeof(SERVER_SCID), cb);
}

// A received CONNECTION_CLOSE closes/drains the server connection.
void test_quic_recv_connection_close()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

    uint8_t fr[32];
    size_t fl = pc_quic_build_connection_close(fr, sizeof(fr), QUIC_ERR_NO_ERROR, 0, NULL, 0);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    pc_quic_conn_recv(&qc, dg, dl);
    TEST_ASSERT_TRUE(pc_quic_conn_is_closed(&qc));
    // A datagram arriving after the connection is closed is rejected outright.
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, dl));
}

// PING and MAX_DATA are accepted with no per-frame state kept.
void test_quic_recv_ping_and_max_data()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

    uint8_t fr[16];
    size_t fl = pc_quic_build_ping(fr, sizeof(fr));
    fl += pc_quic_build_max_data(fr + fl, sizeof(fr) - fl, 1000000);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));
}

// A long header with an unknown version is dropped (Version Negotiation is a
// client concern).
void test_quic_recv_bad_version()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    dg[1] = dg[2] = dg[3] = dg[4] = 0xAA; // clobber the version (not header-protected)
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, dl));
}

// A long header of an unsupported type (0-RTT) is dropped before decryption.
void test_quic_recv_unsupported_long_type()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_0RTT, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, dl));
}

// A short-header (1-RTT) packet arriving before the app keys exist is dropped.
void test_quic_recv_short_before_app_keys()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_short(dg, sizeof(dg), SERVER_SCID, sizeof(SERVER_SCID), 0, &init.client, fr, 1);
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, dl)); // open_keys(APP) is null -> dropped
}

// A short-header datagram too small to hold the DCID + a packet number is dropped.
void test_quic_recv_short_too_short()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    uint8_t dg[1] = {0x40}; // short header, no room for DCID/PN
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, sizeof(dg)));
}

// A packet whose AEAD tag fails to verify is consumed-and-dropped, not fatal.
void test_quic_recv_unprotect_failure()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    dg[dl - 1] ^= 0xFF; // corrupt the auth tag
    pc_quic_conn_recv(&qc, dg, dl);
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc)); // dropped, no effect
    TEST_ASSERT_FALSE(pc_quic_conn_established(&qc));
}

// A truncated long header (too short to parse) is dropped.
void test_quic_recv_truncated_long_header()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    uint8_t dg[4] = {0xC0, 0x00, 0x00, 0x00}; // long header flag, then a truncated version
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, sizeof(dg)));
}

// Before address validation the server sends at most 3x the bytes received; a
// fresh conn (nothing received) is amplification-blocked and sends nothing.
void test_quic_send_amplification_limited()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    uint8_t out[256];
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, out, sizeof(out)));
}

// handle_crypto drops a CRYPTO frame beyond the reassembly window (out of order)
// and one that is wholly a duplicate; the connection survives both.
void test_quic_crypto_out_of_order_and_dup()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t data[4] = {0x01, 0x00, 0x00, 0xFF}; // partial ClientHello header: TLS buffers, no failure
    uint8_t fr[32], dg[256];

    // Out of order: CRYPTO at offset 100 while want==0 -> dropped.
    size_t fl = pc_quic_build_crypto(fr, sizeof(fr), 100, data, sizeof(data));
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));

    // In-window then an identical copy: the second is a full duplicate.
    fl = pc_quic_build_crypto(fr, sizeof(fr), 0, data, sizeof(data));
    dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 1,
                    &init.client, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    uint8_t dg2[256];
    size_t dl2 = build_long(dg2, sizeof(dg2), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            2, &init.client, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg2, dl2)); // duplicate path
}

// on_timeout is a no-op once the connection is closed.
void test_quic_timeout_when_closed()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t fr[32];
    size_t fl = pc_quic_build_connection_close(fr, sizeof(fr), QUIC_ERR_NO_ERROR, 0, NULL, 0);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    pc_quic_conn_recv(&qc, dg, dl);
    TEST_ASSERT_TRUE(pc_quic_conn_is_closed(&qc));
    pc_quic_conn_on_timeout(&qc, 1000); // closed -> immediate return, no effect
}

// pc_quic_conn_stream_send refuses a new stream id once the stream table is full.
void test_quic_stream_send_table_full()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    for (int i = 0; i < PC_QUIC_MAX_STREAMS; i++)
    {
        TEST_ASSERT_EQUAL_UINT(2,
                               pc_quic_conn_stream_send(&qc, (uint64_t)(i * 4), (const uint8_t *)"hi", 2, PROTO_FALSE));
    }
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_stream_send(&qc, 999, (const uint8_t *)"x", 1, PROTO_FALSE)); // table full
}

// recv_packet header-parse guards: crafted Initials that fail before any decryption.
void test_quic_recv_malformed_initial_headers()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    uint8_t dg[1500];

    // 269: a truncated token-length varint (0xC0 announces 8 octets, none follow).
    size_t hn = pc_quic_build_long_header(dg, sizeof dg, QUIC_LP_INITIAL, QUIC_VERSION_1, ODCID, sizeof(ODCID),
                                          CLIENT_SCID, sizeof(CLIENT_SCID), 1);
    dg[hn] = 0xC0;
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, hn + 1));

    // 272: a token length that runs past the datagram end.
    dg[hn] = 0x40;
    dg[hn + 1] = 0xFF; // 2-octet varint = a 255-octet token
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, hn + 2));

    // 276: a truncated payload-length varint after a zero-length token.
    dg[hn] = 0x00;
    dg[hn + 1] = 0xC0; // 8-octet payload varint, none follow
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, hn + 2));

    // 281: a payload length larger than the datagram (pkt_len > len).
    dg[hn] = 0x00;
    dg[hn + 1] = 0x44;
    dg[hn + 2] = 0x00; // payload length 0x400 = 1024
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, hn + 8));

    // 303: a packet larger than the decrypt work buffer (pkt_len > PC_QUIC_MAX_DATAGRAM), yet <= len.
    dg[hn] = 0x00;
    size_t c = pc_quic_varint_encode(dg + hn + 1, sizeof(dg) - hn - 1, 1400);
    memset(dg + hn + 1 + c, 0, 1450 - (hn + 1 + c));
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, 1450));
}

// A HANDSHAKE_DONE frame from a peer is ignored (server-only frame).
void test_quic_recv_handshake_done_frame()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t hd[32];
    size_t hdl = pc_quic_build_handshake_done(hd, sizeof hd);
    memset(hd + hdl, 0, 20); // PADDING so the packet is long enough for header-protection sampling
    hdl += 20;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, hd, hdl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));
}

// handle_stream guards driven from Initial packets (process_frames does not gate frame type by level).
void test_quic_conn_stream_frames()
{
    fill();
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t dg[1500];

    // (a) Out-of-order STREAM (offset beyond the rx window) is held, not delivered.
    {
        QuicConn qc;
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&qc, &cb);
        uint8_t data[4] = {1, 2, 3, 4};
        uint8_t fr[32];
        size_t fl = pc_quic_build_stream(fr, sizeof fr, 0, 100 /*offset > rx_off*/, data, 4, PROTO_FALSE);
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
        g_stream_len = 0;
        pc_quic_conn_recv(&qc, dg, dl);
        TEST_ASSERT_EQUAL_UINT(0, g_stream_len);
    }
    // (b) A pure-FIN STREAM at the current offset delivers a zero-length FIN.
    {
        QuicConn qc;
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&qc, &cb);
        uint8_t d0 = 0;
        uint8_t fr[16];
        size_t fl = pc_quic_build_stream(fr, sizeof fr, 0, 0, &d0, 0, PROTO_TRUE);
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
        g_stream_fin = PROTO_FALSE;
        pc_quic_conn_recv(&qc, dg, dl);
        TEST_ASSERT_TRUE(g_stream_fin);
    }
    // (c) The inbound stream table fills at PC_QUIC_MAX_STREAMS distinct ids; the extra is dropped.
    {
        QuicConn qc;
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&qc, &cb);
        uint8_t d1 = 0x55;
        uint8_t fr[512];
        size_t fl = 0;
        for (int i = 0; i <= PC_QUIC_MAX_STREAMS; i++)
        {
            fl += pc_quic_build_stream(fr + fl, sizeof(fr) - fl, (uint64_t)(i * 4), 0, &d1, 1, PROTO_FALSE);
        }
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
        TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    }
}

// A CRYPTO reassembly window overflow is clamped (two datagrams push past PC_QUIC_CRYPTO_RX).
void test_quic_conn_crypto_window_clamp()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t dg[1500];
    uint8_t chunk[1200];
    chunk[0] = 0x01; // ClientHello with a huge declared length so TLS buffers it without failing
    chunk[1] = 0x00;
    chunk[2] = 0xFF;
    chunk[3] = 0xFF;
    memset(chunk + 4, 0, sizeof(chunk) - 4);
    uint8_t fr[1300];
    size_t fl = pc_quic_build_crypto(fr, sizeof fr, 0, chunk, sizeof chunk);
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));
    fl = pc_quic_build_crypto(fr, sizeof fr, 1200, chunk, sizeof chunk); // offset 1200 -> 2400 > 2048 -> clamp
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 1, &init.client, fr, fl);
    pc_quic_conn_recv(&qc, dg, dl);
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));
}

// A malformed ClientHello in a CRYPTO frame fails the TLS handshake -> a QUIC CRYPTO_ERROR close.
void test_quic_conn_crypto_error_close()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t bad_ch[6] = {0x01, 0x00, 0x00, 0x02, 0x03, 0x03}; // ClientHello, body too short
    uint8_t fr[32];
    size_t fl = pc_quic_build_crypto(fr, sizeof fr, 0, bad_ch, sizeof bad_ch);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
    pc_quic_conn_recv(&qc, dg, dl);
    // A second close request is now a no-op (a close is already queued).
    pc_quic_conn_close(&qc, 0);
    uint8_t out[256];
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, out, sizeof out) > 0); // emits the CONNECTION_CLOSE
}

// pc_quic_conn_send builds nothing for a level whose keys are not ready yet (the seal-keys guard).
void test_quic_conn_no_keys_build()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t fr[32] = {QUIC_FT_PING}; // ack-eliciting (+ trailing PADDING for a full-length packet)
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, sizeof fr);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    uint8_t out[256];
    // Initial builds the ACK; Handshake and 1-RTT have no keys -> build_packet returns 0 for them.
    (void)pc_quic_conn_send(&qc, out, sizeof out);
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));
}

// A PTO timeout before the deadline is a no-op.
void test_quic_conn_pto_not_yet()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&qc, &cb, &init, ch, &ch_len);
    uint8_t out[2048];
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, out, sizeof out) > 0); // ack-eliciting flight now outstanding
    pc_quic_conn_on_timeout(&qc, 0);                               // arm the PTO
    pc_quic_conn_on_timeout(&qc, 1);                               // still before the deadline -> no-op
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));
}

// build_packet header/length/remainder overflow guards, swept across tiny output caps.
void test_quic_conn_send_tiny_cap()
{
    for (size_t cap = 1; cap <= 40; cap++)
    {
        fill();
        QuicConn qc;
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        QuicInitialSecrets init;
        uint8_t ch[512];
        size_t ch_len = 0;
        feed_client_initial(&qc, &cb, &init, ch, &ch_len);
        uint8_t out[64];
        (void)pc_quic_conn_send(&qc, out, cap); // the pending Initial flight cannot fit -> overflow returns
    }
}

// Drive a server QuicConn through a complete handshake; leaves it established (1-RTT ready) with the
// Initial space discarded, and returns the client's Initial secrets + the 1-RTT keys. @p peer_scid_len
// is the client's Source Connection ID length, i.e. the DCID the server puts in its 1-RTT short
// headers; 0 is legal (RFC 9000 sec 5.1) and leaves the most room for the 1-RTT payload.
static void complete_handshake(QuicConn *qc, QuicConnCallbacks *cb, QuicInitialSecrets *init, QuicPacketKeys *ap_client,
                               QuicPacketKeys *ap_server, uint8_t peer_scid_len = sizeof(CLIENT_SCID))
{
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    pc_quic_conn_init(qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, peer_scid_len, SERVER_SCID, sizeof(SERVER_SCID), cb);
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), init);

    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctp_enc[128];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    size_t fl = pc_quic_build_crypto(frames, sizeof(frames), 0, ch, ch_len);
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init->client, frames, fl);
    pc_quic_conn_recv(qc, dg, dl);

    uint8_t sdg[1500];
    size_t sl = pc_quic_conn_send(qc, sdg, sizeof(sdg));
    uint8_t plain[2048], sh[512], hsflight[1024];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg, sl, &init->server, plain, &wire, &type);
    size_t sh_len = extract_crypto(plain, pt, sh);

    uint8_t server_pub[32], ecdhe[32];
    pc_x25519_base(server_pub, SERVER_PRIV);
    pc_x25519(ecdhe, CLIENT_PRIV, server_pub);
    pc_sha256_ctx t;
    uint8_t ch_sh[32], ch_sf[32];
    pc_sha256_init(&t);
    pc_sha256_update(&t, ch, ch_len);
    pc_sha256_update(&t, sh, sh_len);
    {
        pc_sha256_ctx tmp = t;
        pc_sha256_final(&tmp, ch_sh);
    }
    Tls13KeySchedule cks;
    pc_tls13_ks_early(&TLS13_KDF, &cks);
    pc_tls13_ks_handshake(&cks, ecdhe, ch_sh);
    QuicPacketKeys hs_server_keys, hs_client_keys;
    pc_quic_keys_from_secret(cks.server_hs_traffic, &hs_server_keys);
    pc_quic_keys_from_secret(cks.client_hs_traffic, &hs_client_keys);
    size_t hswire = 0;
    uint8_t hstype = 0;
    size_t hpt = open_long(sdg + wire, sl - wire, &hs_server_keys, plain, &hswire, &hstype);
    size_t hsflen = extract_crypto(plain, hpt, hsflight);
    pc_sha256_update(&t, hsflight, hsflen);
    pc_sha256_final(&t, ch_sf);
    pc_tls13_ks_master(&cks, ch_sf);
    pc_quic_keys_from_secret(cks.client_ap_traffic, ap_client);
    pc_quic_keys_from_secret(cks.server_ap_traffic, ap_server);

    uint8_t ifr[64];
    size_t ifl = pc_quic_build_ack(ifr, sizeof(ifr), 0, 0, 0);
    uint8_t idg[256];
    size_t idl = build_long(idg, sizeof(idg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init->client, ifr, ifl);
    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    pc_tls13_finished_mac(&TLS13_KDF, cks.client_hs_traffic, ch_sf, cfin + 4);
    uint8_t hfr[64];
    size_t hfl = pc_quic_build_ack(hfr, sizeof(hfr), 0, 0, 0);
    hfl += pc_quic_build_crypto(hfr + hfl, sizeof(hfr) - hfl, 0, cfin, sizeof(cfin));
    size_t hdl = build_long(idg + idl, sizeof(idg) - idl, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID,
                            sizeof(CLIENT_SCID), 0, &hs_client_keys, hfr, hfl);
    pc_quic_conn_recv(qc, idg, idl + hdl);
    pc_quic_conn_send(qc, sdg, sizeof(sdg)); // drain HANDSHAKE_DONE so the 1-RTT send queue is clear
}

// build_frames' 1-RTT stream loop skips a stream with nothing left to send.
void test_quic_conn_stream_nothing_to_send()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&qc, &cb, &init, &apc, &aps);
    uint8_t out[512];
    TEST_ASSERT_EQUAL_UINT(2, pc_quic_conn_stream_send(&qc, 0, (const uint8_t *)"OK", 2, PROTO_TRUE));
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, out, sizeof out) > 0); // drains the stream
    pc_quic_conn_send(&qc, out, sizeof out);                       // now nothing to send -> the skip
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));
}

// A short-header packet whose DCID does not fit the output cap fails closed.
void test_quic_conn_short_header_tiny_cap()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&qc, &cb, &init, &apc, &aps);
    pc_quic_conn_stream_send(&qc, 0, (const uint8_t *)"DATA", 4, PROTO_FALSE);
    uint8_t out[8];
    (void)pc_quic_conn_send(&qc, out, 4); // 1 + dcid_len(4) > cap(4) at the 1-RTT short header
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));
}

// A CONNECTION_CLOSE queued at the (now discarded) Initial level falls back to the highest live level.
void test_quic_conn_close_level_fallback()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&qc, &cb, &init, &apc, &aps);
    // A malformed frame in an Initial packet (Initial is discarded post-handshake) queues a close at
    // the Initial level; the send path must then fall back to a level whose keys are still live.
    uint8_t bad[20] = {0x06, 0x00, 0x44, 0x00}; // CRYPTO frame declaring 1024 octets that are not present
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 5,
                           &init.client, bad, sizeof bad);
    pc_quic_conn_recv(&qc, dg, dl);
    uint8_t out[256];
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, out, sizeof out) > 0); // close emitted at the fallback level
    TEST_ASSERT_TRUE(pc_quic_conn_is_closed(&qc));
}

// A connection initialized with no callbacks runs the whole handshake and delivers stream data
// without dispatching anything: every hook is null-checked before it is invoked.
void test_quic_conn_null_callbacks()
{
    fill();
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConn qc;
    pc_quic_conn_init(&qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                      sizeof(SERVER_SCID), NULL); // no callbacks at all
    TEST_ASSERT_NULL(qc.cb.on_stream_data);
    TEST_ASSERT_NULL(qc.cb.on_handshake_done);

    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

    // A STREAM frame with data, and a pure-FIN one, at the Initial level: both take the callback
    // paths, both find a null hook, and neither disturbs the connection.
    uint8_t d[3] = {1, 2, 3};
    uint8_t fr[64];
    size_t fl = pc_quic_build_stream(fr, sizeof fr, 0, 0, d, 3, PROTO_FALSE);
    fl += pc_quic_build_stream(fr + fl, sizeof(fr) - fl, 0, 3, d, 0, PROTO_TRUE);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT(0, g_stream_len); // the test's own callback was never registered
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));

    // A full handshake still completes and still queues HANDSHAKE_DONE with no on_handshake_done.
    QuicConn qc2;
    QuicPacketKeys apc, aps;
    QuicInitialSecrets init2;
    complete_handshake(&qc2, NULL, &init2, &apc, &aps);
    TEST_ASSERT_TRUE(pc_quic_conn_established(&qc2));
    TEST_ASSERT_FALSE(g_hs_done); // no hook ran
}

// handle_stream's in-window cases that are not a fresh delivery: a wholly duplicate STREAM frame, a
// FIN whose final offset is behind the stream's current offset, and a repeated pure-FIN.
void test_quic_conn_stream_duplicate_and_stale_fin()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t d[4] = {0xA1, 0xA2, 0xA3, 0xA4};
    uint8_t fr[64], dg[256];
    uint64_t pn = 0;

    // 4 bytes at offset 0 -> delivered.
    size_t fl = pc_quic_build_stream(fr, sizeof fr, 0, 0, d, 4, PROTO_FALSE);
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT(4, g_stream_len);

    // The identical frame again: entirely behind the window and not a FIN -> nothing delivered.
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT(4, g_stream_len); // unchanged

    // A FIN at offset 0 length 0: in window, but its final offset (0) is not the stream's current
    // offset (4), so it is not the end-of-stream marker and is ignored.
    fl = pc_quic_build_stream(fr, sizeof fr, 0, 0, d, 0, PROTO_TRUE);
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    g_stream_fin = PROTO_FALSE;
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_FALSE(g_stream_fin);

    // The real pure-FIN at offset 4 -> delivered as a zero-length FIN.
    fl = pc_quic_build_stream(fr, sizeof fr, 0, 4, d, 0, PROTO_TRUE);
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_TRUE(g_stream_fin);

    // A retransmission of that FIN: rx_fin is already set, so it is not re-delivered.
    g_stream_fin = PROTO_FALSE;
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_FALSE(g_stream_fin); // no second FIN callback
}

// Frame-dispatch coverage the happy path never reaches: ACK_ECN and the application-level
// CONNECTION_CLOSE are both non-ack-eliciting, a stale ACK does not move largest_acked, and the
// frame types either side of the STREAM range fall through the dispatcher untouched.
void test_quic_conn_frame_dispatch_variants()
{
    fill();
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t dg[512];

    // (a) An ACK_ECN, then a stale ACK: the first advances largest_acked, the second does not.
    {
        QuicConn qc;
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&qc, &cb);
        // ACK_ECN (0x03): largest 5, delay 0, 0 ranges, first_range 0, ECN counts 0/0/0.
        const uint8_t ack_ecn[8] = {0x03, 5, 0, 0, 0, 0, 0, 0};
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, ack_ecn,
                               sizeof ack_ecn);
        TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
        TEST_ASSERT_EQUAL_INT64(5, qc.space[QUIC_ENC_INITIAL].largest_acked);
        TEST_ASSERT_FALSE(qc.space[QUIC_ENC_INITIAL].ack_eliciting_rx); // ACKs do not elicit

        uint8_t older[8];
        size_t ol = pc_quic_build_ack(older, sizeof older, 3, 0, 0); // largest 3 < 5
        dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 1, &init.client, older, ol);
        TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
        TEST_ASSERT_EQUAL_INT64(5, qc.space[QUIC_ENC_INITIAL].largest_acked); // not lowered
    }
    // (b) An application CONNECTION_CLOSE (0x1d) closes the connection and is not ack-eliciting.
    {
        QuicConn qc;
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&qc, &cb);
        const uint8_t app_close[3] = {0x1d, 0x05, 0x00}; // error 5, no reason
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, app_close,
                               sizeof app_close);
        TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
        TEST_ASSERT_TRUE(pc_quic_conn_is_closed(&qc));
        TEST_ASSERT_FALSE(qc.space[QUIC_ENC_INITIAL].ack_eliciting_rx);
    }
    // (c) Frame types below and above the STREAM range (0x08..0x0f) reach the dispatcher's default
    // arm and are ignored: RESET_STREAM (0x04) and MAX_STREAMS_BIDI (0x12).
    {
        QuicConn qc;
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&qc, &cb);
        const uint8_t misc[6] = {QUIC_FT_RESET_STREAM, 0x00, 0x01, 0x02, QUIC_FT_MAX_STREAMS_BIDI, 0x08};
        size_t dl =
            build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, misc, sizeof misc);
        TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
        TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));
        TEST_ASSERT_EQUAL_UINT(0, g_stream_len); // neither was mistaken for a STREAM frame
    }
}

// A long header whose version field is zero (the Version Negotiation codepoint) is dropped: a
// server never answers one, so it must not reach the key/decrypt path.
void test_quic_recv_zero_version()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    dg[1] = dg[2] = dg[3] = dg[4] = 0x00; // version 0 (not header-protected)
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, dl));
}

// A packet number at or below the largest already received does not lower largest_rx (which anchors
// packet-number decoding and the ACK we generate).
void test_quic_recv_older_packet_number()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);
    uint8_t fr[24] = {QUIC_FT_PING}; // PING + PADDING (long enough to header-protect)
    uint8_t dg[256];

    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 5, &init.client, fr, sizeof fr);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT64(5, qc.space[QUIC_ENC_INITIAL].largest_rx);

    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 1, &init.client, fr, sizeof fr);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT64(5, qc.space[QUIC_ENC_INITIAL].largest_rx); // still 5
}

// A 1-RTT (short-header) packet whose AEAD tag does not verify stops the datagram walk: unlike a
// long-header packet there is no Length field to skip past, so nothing after it can be parsed.
void test_quic_recv_short_header_decrypt_failure()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&qc, &cb, &init, &apc, &aps);

    uint8_t fr[32];
    size_t fl = pc_quic_build_ping(fr, sizeof fr);
    memset(fr + fl, 0, 20);
    fl += 20;
    uint8_t dg[256];
    size_t dl = build_short(dg, sizeof dg, SERVER_SCID, sizeof(SERVER_SCID), 0, &apc, fr, fl);
    dg[dl - 1] ^= 0xFF; // corrupt the auth tag
    TEST_ASSERT_FALSE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc)); // dropped, not fatal
}

// The CRYPTO stream after the handshake has completed: further bytes keep the TLS state at DONE and
// must not re-queue HANDSHAKE_DONE, whether it is still queued or has already been sent.
void test_quic_conn_crypto_after_handshake_done()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&qc, &cb, &init, &apc, &aps);
    TEST_ASSERT_TRUE(qc.handshake_done_sent); // complete_handshake drained it
    TEST_ASSERT_FALSE(qc.handshake_done_queued);

    // A short CRYPTO frame at the Handshake level: fewer than 4 bytes, so no TLS message completes
    // and the state stays DONE. HANDSHAKE_DONE must NOT be queued again.
    QuicPacketKeys hs_client_keys;
    {
        // Re-derive the client's Handshake write keys from the server's (they are the same secrets
        // the engine installed).
        hs_client_keys = qc.tls.hs_client;
    }
    uint64_t off = qc.space[QUIC_ENC_HANDSHAKE].crypto_rx_off;
    const uint8_t frag[2] = {0xFE, 0xFF};
    uint8_t fr[64];
    size_t fl = pc_quic_build_crypto(fr, sizeof fr, off, frag, sizeof frag);
    memset(fr + fl, 0, 20);
    fl += 20;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 7,
                           &hs_client_keys, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT8(QTLS_DONE, qc.tls.state);
    TEST_ASSERT_FALSE(qc.handshake_done_queued); // already sent; not re-queued
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));

    // Now with HANDSHAKE_DONE queued but not yet sent: a further CRYPTO fragment - at the offset the
    // engine is actually waiting for, so it is not dropped as a duplicate - must leave HANDSHAKE_DONE
    // queued exactly once (the callback does not fire a second time either).
    qc.handshake_done_sent = PROTO_FALSE;
    qc.handshake_done_queued = PROTO_TRUE;
    g_hs_done = PROTO_FALSE;
    off = qc.space[QUIC_ENC_HANDSHAKE].crypto_rx_off;
    TEST_ASSERT_TRUE(off > 0); // the first fragment really was consumed
    fl = pc_quic_build_crypto(fr, sizeof fr, off, frag, sizeof frag);
    memset(fr + fl, 0, 20);
    fl += 20;
    dl = build_long(dg, sizeof dg, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 8,
                    &hs_client_keys, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT64(off + sizeof(frag), qc.space[QUIC_ENC_HANDSHAKE].crypto_rx_off);
    TEST_ASSERT_TRUE(qc.handshake_done_queued);
    TEST_ASSERT_FALSE(qc.handshake_done_sent); // still queued, not re-sent behind our back
    TEST_ASSERT_FALSE(g_hs_done);              // on_handshake_done did not run again
}

// A close request that arrives after the peer has already closed the connection is ignored: the
// first close wins (RFC 9000 sec 10.2.3), and a draining connection sends nothing more.
void test_quic_conn_close_after_peer_close()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

    uint8_t fr[32];
    size_t fl = pc_quic_build_connection_close(fr, sizeof fr, QUIC_ERR_NO_ERROR, 0, NULL, 0);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    pc_quic_conn_recv(&qc, dg, dl);
    TEST_ASSERT_TRUE(qc.closed);
    TEST_ASSERT_TRUE(qc.draining);

    pc_quic_conn_close(&qc, QUIC_ERR_FRAME_ENCODING);
    TEST_ASSERT_FALSE(qc.close_queued); // already closed: the request is dropped
    uint8_t out[512];
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, out, sizeof out)); // draining: nothing to send
}

// A queued CONNECTION_CLOSE that is overtaken by the peer's own close: the close is still emitted
// once, and the connection then keeps draining without emitting it a second time.
void test_quic_conn_close_queued_then_peer_close()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&qc, &cb, &init, ch, &ch_len);

    // Drain the owed ACK and the whole server handshake flight first, so that once the close has gone
    // out there is genuinely nothing else queued and the "draining" send can be observed in isolation.
    qc.address_validated = PROTO_TRUE;
    uint8_t out[PC_QUIC_MAX_DATAGRAM];
    for (int i = 0; i < 8 && pc_quic_conn_send(&qc, out, sizeof out) > 0; i++)
    {
    }
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, out, sizeof out));

    // Our own close is queued first (application-initiated).
    pc_quic_conn_close(&qc, QUIC_ERR_NO_ERROR);
    TEST_ASSERT_TRUE(qc.close_queued);

    // The peer's CONNECTION_CLOSE arrives before we have sent ours: we are now draining too.
    uint8_t fr[32];
    size_t fl = pc_quic_build_connection_close(fr, sizeof fr, QUIC_ERR_NO_ERROR, 0, NULL, 0);
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 1,
                           &init.client, fr, fl);
    pc_quic_conn_recv(&qc, dg, dl);
    TEST_ASSERT_TRUE(qc.draining);

    // The queued close still goes out exactly once...
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, out, sizeof out) > 0);
    TEST_ASSERT_TRUE(qc.close_sent);
    // ...and the next send, still draining with the close already sent, emits nothing.
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, out, sizeof out));
}

// A queued CONNECTION_CLOSE that cannot be built into the caller's buffer is NOT marked sent, so a
// later call with room still reports the error to the peer.
void test_quic_conn_close_send_no_room()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&qc, &cb, &init, ch, &ch_len);

    pc_quic_conn_close(&qc, QUIC_ERR_NO_ERROR);
    uint8_t tiny[8];
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, tiny, sizeof tiny));
    TEST_ASSERT_FALSE(qc.close_sent);
    TEST_ASSERT_FALSE(qc.closed);

    uint8_t out[512];
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, out, sizeof out) > 0);
    TEST_ASSERT_TRUE(qc.close_sent);
}

// A close queued at an encryption level outside INITIAL..APP falls back to the highest level whose
// keys are still live, rather than indexing a non-existent packet-number space.
void test_quic_conn_close_level_out_of_range()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&qc, &cb, &init, ch, &ch_len);

    pc_quic_conn_close(&qc, QUIC_ERR_NO_ERROR);
    qc.close_level = 200; // above QUIC_ENC_APP
    uint8_t out[512];
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, out, sizeof out) > 0); // emitted at the fallback level
    TEST_ASSERT_TRUE(pc_quic_conn_is_closed(&qc));
}

// With every packet-number space discarded there is no level left holding seal keys, so the close
// level falls back to Initial rather than running off the bottom of the search.
void test_quic_conn_highest_sealed_level_fallback()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    for (int l = QUIC_ENC_INITIAL; l <= QUIC_ENC_APP; l++)
    {
        qc.space[l].discarded = PROTO_TRUE;
    }

    pc_quic_conn_close(&qc, QUIC_ERR_NO_ERROR);
    TEST_ASSERT_TRUE(qc.close_queued);
    TEST_ASSERT_EQUAL_UINT8(QUIC_ENC_INITIAL, qc.close_level); // the documented fallback
}

// The CRYPTO flight is fragmented across datagrams when it does not fit one packet: a large
// certificate makes the Handshake flight exceed a single packet's room, and the remainder goes out
// on the next send.
void test_quic_conn_crypto_flight_fragmented()
{
    fill();
    static uint8_t big_cert[1500];
    memset(big_cert, 0x30, sizeof(big_cert));
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    cfg.cert_der = big_cert;
    cfg.cert_len = sizeof(big_cert);

    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    pc_quic_conn_init(&qc, &cfg, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), SERVER_SCID,
                      sizeof(SERVER_SCID), &cb);
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    uint8_t ctp_enc[128];
    size_t ctp_len = pc_quic_tp_encode(&ctp, ctp_enc, sizeof(ctp_enc));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t chb[512];
    size_t ch_len = build_client_hello(chb, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    size_t fl = pc_quic_build_crypto(frames, sizeof(frames), 0, chb, ch_len);
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT8(QTLS_WAIT_FINISHED, qc.tls.state);

    // The Handshake flight is larger than one datagram can carry, so it takes more than one send.
    size_t hs_flight_len = 0;
    (void)pc_quic_tls_flight(&qc.tls, QUIC_ENC_HANDSHAKE, &hs_flight_len);
    TEST_ASSERT_TRUE(hs_flight_len > PC_QUIC_MAX_DATAGRAM);

    uint8_t out[PC_QUIC_MAX_DATAGRAM];
    size_t first = pc_quic_conn_send(&qc, out, sizeof out);
    TEST_ASSERT_TRUE(first > 0);
    uint64_t sent_after_first = qc.space[QUIC_ENC_HANDSHAKE].crypto_tx_off;
    TEST_ASSERT_TRUE(sent_after_first > 0);
    TEST_ASSERT_TRUE(sent_after_first < hs_flight_len); // only part of the flight went out

    // Lift the amplification limit so the remainder can follow, and drain the rest.
    qc.address_validated = PROTO_TRUE;
    size_t total = sent_after_first;
    for (int i = 0; i < 8 && total < hs_flight_len; i++)
    {
        TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, out, sizeof out) > 0);
        total = qc.space[QUIC_ENC_HANDSHAKE].crypto_tx_off;
    }
    TEST_ASSERT_EQUAL_UINT64(hs_flight_len, total); // the whole flight was eventually sent
}

// build_app_frames' stream loop: a stream with nothing pending is skipped; a response larger than the
// room left in the frame payload is split, taking only what fits and withholding the FIN until the
// last byte is taken; and data queued after the FIN went out still goes out with tx_fin_sent latched.
void test_quic_conn_stream_tx_partitioning()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&qc, &cb, &init, &apc, &aps);

    // A client STREAM frame creates an inbound stream with nothing queued to send: the loop must
    // skip it (no data, no pending FIN).
    uint8_t d[2] = {0x11, 0x22};
    uint8_t fr[64];
    size_t fl = pc_quic_build_stream(fr, sizeof fr, 0, 0, d, 2, PROTO_FALSE);
    uint8_t sdg[256];
    size_t sl = build_short(sdg, sizeof sdg, SERVER_SCID, sizeof(SERVER_SCID), 1, &apc, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, sdg, sl));
    uint8_t out[PC_QUIC_MAX_DATAGRAM];
    (void)pc_quic_conn_send(&qc, out, sizeof out); // emits the owed ACK; stream 0 is skipped

    // A response larger than the room the frame payload has left, with a FIN: the first round takes
    // `room` rather than `remain` and does NOT set the FIN, because the final size is not reached yet.
    static uint8_t big[PC_QUIC_STREAM_TX - 64];
    memset(big, 0x5A, sizeof(big));
    size_t queued = pc_quic_conn_stream_send(&qc, 0, big, sizeof(big), PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT(sizeof(big), queued);
    QuicStream *st = NULL;
    for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
    {
        if (qc.streams[i].id == 0)
        {
            st = &qc.streams[i];
        }
    }
    TEST_ASSERT_NOT_NULL(st);

    (void)pc_quic_conn_send(&qc, out, sizeof out);
    TEST_ASSERT_TRUE(st->tx_sent > 0);
    TEST_ASSERT_TRUE(st->tx_sent < st->tx_have);       // split: only what fit was taken
    TEST_ASSERT_EQUAL_UINT64(st->tx_sent, st->tx_off); // the stream offset tracks what was taken
    TEST_ASSERT_FALSE(st->tx_fin_sent);                // the FIN waits for the last byte

    // The tail now fits in one frame, so this round takes `remain` and carries the FIN.
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, out, sizeof out) > 0);
    TEST_ASSERT_EQUAL_UINT(st->tx_have, st->tx_sent);
    TEST_ASSERT_TRUE(st->tx_fin_sent);

    // More data queued after the FIN was already sent still goes out, and tx_fin_sent stays latched.
    TEST_ASSERT_EQUAL_UINT(2, pc_quic_conn_stream_send(&qc, 0, d, 2, PROTO_FALSE));
    size_t before = st->tx_sent;
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, out, sizeof out) > 0);
    TEST_ASSERT_EQUAL_UINT(before + 2, st->tx_sent);
    TEST_ASSERT_TRUE(st->tx_fin_sent);
}

// A stream whose data has already been drained and is then closed with a zero-length FIN emits the
// FIN on its own: the loop's fin_due arm runs with nothing left to send.
void test_quic_conn_stream_fin_only()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&qc, &cb, &init, &apc, &aps);

    uint8_t out[PC_QUIC_MAX_DATAGRAM];
    TEST_ASSERT_EQUAL_UINT(4, pc_quic_conn_stream_send(&qc, 0, (const uint8_t *)"BODY", 4, PROTO_FALSE));
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, out, sizeof out) > 0);
    QuicStream *st = NULL;
    for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
    {
        if (qc.streams[i].id == 0)
        {
            st = &qc.streams[i];
        }
    }
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT(st->tx_have, st->tx_sent);
    TEST_ASSERT_FALSE(st->tx_fin_sent);

    // Close the stream with no payload: nothing is "more", but the FIN is due.
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_stream_send(&qc, 0, (const uint8_t *)"", 0, PROTO_TRUE));
    TEST_ASSERT_TRUE(st->tx_fin);
    size_t n = pc_quic_conn_send(&qc, out, sizeof out);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(st->tx_fin_sent);

    // The empty FIN really is on the wire as a zero-length STREAM frame.
    uint8_t plain[PC_QUIC_MAX_DATAGRAM];
    size_t pt = open_short(out, n, sizeof(CLIENT_SCID), &aps, plain);
    TEST_ASSERT_NOT_EQUAL(SIZE_MAX, pt);
    QuicFrame f;
    size_t got = pc_quic_frame_parse(plain, pt, &f);
    TEST_ASSERT_TRUE(got > 0);
    TEST_ASSERT_TRUE(f.type >= QUIC_FT_STREAM);
    TEST_ASSERT_EQUAL_UINT64(0, f.stream.length);
    TEST_ASSERT_TRUE(f.stream.fin);

    // With the FIN sent and nothing pending the stream is skipped entirely from here on.
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, out, sizeof out));
}

// When several streams have more data than one frame payload can hold, the ones that no longer fit
// emit nothing at all rather than a truncated frame - their data waits for the next round, and the
// backlog still drains completely over successive rounds.
void test_quic_conn_stream_tx_datagram_full()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&qc, &cb, &init, &apc, &aps);

    // Large stream ids force 8-byte varints, so a frame header alone needs more room than the tail
    // of a full datagram has left.
    static uint8_t payload[500];
    memset(payload, 0x77, sizeof(payload));
    uint64_t ids[PC_QUIC_MAX_STREAMS];
    for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
    {
        ids[i] = 0x3F00000000000000ull + (uint64_t)(i * 4);
        TEST_ASSERT_EQUAL_UINT(sizeof(payload),
                               pc_quic_conn_stream_send(&qc, ids[i], payload, sizeof(payload), PROTO_FALSE));
    }

    uint8_t out[PC_QUIC_MAX_DATAGRAM];
    (void)pc_quic_conn_send(&qc, out, sizeof out);

    // At least one stream got nothing this round - the frame payload had no room left for it - and at
    // least one was cut short mid-buffer.
    size_t untouched = 0, partial = 0;
    for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
    {
        for (size_t j = 0; j < PC_QUIC_MAX_STREAMS; j++)
        {
            if (qc.streams[j].id == ids[i])
            {
                if (qc.streams[j].tx_sent == 0)
                {
                    untouched++;
                }
                else if (qc.streams[j].tx_sent < qc.streams[j].tx_have)
                {
                    partial++;
                }
            }
        }
    }
    TEST_ASSERT_TRUE(untouched > 0);
    TEST_ASSERT_TRUE(partial > 0);

    // Everything still drains over subsequent rounds.
    qc.address_validated = PROTO_TRUE;
    for (int round = 0; round < 20; round++)
    {
        (void)pc_quic_conn_send(&qc, out, sizeof out);
    }
    for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
    {
        for (size_t j = 0; j < PC_QUIC_MAX_STREAMS; j++)
        {
            if (qc.streams[j].id == ids[i])
            {
                TEST_ASSERT_EQUAL_UINT(qc.streams[j].tx_have, qc.streams[j].tx_sent);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, out, sizeof out)); // nothing left queued
}

// pc_quic_conn_stream_send clamps to the space left in the stream's send buffer, and withholds the
// FIN when it could not take everything the caller offered (the final size is not yet known).
void test_quic_conn_stream_send_clamped()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);

    static uint8_t huge[PC_QUIC_STREAM_TX + 512];
    memset(huge, 0x2B, sizeof(huge));
    size_t took = pc_quic_conn_stream_send(&qc, 0, huge, sizeof(huge), PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT(PC_QUIC_STREAM_TX, took); // clamped to the buffer

    QuicStream *st = NULL;
    for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
    {
        if (qc.streams[i].id == 0)
        {
            st = &qc.streams[i];
        }
    }
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT(PC_QUIC_STREAM_TX, st->tx_have);
    TEST_ASSERT_FALSE(st->tx_fin); // the caller's FIN was not honoured: not all bytes were accepted
}

// UINT64_MAX is the free-slot sentinel, so it can never name a real stream: bytes "sent" on it are
// accepted into a slot that stays free and are never put on the wire.
void test_quic_conn_stream_send_sentinel_id()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&qc, &cb, &init, &apc, &aps);

    TEST_ASSERT_EQUAL_UINT(2, pc_quic_conn_stream_send(&qc, UINT64_MAX, (const uint8_t *)"hi", 2, PROTO_TRUE));
    for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
    {
        TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, qc.streams[i].id); // every slot is still free
    }

    uint8_t out[PC_QUIC_MAX_DATAGRAM];
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, out, sizeof out)); // nothing to send
}

// The Probe Timeout backs off exponentially, stops doubling at its ceiling, and (once the count is
// pinned high) saturates the period instead of overflowing the shift.
void test_quic_conn_pto_backoff_ceiling()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&qc, &cb, &init, ch, &ch_len);
    uint8_t out[2048];
    TEST_ASSERT_TRUE(pc_quic_conn_send(&qc, out, sizeof out) > 0); // an ack-eliciting flight is outstanding

    uint32_t now = 1000;
    pc_quic_conn_on_timeout(&qc, now); // arm
    TEST_ASSERT_TRUE(qc.pto_armed);
    // Fire it more times than the backoff counter can grow: it stops at its cap.
    for (int i = 0; i < 12; i++)
    {
        now = qc.pto_deadline_ms + 1;
        pc_quic_conn_on_timeout(&qc, now);
    }
    TEST_ASSERT_EQUAL_UINT8(8, qc.pto_count); // capped

    // With the counter pinned far above the cap the period saturates below 2^31 rather than
    // overflowing the shift (RFC 9002 sec 6.2.1).
    qc.pto_count = 40;
    qc.pto_armed = PROTO_FALSE;
    pc_quic_conn_on_timeout(&qc, 0); // re-arm with the saturated period
    TEST_ASSERT_TRUE(qc.pto_armed);
    TEST_ASSERT_EQUAL_UINT32(2097152000u, qc.pto_deadline_ms); // PC_QUIC_PTO_MS << 21
}

// An "ACK owed" flag on a space that has never received a packet emits no ACK: there is no largest
// received packet number to acknowledge, so the flag is left standing rather than acked from nothing.
void test_quic_conn_ack_owed_without_rx()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&qc, &cb, &init, &apc, &aps);
    uint8_t out[PC_QUIC_MAX_DATAGRAM];
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, out, sizeof out)); // baseline: nothing queued

    qc.space[QUIC_ENC_APP].ack_eliciting_rx = PROTO_TRUE;
    qc.space[QUIC_ENC_APP].have_rx = PROTO_FALSE;
    TEST_ASSERT_EQUAL_UINT(0, pc_quic_conn_send(&qc, out, sizeof out));
    TEST_ASSERT_TRUE(qc.space[QUIC_ENC_APP].ack_eliciting_rx); // not consumed: nothing was sent
}

// A close queued at a level we hold no seal keys for falls back to the highest level that still has
// them, rather than trying to protect the packet with keys that were never installed.
void test_quic_conn_close_level_without_keys()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb); // no ClientHello yet: only the Initial keys exist
    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

    // A padded Initial PING gives the connection an amplification budget without starting the TLS
    // handshake, so the Handshake and 1-RTT keys are still absent.
    uint8_t fr[64] = {QUIC_FT_PING};
    uint8_t dg[512];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, sizeof fr);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_EQUAL_UINT8(QTLS_START, qc.tls.state);

    pc_quic_conn_close(&qc, QUIC_ERR_NO_ERROR);
    TEST_ASSERT_TRUE(qc.close_queued);
    qc.close_level = QUIC_ENC_APP; // in range and not discarded, but 1-RTT keys are absent
    TEST_ASSERT_FALSE(qc.space[QUIC_ENC_APP].discarded);

    uint8_t out[512];
    size_t n = pc_quic_conn_send(&qc, out, sizeof out);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(pc_quic_conn_is_closed(&qc));

    // It went out at Initial - the only level with keys - so the peer can actually read it.
    uint8_t plain[512];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(out, n, &init.server, plain, &wire, &type);
    TEST_ASSERT_EQUAL_UINT8(QUIC_LP_INITIAL, type);
    TEST_ASSERT_NOT_EQUAL(SIZE_MAX, pt);
    TEST_ASSERT_TRUE(has_frame(plain, pt, QUIC_FT_CONNECTION_CLOSE));
}

// A connection that is draining but was not itself closed still reports closed to the application:
// no new work may be started on it (RFC 9000 sec 10.2.2).
void test_quic_conn_is_closed_draining_only()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&qc, &cb);
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc)); // neither flag

    qc.draining = PROTO_TRUE;
    TEST_ASSERT_FALSE(qc.closed);
    TEST_ASSERT_TRUE(pc_quic_conn_is_closed(&qc)); // draining alone is enough
}

// The PTO's "is anything outstanding?" test consults every packet-number space in turn: an
// unacknowledged ack-eliciting packet in the Initial, Handshake OR 1-RTT space arms the timer, a
// discarded space never counts, and only when no space has anything left does the timer disarm.
void test_quic_conn_pto_outstanding_per_space()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    static const int levels[3] = {QUIC_ENC_INITIAL, QUIC_ENC_HANDSHAKE, QUIC_ENC_APP};

    for (int i = 0; i < 3; i++)
    {
        init_conn(&qc, &cb);
        // Exactly one space has sent ack-eliciting packet 3 and had only up to 2 acknowledged.
        qc.space[levels[i]].last_ae_pn = 3;
        qc.space[levels[i]].largest_acked = 2;
        pc_quic_conn_on_timeout(&qc, 1000);
        TEST_ASSERT_TRUE(qc.pto_armed);
        TEST_ASSERT_EQUAL_UINT32(1000 + PC_QUIC_PTO_MS, qc.pto_deadline_ms);
    }

    // Nothing outstanding anywhere: the timer disarms and the backoff resets.
    init_conn(&qc, &cb);
    qc.pto_armed = PROTO_TRUE;
    qc.pto_count = 3;
    pc_quic_conn_on_timeout(&qc, 1000);
    TEST_ASSERT_FALSE(qc.pto_armed);
    TEST_ASSERT_EQUAL_UINT8(0, qc.pto_count);

    // A discarded space does not count, even with an unacknowledged ack-eliciting packet recorded:
    // its keys are gone, so there is nothing left to retransmit at that level.
    init_conn(&qc, &cb);
    qc.space[QUIC_ENC_APP].last_ae_pn = 3;
    qc.space[QUIC_ENC_APP].largest_acked = 2;
    qc.space[QUIC_ENC_APP].discarded = PROTO_TRUE;
    pc_quic_conn_on_timeout(&qc, 1000);
    TEST_ASSERT_FALSE(qc.pto_armed);
}

// Once the peer has acknowledged everything in every space - Initial discarded, Handshake acked, and
// now the 1-RTT packet acked too - the PTO disarms instead of retransmitting data the peer already has.
void test_quic_conn_pto_disarms_when_all_acked()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&qc, &cb, &init, &apc, &aps);

    // The 1-RTT HANDSHAKE_DONE packet is still unacknowledged, so the PTO arms.
    pc_quic_conn_on_timeout(&qc, 1000);
    TEST_ASSERT_TRUE(qc.pto_armed);

    // The client acknowledges 1-RTT packet 0; now no space has anything outstanding.
    uint8_t fr[32];
    size_t fl = pc_quic_build_ack(fr, sizeof fr, 0, 0, 0);
    uint8_t dg[256];
    size_t dl = build_short(dg, sizeof dg, SERVER_SCID, sizeof(SERVER_SCID), 0, &apc, fr, fl);
    TEST_ASSERT_TRUE(pc_quic_conn_recv(&qc, dg, dl));
    TEST_ASSERT_EQUAL_INT64(0, qc.space[QUIC_ENC_APP].largest_acked);

    qc.pto_armed = PROTO_TRUE; // the ACK already cleared it; re-arm so the disarm below is what clears it
    pc_quic_conn_on_timeout(&qc, 2000);
    TEST_ASSERT_FALSE(qc.pto_armed);
    TEST_ASSERT_EQUAL_UINT8(0, qc.pto_count);
    TEST_ASSERT_FALSE(pc_quic_conn_is_closed(&qc));
}

// A PTO that fires twice without an intervening send re-queues HANDSHAKE_DONE only once: the second
// firing sees it already queued (and not sent) and leaves it alone.
void test_quic_conn_pto_requeues_handshake_done_once()
{
    fill();
    QuicConn qc;
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&qc, &cb, &init, &apc, &aps);
    TEST_ASSERT_TRUE(qc.handshake_done_sent);

    // The client never acknowledged the 1-RTT packet, so the APP space is still outstanding.
    pc_quic_conn_on_timeout(&qc, 1000); // arm
    pc_quic_conn_on_timeout(&qc, 1000 + PC_QUIC_PTO_MS + 1);
    TEST_ASSERT_TRUE(qc.handshake_done_queued);
    TEST_ASSERT_FALSE(qc.handshake_done_sent);

    // A second PTO without a send in between: already queued, so nothing changes.
    pc_quic_conn_on_timeout(&qc, qc.pto_deadline_ms + 1);
    TEST_ASSERT_TRUE(qc.handshake_done_queued);
    TEST_ASSERT_FALSE(qc.handshake_done_sent);
}

int main(int, char **)
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
