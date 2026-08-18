// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/asymmetric/curve25519.h"
#include "crypto/hash/sha256.h"
// This suite asserts on the engine's own state - the packet-number spaces, the stream table, the
// Probe Timeout - which the golden shape keeps private to quic_conn.c. It compiles that translation
// unit into itself rather than widening the header, so quic_conn.h stays the public contract and
// every assertion below reads the real thing. The env's src list drops quic_conn.c to match.
#include "network_drivers/presentation/http/http3/quic_conn.c"
#include "network_drivers/presentation/http/http3/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_tls.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include <string.h>

#include <unity.h>

static uint8_t quic_crypto_work[16]; // the borrow an entry takes; QuicCrypto never reads it

static uint8_t quic_packet_work[16]; // the borrow an entry takes; QuicPacket never reads it

static uint8_t quic_frame_work[16]; // the borrow an entry takes; QuicFrame never reads it

static uint8_t quic_tp_work[16]; // the borrow an entry takes; QuicTp never reads it

static uint8_t quic_varint_work[16]; // the borrow an entry takes; QuicVarint never reads it

// The two spans each connection under test runs out of: the context (secure on a device, plain
// storage here) and the bytes it owes its streams.
static uint8_t g_qc_ctx[PROTOCORE_QUIC_CONN_CTX_BORROW];
static uint8_t g_qc_b[PROTOCORE_QUIC_CONN_BORROW];
static uint8_t g_qc2_ctx[PROTOCORE_QUIC_CONN_CTX_BORROW];
static uint8_t g_qc2_b[PROTOCORE_QUIC_CONN_BORROW];

// The connection each span holds, named as the suite already names it.
#define g_qc (*QUIC_CTX(g_qc_ctx))
#define g_qc2 (*QUIC_CTX(g_qc2_ctx))

// The byte span that goes with a connection's context span. A helper is handed the context and has
// to bind both, so the pairing lives here rather than at every call.
static uint8_t *qc_span(const QuicConnCtx *qc)
{
    return (qc == QUIC_CTX(g_qc2_ctx)) ? g_qc2_b : g_qc_b;
}

static uint8_t tw[4096];
static uint8_t tw_t[4096];
static uint8_t tw_tctx[4096];

static void assert_ctx_match(uint8_t *a, uint8_t *b)
{
    uint8_t n12[12] = {0}, zpt[16] = {0}, c1[16], t1[16], c2[16], t2[16];
    Aes128Gcm.seal_args.nonce = n12;
    Aes128Gcm.seal_args.aad = NULL;
    Aes128Gcm.seal_args.aad_len = 0;
    Aes128Gcm.seal_args.pt = zpt;
    Aes128Gcm.seal_args.pt_len = sizeof zpt;
    Aes128Gcm.seal_args.ct_out = c1;
    Aes128Gcm.seal_args.tag_out = t1;
    Aes128Gcm.seal(a);
    (void)Aes128Gcm.ok;
    Aes128Gcm.seal_args.nonce = n12;
    Aes128Gcm.seal_args.aad = NULL;
    Aes128Gcm.seal_args.aad_len = 0;
    Aes128Gcm.seal_args.pt = zpt;
    Aes128Gcm.seal_args.pt_len = sizeof zpt;
    Aes128Gcm.seal_args.ct_out = c2;
    Aes128Gcm.seal_args.tag_out = t2;
    Aes128Gcm.seal(b);
    (void)Aes128Gcm.ok;
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
    uint64_t length = (uint64_t)pn_len + frame_len + 16;
    QuicVarint.encode_args.out = out + p;
    QuicVarint.encode_args.cap = cap - p;
    QuicVarint.encode_args.value = length;
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

static size_t open_long(const uint8_t *dg, size_t len, QuicPacketKeys *keys, uint8_t *plain, size_t *wire_len,
                        uint8_t *type_out)
{
    QuicLongHeader h;
    QuicPacket.parse_long_header_args.buf = dg;
    QuicPacket.parse_long_header_args.len = len;
    QuicPacket.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacket.ok);
    *type_out = h.type;
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
    *wire_len = off + (size_t)length;
    static uint8_t work[2048];
    memcpy(work, dg, *wire_len);
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
    QuicTp.defaults_args.tp = &cfg->params;
    QuicTp.defaults(quic_tp_work);
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.init_args.cfg = &cfg;
    QuicConn.init_args.odcid = ODCID;
    QuicConn.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConn.init_args.peer_scid = CLIENT_SCID;
    QuicConn.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConn.init_args.our_scid = SERVER_SCID;
    QuicConn.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    QuicConn.cb = cb;
    QuicConn.init(QuicConn.internal);

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
    uint8_t ctp_enc[128];
    QuicTp.encode_args.tp = &ctp;
    QuicTp.encode_args.out = ctp_enc;
    QuicTp.encode_args.cap = sizeof(ctp_enc);
    QuicTp.encode(quic_tp_work);
    size_t ctp_len = QuicTp.n;
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);

    uint8_t frames[1200];
    QuicFrame.build_crypto_args.out = frames;
    QuicFrame.build_crypto_args.cap = sizeof(frames);
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = ch;
    QuicFrame.build_crypto_args.len = ch_len;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrame.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);

    uint8_t sdg[1500];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = sdg;
    QuicConn.send_args.cap = sizeof(sdg);
    QuicConn.send(QuicConn.internal);
    size_t sl = QuicConn.n;
    TEST_ASSERT_TRUE(sl > 0);

    uint8_t plain[2048], sh[512], hsflight[1024];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg, sl, &init.server, plain, &wire, &type);
    TEST_ASSERT_EQUAL_UINT8(QUIC_LP_INITIAL, type);
    size_t sh_len = extract_crypto(plain, pt, sh);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_SERVER_HELLO, sh[0]);

    uint8_t server_pub[32], ecdhe[32];
    Curve25519.x25519_base_args.out = server_pub;
    Curve25519.x25519_base_args.scalar = SERVER_PRIV;
    Curve25519.x25519_base(tw);
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    uint8_t *t;
    uint8_t ch_sh[32], ch_sf[32];
    t = tw_t;
    Sha256.init(t);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = ch_len;
    Sha256.update(t);
    Sha256.update_args.data = sh;
    Sha256.update_args.len = sh_len;
    Sha256.update(t);
    {
        Sha256.final_args.out = ch_sh;
        Sha256.final(t);
    }
    Tls13KeySchedule cks;
    static uint8_t ks_store_340[PROTOCORE_TLS13_KS_BORROW];
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.bind.s = ks_store_340;
    Tls13Ks.early(NULL);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = sizeof(ecdhe);
    Tls13Ks.step.ch_sh_hash = ch_sh;
    Tls13Ks.handshake(NULL);
    QuicPacketKeys hs_server_keys, hs_client_keys;
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_HS;
    QuicCrypto.keys_from_secret_args.out = &hs_server_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_HS;
    QuicCrypto.keys_from_secret_args.out = &hs_client_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);

    size_t hswire = 0;
    uint8_t hstype = 0;
    size_t hpt = open_long(sdg + wire, sl - wire, &hs_server_keys, plain, &hswire, &hstype);
    TEST_ASSERT_EQUAL_UINT8(QUIC_LP_HANDSHAKE, hstype);
    size_t hsflen = extract_crypto(plain, hpt, hsflight);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_ENCRYPTED_EXTENSIONS, hsflight[0]);

    Sha256.update_args.data = hsflight;
    Sha256.update_args.len = hsflen;
    Sha256.update(t);
    Sha256.final_args.out = ch_sf;
    Sha256.final(t);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ch_sfin_hash = ch_sf;
    Tls13Ks.master(NULL);
    QuicPacketKeys ap_server_keys, ap_client_keys;
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_AP;
    QuicCrypto.keys_from_secret_args.out = &ap_server_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_AP;
    QuicCrypto.keys_from_secret_args.out = &ap_client_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);

    assert_ctx_match(g_qc.tls.hs_server.gcm, hs_server_keys.gcm);
    assert_ctx_match(g_qc.tls.ap_server.gcm, ap_server_keys.gcm);

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
    Tls13Ks.finished_args.transcript_hash = ch_sf;
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
                            sizeof(CLIENT_SCID), 0, &hs_client_keys, hfr, hfl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = idg;
    QuicConn.recv_args.len = idl + hdl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_established(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.established);
    TEST_ASSERT_TRUE(g_hs_done);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = sdg;
    QuicConn.send_args.cap = sizeof(sdg);
    QuicConn.send(QuicConn.internal);
    sl = QuicConn.n;
    TEST_ASSERT_TRUE(sl > 0);
    proto_bool saw_hs_done = PROTO_FALSE;
    size_t off = 0;
    while (off < sl)
    {
        QuicPacket.is_long_header_args.first = sdg[off];
        QuicPacket.is_long_header(quic_packet_work);
        if (QuicPacket.ok)
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
    QuicFrame.build_stream_args.out = sfr;
    QuicFrame.build_stream_args.cap = sizeof(sfr);
    QuicFrame.build_stream_args.id = 0;
    QuicFrame.build_stream_args.offset = 0;
    QuicFrame.build_stream_args.data = (const uint8_t *)"GET";
    QuicFrame.build_stream_args.len = 3;
    QuicFrame.build_stream_args.fin = PROTO_TRUE;
    QuicFrame.build_stream(quic_frame_work);
    size_t sfl = QuicFrame.n;
    uint8_t s1[256];
    size_t s1l = build_short(s1, sizeof(s1), SERVER_SCID, sizeof(SERVER_SCID), 0, &ap_client_keys, sfr, sfl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = s1;
    QuicConn.recv_args.len = s1l;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    TEST_ASSERT_EQUAL_UINT64(0, g_stream_id);
    TEST_ASSERT_EQUAL_UINT(3, g_stream_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("GET", g_stream_data, 3);
    TEST_ASSERT_TRUE(g_stream_fin);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.stream_send_args.stream_id = 0;
    QuicConn.stream_send_args.data = (const uint8_t *)"OK";
    QuicConn.stream_send_args.len = 2;
    QuicConn.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(QuicConn.internal);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = sdg;
    QuicConn.send_args.cap = sizeof(sdg);
    QuicConn.send(QuicConn.internal);
    sl = QuicConn.n;
    TEST_ASSERT_TRUE(sl > 0);

    off = 0;
    proto_bool got_resp = PROTO_FALSE;
    while (off < sl)
    {
        QuicPacket.is_long_header_args.first = sdg[off];
        QuicPacket.is_long_header(quic_packet_work);
        if (QuicPacket.ok)
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

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 5000;
    QuicConn.on_timeout(QuicConn.internal);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 5000 + PROTOCORE_QUIC_PTO_MS + 1;
    QuicConn.on_timeout(QuicConn.internal);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = sdg;
    QuicConn.send_args.cap = sizeof(sdg);
    QuicConn.send(QuicConn.internal);
    sl = QuicConn.n;
    TEST_ASSERT_TRUE(sl > 0);
    proto_bool resent = PROTO_FALSE;
    off = 0;
    while (off < sl)
    {
        QuicPacket.is_long_header_args.first = sdg[off];
        QuicPacket.is_long_header(quic_packet_work);
        if (QuicPacket.ok)
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.init_args.cfg = &cfg;
    QuicConn.init_args.odcid = ODCID;
    QuicConn.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConn.init_args.peer_scid = CLIENT_SCID;
    QuicConn.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConn.init_args.our_scid = SERVER_SCID;
    QuicConn.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    QuicConn.cb = cb;
    QuicConn.init(QuicConn.internal);

    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    QuicTransportParams ctp;
    QuicTp.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    uint8_t ctp_enc[128];
    QuicTp.encode_args.tp = &ctp;
    QuicTp.encode_args.out = ctp_enc;
    QuicTp.encode_args.cap = sizeof(ctp_enc);
    QuicTp.encode(quic_tp_work);
    size_t ctp_len = QuicTp.n;
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    QuicFrame.build_crypto_args.out = frames;
    QuicFrame.build_crypto_args.cap = sizeof(frames);
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = ch;
    QuicFrame.build_crypto_args.len = ch_len;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrame.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);

    uint8_t sdg[1500];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = sdg;
    QuicConn.send_args.cap = sizeof(sdg);
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = sdg;
    QuicConn.send_args.cap = sizeof(sdg);
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 1000;
    QuicConn.on_timeout(QuicConn.internal);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = sdg;
    QuicConn.send_args.cap = sizeof(sdg);
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 1000 + PROTOCORE_QUIC_PTO_MS + 1;
    QuicConn.on_timeout(QuicConn.internal);
    uint8_t sdg2[1500];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = sdg2;
    QuicConn.send_args.cap = sizeof(sdg2);
    QuicConn.send(QuicConn.internal);
    size_t sl2 = QuicConn.n;
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 1000 + 10 * PROTOCORE_QUIC_PTO_MS;
    QuicConn.on_timeout(QuicConn.internal);
    TEST_ASSERT_FALSE(g_qc.pto_armed);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = sdg2;
    QuicConn.send_args.cap = sizeof(sdg2);
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
}

static void feed_client_initial(QuicConnCtx *qc, QuicConnCallbacks *cb, QuicInitialSecrets *init, uint8_t *ch,
                                size_t *ch_len)
{
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConn.bind.ctx = QUIC_SPAN(qc);
    QuicConn.bind.b = qc_span(qc);
    QuicConn.init_args.cfg = &cfg;
    QuicConn.init_args.odcid = ODCID;
    QuicConn.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConn.init_args.peer_scid = CLIENT_SCID;
    QuicConn.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConn.init_args.our_scid = SERVER_SCID;
    QuicConn.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    if (cb)
    {
        QuicConn.cb = *cb;
    }
    else
    {
        QuicConn.cb.on_stream_data = NULL;
        QuicConn.cb.on_handshake_done = NULL;
        QuicConn.cb.app = NULL;
    }
    QuicConn.init(QuicConn.internal);
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    QuicTransportParams ctp;
    QuicTp.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    uint8_t ctp_enc[128];
    QuicTp.encode_args.tp = &ctp;
    QuicTp.encode_args.out = ctp_enc;
    QuicTp.encode_args.cap = sizeof(ctp_enc);
    QuicTp.encode(quic_tp_work);
    size_t ctp_len = QuicTp.n;
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    *ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    QuicFrame.build_crypto_args.out = frames;
    QuicFrame.build_crypto_args.cap = sizeof(frames);
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = ch;
    QuicFrame.build_crypto_args.len = *ch_len;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrame.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init->client, frames, fl);
    QuicConn.bind.ctx = QUIC_SPAN(qc);
    QuicConn.bind.b = qc_span(qc);
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
}

void test_connection_close_api()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&g_qc, &cb, &init, ch, &ch_len);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicConn.close(QuicConn.internal);
    uint8_t cdg[512];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = cdg;
    QuicConn.send_args.cap = sizeof(cdg);
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.closed);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = cdg;
    QuicConn.send_args.cap = sizeof(cdg);
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = sdg;
    QuicConn.send_args.cap = sizeof(sdg);
    QuicConn.send(QuicConn.internal);
    size_t sl = QuicConn.n;
    TEST_ASSERT_TRUE(sl > 0);
    uint8_t plain[2048], sh[512];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg, sl, &init.server, plain, &wire, &type);
    size_t sh_len = extract_crypto(plain, pt, sh);
    uint8_t server_pub[32], ecdhe[32];
    Curve25519.x25519_base_args.out = server_pub;
    Curve25519.x25519_base_args.scalar = SERVER_PRIV;
    Curve25519.x25519_base(tw);
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    uint8_t *tctx;
    uint8_t ch_sh[32];
    tctx = tw_tctx;
    Sha256.init(tctx);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = ch_len;
    Sha256.update(tctx);
    Sha256.update_args.data = sh;
    Sha256.update_args.len = sh_len;
    Sha256.update(tctx);
    Sha256.final_args.out = ch_sh;
    Sha256.final(tctx);
    Tls13KeySchedule cks;
    static uint8_t ks_store_652[PROTOCORE_TLS13_KS_BORROW];
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.bind.s = ks_store_652;
    Tls13Ks.early(NULL);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = sizeof(ecdhe);
    Tls13Ks.step.ch_sh_hash = ch_sh;
    Tls13Ks.handshake(NULL);
    QuicPacketKeys hs_server_keys, hs_client_keys;
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_HS;
    QuicCrypto.keys_from_secret_args.out = &hs_server_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_HS;
    QuicCrypto.keys_from_secret_args.out = &hs_client_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);

    uint8_t bad[4] = {QUIC_FT_CRYPTO, 0x00, 0x7f, 0xff};
    uint8_t bdg[256];
    size_t bl = build_long(bdg, sizeof(bdg), QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                           0, &hs_client_keys, bad, sizeof(bad));
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = bdg;
    QuicConn.recv_args.len = bl;
    QuicConn.recv(QuicConn.internal);

    uint8_t cdg[512];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = cdg;
    QuicConn.send_args.cap = sizeof(cdg);
    QuicConn.send(QuicConn.internal);
    size_t cl = QuicConn.n;
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
        QuicFrameHeader f;
        QuicFrame.parse_args.buf = plain + fo;
        QuicFrame.parse_args.len = cpt - fo;
        QuicFrame.parse_args.out = &f;
        QuicFrame.parse(quic_frame_work);
        size_t n = QuicFrame.n;
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.closed);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = cdg;
    QuicConn.send_args.cap = sizeof(cdg);
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
}

static void init_conn(QuicConnCtx *qc, QuicConnCallbacks *cb)
{
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConn.bind.ctx = QUIC_SPAN(qc);
    QuicConn.bind.b = qc_span(qc);
    QuicConn.init_args.cfg = &cfg;
    QuicConn.init_args.odcid = ODCID;
    QuicConn.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConn.init_args.peer_scid = CLIENT_SCID;
    QuicConn.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConn.init_args.our_scid = SERVER_SCID;
    QuicConn.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    if (cb)
    {
        QuicConn.cb = *cb;
    }
    else
    {
        QuicConn.cb.on_stream_data = NULL;
        QuicConn.cb.on_handshake_done = NULL;
        QuicConn.cb.app = NULL;
    }
    QuicConn.init(QuicConn.internal);
}

void test_quic_recv_connection_close()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    uint8_t fr[32];
    QuicFrame.build_connection_close_args.out = fr;
    QuicFrame.build_connection_close_args.cap = sizeof(fr);
    QuicFrame.build_connection_close_args.app = PROTO_FALSE;
    QuicFrame.build_connection_close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicFrame.build_connection_close_args.frame_type = 0;
    QuicFrame.build_connection_close_args.reason = NULL;
    QuicFrame.build_connection_close_args.reason_len = 0;
    QuicFrame.build_connection_close(quic_frame_work);
    size_t fl = QuicFrame.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.closed);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);
}

void test_quic_recv_ping_and_max_data()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    uint8_t fr[16];
    QuicFrame.build_ping_args.out = fr;
    QuicFrame.build_ping_args.cap = sizeof(fr);
    QuicFrame.build_ping(quic_frame_work);
    size_t fl = QuicFrame.n;
    QuicFrame.build_max_data_args.out = fr + fl;
    QuicFrame.build_max_data_args.cap = sizeof(fr) - fl;
    QuicFrame.build_max_data_args.max = 1000000;
    QuicFrame.build_max_data(quic_frame_work);
    fl += QuicFrame.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);
}

void test_quic_recv_bad_version()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    dg[1] = dg[2] = dg[3] = dg[4] = 0xAA;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);
}

void test_quic_recv_unsupported_long_type()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_0RTT, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);
}

void test_quic_recv_short_before_app_keys()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_short(dg, sizeof(dg), SERVER_SCID, sizeof(SERVER_SCID), 0, &init.client, fr, 1);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);
}

void test_quic_recv_short_too_short()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    uint8_t dg[1] = {0x40};
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = sizeof(dg);
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);
}

void test_quic_recv_unprotect_failure()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    dg[dl - 1] ^= 0xFF;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_established(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.established);
}

void test_quic_recv_truncated_long_header()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    uint8_t dg[4] = {0xC0, 0x00, 0x00, 0x00};
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = sizeof(dg);
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);
}

void test_quic_send_amplification_limited()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    uint8_t out[256];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof(out);
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
}

void test_quic_crypto_out_of_order_and_dup()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t data[4] = {0x01, 0x00, 0x00, 0xFF};
    uint8_t fr[32], dg[256];

    QuicFrame.build_crypto_args.out = fr;
    QuicFrame.build_crypto_args.cap = sizeof(fr);
    QuicFrame.build_crypto_args.offset = 100;
    QuicFrame.build_crypto_args.data = data;
    QuicFrame.build_crypto_args.len = sizeof(data);
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrame.n;
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);

    QuicFrame.build_crypto_args.out = fr;
    QuicFrame.build_crypto_args.cap = sizeof(fr);
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = data;
    QuicFrame.build_crypto_args.len = sizeof(data);
    QuicFrame.build_crypto(quic_frame_work);
    fl = QuicFrame.n;
    dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 1,
                    &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    uint8_t dg2[256];
    size_t dl2 = build_long(dg2, sizeof(dg2), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            2, &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg2;
    QuicConn.recv_args.len = dl2;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
}

void test_quic_timeout_when_closed()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[32];
    QuicFrame.build_connection_close_args.out = fr;
    QuicFrame.build_connection_close_args.cap = sizeof(fr);
    QuicFrame.build_connection_close_args.app = PROTO_FALSE;
    QuicFrame.build_connection_close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicFrame.build_connection_close_args.frame_type = 0;
    QuicFrame.build_connection_close_args.reason = NULL;
    QuicFrame.build_connection_close_args.reason_len = 0;
    QuicFrame.build_connection_close(quic_frame_work);
    size_t fl = QuicFrame.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.closed);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 1000;
    QuicConn.on_timeout(QuicConn.internal);
}

void test_quic_stream_send_table_full()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    for (int i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.stream_send_args.stream_id = (uint64_t)(i * 4);
        QuicConn.stream_send_args.data = (const uint8_t *)"hi";
        QuicConn.stream_send_args.len = 2;
        QuicConn.stream_send_args.fin = PROTO_FALSE;
        QuicConn.stream_send(QuicConn.internal);
        TEST_ASSERT_EQUAL_UINT(2, QuicConn.n);
    }
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.stream_send_args.stream_id = 999;
    QuicConn.stream_send_args.data = (const uint8_t *)"x";
    QuicConn.stream_send_args.len = 1;
    QuicConn.stream_send_args.fin = PROTO_FALSE;
    QuicConn.stream_send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
}

void test_quic_recv_malformed_initial_headers()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    uint8_t dg[1500];

    QuicPacket.build_long_header_args.out = dg;
    QuicPacket.build_long_header_args.cap = sizeof dg;
    QuicPacket.build_long_header_args.type = QUIC_LP_INITIAL;
    QuicPacket.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacket.build_long_header_args.dcid = ODCID;
    QuicPacket.build_long_header_args.dcid_len = sizeof(ODCID);
    QuicPacket.build_long_header_args.scid = CLIENT_SCID;
    QuicPacket.build_long_header_args.scid_len = sizeof(CLIENT_SCID);
    QuicPacket.build_long_header_args.pn_len = 1;
    QuicPacket.build_long_header(quic_packet_work);
    size_t hn = QuicPacket.n;
    dg[hn] = 0xC0;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = hn + 1;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);

    dg[hn] = 0x40;
    dg[hn + 1] = 0xFF;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = hn + 2;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);

    dg[hn] = 0x00;
    dg[hn + 1] = 0xC0;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = hn + 2;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);

    dg[hn] = 0x00;
    dg[hn + 1] = 0x44;
    dg[hn + 2] = 0x00;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = hn + 8;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);

    dg[hn] = 0x00;
    QuicVarint.encode_args.out = dg + hn + 1;
    QuicVarint.encode_args.cap = sizeof(dg) - hn - 1;
    QuicVarint.encode_args.value = 1400;
    QuicVarint.encode(quic_varint_work);
    size_t c = QuicVarint.n;
    memset(dg + hn + 1 + c, 0, 1450 - (hn + 1 + c));
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = 1450;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);
}

void test_quic_recv_handshake_done_frame()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t hd[32];
    QuicFrame.build_handshake_done_args.out = hd;
    QuicFrame.build_handshake_done_args.cap = sizeof hd;
    QuicFrame.build_handshake_done(quic_frame_work);
    size_t hdl = QuicFrame.n;
    memset(hd + hdl, 0, 20);
    hdl += 20;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, hd, hdl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);
}

void test_quic_conn_stream_frames()
{
    fill();
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t dg[1500];

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        uint8_t data[4] = {1, 2, 3, 4};
        uint8_t fr[32];
        QuicFrame.build_stream_args.out = fr;
        QuicFrame.build_stream_args.cap = sizeof fr;
        QuicFrame.build_stream_args.id = 0;
        QuicFrame.build_stream_args.offset = 100;
        QuicFrame.build_stream_args.data = data;
        QuicFrame.build_stream_args.len = 4;
        QuicFrame.build_stream_args.fin = PROTO_FALSE;
        QuicFrame.build_stream(quic_frame_work);
        size_t fl = QuicFrame.n;
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
        g_stream_len = 0;
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.recv_args.datagram = dg;
        QuicConn.recv_args.len = dl;
        QuicConn.recv(QuicConn.internal);
        TEST_ASSERT_EQUAL_UINT(0, g_stream_len);
    }

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        uint8_t d0 = 0;
        uint8_t fr[16];
        QuicFrame.build_stream_args.out = fr;
        QuicFrame.build_stream_args.cap = sizeof fr;
        QuicFrame.build_stream_args.id = 0;
        QuicFrame.build_stream_args.offset = 0;
        QuicFrame.build_stream_args.data = &d0;
        QuicFrame.build_stream_args.len = 0;
        QuicFrame.build_stream_args.fin = PROTO_TRUE;
        QuicFrame.build_stream(quic_frame_work);
        size_t fl = QuicFrame.n;
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
        g_stream_fin = PROTO_FALSE;
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.recv_args.datagram = dg;
        QuicConn.recv_args.len = dl;
        QuicConn.recv(QuicConn.internal);
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
            QuicFrame.build_stream_args.out = fr + fl;
            QuicFrame.build_stream_args.cap = sizeof(fr) - fl;
            QuicFrame.build_stream_args.id = (uint64_t)(i * 4);
            QuicFrame.build_stream_args.offset = 0;
            QuicFrame.build_stream_args.data = &d1;
            QuicFrame.build_stream_args.len = 1;
            QuicFrame.build_stream_args.fin = PROTO_FALSE;
            QuicFrame.build_stream(quic_frame_work);
            fl += QuicFrame.n;
        }
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.recv_args.datagram = dg;
        QuicConn.recv_args.len = dl;
        QuicConn.recv(QuicConn.internal);
        TEST_ASSERT_TRUE(QuicConn.ok);
    }
}

void test_quic_conn_crypto_window_clamp()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t dg[1500];
    uint8_t chunk[1200];
    chunk[0] = 0x01;
    chunk[1] = 0x00;
    chunk[2] = 0xFF;
    chunk[3] = 0xFF;
    memset(chunk + 4, 0, sizeof(chunk) - 4);
    uint8_t fr[1300];
    QuicFrame.build_crypto_args.out = fr;
    QuicFrame.build_crypto_args.cap = sizeof fr;
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = chunk;
    QuicFrame.build_crypto_args.len = sizeof chunk;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrame.n;
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);
    QuicFrame.build_crypto_args.out = fr;
    QuicFrame.build_crypto_args.cap = sizeof fr;
    QuicFrame.build_crypto_args.offset = 1200;
    QuicFrame.build_crypto_args.data = chunk;
    QuicFrame.build_crypto_args.len = sizeof chunk;
    QuicFrame.build_crypto(quic_frame_work);
    fl = QuicFrame.n;
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 1, &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);
}

void test_quic_conn_crypto_error_close()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t bad_ch[6] = {0x01, 0x00, 0x00, 0x02, 0x03, 0x03};
    uint8_t fr[32];
    QuicFrame.build_crypto_args.out = fr;
    QuicFrame.build_crypto_args.cap = sizeof fr;
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = bad_ch;
    QuicFrame.build_crypto_args.len = sizeof bad_ch;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrame.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.close_args.error_code = 0;
    QuicConn.close(QuicConn.internal);
    uint8_t out[256];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);
}

void test_quic_conn_no_keys_build()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[32] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, sizeof fr);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    uint8_t out[256];

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    (void)QuicConn.n;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 0;
    QuicConn.on_timeout(QuicConn.internal);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 1;
    QuicConn.on_timeout(QuicConn.internal);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);
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
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.send_args.out = out;
        QuicConn.send_args.cap = cap;
        QuicConn.send(QuicConn.internal);
        (void)QuicConn.n;
    }
}

static void complete_handshake(QuicConnCtx *qc, QuicConnCallbacks *cb, QuicInitialSecrets *init,
                               QuicPacketKeys *ap_client, QuicPacketKeys *ap_server, uint8_t peer_scid_len)
{
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConn.bind.ctx = QUIC_SPAN(qc);
    QuicConn.bind.b = qc_span(qc);
    QuicConn.init_args.cfg = &cfg;
    QuicConn.init_args.odcid = ODCID;
    QuicConn.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConn.init_args.peer_scid = CLIENT_SCID;
    QuicConn.init_args.peer_scid_len = (uint8_t)(peer_scid_len);
    QuicConn.init_args.our_scid = SERVER_SCID;
    QuicConn.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    if (cb)
    {
        QuicConn.cb = *cb;
    }
    else
    {
        QuicConn.cb.on_stream_data = NULL;
        QuicConn.cb.on_handshake_done = NULL;
        QuicConn.cb.app = NULL;
    }
    QuicConn.init(QuicConn.internal);
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    QuicTransportParams ctp;
    QuicTp.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctp_enc[128];
    QuicTp.encode_args.tp = &ctp;
    QuicTp.encode_args.out = ctp_enc;
    QuicTp.encode_args.cap = sizeof(ctp_enc);
    QuicTp.encode(quic_tp_work);
    size_t ctp_len = QuicTp.n;
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    QuicFrame.build_crypto_args.out = frames;
    QuicFrame.build_crypto_args.cap = sizeof(frames);
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = ch;
    QuicFrame.build_crypto_args.len = ch_len;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrame.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init->client, frames, fl);
    QuicConn.bind.ctx = QUIC_SPAN(qc);
    QuicConn.bind.b = qc_span(qc);
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);

    uint8_t sdg[1500];
    QuicConn.bind.ctx = QUIC_SPAN(qc);
    QuicConn.bind.b = qc_span(qc);
    QuicConn.send_args.out = sdg;
    QuicConn.send_args.cap = sizeof(sdg);
    QuicConn.send(QuicConn.internal);
    size_t sl = QuicConn.n;
    uint8_t plain[2048], sh[512], hsflight[1024];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg, sl, &init->server, plain, &wire, &type);
    size_t sh_len = extract_crypto(plain, pt, sh);

    uint8_t server_pub[32], ecdhe[32];
    Curve25519.x25519_base_args.out = server_pub;
    Curve25519.x25519_base_args.scalar = SERVER_PRIV;
    Curve25519.x25519_base(tw);
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    uint8_t *t;
    uint8_t ch_sh[32], ch_sf[32];
    t = tw_t;
    Sha256.init(t);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = ch_len;
    Sha256.update(t);
    Sha256.update_args.data = sh;
    Sha256.update_args.len = sh_len;
    Sha256.update(t);
    {
        Sha256.final_args.out = ch_sh;
        Sha256.final(t);
    }
    Tls13KeySchedule cks;
    static uint8_t ks_store_1181[PROTOCORE_TLS13_KS_BORROW];
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.bind.s = ks_store_1181;
    Tls13Ks.early(NULL);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = sizeof(ecdhe);
    Tls13Ks.step.ch_sh_hash = ch_sh;
    Tls13Ks.handshake(NULL);
    QuicPacketKeys hs_server_keys, hs_client_keys;
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_HS;
    QuicCrypto.keys_from_secret_args.out = &hs_server_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_HS;
    QuicCrypto.keys_from_secret_args.out = &hs_client_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    size_t hswire = 0;
    uint8_t hstype = 0;
    size_t hpt = open_long(sdg + wire, sl - wire, &hs_server_keys, plain, &hswire, &hstype);
    size_t hsflen = extract_crypto(plain, hpt, hsflight);
    Sha256.update_args.data = hsflight;
    Sha256.update_args.len = hsflen;
    Sha256.update(t);
    Sha256.final_args.out = ch_sf;
    Sha256.final(t);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ch_sfin_hash = ch_sf;
    Tls13Ks.master(NULL);
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_AP;
    QuicCrypto.keys_from_secret_args.out = ap_client;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCrypto.keys_from_secret_args.keys_work = tw;
    QuicCrypto.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_AP;
    QuicCrypto.keys_from_secret_args.out = ap_server;
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
                            1, &init->client, ifr, ifl);
    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.finished_args.base_secret = cks.s + TLS13_KS_CLIENT_HS;
    Tls13Ks.finished_args.transcript_hash = ch_sf;
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
                            sizeof(CLIENT_SCID), 0, &hs_client_keys, hfr, hfl);
    QuicConn.bind.ctx = QUIC_SPAN(qc);
    QuicConn.bind.b = qc_span(qc);
    QuicConn.recv_args.datagram = idg;
    QuicConn.recv_args.len = idl + hdl;
    QuicConn.recv(QuicConn.internal);
    QuicConn.bind.ctx = QUIC_SPAN(qc);
    QuicConn.bind.b = qc_span(qc);
    QuicConn.send_args.out = sdg;
    QuicConn.send_args.cap = sizeof(sdg);
    QuicConn.send(QuicConn.internal);
}

void test_quic_conn_stream_nothing_to_send()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));
    uint8_t out[512];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.stream_send_args.stream_id = 0;
    QuicConn.stream_send_args.data = (const uint8_t *)"OK";
    QuicConn.stream_send_args.len = 2;
    QuicConn.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(2, QuicConn.n);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);
}

void test_quic_conn_short_header_tiny_cap()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.stream_send_args.stream_id = 0;
    QuicConn.stream_send_args.data = (const uint8_t *)"DATA";
    QuicConn.stream_send_args.len = 4;
    QuicConn.stream_send_args.fin = PROTO_FALSE;
    QuicConn.stream_send(QuicConn.internal);
    uint8_t out[8];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = 4;
    QuicConn.send(QuicConn.internal);
    (void)QuicConn.n;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    uint8_t out[256];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.closed);
}

void test_quic_conn_null_callbacks()
{
    fill();
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.init_args.cfg = &cfg;
    QuicConn.init_args.odcid = ODCID;
    QuicConn.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConn.init_args.peer_scid = CLIENT_SCID;
    QuicConn.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConn.init_args.our_scid = SERVER_SCID;
    QuicConn.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    QuicConn.cb.on_stream_data = NULL; // this connection is opened with no hooks at all
    QuicConn.cb.on_handshake_done = NULL;
    QuicConn.cb.app = NULL;
    QuicConn.init(QuicConn.internal);
    TEST_ASSERT_NULL(g_qc.cb.on_stream_data);
    TEST_ASSERT_NULL(g_qc.cb.on_handshake_done);

    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    uint8_t d[3] = {1, 2, 3};
    uint8_t fr[64];
    QuicFrame.build_stream_args.out = fr;
    QuicFrame.build_stream_args.cap = sizeof fr;
    QuicFrame.build_stream_args.id = 0;
    QuicFrame.build_stream_args.offset = 0;
    QuicFrame.build_stream_args.data = d;
    QuicFrame.build_stream_args.len = 3;
    QuicFrame.build_stream_args.fin = PROTO_FALSE;
    QuicFrame.build_stream(quic_frame_work);
    size_t fl = QuicFrame.n;
    QuicFrame.build_stream_args.out = fr + fl;
    QuicFrame.build_stream_args.cap = sizeof(fr) - fl;
    QuicFrame.build_stream_args.id = 0;
    QuicFrame.build_stream_args.offset = 3;
    QuicFrame.build_stream_args.data = d;
    QuicFrame.build_stream_args.len = 0;
    QuicFrame.build_stream_args.fin = PROTO_TRUE;
    QuicFrame.build_stream(quic_frame_work);
    fl += QuicFrame.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    TEST_ASSERT_EQUAL_UINT(0, g_stream_len);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);

    QuicPacketKeys apc, aps;
    QuicInitialSecrets init2;
    complete_handshake(&g_qc2, NULL, &init2, &apc, &aps, sizeof(CLIENT_SCID));
    QuicConn.bind.ctx = g_qc2_ctx;
    QuicConn.is_established(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.established);
    TEST_ASSERT_FALSE(g_hs_done);
}

void test_quic_conn_stream_duplicate_and_stale_fin()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t d[4] = {0xA1, 0xA2, 0xA3, 0xA4};
    uint8_t fr[64], dg[256];
    uint64_t pn = 0;

    QuicFrame.build_stream_args.out = fr;
    QuicFrame.build_stream_args.cap = sizeof fr;
    QuicFrame.build_stream_args.id = 0;
    QuicFrame.build_stream_args.offset = 0;
    QuicFrame.build_stream_args.data = d;
    QuicFrame.build_stream_args.len = 4;
    QuicFrame.build_stream_args.fin = PROTO_FALSE;
    QuicFrame.build_stream(quic_frame_work);
    size_t fl = QuicFrame.n;
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    TEST_ASSERT_EQUAL_UINT(4, g_stream_len);

    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    TEST_ASSERT_EQUAL_UINT(4, g_stream_len);

    QuicFrame.build_stream_args.out = fr;
    QuicFrame.build_stream_args.cap = sizeof fr;
    QuicFrame.build_stream_args.id = 0;
    QuicFrame.build_stream_args.offset = 0;
    QuicFrame.build_stream_args.data = d;
    QuicFrame.build_stream_args.len = 0;
    QuicFrame.build_stream_args.fin = PROTO_TRUE;
    QuicFrame.build_stream(quic_frame_work);
    fl = QuicFrame.n;
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    g_stream_fin = PROTO_FALSE;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    TEST_ASSERT_FALSE(g_stream_fin);

    QuicFrame.build_stream_args.out = fr;
    QuicFrame.build_stream_args.cap = sizeof fr;
    QuicFrame.build_stream_args.id = 0;
    QuicFrame.build_stream_args.offset = 4;
    QuicFrame.build_stream_args.data = d;
    QuicFrame.build_stream_args.len = 0;
    QuicFrame.build_stream_args.fin = PROTO_TRUE;
    QuicFrame.build_stream(quic_frame_work);
    fl = QuicFrame.n;
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    TEST_ASSERT_TRUE(g_stream_fin);

    g_stream_fin = PROTO_FALSE;
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    TEST_ASSERT_FALSE(g_stream_fin);
}

void test_quic_conn_frame_dispatch_variants()
{
    fill();
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t dg[512];

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);

        const uint8_t ack_ecn[8] = {0x03, 5, 0, 0, 0, 0, 0, 0};
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, ack_ecn,
                               sizeof ack_ecn);
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.recv_args.datagram = dg;
        QuicConn.recv_args.len = dl;
        QuicConn.recv(QuicConn.internal);
        TEST_ASSERT_TRUE(QuicConn.ok);
        TEST_ASSERT_EQUAL_INT64(5, g_qc.space[QUIC_ENC_INITIAL].largest_acked);
        TEST_ASSERT_FALSE(g_qc.space[QUIC_ENC_INITIAL].ack_eliciting_rx);

        uint8_t older[8];
        QuicFrame.build_ack_args.out = older;
        QuicFrame.build_ack_args.cap = sizeof older;
        QuicFrame.build_ack_args.largest = 3;
        QuicFrame.build_ack_args.delay = 0;
        QuicFrame.build_ack_args.first_range = 0;
        QuicFrame.build_ack(quic_frame_work);
        size_t ol = QuicFrame.n;
        dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 1, &init.client, older, ol);
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.recv_args.datagram = dg;
        QuicConn.recv_args.len = dl;
        QuicConn.recv(QuicConn.internal);
        TEST_ASSERT_TRUE(QuicConn.ok);
        TEST_ASSERT_EQUAL_INT64(5, g_qc.space[QUIC_ENC_INITIAL].largest_acked);
    }

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        const uint8_t app_close[3] = {0x1d, 0x05, 0x00};
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, app_close,
                               sizeof app_close);
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.recv_args.datagram = dg;
        QuicConn.recv_args.len = dl;
        QuicConn.recv(QuicConn.internal);
        TEST_ASSERT_TRUE(QuicConn.ok);
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.is_closed(QuicConn.internal);
        TEST_ASSERT_TRUE(QuicConn.closed);
        TEST_ASSERT_FALSE(g_qc.space[QUIC_ENC_INITIAL].ack_eliciting_rx);
    }

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        const uint8_t misc[6] = {QUIC_FT_RESET_STREAM, 0x00, 0x01, 0x02, QUIC_FT_MAX_STREAMS_BIDI, 0x08};
        size_t dl =
            build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, misc, sizeof misc);
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.recv_args.datagram = dg;
        QuicConn.recv_args.len = dl;
        QuicConn.recv(QuicConn.internal);
        TEST_ASSERT_TRUE(QuicConn.ok);
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.is_closed(QuicConn.internal);
        TEST_ASSERT_FALSE(QuicConn.closed);
        TEST_ASSERT_EQUAL_UINT(0, g_stream_len);
    }
}

void test_quic_recv_zero_version()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    dg[1] = dg[2] = dg[3] = dg[4] = 0x00;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);
}

void test_quic_recv_older_packet_number()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[24] = {QUIC_FT_PING};
    uint8_t dg[256];

    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 5, &init.client, fr, sizeof fr);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    TEST_ASSERT_EQUAL_UINT64(5, g_qc.space[QUIC_ENC_INITIAL].largest_rx);

    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 1, &init.client, fr, sizeof fr);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
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
    QuicFrame.build_ping_args.out = fr;
    QuicFrame.build_ping_args.cap = sizeof fr;
    QuicFrame.build_ping(quic_frame_work);
    size_t fl = QuicFrame.n;
    memset(fr + fl, 0, 20);
    fl += 20;
    uint8_t dg[256];
    size_t dl = build_short(dg, sizeof dg, SERVER_SCID, sizeof(SERVER_SCID), 0, &apc, fr, fl);
    dg[dl - 1] ^= 0xFF;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.ok);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);
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
    QuicFrame.build_crypto_args.out = fr;
    QuicFrame.build_crypto_args.cap = sizeof fr;
    QuicFrame.build_crypto_args.offset = off;
    QuicFrame.build_crypto_args.data = frag;
    QuicFrame.build_crypto_args.len = sizeof frag;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrame.n;
    memset(fr + fl, 0, 20);
    fl += 20;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 7,
                           &hs_client_keys, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    TEST_ASSERT_EQUAL_UINT8(QTLS_DONE, g_qc.tls.state);
    TEST_ASSERT_FALSE(g_qc.handshake_done_queued);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);

    g_qc.handshake_done_sent = PROTO_FALSE;
    g_qc.handshake_done_queued = PROTO_TRUE;
    g_hs_done = PROTO_FALSE;
    off = g_qc.space[QUIC_ENC_HANDSHAKE].crypto_rx_off;
    TEST_ASSERT_TRUE(off > 0);
    QuicFrame.build_crypto_args.out = fr;
    QuicFrame.build_crypto_args.cap = sizeof fr;
    QuicFrame.build_crypto_args.offset = off;
    QuicFrame.build_crypto_args.data = frag;
    QuicFrame.build_crypto_args.len = sizeof frag;
    QuicFrame.build_crypto(quic_frame_work);
    fl = QuicFrame.n;
    memset(fr + fl, 0, 20);
    fl += 20;
    dl = build_long(dg, sizeof dg, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 8,
                    &hs_client_keys, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
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
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    uint8_t fr[32];
    QuicFrame.build_connection_close_args.out = fr;
    QuicFrame.build_connection_close_args.cap = sizeof fr;
    QuicFrame.build_connection_close_args.app = PROTO_FALSE;
    QuicFrame.build_connection_close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicFrame.build_connection_close_args.frame_type = 0;
    QuicFrame.build_connection_close_args.reason = NULL;
    QuicFrame.build_connection_close_args.reason_len = 0;
    QuicFrame.build_connection_close(quic_frame_work);
    size_t fl = QuicFrame.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(g_qc.closed);
    TEST_ASSERT_TRUE(g_qc.draining);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.close_args.error_code = QUIC_ERR_FRAME_ENCODING;
    QuicConn.close(QuicConn.internal);
    TEST_ASSERT_FALSE(g_qc.close_queued);
    uint8_t out[512];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    for (int i = 0; i < 8 && QuicConn.n > 0; i++)
    {
    }
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicConn.close(QuicConn.internal);
    TEST_ASSERT_TRUE(g_qc.close_queued);

    uint8_t fr[32];
    QuicFrame.build_connection_close_args.out = fr;
    QuicFrame.build_connection_close_args.cap = sizeof fr;
    QuicFrame.build_connection_close_args.app = PROTO_FALSE;
    QuicFrame.build_connection_close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicFrame.build_connection_close_args.frame_type = 0;
    QuicFrame.build_connection_close_args.reason = NULL;
    QuicFrame.build_connection_close_args.reason_len = 0;
    QuicFrame.build_connection_close(quic_frame_work);
    size_t fl = QuicFrame.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 1,
                           &init.client, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(g_qc.draining);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);
    TEST_ASSERT_TRUE(g_qc.close_sent);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
}

void test_quic_conn_close_send_no_room()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&g_qc, &cb, &init, ch, &ch_len);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicConn.close(QuicConn.internal);
    uint8_t tiny[8];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = tiny;
    QuicConn.send_args.cap = sizeof tiny;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
    TEST_ASSERT_FALSE(g_qc.close_sent);
    TEST_ASSERT_FALSE(g_qc.closed);

    uint8_t out[512];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);
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

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicConn.close(QuicConn.internal);
    g_qc.close_level = 200;
    uint8_t out[512];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.closed);
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

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicConn.close(QuicConn.internal);
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.init_args.cfg = &cfg;
    QuicConn.init_args.odcid = ODCID;
    QuicConn.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConn.init_args.peer_scid = CLIENT_SCID;
    QuicConn.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConn.init_args.our_scid = SERVER_SCID;
    QuicConn.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    QuicConn.cb = cb;
    QuicConn.init(QuicConn.internal);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    QuicTransportParams ctp;
    QuicTp.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    uint8_t ctp_enc[128];
    QuicTp.encode_args.tp = &ctp;
    QuicTp.encode_args.out = ctp_enc;
    QuicTp.encode_args.cap = sizeof(ctp_enc);
    QuicTp.encode(quic_tp_work);
    size_t ctp_len = QuicTp.n;
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t chb[512];
    size_t ch_len = build_client_hello(chb, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    QuicFrame.build_crypto_args.out = frames;
    QuicFrame.build_crypto_args.cap = sizeof(frames);
    QuicFrame.build_crypto_args.offset = 0;
    QuicFrame.build_crypto_args.data = chb;
    QuicFrame.build_crypto_args.len = ch_len;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrame.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    TEST_ASSERT_EQUAL_UINT8(QTLS_WAIT_FINISHED, g_qc.tls.state);

    size_t hs_flight_len = 0;
    (void)protocore_quic_tls_flight(&g_qc.tls, QUIC_ENC_HANDSHAKE, &hs_flight_len);
    TEST_ASSERT_TRUE(hs_flight_len > PROTOCORE_QUIC_MAX_DATAGRAM);

    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    size_t first = QuicConn.n;
    TEST_ASSERT_TRUE(first > 0);
    uint64_t sent_after_first = g_qc.space[QUIC_ENC_HANDSHAKE].crypto_tx_off;
    TEST_ASSERT_TRUE(sent_after_first > 0);
    TEST_ASSERT_TRUE(sent_after_first < hs_flight_len);

    g_qc.address_validated = PROTO_TRUE;
    size_t total = sent_after_first;
    for (int i = 0; i < 8 && total < hs_flight_len; i++)
    {
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.send_args.out = out;
        QuicConn.send_args.cap = sizeof out;
        QuicConn.send(QuicConn.internal);
        TEST_ASSERT_TRUE(QuicConn.n > 0);
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
    QuicFrame.build_stream_args.out = fr;
    QuicFrame.build_stream_args.cap = sizeof fr;
    QuicFrame.build_stream_args.id = 0;
    QuicFrame.build_stream_args.offset = 0;
    QuicFrame.build_stream_args.data = d;
    QuicFrame.build_stream_args.len = 2;
    QuicFrame.build_stream_args.fin = PROTO_FALSE;
    QuicFrame.build_stream(quic_frame_work);
    size_t fl = QuicFrame.n;
    uint8_t sdg[256];
    size_t sl = build_short(sdg, sizeof sdg, SERVER_SCID, sizeof(SERVER_SCID), 1, &apc, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = sdg;
    QuicConn.recv_args.len = sl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    (void)QuicConn.n;

    static uint8_t big[PROTOCORE_QUIC_STREAM_TX - 64];
    memset(big, 0x5A, sizeof(big));
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.stream_send_args.stream_id = 0;
    QuicConn.stream_send_args.data = big;
    QuicConn.stream_send_args.len = sizeof(big);
    QuicConn.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(QuicConn.internal);
    size_t queued = QuicConn.n;
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

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    (void)QuicConn.n;
    TEST_ASSERT_TRUE(st->tx_sent > 0);
    TEST_ASSERT_TRUE(st->tx_sent < st->tx_have);
    TEST_ASSERT_EQUAL_UINT64(st->tx_sent, st->tx_off);
    TEST_ASSERT_FALSE(st->tx_fin_sent);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);
    TEST_ASSERT_EQUAL_UINT(st->tx_have, st->tx_sent);
    TEST_ASSERT_TRUE(st->tx_fin_sent);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.stream_send_args.stream_id = 0;
    QuicConn.stream_send_args.data = d;
    QuicConn.stream_send_args.len = 2;
    QuicConn.stream_send_args.fin = PROTO_FALSE;
    QuicConn.stream_send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(2, QuicConn.n);
    size_t before = st->tx_sent;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.stream_send_args.stream_id = 0;
    QuicConn.stream_send_args.data = (const uint8_t *)"BODY";
    QuicConn.stream_send_args.len = 4;
    QuicConn.stream_send_args.fin = PROTO_FALSE;
    QuicConn.stream_send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(4, QuicConn.n);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);
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

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.stream_send_args.stream_id = 0;
    QuicConn.stream_send_args.data = (const uint8_t *)"";
    QuicConn.stream_send_args.len = 0;
    QuicConn.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
    TEST_ASSERT_TRUE(st->tx_fin);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    size_t n = QuicConn.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(st->tx_fin_sent);

    uint8_t plain[PROTOCORE_QUIC_MAX_DATAGRAM];
    size_t pt = open_short(out, n, sizeof(CLIENT_SCID), &aps, plain);
    TEST_ASSERT_NOT_EQUAL(SIZE_MAX, pt);
    QuicFrameHeader f;
    QuicFrame.parse_args.buf = plain;
    QuicFrame.parse_args.len = pt;
    QuicFrame.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    size_t got = QuicFrame.n;
    TEST_ASSERT_TRUE(got > 0);
    TEST_ASSERT_TRUE(f.type >= QUIC_FT_STREAM);
    TEST_ASSERT_EQUAL_UINT64(0, f.stream.length);
    TEST_ASSERT_TRUE(f.stream.fin);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
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
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.stream_send_args.stream_id = ids[i];
        QuicConn.stream_send_args.data = payload;
        QuicConn.stream_send_args.len = sizeof(payload);
        QuicConn.stream_send_args.fin = PROTO_FALSE;
        QuicConn.stream_send(QuicConn.internal);
        TEST_ASSERT_EQUAL_UINT(sizeof(payload), QuicConn.n);
    }

    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    (void)QuicConn.n;

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
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.send_args.out = out;
        QuicConn.send_args.cap = sizeof out;
        QuicConn.send(QuicConn.internal);
        (void)QuicConn.n;
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
}

void test_quic_conn_stream_send_clamped()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);

    static uint8_t huge[PROTOCORE_QUIC_STREAM_TX + 512];
    memset(huge, 0x2B, sizeof(huge));
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.stream_send_args.stream_id = 0;
    QuicConn.stream_send_args.data = huge;
    QuicConn.stream_send_args.len = sizeof(huge);
    QuicConn.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(QuicConn.internal);
    size_t took = QuicConn.n;
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

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.stream_send_args.stream_id = UINT64_MAX;
    QuicConn.stream_send_args.data = (const uint8_t *)"hi";
    QuicConn.stream_send_args.len = 2;
    QuicConn.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(2, QuicConn.n);
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, g_qc.streams[i].id);
    }

    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.n > 0);

    uint32_t now = 1000;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = now;
    QuicConn.on_timeout(QuicConn.internal);
    TEST_ASSERT_TRUE(g_qc.pto_armed);

    for (int i = 0; i < 12; i++)
    {
        now = g_qc.pto_deadline_ms + 1;
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.timeout_args.now_ms = now;
        QuicConn.on_timeout(QuicConn.internal);
    }
    TEST_ASSERT_EQUAL_UINT8(8, g_qc.pto_count);

    g_qc.pto_count = 40;
    g_qc.pto_armed = PROTO_FALSE;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 0;
    QuicConn.on_timeout(QuicConn.internal);
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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);

    g_qc.space[QUIC_ENC_APP].ack_eliciting_rx = PROTO_TRUE;
    g_qc.space[QUIC_ENC_APP].have_rx = PROTO_FALSE;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    TEST_ASSERT_EQUAL_UINT(0, QuicConn.n);
    TEST_ASSERT_TRUE(g_qc.space[QUIC_ENC_APP].ack_eliciting_rx);
}

void test_quic_conn_close_level_without_keys()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCrypto.derive_initial_secrets_args.keys_work = tw;
    QuicCrypto.derive_initial_secrets_args.dcid = ODCID;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCrypto.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    uint8_t fr[64] = {QUIC_FT_PING};
    uint8_t dg[512];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, sizeof fr);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    TEST_ASSERT_EQUAL_UINT8(QTLS_START, g_qc.tls.state);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicConn.close(QuicConn.internal);
    TEST_ASSERT_TRUE(g_qc.close_queued);
    g_qc.close_level = QUIC_ENC_APP;
    TEST_ASSERT_FALSE(g_qc.space[QUIC_ENC_APP].discarded);

    uint8_t out[512];
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.send_args.out = out;
    QuicConn.send_args.cap = sizeof out;
    QuicConn.send(QuicConn.internal);
    size_t n = QuicConn.n;
    TEST_ASSERT_TRUE(n > 0);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.closed);

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
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);

    g_qc.draining = PROTO_TRUE;
    TEST_ASSERT_FALSE(g_qc.closed);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.closed);
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
        QuicConn.bind.ctx = g_qc_ctx;
        QuicConn.bind.b = g_qc_b;
        QuicConn.timeout_args.now_ms = 1000;
        QuicConn.on_timeout(QuicConn.internal);
        TEST_ASSERT_TRUE(g_qc.pto_armed);
        TEST_ASSERT_EQUAL_UINT32(1000 + PROTOCORE_QUIC_PTO_MS, g_qc.pto_deadline_ms);
    }

    init_conn(&g_qc, &cb);
    g_qc.pto_armed = PROTO_TRUE;
    g_qc.pto_count = 3;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 1000;
    QuicConn.on_timeout(QuicConn.internal);
    TEST_ASSERT_FALSE(g_qc.pto_armed);
    TEST_ASSERT_EQUAL_UINT8(0, g_qc.pto_count);

    init_conn(&g_qc, &cb);
    g_qc.space[QUIC_ENC_APP].last_ae_pn = 3;
    g_qc.space[QUIC_ENC_APP].largest_acked = 2;
    g_qc.space[QUIC_ENC_APP].discarded = PROTO_TRUE;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 1000;
    QuicConn.on_timeout(QuicConn.internal);
    TEST_ASSERT_FALSE(g_qc.pto_armed);
}

void test_quic_conn_pto_disarms_when_all_acked()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 1000;
    QuicConn.on_timeout(QuicConn.internal);
    TEST_ASSERT_TRUE(g_qc.pto_armed);

    uint8_t fr[32];
    QuicFrame.build_ack_args.out = fr;
    QuicFrame.build_ack_args.cap = sizeof fr;
    QuicFrame.build_ack_args.largest = 0;
    QuicFrame.build_ack_args.delay = 0;
    QuicFrame.build_ack_args.first_range = 0;
    QuicFrame.build_ack(quic_frame_work);
    size_t fl = QuicFrame.n;
    uint8_t dg[256];
    size_t dl = build_short(dg, sizeof dg, SERVER_SCID, sizeof(SERVER_SCID), 0, &apc, fr, fl);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.ok);
    TEST_ASSERT_EQUAL_INT64(0, g_qc.space[QUIC_ENC_APP].largest_acked);

    g_qc.pto_armed = PROTO_TRUE;
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 2000;
    QuicConn.on_timeout(QuicConn.internal);
    TEST_ASSERT_FALSE(g_qc.pto_armed);
    TEST_ASSERT_EQUAL_UINT8(0, g_qc.pto_count);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.is_closed(QuicConn.internal);
    TEST_ASSERT_FALSE(QuicConn.closed);
}

void test_quic_conn_pto_requeues_handshake_done_once()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));
    TEST_ASSERT_TRUE(g_qc.handshake_done_sent);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 1000;
    QuicConn.on_timeout(QuicConn.internal);
    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = 1000 + PROTOCORE_QUIC_PTO_MS + 1;
    QuicConn.on_timeout(QuicConn.internal);
    TEST_ASSERT_TRUE(g_qc.handshake_done_queued);
    TEST_ASSERT_FALSE(g_qc.handshake_done_sent);

    QuicConn.bind.ctx = g_qc_ctx;
    QuicConn.bind.b = g_qc_b;
    QuicConn.timeout_args.now_ms = g_qc.pto_deadline_ms + 1;
    QuicConn.on_timeout(QuicConn.internal);
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
