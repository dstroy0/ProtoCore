// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/asymmetric/curve25519/curve25519.h"
#include "crypto/hash/sha256/sha256.h"
// This suite asserts on the engine's own state - the packet-number spaces, the stream table, the
// Probe Timeout - which the golden shape keeps private to quic_conn.c. It compiles that translation
// unit into itself rather than widening the header, so quic_conn.h stays the public contract and
// every assertion below reads the real thing. The env's src list drops quic_conn.c to match.
#include "network_drivers/presentation/http/http3/quic_conn/quic_conn.c"
#include "network_drivers/presentation/http/http3/quic_crypto/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_frame/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_tls/quic_tls.h"
#include "network_drivers/presentation/http/http3/quic_varint/quic_varint.h"
#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include <string.h>

#include <unity.h>

static uint8_t quic_tls_work[16]; // the borrow an entry takes; QuicTlsServer never reads it

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
    Aes128GcmV.seal_args.nonce = n12;
    Aes128GcmV.seal_args.aad = NULL;
    Aes128GcmV.seal_args.aad_len = 0;
    Aes128GcmV.seal_args.pt = zpt;
    Aes128GcmV.seal_args.pt_len = sizeof zpt;
    Aes128GcmV.seal_args.ct_out = c1;
    Aes128GcmV.seal_args.tag_out = t1;
    Aes128Gcm.seal(a);
    (void)Aes128GcmV.ok;
    Aes128GcmV.seal_args.nonce = n12;
    Aes128GcmV.seal_args.aad = NULL;
    Aes128GcmV.seal_args.aad_len = 0;
    Aes128GcmV.seal_args.pt = zpt;
    Aes128GcmV.seal_args.pt_len = sizeof zpt;
    Aes128GcmV.seal_args.ct_out = c2;
    Aes128GcmV.seal_args.tag_out = t2;
    Aes128Gcm.seal(b);
    (void)Aes128GcmV.ok;
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

static void on_hs_done(void *, uint8_t *)
{
    g_hs_done = PROTO_TRUE;
}
static void on_stream_data(void *, uint8_t *, uint64_t id, const uint8_t *data, size_t len, proto_bool fin)
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
    QuicPacketV.pn_length_args.full_pn = pn;
    QuicPacketV.pn_length_args.largest_acked = -1;
    QuicPacket.pn_length(quic_packet_work);
    uint8_t pn_len = QuicPacketV.u8;
    QuicPacketV.build_long_header_args.out = out;
    QuicPacketV.build_long_header_args.cap = cap;
    QuicPacketV.build_long_header_args.type = type;
    QuicPacketV.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacketV.build_long_header_args.dcid = dcid;
    QuicPacketV.build_long_header_args.dcid_len = dcl;
    QuicPacketV.build_long_header_args.scid = scid;
    QuicPacketV.build_long_header_args.scid_len = scl;
    QuicPacketV.build_long_header_args.pn_len = pn_len;
    QuicPacket.build_long_header(quic_packet_work);
    size_t p = QuicPacketV.n;
    if (type == QUIC_LP_INITIAL)
    {
        QuicVarintV.encode_args.out = out + p;
        QuicVarintV.encode_args.cap = cap - p;
        QuicVarintV.encode_args.value = 0;
        QuicVarint.encode(quic_varint_work);
        p += QuicVarintV.n;
    }
    uint64_t length = (uint64_t)pn_len + frame_len + 16;
    QuicVarintV.encode_args.out = out + p;
    QuicVarintV.encode_args.cap = cap - p;
    QuicVarintV.encode_args.value = length;
    QuicVarint.encode(quic_varint_work);
    p += QuicVarintV.n;
    size_t pn_off = p;
    wr_pn(out + p, pn, pn_len);
    p += pn_len;
    memcpy(out + p, frames, frame_len);
    QuicCryptoV.packet_protect_args.pkt = out;
    QuicCryptoV.packet_protect_args.cap = cap;
    QuicCryptoV.packet_protect_args.pn_offset = pn_off;
    QuicCryptoV.packet_protect_args.pn_len = pn_len;
    QuicCryptoV.packet_protect_args.full_pn = pn;
    QuicCryptoV.packet_protect_args.payload_len = frame_len;
    QuicCryptoV.packet_protect_args.keys = keys;
    QuicCryptoV.packet_protect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_protect(quic_crypto_work);
    return QuicCryptoV.n;
}

static size_t build_short(uint8_t *out, size_t cap, const uint8_t *dcid, uint8_t dcl, uint64_t pn, QuicPacketKeys *keys,
                          const uint8_t *frames, size_t frame_len)
{
    QuicPacketV.pn_length_args.full_pn = pn;
    QuicPacketV.pn_length_args.largest_acked = -1;
    QuicPacket.pn_length(quic_packet_work);
    uint8_t pn_len = QuicPacketV.u8;
    out[0] = (uint8_t)(0x40 | (pn_len - 1));
    memcpy(out + 1, dcid, dcl);
    size_t pn_off = 1 + dcl;
    wr_pn(out + pn_off, pn, pn_len);
    memcpy(out + pn_off + pn_len, frames, frame_len);
    QuicCryptoV.packet_protect_args.pkt = out;
    QuicCryptoV.packet_protect_args.cap = cap;
    QuicCryptoV.packet_protect_args.pn_offset = pn_off;
    QuicCryptoV.packet_protect_args.pn_len = pn_len;
    QuicCryptoV.packet_protect_args.full_pn = pn;
    QuicCryptoV.packet_protect_args.payload_len = frame_len;
    QuicCryptoV.packet_protect_args.keys = keys;
    QuicCryptoV.packet_protect_args.is_long = PROTO_FALSE;
    QuicCrypto.packet_protect(quic_crypto_work);
    return QuicCryptoV.n;
}

static size_t open_long(const uint8_t *dg, size_t len, QuicPacketKeys *keys, uint8_t *plain, size_t *wire_len,
                        uint8_t *type_out)
{
    QuicLongHeader h;
    QuicPacketV.parse_long_header_args.buf = dg;
    QuicPacketV.parse_long_header_args.len = len;
    QuicPacketV.parse_long_header_args.out = &h;
    QuicPacket.parse_long_header(quic_packet_work);
    TEST_ASSERT_TRUE(QuicPacketV.ok);
    *type_out = h.type;
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
    *wire_len = off + (size_t)length;
    static uint8_t work[2048];
    memcpy(work, dg, *wire_len);
    uint64_t pn = 0;
    QuicCryptoV.packet_unprotect_args.pkt = work;
    QuicCryptoV.packet_unprotect_args.pn_offset = off;
    QuicCryptoV.packet_unprotect_args.length = (size_t)length;
    QuicCryptoV.packet_unprotect_args.largest_pn = 0;
    QuicCryptoV.packet_unprotect_args.keys = keys;
    QuicCryptoV.packet_unprotect_args.is_long = PROTO_TRUE;
    QuicCryptoV.packet_unprotect_args.out = plain;
    QuicCryptoV.packet_unprotect_args.out_pn = &pn;
    QuicCrypto.packet_unprotect(quic_crypto_work);
    return QuicCryptoV.n;
}

static size_t open_short(const uint8_t *dg, size_t len, uint8_t dcl, QuicPacketKeys *keys, uint8_t *plain)
{
    static uint8_t work[2048];
    memcpy(work, dg, len);
    uint64_t pn = 0;
    QuicCryptoV.packet_unprotect_args.pkt = work;
    QuicCryptoV.packet_unprotect_args.pn_offset = 1 + dcl;
    QuicCryptoV.packet_unprotect_args.length = len - (1 + dcl);
    QuicCryptoV.packet_unprotect_args.largest_pn = 0;
    QuicCryptoV.packet_unprotect_args.keys = keys;
    QuicCryptoV.packet_unprotect_args.is_long = PROTO_FALSE;
    QuicCryptoV.packet_unprotect_args.out = plain;
    QuicCryptoV.packet_unprotect_args.out_pn = &pn;
    QuicCrypto.packet_unprotect(quic_crypto_work);
    return QuicCryptoV.n;
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
        QuicFrameV.parse_args.buf = p + off;
        QuicFrameV.parse_args.len = len - off;
        QuicFrameV.parse_args.out = &f;
        QuicFrame.parse(quic_frame_work);
        size_t n = QuicFrameV.n;
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
        QuicFrameV.parse_args.buf = p + off;
        QuicFrameV.parse_args.len = len - off;
        QuicFrameV.parse_args.out = &f;
        QuicFrame.parse(quic_frame_work);
        size_t n = QuicFrameV.n;
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
    QuicTpV.defaults_args.tp = &cfg->params;
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
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.init_args.cfg = &cfg;
    QuicConnV.init_args.odcid = ODCID;
    QuicConnV.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConnV.init_args.peer_scid = CLIENT_SCID;
    QuicConnV.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConnV.init_args.our_scid = SERVER_SCID;
    QuicConnV.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    QuicConnV.cb = cb;
    QuicConn.init(g_qc_ctx);

    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    QuicTransportParams ctp;
    QuicTpV.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctp_enc[128];
    QuicTpV.encode_args.tp = &ctp;
    QuicTpV.encode_args.out = ctp_enc;
    QuicTpV.encode_args.cap = sizeof(ctp_enc);
    QuicTp.encode(quic_tp_work);
    size_t ctp_len = QuicTpV.n;
    uint8_t client_pub[32];
    Curve25519V.x25519_base_args.out = client_pub;
    Curve25519V.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);

    uint8_t frames[1200];
    QuicFrameV.build_crypto_args.out = frames;
    QuicFrameV.build_crypto_args.cap = sizeof(frames);
    QuicFrameV.build_crypto_args.offset = 0;
    QuicFrameV.build_crypto_args.data = ch;
    QuicFrameV.build_crypto_args.len = ch_len;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrameV.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);

    uint8_t sdg[1500];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = sdg;
    QuicConnV.send_args.cap = sizeof(sdg);
    QuicConnV.send(g_qc_ctx);
    size_t sl = QuicConnV.n;
    TEST_ASSERT_TRUE(sl > 0);

    uint8_t plain[2048], sh[512], hsflight[1024];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg, sl, &init.server, plain, &wire, &type);
    TEST_ASSERT_EQUAL_UINT8(QUIC_LP_INITIAL, type);
    size_t sh_len = extract_crypto(plain, pt, sh);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_SERVER_HELLO, sh[0]);

    uint8_t server_pub[32], ecdhe[32];
    Curve25519V.x25519_base_args.out = server_pub;
    Curve25519V.x25519_base_args.scalar = SERVER_PRIV;
    Curve25519.x25519_base(tw);
    Curve25519V.x25519_args.out = ecdhe;
    Curve25519V.x25519_args.scalar = CLIENT_PRIV;
    Curve25519V.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    uint8_t *t;
    uint8_t ch_sh[32], ch_sf[32];
    t = tw_t;
    Sha256.init(t);
    Sha256V.update_args.data = ch;
    Sha256V.update_args.len = ch_len;
    Sha256.update(t);
    Sha256V.update_args.data = sh;
    Sha256V.update_args.len = sh_len;
    Sha256.update(t);
    {
        Sha256V.final_args.out = ch_sh;
        Sha256.final(t);
    }
    Tls13KeySchedule cks;
    static uint8_t ks_store_340[PROTOCORE_TLS13_KS_BORROW];
    Tls13KsV.bind.kdf = &TLS13_KDF;
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.bind.s = ks_store_340;
    Tls13KsV.early(NULL);
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.step.ecdhe = ecdhe;
    Tls13KsV.step.ecdhe_len = sizeof(ecdhe);
    Tls13KsV.step.ch_sh_hash = ch_sh;
    Tls13Ks.handshake(NULL);
    QuicPacketKeys hs_server_keys, hs_client_keys;
    QuicCryptoV.keys_from_secret_args.keys_work = tw;
    QuicCryptoV.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_HS;
    QuicCryptoV.keys_from_secret_args.out = &hs_server_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCryptoV.keys_from_secret_args.keys_work = tw;
    QuicCryptoV.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_HS;
    QuicCryptoV.keys_from_secret_args.out = &hs_client_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);

    size_t hswire = 0;
    uint8_t hstype = 0;
    size_t hpt = open_long(sdg + wire, sl - wire, &hs_server_keys, plain, &hswire, &hstype);
    TEST_ASSERT_EQUAL_UINT8(QUIC_LP_HANDSHAKE, hstype);
    size_t hsflen = extract_crypto(plain, hpt, hsflight);
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_ENCRYPTED_EXTENSIONS, hsflight[0]);

    Sha256V.update_args.data = hsflight;
    Sha256V.update_args.len = hsflen;
    Sha256.update(t);
    Sha256V.final_args.out = ch_sf;
    Sha256.final(t);
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.step.ch_sfin_hash = ch_sf;
    Tls13Ks.master(NULL);
    QuicPacketKeys ap_server_keys, ap_client_keys;
    QuicCryptoV.keys_from_secret_args.keys_work = tw;
    QuicCryptoV.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_AP;
    QuicCryptoV.keys_from_secret_args.out = &ap_server_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCryptoV.keys_from_secret_args.keys_work = tw;
    QuicCryptoV.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_AP;
    QuicCryptoV.keys_from_secret_args.out = &ap_client_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);

    assert_ctx_match(g_qc.tls.hs_server.gcm, hs_server_keys.gcm);
    assert_ctx_match(g_qc.tls.ap_server.gcm, ap_server_keys.gcm);

    uint8_t ifr[64];
    QuicFrameV.build_ack_args.out = ifr;
    QuicFrameV.build_ack_args.cap = sizeof(ifr);
    QuicFrameV.build_ack_args.largest = 0;
    QuicFrameV.build_ack_args.delay = 0;
    QuicFrameV.build_ack_args.first_range = 0;
    QuicFrame.build_ack(quic_frame_work);
    size_t ifl = QuicFrameV.n;
    uint8_t idg[256];
    size_t idl = build_long(idg, sizeof(idg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init.client, ifr, ifl);

    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.finished_args.base_secret = cks.s + TLS13_KS_CLIENT_HS;
    Tls13KsV.finished_args.transcript_hash = ch_sf;
    Tls13KsV.finished_args.out = cfin + 4;
    Tls13Ks.finished_mac(NULL);
    uint8_t hfr[64];
    QuicFrameV.build_ack_args.out = hfr;
    QuicFrameV.build_ack_args.cap = sizeof(hfr);
    QuicFrameV.build_ack_args.largest = 0;
    QuicFrameV.build_ack_args.delay = 0;
    QuicFrameV.build_ack_args.first_range = 0;
    QuicFrame.build_ack(quic_frame_work);
    size_t hfl = QuicFrameV.n;
    QuicFrameV.build_crypto_args.out = hfr + hfl;
    QuicFrameV.build_crypto_args.cap = sizeof(hfr) - hfl;
    QuicFrameV.build_crypto_args.offset = 0;
    QuicFrameV.build_crypto_args.data = cfin;
    QuicFrameV.build_crypto_args.len = sizeof(cfin);
    QuicFrame.build_crypto(quic_frame_work);
    hfl += QuicFrameV.n;
    size_t hdl = build_long(idg + idl, sizeof(idg) - idl, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID,
                            sizeof(CLIENT_SCID), 0, &hs_client_keys, hfr, hfl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = idg;
    QuicConnV.recv_args.len = idl + hdl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);

    QuicConn.is_established(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.established);
    TEST_ASSERT_TRUE(g_hs_done);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = sdg;
    QuicConnV.send_args.cap = sizeof(sdg);
    QuicConnV.send(g_qc_ctx);
    sl = QuicConnV.n;
    TEST_ASSERT_TRUE(sl > 0);
    proto_bool saw_hs_done = PROTO_FALSE;
    size_t off = 0;
    while (off < sl)
    {
        QuicPacketV.is_long_header_args.first = sdg[off];
        QuicPacket.is_long_header(quic_packet_work);
        if (QuicPacketV.ok)
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
    QuicFrameV.build_stream_args.out = sfr;
    QuicFrameV.build_stream_args.cap = sizeof(sfr);
    QuicFrameV.build_stream_args.id = 0;
    QuicFrameV.build_stream_args.offset = 0;
    QuicFrameV.build_stream_args.data = (const uint8_t *)"GET";
    QuicFrameV.build_stream_args.len = 3;
    QuicFrameV.build_stream_args.fin = PROTO_TRUE;
    QuicFrame.build_stream(quic_frame_work);
    size_t sfl = QuicFrameV.n;
    uint8_t s1[256];
    size_t s1l = build_short(s1, sizeof(s1), SERVER_SCID, sizeof(SERVER_SCID), 0, &ap_client_keys, sfr, sfl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = s1;
    QuicConnV.recv_args.len = s1l;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    TEST_ASSERT_EQUAL_UINT64(0, g_stream_id);
    TEST_ASSERT_EQUAL_UINT(3, g_stream_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("GET", g_stream_data, 3);
    TEST_ASSERT_TRUE(g_stream_fin);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.stream_send_args.stream_id = 0;
    QuicConnV.stream_send_args.data = (const uint8_t *)"OK";
    QuicConnV.stream_send_args.len = 2;
    QuicConnV.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(g_qc_ctx);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = sdg;
    QuicConnV.send_args.cap = sizeof(sdg);
    QuicConnV.send(g_qc_ctx);
    sl = QuicConnV.n;
    TEST_ASSERT_TRUE(sl > 0);

    off = 0;
    proto_bool got_resp = PROTO_FALSE;
    while (off < sl)
    {
        QuicPacketV.is_long_header_args.first = sdg[off];
        QuicPacket.is_long_header(quic_packet_work);
        if (QuicPacketV.ok)
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
            QuicFrameV.parse_args.buf = plain + fo;
            QuicFrameV.parse_args.len = p2 - fo;
            QuicFrameV.parse_args.out = &f;
            QuicFrame.parse(quic_frame_work);
            size_t n = QuicFrameV.n;
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

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 5000;
    QuicConn.on_timeout(g_qc_ctx);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 5000 + PROTOCORE_QUIC_PTO_MS + 1;
    QuicConn.on_timeout(g_qc_ctx);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = sdg;
    QuicConnV.send_args.cap = sizeof(sdg);
    QuicConnV.send(g_qc_ctx);
    sl = QuicConnV.n;
    TEST_ASSERT_TRUE(sl > 0);
    proto_bool resent = PROTO_FALSE;
    off = 0;
    while (off < sl)
    {
        QuicPacketV.is_long_header_args.first = sdg[off];
        QuicPacket.is_long_header(quic_packet_work);
        if (QuicPacketV.ok)
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
            QuicFrameV.parse_args.buf = plain + fo;
            QuicFrameV.parse_args.len = p2 - fo;
            QuicFrameV.parse_args.out = &f;
            QuicFrame.parse(quic_frame_work);
            size_t n = QuicFrameV.n;
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
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.init_args.cfg = &cfg;
    QuicConnV.init_args.odcid = ODCID;
    QuicConnV.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConnV.init_args.peer_scid = CLIENT_SCID;
    QuicConnV.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConnV.init_args.our_scid = SERVER_SCID;
    QuicConnV.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    QuicConnV.cb = cb;
    QuicConn.init(g_qc_ctx);

    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    QuicTransportParams ctp;
    QuicTpV.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    uint8_t ctp_enc[128];
    QuicTpV.encode_args.tp = &ctp;
    QuicTpV.encode_args.out = ctp_enc;
    QuicTpV.encode_args.cap = sizeof(ctp_enc);
    QuicTp.encode(quic_tp_work);
    size_t ctp_len = QuicTpV.n;
    uint8_t client_pub[32];
    Curve25519V.x25519_base_args.out = client_pub;
    Curve25519V.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    QuicFrameV.build_crypto_args.out = frames;
    QuicFrameV.build_crypto_args.cap = sizeof(frames);
    QuicFrameV.build_crypto_args.offset = 0;
    QuicFrameV.build_crypto_args.data = ch;
    QuicFrameV.build_crypto_args.len = ch_len;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrameV.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);

    uint8_t sdg[1500];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = sdg;
    QuicConnV.send_args.cap = sizeof(sdg);
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = sdg;
    QuicConnV.send_args.cap = sizeof(sdg);
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 1000;
    QuicConn.on_timeout(g_qc_ctx);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = sdg;
    QuicConnV.send_args.cap = sizeof(sdg);
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 1000 + PROTOCORE_QUIC_PTO_MS + 1;
    QuicConn.on_timeout(g_qc_ctx);
    uint8_t sdg2[1500];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = sdg2;
    QuicConnV.send_args.cap = sizeof(sdg2);
    QuicConnV.send(g_qc_ctx);
    size_t sl2 = QuicConnV.n;
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
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 1000 + 10 * PROTOCORE_QUIC_PTO_MS;
    QuicConn.on_timeout(g_qc_ctx);
    TEST_ASSERT_FALSE(g_qc.pto_armed);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = sdg2;
    QuicConnV.send_args.cap = sizeof(sdg2);
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
}

static void feed_client_initial(QuicConnCtx *qc, QuicConnCallbacks *cb, QuicInitialSecrets *init, uint8_t *ch,
                                size_t *ch_len)
{
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConnV.bind.b = qc_span(qc);
    QuicConnV.init_args.cfg = &cfg;
    QuicConnV.init_args.odcid = ODCID;
    QuicConnV.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConnV.init_args.peer_scid = CLIENT_SCID;
    QuicConnV.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConnV.init_args.our_scid = SERVER_SCID;
    QuicConnV.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    if (cb)
    {
        QuicConnV.cb = *cb;
    }
    else
    {
        QuicConnV.cb.on_stream_data = NULL;
        QuicConnV.cb.on_handshake_done = NULL;
        QuicConnV.cb.app = NULL;
    }
    QuicConn.init(QUIC_SPAN(qc));
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    QuicTransportParams ctp;
    QuicTpV.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    uint8_t ctp_enc[128];
    QuicTpV.encode_args.tp = &ctp;
    QuicTpV.encode_args.out = ctp_enc;
    QuicTpV.encode_args.cap = sizeof(ctp_enc);
    QuicTp.encode(quic_tp_work);
    size_t ctp_len = QuicTpV.n;
    uint8_t client_pub[32];
    Curve25519V.x25519_base_args.out = client_pub;
    Curve25519V.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    *ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    QuicFrameV.build_crypto_args.out = frames;
    QuicFrameV.build_crypto_args.cap = sizeof(frames);
    QuicFrameV.build_crypto_args.offset = 0;
    QuicFrameV.build_crypto_args.data = ch;
    QuicFrameV.build_crypto_args.len = *ch_len;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrameV.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init->client, frames, fl);
    QuicConnV.bind.b = qc_span(qc);
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(QUIC_SPAN(qc));
    TEST_ASSERT_TRUE(QuicConnV.ok);
}

void test_connection_close_api()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&g_qc, &cb, &init, ch, &ch_len);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicConn.close(g_qc_ctx);
    uint8_t cdg[512];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = cdg;
    QuicConnV.send_args.cap = sizeof(cdg);
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.closed);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = cdg;
    QuicConnV.send_args.cap = sizeof(cdg);
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
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
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = sdg;
    QuicConnV.send_args.cap = sizeof(sdg);
    QuicConnV.send(g_qc_ctx);
    size_t sl = QuicConnV.n;
    TEST_ASSERT_TRUE(sl > 0);
    uint8_t plain[2048], sh[512];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg, sl, &init.server, plain, &wire, &type);
    size_t sh_len = extract_crypto(plain, pt, sh);
    uint8_t server_pub[32], ecdhe[32];
    Curve25519V.x25519_base_args.out = server_pub;
    Curve25519V.x25519_base_args.scalar = SERVER_PRIV;
    Curve25519.x25519_base(tw);
    Curve25519V.x25519_args.out = ecdhe;
    Curve25519V.x25519_args.scalar = CLIENT_PRIV;
    Curve25519V.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    uint8_t *tctx;
    uint8_t ch_sh[32];
    tctx = tw_tctx;
    Sha256.init(tctx);
    Sha256V.update_args.data = ch;
    Sha256V.update_args.len = ch_len;
    Sha256.update(tctx);
    Sha256V.update_args.data = sh;
    Sha256V.update_args.len = sh_len;
    Sha256.update(tctx);
    Sha256V.final_args.out = ch_sh;
    Sha256.final(tctx);
    Tls13KeySchedule cks;
    static uint8_t ks_store_652[PROTOCORE_TLS13_KS_BORROW];
    Tls13KsV.bind.kdf = &TLS13_KDF;
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.bind.s = ks_store_652;
    Tls13KsV.early(NULL);
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.step.ecdhe = ecdhe;
    Tls13KsV.step.ecdhe_len = sizeof(ecdhe);
    Tls13KsV.step.ch_sh_hash = ch_sh;
    Tls13Ks.handshake(NULL);
    QuicPacketKeys hs_server_keys, hs_client_keys;
    QuicCryptoV.keys_from_secret_args.keys_work = tw;
    QuicCryptoV.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_HS;
    QuicCryptoV.keys_from_secret_args.out = &hs_server_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCryptoV.keys_from_secret_args.keys_work = tw;
    QuicCryptoV.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_HS;
    QuicCryptoV.keys_from_secret_args.out = &hs_client_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);

    uint8_t bad[4] = {QUIC_FT_CRYPTO, 0x00, 0x7f, 0xff};
    uint8_t bdg[256];
    size_t bl = build_long(bdg, sizeof(bdg), QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                           0, &hs_client_keys, bad, sizeof(bad));
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = bdg;
    QuicConnV.recv_args.len = bl;
    QuicConn.recv(g_qc_ctx);

    uint8_t cdg[512];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = cdg;
    QuicConnV.send_args.cap = sizeof(cdg);
    QuicConnV.send(g_qc_ctx);
    size_t cl = QuicConnV.n;
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
        QuicFrameV.parse_args.buf = plain + fo;
        QuicFrameV.parse_args.len = cpt - fo;
        QuicFrameV.parse_args.out = &f;
        QuicFrame.parse(quic_frame_work);
        size_t n = QuicFrameV.n;
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
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.closed);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = cdg;
    QuicConnV.send_args.cap = sizeof(cdg);
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
}

static void init_conn(QuicConnCtx *qc, QuicConnCallbacks *cb)
{
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConnV.bind.b = qc_span(qc);
    QuicConnV.init_args.cfg = &cfg;
    QuicConnV.init_args.odcid = ODCID;
    QuicConnV.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConnV.init_args.peer_scid = CLIENT_SCID;
    QuicConnV.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConnV.init_args.our_scid = SERVER_SCID;
    QuicConnV.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    if (cb)
    {
        QuicConnV.cb = *cb;
    }
    else
    {
        QuicConnV.cb.on_stream_data = NULL;
        QuicConnV.cb.on_handshake_done = NULL;
        QuicConnV.cb.app = NULL;
    }
    QuicConn.init(QUIC_SPAN(qc));
}

void test_quic_recv_connection_close()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    uint8_t fr[32];
    QuicFrameV.build_connection_close_args.out = fr;
    QuicFrameV.build_connection_close_args.cap = sizeof(fr);
    QuicFrameV.build_connection_close_args.app = PROTO_FALSE;
    QuicFrameV.build_connection_close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicFrameV.build_connection_close_args.frame_type = 0;
    QuicFrameV.build_connection_close_args.reason = NULL;
    QuicFrameV.build_connection_close_args.reason_len = 0;
    QuicFrame.build_connection_close(quic_frame_work);
    size_t fl = QuicFrameV.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.closed);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);
}

void test_quic_recv_ping_and_max_data()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    uint8_t fr[16];
    QuicFrameV.build_ping_args.out = fr;
    QuicFrameV.build_ping_args.cap = sizeof(fr);
    QuicFrame.build_ping(quic_frame_work);
    size_t fl = QuicFrameV.n;
    QuicFrameV.build_max_data_args.out = fr + fl;
    QuicFrameV.build_max_data_args.cap = sizeof(fr) - fl;
    QuicFrameV.build_max_data_args.max = 1000000;
    QuicFrame.build_max_data(quic_frame_work);
    fl += QuicFrameV.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);
}

void test_quic_recv_bad_version()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    dg[1] = dg[2] = dg[3] = dg[4] = 0xAA;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);
}

void test_quic_recv_unsupported_long_type()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_0RTT, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);
}

void test_quic_recv_short_before_app_keys()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_short(dg, sizeof(dg), SERVER_SCID, sizeof(SERVER_SCID), 0, &init.client, fr, 1);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);
}

void test_quic_recv_short_too_short()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    uint8_t dg[1] = {0x40};
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = sizeof(dg);
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);
}

void test_quic_recv_unprotect_failure()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    dg[dl - 1] ^= 0xFF;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);
    QuicConn.is_established(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.established);
}

void test_quic_recv_truncated_long_header()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    uint8_t dg[4] = {0xC0, 0x00, 0x00, 0x00};
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = sizeof(dg);
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);
}

void test_quic_send_amplification_limited()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    uint8_t out[256];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof(out);
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
}

void test_quic_crypto_out_of_order_and_dup()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t data[4] = {0x01, 0x00, 0x00, 0xFF};
    uint8_t fr[32], dg[256];

    QuicFrameV.build_crypto_args.out = fr;
    QuicFrameV.build_crypto_args.cap = sizeof(fr);
    QuicFrameV.build_crypto_args.offset = 100;
    QuicFrameV.build_crypto_args.data = data;
    QuicFrameV.build_crypto_args.len = sizeof(data);
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrameV.n;
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);

    QuicFrameV.build_crypto_args.out = fr;
    QuicFrameV.build_crypto_args.cap = sizeof(fr);
    QuicFrameV.build_crypto_args.offset = 0;
    QuicFrameV.build_crypto_args.data = data;
    QuicFrameV.build_crypto_args.len = sizeof(data);
    QuicFrame.build_crypto(quic_frame_work);
    fl = QuicFrameV.n;
    dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 1,
                    &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    uint8_t dg2[256];
    size_t dl2 = build_long(dg2, sizeof(dg2), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            2, &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg2;
    QuicConnV.recv_args.len = dl2;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
}

void test_quic_timeout_when_closed()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[32];
    QuicFrameV.build_connection_close_args.out = fr;
    QuicFrameV.build_connection_close_args.cap = sizeof(fr);
    QuicFrameV.build_connection_close_args.app = PROTO_FALSE;
    QuicFrameV.build_connection_close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicFrameV.build_connection_close_args.frame_type = 0;
    QuicFrameV.build_connection_close_args.reason = NULL;
    QuicFrameV.build_connection_close_args.reason_len = 0;
    QuicFrame.build_connection_close(quic_frame_work);
    size_t fl = QuicFrameV.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.closed);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 1000;
    QuicConn.on_timeout(g_qc_ctx);
}

void test_quic_stream_send_table_full()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    for (int i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.stream_send_args.stream_id = (uint64_t)(i * 4);
        QuicConnV.stream_send_args.data = (const uint8_t *)"hi";
        QuicConnV.stream_send_args.len = 2;
        QuicConnV.stream_send_args.fin = PROTO_FALSE;
        QuicConn.stream_send(g_qc_ctx);
        TEST_ASSERT_EQUAL_UINT(2, QuicConnV.n);
    }
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.stream_send_args.stream_id = 999;
    QuicConnV.stream_send_args.data = (const uint8_t *)"x";
    QuicConnV.stream_send_args.len = 1;
    QuicConnV.stream_send_args.fin = PROTO_FALSE;
    QuicConn.stream_send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
}

void test_quic_recv_malformed_initial_headers()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    uint8_t dg[1500];

    QuicPacketV.build_long_header_args.out = dg;
    QuicPacketV.build_long_header_args.cap = sizeof dg;
    QuicPacketV.build_long_header_args.type = QUIC_LP_INITIAL;
    QuicPacketV.build_long_header_args.version = QUIC_VERSION_1;
    QuicPacketV.build_long_header_args.dcid = ODCID;
    QuicPacketV.build_long_header_args.dcid_len = sizeof(ODCID);
    QuicPacketV.build_long_header_args.scid = CLIENT_SCID;
    QuicPacketV.build_long_header_args.scid_len = sizeof(CLIENT_SCID);
    QuicPacketV.build_long_header_args.pn_len = 1;
    QuicPacket.build_long_header(quic_packet_work);
    size_t hn = QuicPacketV.n;
    dg[hn] = 0xC0;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = hn + 1;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);

    dg[hn] = 0x40;
    dg[hn + 1] = 0xFF;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = hn + 2;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);

    dg[hn] = 0x00;
    dg[hn + 1] = 0xC0;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = hn + 2;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);

    dg[hn] = 0x00;
    dg[hn + 1] = 0x44;
    dg[hn + 2] = 0x00;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = hn + 8;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);

    dg[hn] = 0x00;
    QuicVarintV.encode_args.out = dg + hn + 1;
    QuicVarintV.encode_args.cap = sizeof(dg) - hn - 1;
    QuicVarintV.encode_args.value = 1400;
    QuicVarint.encode(quic_varint_work);
    size_t c = QuicVarintV.n;
    memset(dg + hn + 1 + c, 0, 1450 - (hn + 1 + c));
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = 1450;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);
}

void test_quic_recv_handshake_done_frame()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t hd[32];
    QuicFrameV.build_handshake_done_args.out = hd;
    QuicFrameV.build_handshake_done_args.cap = sizeof hd;
    QuicFrame.build_handshake_done(quic_frame_work);
    size_t hdl = QuicFrameV.n;
    memset(hd + hdl, 0, 20);
    hdl += 20;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, hd, hdl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);
}

void test_quic_conn_stream_frames()
{
    fill();
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t dg[1500];

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        uint8_t data[4] = {1, 2, 3, 4};
        uint8_t fr[32];
        QuicFrameV.build_stream_args.out = fr;
        QuicFrameV.build_stream_args.cap = sizeof fr;
        QuicFrameV.build_stream_args.id = 0;
        QuicFrameV.build_stream_args.offset = 100;
        QuicFrameV.build_stream_args.data = data;
        QuicFrameV.build_stream_args.len = 4;
        QuicFrameV.build_stream_args.fin = PROTO_FALSE;
        QuicFrame.build_stream(quic_frame_work);
        size_t fl = QuicFrameV.n;
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
        g_stream_len = 0;
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.recv_args.datagram = dg;
        QuicConnV.recv_args.len = dl;
        QuicConn.recv(g_qc_ctx);
        TEST_ASSERT_EQUAL_UINT(0, g_stream_len);
    }

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        uint8_t d0 = 0;
        uint8_t fr[16];
        QuicFrameV.build_stream_args.out = fr;
        QuicFrameV.build_stream_args.cap = sizeof fr;
        QuicFrameV.build_stream_args.id = 0;
        QuicFrameV.build_stream_args.offset = 0;
        QuicFrameV.build_stream_args.data = &d0;
        QuicFrameV.build_stream_args.len = 0;
        QuicFrameV.build_stream_args.fin = PROTO_TRUE;
        QuicFrame.build_stream(quic_frame_work);
        size_t fl = QuicFrameV.n;
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
        g_stream_fin = PROTO_FALSE;
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.recv_args.datagram = dg;
        QuicConnV.recv_args.len = dl;
        QuicConn.recv(g_qc_ctx);
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
            QuicFrameV.build_stream_args.out = fr + fl;
            QuicFrameV.build_stream_args.cap = sizeof(fr) - fl;
            QuicFrameV.build_stream_args.id = (uint64_t)(i * 4);
            QuicFrameV.build_stream_args.offset = 0;
            QuicFrameV.build_stream_args.data = &d1;
            QuicFrameV.build_stream_args.len = 1;
            QuicFrameV.build_stream_args.fin = PROTO_FALSE;
            QuicFrame.build_stream(quic_frame_work);
            fl += QuicFrameV.n;
        }
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.recv_args.datagram = dg;
        QuicConnV.recv_args.len = dl;
        QuicConn.recv(g_qc_ctx);
        TEST_ASSERT_TRUE(QuicConnV.ok);
    }
}

void test_quic_conn_crypto_window_clamp()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t dg[1500];
    uint8_t chunk[1200];
    chunk[0] = 0x01;
    chunk[1] = 0x00;
    chunk[2] = 0xFF;
    chunk[3] = 0xFF;
    memset(chunk + 4, 0, sizeof(chunk) - 4);
    uint8_t fr[1300];
    QuicFrameV.build_crypto_args.out = fr;
    QuicFrameV.build_crypto_args.cap = sizeof fr;
    QuicFrameV.build_crypto_args.offset = 0;
    QuicFrameV.build_crypto_args.data = chunk;
    QuicFrameV.build_crypto_args.len = sizeof chunk;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrameV.n;
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);
    QuicFrameV.build_crypto_args.out = fr;
    QuicFrameV.build_crypto_args.cap = sizeof fr;
    QuicFrameV.build_crypto_args.offset = 1200;
    QuicFrameV.build_crypto_args.data = chunk;
    QuicFrameV.build_crypto_args.len = sizeof chunk;
    QuicFrame.build_crypto(quic_frame_work);
    fl = QuicFrameV.n;
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 1, &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);
}

void test_quic_conn_crypto_error_close()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t bad_ch[6] = {0x01, 0x00, 0x00, 0x02, 0x03, 0x03};
    uint8_t fr[32];
    QuicFrameV.build_crypto_args.out = fr;
    QuicFrameV.build_crypto_args.cap = sizeof fr;
    QuicFrameV.build_crypto_args.offset = 0;
    QuicFrameV.build_crypto_args.data = bad_ch;
    QuicFrameV.build_crypto_args.len = sizeof bad_ch;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrameV.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.close_args.error_code = 0;
    QuicConn.close(g_qc_ctx);
    uint8_t out[256];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);
}

void test_quic_conn_no_keys_build()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[32] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, sizeof fr);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    uint8_t out[256];

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    (void)QuicConnV.n;
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);
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
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 0;
    QuicConn.on_timeout(g_qc_ctx);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 1;
    QuicConn.on_timeout(g_qc_ctx);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);
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
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.send_args.out = out;
        QuicConnV.send_args.cap = cap;
        QuicConnV.send(g_qc_ctx);
        (void)QuicConnV.n;
    }
}

static void complete_handshake(QuicConnCtx *qc, QuicConnCallbacks *cb, QuicInitialSecrets *init,
                               QuicPacketKeys *ap_client, QuicPacketKeys *ap_server, uint8_t peer_scid_len)
{
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConnV.bind.b = qc_span(qc);
    QuicConnV.init_args.cfg = &cfg;
    QuicConnV.init_args.odcid = ODCID;
    QuicConnV.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConnV.init_args.peer_scid = CLIENT_SCID;
    QuicConnV.init_args.peer_scid_len = (uint8_t)(peer_scid_len);
    QuicConnV.init_args.our_scid = SERVER_SCID;
    QuicConnV.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    if (cb)
    {
        QuicConnV.cb = *cb;
    }
    else
    {
        QuicConnV.cb.on_stream_data = NULL;
        QuicConnV.cb.on_handshake_done = NULL;
        QuicConnV.cb.app = NULL;
    }
    QuicConn.init(QUIC_SPAN(qc));
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    QuicTransportParams ctp;
    QuicTpV.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctp_enc[128];
    QuicTpV.encode_args.tp = &ctp;
    QuicTpV.encode_args.out = ctp_enc;
    QuicTpV.encode_args.cap = sizeof(ctp_enc);
    QuicTp.encode(quic_tp_work);
    size_t ctp_len = QuicTpV.n;
    uint8_t client_pub[32];
    Curve25519V.x25519_base_args.out = client_pub;
    Curve25519V.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t ch[512];
    size_t ch_len = build_client_hello(ch, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    QuicFrameV.build_crypto_args.out = frames;
    QuicFrameV.build_crypto_args.cap = sizeof(frames);
    QuicFrameV.build_crypto_args.offset = 0;
    QuicFrameV.build_crypto_args.data = ch;
    QuicFrameV.build_crypto_args.len = ch_len;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrameV.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init->client, frames, fl);
    QuicConnV.bind.b = qc_span(qc);
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(QUIC_SPAN(qc));

    uint8_t sdg[1500];
    QuicConnV.bind.b = qc_span(qc);
    QuicConnV.send_args.out = sdg;
    QuicConnV.send_args.cap = sizeof(sdg);
    QuicConnV.send(QUIC_SPAN(qc));
    size_t sl = QuicConnV.n;
    uint8_t plain[2048], sh[512], hsflight[1024];
    size_t wire = 0;
    uint8_t type = 0;
    size_t pt = open_long(sdg, sl, &init->server, plain, &wire, &type);
    size_t sh_len = extract_crypto(plain, pt, sh);

    uint8_t server_pub[32], ecdhe[32];
    Curve25519V.x25519_base_args.out = server_pub;
    Curve25519V.x25519_base_args.scalar = SERVER_PRIV;
    Curve25519.x25519_base(tw);
    Curve25519V.x25519_args.out = ecdhe;
    Curve25519V.x25519_args.scalar = CLIENT_PRIV;
    Curve25519V.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    uint8_t *t;
    uint8_t ch_sh[32], ch_sf[32];
    t = tw_t;
    Sha256.init(t);
    Sha256V.update_args.data = ch;
    Sha256V.update_args.len = ch_len;
    Sha256.update(t);
    Sha256V.update_args.data = sh;
    Sha256V.update_args.len = sh_len;
    Sha256.update(t);
    {
        Sha256V.final_args.out = ch_sh;
        Sha256.final(t);
    }
    Tls13KeySchedule cks;
    static uint8_t ks_store_1181[PROTOCORE_TLS13_KS_BORROW];
    Tls13KsV.bind.kdf = &TLS13_KDF;
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.bind.s = ks_store_1181;
    Tls13KsV.early(NULL);
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.step.ecdhe = ecdhe;
    Tls13KsV.step.ecdhe_len = sizeof(ecdhe);
    Tls13KsV.step.ch_sh_hash = ch_sh;
    Tls13Ks.handshake(NULL);
    QuicPacketKeys hs_server_keys, hs_client_keys;
    QuicCryptoV.keys_from_secret_args.keys_work = tw;
    QuicCryptoV.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_HS;
    QuicCryptoV.keys_from_secret_args.out = &hs_server_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCryptoV.keys_from_secret_args.keys_work = tw;
    QuicCryptoV.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_HS;
    QuicCryptoV.keys_from_secret_args.out = &hs_client_keys;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    size_t hswire = 0;
    uint8_t hstype = 0;
    size_t hpt = open_long(sdg + wire, sl - wire, &hs_server_keys, plain, &hswire, &hstype);
    size_t hsflen = extract_crypto(plain, hpt, hsflight);
    Sha256V.update_args.data = hsflight;
    Sha256V.update_args.len = hsflen;
    Sha256.update(t);
    Sha256V.final_args.out = ch_sf;
    Sha256.final(t);
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.step.ch_sfin_hash = ch_sf;
    Tls13Ks.master(NULL);
    QuicCryptoV.keys_from_secret_args.keys_work = tw;
    QuicCryptoV.keys_from_secret_args.secret = cks.s + TLS13_KS_CLIENT_AP;
    QuicCryptoV.keys_from_secret_args.out = ap_client;
    QuicCrypto.keys_from_secret(quic_crypto_work);
    QuicCryptoV.keys_from_secret_args.keys_work = tw;
    QuicCryptoV.keys_from_secret_args.secret = cks.s + TLS13_KS_SERVER_AP;
    QuicCryptoV.keys_from_secret_args.out = ap_server;
    QuicCrypto.keys_from_secret(quic_crypto_work);

    uint8_t ifr[64];
    QuicFrameV.build_ack_args.out = ifr;
    QuicFrameV.build_ack_args.cap = sizeof(ifr);
    QuicFrameV.build_ack_args.largest = 0;
    QuicFrameV.build_ack_args.delay = 0;
    QuicFrameV.build_ack_args.first_range = 0;
    QuicFrame.build_ack(quic_frame_work);
    size_t ifl = QuicFrameV.n;
    uint8_t idg[256];
    size_t idl = build_long(idg, sizeof(idg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID),
                            1, &init->client, ifr, ifl);
    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    Tls13KsV.bind.ks = &cks;
    Tls13KsV.finished_args.base_secret = cks.s + TLS13_KS_CLIENT_HS;
    Tls13KsV.finished_args.transcript_hash = ch_sf;
    Tls13KsV.finished_args.out = cfin + 4;
    Tls13Ks.finished_mac(NULL);
    uint8_t hfr[64];
    QuicFrameV.build_ack_args.out = hfr;
    QuicFrameV.build_ack_args.cap = sizeof(hfr);
    QuicFrameV.build_ack_args.largest = 0;
    QuicFrameV.build_ack_args.delay = 0;
    QuicFrameV.build_ack_args.first_range = 0;
    QuicFrame.build_ack(quic_frame_work);
    size_t hfl = QuicFrameV.n;
    QuicFrameV.build_crypto_args.out = hfr + hfl;
    QuicFrameV.build_crypto_args.cap = sizeof(hfr) - hfl;
    QuicFrameV.build_crypto_args.offset = 0;
    QuicFrameV.build_crypto_args.data = cfin;
    QuicFrameV.build_crypto_args.len = sizeof(cfin);
    QuicFrame.build_crypto(quic_frame_work);
    hfl += QuicFrameV.n;
    size_t hdl = build_long(idg + idl, sizeof(idg) - idl, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID,
                            sizeof(CLIENT_SCID), 0, &hs_client_keys, hfr, hfl);
    QuicConnV.bind.b = qc_span(qc);
    QuicConnV.recv_args.datagram = idg;
    QuicConnV.recv_args.len = idl + hdl;
    QuicConn.recv(QUIC_SPAN(qc));
    QuicConnV.bind.b = qc_span(qc);
    QuicConnV.send_args.out = sdg;
    QuicConnV.send_args.cap = sizeof(sdg);
    QuicConnV.send(QUIC_SPAN(qc));
}

void test_quic_conn_stream_nothing_to_send()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));
    uint8_t out[512];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.stream_send_args.stream_id = 0;
    QuicConnV.stream_send_args.data = (const uint8_t *)"OK";
    QuicConnV.stream_send_args.len = 2;
    QuicConnV.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(2, QuicConnV.n);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);
}

void test_quic_conn_short_header_tiny_cap()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.stream_send_args.stream_id = 0;
    QuicConnV.stream_send_args.data = (const uint8_t *)"DATA";
    QuicConnV.stream_send_args.len = 4;
    QuicConnV.stream_send_args.fin = PROTO_FALSE;
    QuicConn.stream_send(g_qc_ctx);
    uint8_t out[8];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = 4;
    QuicConnV.send(g_qc_ctx);
    (void)QuicConnV.n;
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);
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
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    uint8_t out[256];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.closed);
}

void test_quic_conn_null_callbacks()
{
    fill();
    QuicTlsConfig cfg;
    make_cfg(&cfg);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.init_args.cfg = &cfg;
    QuicConnV.init_args.odcid = ODCID;
    QuicConnV.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConnV.init_args.peer_scid = CLIENT_SCID;
    QuicConnV.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConnV.init_args.our_scid = SERVER_SCID;
    QuicConnV.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    QuicConnV.cb.on_stream_data = NULL; // this connection is opened with no hooks at all
    QuicConnV.cb.on_handshake_done = NULL;
    QuicConnV.cb.app = NULL;
    QuicConn.init(g_qc_ctx);
    TEST_ASSERT_NULL(g_qc.cb.on_stream_data);
    TEST_ASSERT_NULL(g_qc.cb.on_handshake_done);

    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    uint8_t d[3] = {1, 2, 3};
    uint8_t fr[64];
    QuicFrameV.build_stream_args.out = fr;
    QuicFrameV.build_stream_args.cap = sizeof fr;
    QuicFrameV.build_stream_args.id = 0;
    QuicFrameV.build_stream_args.offset = 0;
    QuicFrameV.build_stream_args.data = d;
    QuicFrameV.build_stream_args.len = 3;
    QuicFrameV.build_stream_args.fin = PROTO_FALSE;
    QuicFrame.build_stream(quic_frame_work);
    size_t fl = QuicFrameV.n;
    QuicFrameV.build_stream_args.out = fr + fl;
    QuicFrameV.build_stream_args.cap = sizeof(fr) - fl;
    QuicFrameV.build_stream_args.id = 0;
    QuicFrameV.build_stream_args.offset = 3;
    QuicFrameV.build_stream_args.data = d;
    QuicFrameV.build_stream_args.len = 0;
    QuicFrameV.build_stream_args.fin = PROTO_TRUE;
    QuicFrame.build_stream(quic_frame_work);
    fl += QuicFrameV.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    TEST_ASSERT_EQUAL_UINT(0, g_stream_len);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);

    QuicPacketKeys apc, aps;
    QuicInitialSecrets init2;
    complete_handshake(&g_qc2, NULL, &init2, &apc, &aps, sizeof(CLIENT_SCID));
    QuicConn.is_established(g_qc2_ctx);
    TEST_ASSERT_TRUE(QuicConnV.established);
    TEST_ASSERT_FALSE(g_hs_done);
}

void test_quic_conn_stream_duplicate_and_stale_fin()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t d[4] = {0xA1, 0xA2, 0xA3, 0xA4};
    uint8_t fr[64], dg[256];
    uint64_t pn = 0;

    QuicFrameV.build_stream_args.out = fr;
    QuicFrameV.build_stream_args.cap = sizeof fr;
    QuicFrameV.build_stream_args.id = 0;
    QuicFrameV.build_stream_args.offset = 0;
    QuicFrameV.build_stream_args.data = d;
    QuicFrameV.build_stream_args.len = 4;
    QuicFrameV.build_stream_args.fin = PROTO_FALSE;
    QuicFrame.build_stream(quic_frame_work);
    size_t fl = QuicFrameV.n;
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    TEST_ASSERT_EQUAL_UINT(4, g_stream_len);

    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    TEST_ASSERT_EQUAL_UINT(4, g_stream_len);

    QuicFrameV.build_stream_args.out = fr;
    QuicFrameV.build_stream_args.cap = sizeof fr;
    QuicFrameV.build_stream_args.id = 0;
    QuicFrameV.build_stream_args.offset = 0;
    QuicFrameV.build_stream_args.data = d;
    QuicFrameV.build_stream_args.len = 0;
    QuicFrameV.build_stream_args.fin = PROTO_TRUE;
    QuicFrame.build_stream(quic_frame_work);
    fl = QuicFrameV.n;
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    g_stream_fin = PROTO_FALSE;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    TEST_ASSERT_FALSE(g_stream_fin);

    QuicFrameV.build_stream_args.out = fr;
    QuicFrameV.build_stream_args.cap = sizeof fr;
    QuicFrameV.build_stream_args.id = 0;
    QuicFrameV.build_stream_args.offset = 4;
    QuicFrameV.build_stream_args.data = d;
    QuicFrameV.build_stream_args.len = 0;
    QuicFrameV.build_stream_args.fin = PROTO_TRUE;
    QuicFrame.build_stream(quic_frame_work);
    fl = QuicFrameV.n;
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    TEST_ASSERT_TRUE(g_stream_fin);

    g_stream_fin = PROTO_FALSE;
    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, pn++, &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    TEST_ASSERT_FALSE(g_stream_fin);
}

void test_quic_conn_frame_dispatch_variants()
{
    fill();
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t dg[512];

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);

        const uint8_t ack_ecn[8] = {0x03, 5, 0, 0, 0, 0, 0, 0};
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, ack_ecn,
                               sizeof ack_ecn);
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.recv_args.datagram = dg;
        QuicConnV.recv_args.len = dl;
        QuicConn.recv(g_qc_ctx);
        TEST_ASSERT_TRUE(QuicConnV.ok);
        TEST_ASSERT_EQUAL_INT64(5, g_qc.space[QUIC_ENC_INITIAL].largest_acked);
        TEST_ASSERT_FALSE(g_qc.space[QUIC_ENC_INITIAL].ack_eliciting_rx);

        uint8_t older[8];
        QuicFrameV.build_ack_args.out = older;
        QuicFrameV.build_ack_args.cap = sizeof older;
        QuicFrameV.build_ack_args.largest = 3;
        QuicFrameV.build_ack_args.delay = 0;
        QuicFrameV.build_ack_args.first_range = 0;
        QuicFrame.build_ack(quic_frame_work);
        size_t ol = QuicFrameV.n;
        dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 1, &init.client, older, ol);
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.recv_args.datagram = dg;
        QuicConnV.recv_args.len = dl;
        QuicConn.recv(g_qc_ctx);
        TEST_ASSERT_TRUE(QuicConnV.ok);
        TEST_ASSERT_EQUAL_INT64(5, g_qc.space[QUIC_ENC_INITIAL].largest_acked);
    }

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        const uint8_t app_close[3] = {0x1d, 0x05, 0x00};
        size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, app_close,
                               sizeof app_close);
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.recv_args.datagram = dg;
        QuicConnV.recv_args.len = dl;
        QuicConn.recv(g_qc_ctx);
        TEST_ASSERT_TRUE(QuicConnV.ok);
        QuicConn.is_closed(g_qc_ctx);
        TEST_ASSERT_TRUE(QuicConnV.closed);
        TEST_ASSERT_FALSE(g_qc.space[QUIC_ENC_INITIAL].ack_eliciting_rx);
    }

    {
        QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
        init_conn(&g_qc, &cb);
        const uint8_t misc[6] = {QUIC_FT_RESET_STREAM, 0x00, 0x01, 0x02, QUIC_FT_MAX_STREAMS_BIDI, 0x08};
        size_t dl =
            build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 0, &init.client, misc, sizeof misc);
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.recv_args.datagram = dg;
        QuicConnV.recv_args.len = dl;
        QuicConn.recv(g_qc_ctx);
        TEST_ASSERT_TRUE(QuicConnV.ok);
        QuicConn.is_closed(g_qc_ctx);
        TEST_ASSERT_FALSE(QuicConnV.closed);
        TEST_ASSERT_EQUAL_UINT(0, g_stream_len);
    }
}

void test_quic_recv_zero_version()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[8] = {QUIC_FT_PING};
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, 1);
    dg[1] = dg[2] = dg[3] = dg[4] = 0x00;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);
}

void test_quic_recv_older_packet_number()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    uint8_t fr[24] = {QUIC_FT_PING};
    uint8_t dg[256];

    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 5, &init.client, fr, sizeof fr);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    TEST_ASSERT_EQUAL_UINT64(5, g_qc.space[QUIC_ENC_INITIAL].largest_rx);

    dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, 8, CLIENT_SCID, 4, 1, &init.client, fr, sizeof fr);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
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
    QuicFrameV.build_ping_args.out = fr;
    QuicFrameV.build_ping_args.cap = sizeof fr;
    QuicFrame.build_ping(quic_frame_work);
    size_t fl = QuicFrameV.n;
    memset(fr + fl, 0, 20);
    fl += 20;
    uint8_t dg[256];
    size_t dl = build_short(dg, sizeof dg, SERVER_SCID, sizeof(SERVER_SCID), 0, &apc, fr, fl);
    dg[dl - 1] ^= 0xFF;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.ok);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);
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
    QuicFrameV.build_crypto_args.out = fr;
    QuicFrameV.build_crypto_args.cap = sizeof fr;
    QuicFrameV.build_crypto_args.offset = off;
    QuicFrameV.build_crypto_args.data = frag;
    QuicFrameV.build_crypto_args.len = sizeof frag;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrameV.n;
    memset(fr + fl, 0, 20);
    fl += 20;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 7,
                           &hs_client_keys, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    TEST_ASSERT_EQUAL_UINT8(QTLS_DONE, g_qc.tls.state);
    TEST_ASSERT_FALSE(g_qc.handshake_done_queued);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);

    g_qc.handshake_done_sent = PROTO_FALSE;
    g_qc.handshake_done_queued = PROTO_TRUE;
    g_hs_done = PROTO_FALSE;
    off = g_qc.space[QUIC_ENC_HANDSHAKE].crypto_rx_off;
    TEST_ASSERT_TRUE(off > 0);
    QuicFrameV.build_crypto_args.out = fr;
    QuicFrameV.build_crypto_args.cap = sizeof fr;
    QuicFrameV.build_crypto_args.offset = off;
    QuicFrameV.build_crypto_args.data = frag;
    QuicFrameV.build_crypto_args.len = sizeof frag;
    QuicFrame.build_crypto(quic_frame_work);
    fl = QuicFrameV.n;
    memset(fr + fl, 0, 20);
    fl += 20;
    dl = build_long(dg, sizeof dg, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 8,
                    &hs_client_keys, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
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
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    uint8_t fr[32];
    QuicFrameV.build_connection_close_args.out = fr;
    QuicFrameV.build_connection_close_args.cap = sizeof fr;
    QuicFrameV.build_connection_close_args.app = PROTO_FALSE;
    QuicFrameV.build_connection_close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicFrameV.build_connection_close_args.frame_type = 0;
    QuicFrameV.build_connection_close_args.reason = NULL;
    QuicFrameV.build_connection_close_args.reason_len = 0;
    QuicFrame.build_connection_close(quic_frame_work);
    size_t fl = QuicFrameV.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(g_qc.closed);
    TEST_ASSERT_TRUE(g_qc.draining);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.close_args.error_code = QUIC_ERR_FRAME_ENCODING;
    QuicConn.close(g_qc_ctx);
    TEST_ASSERT_FALSE(g_qc.close_queued);
    uint8_t out[512];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
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
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    for (int i = 0; i < 8 && QuicConnV.n > 0; i++)
    {
    }
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicConn.close(g_qc_ctx);
    TEST_ASSERT_TRUE(g_qc.close_queued);

    uint8_t fr[32];
    QuicFrameV.build_connection_close_args.out = fr;
    QuicFrameV.build_connection_close_args.cap = sizeof fr;
    QuicFrameV.build_connection_close_args.app = PROTO_FALSE;
    QuicFrameV.build_connection_close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicFrameV.build_connection_close_args.frame_type = 0;
    QuicFrameV.build_connection_close_args.reason = NULL;
    QuicFrameV.build_connection_close_args.reason_len = 0;
    QuicFrame.build_connection_close(quic_frame_work);
    size_t fl = QuicFrameV.n;
    uint8_t dg[256];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 1,
                           &init.client, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(g_qc.draining);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);
    TEST_ASSERT_TRUE(g_qc.close_sent);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
}

void test_quic_conn_close_send_no_room()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    uint8_t ch[512];
    size_t ch_len = 0;
    feed_client_initial(&g_qc, &cb, &init, ch, &ch_len);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicConn.close(g_qc_ctx);
    uint8_t tiny[8];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = tiny;
    QuicConnV.send_args.cap = sizeof tiny;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
    TEST_ASSERT_FALSE(g_qc.close_sent);
    TEST_ASSERT_FALSE(g_qc.closed);

    uint8_t out[512];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);
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

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicConn.close(g_qc_ctx);
    g_qc.close_level = 200;
    uint8_t out[512];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.closed);
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

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicConn.close(g_qc_ctx);
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
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.init_args.cfg = &cfg;
    QuicConnV.init_args.odcid = ODCID;
    QuicConnV.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConnV.init_args.peer_scid = CLIENT_SCID;
    QuicConnV.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConnV.init_args.our_scid = SERVER_SCID;
    QuicConnV.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    QuicConnV.cb = cb;
    QuicConn.init(g_qc_ctx);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    QuicTransportParams ctp;
    QuicTpV.defaults_args.tp = &ctp;
    QuicTp.defaults(quic_tp_work);
    uint8_t ctp_enc[128];
    QuicTpV.encode_args.tp = &ctp;
    QuicTpV.encode_args.out = ctp_enc;
    QuicTpV.encode_args.cap = sizeof(ctp_enc);
    QuicTp.encode(quic_tp_work);
    size_t ctp_len = QuicTpV.n;
    uint8_t client_pub[32];
    Curve25519V.x25519_base_args.out = client_pub;
    Curve25519V.x25519_base_args.scalar = CLIENT_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t chb[512];
    size_t ch_len = build_client_hello(chb, client_pub, ctp_enc, ctp_len);
    uint8_t frames[1200];
    QuicFrameV.build_crypto_args.out = frames;
    QuicFrameV.build_crypto_args.cap = sizeof(frames);
    QuicFrameV.build_crypto_args.offset = 0;
    QuicFrameV.build_crypto_args.data = chb;
    QuicFrameV.build_crypto_args.len = ch_len;
    QuicFrame.build_crypto(quic_frame_work);
    size_t fl = QuicFrameV.n;
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, frames, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    TEST_ASSERT_EQUAL_UINT8(QTLS_WAIT_FINISHED, g_qc.tls.state);

    size_t hs_flight_len = 0;
    QuicTlsServerV.flight_args.qt = &g_qc.tls;
    QuicTlsServerV.flight_args.level = QUIC_ENC_HANDSHAKE;
    QuicTlsServerV.flight_args.len = &hs_flight_len;
    QuicTlsServer.flight(quic_tls_work);
    (void)QuicTlsServerV.bytes;
    TEST_ASSERT_TRUE(hs_flight_len > PROTOCORE_QUIC_MAX_DATAGRAM);

    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    size_t first = QuicConnV.n;
    TEST_ASSERT_TRUE(first > 0);
    uint64_t sent_after_first = g_qc.space[QUIC_ENC_HANDSHAKE].crypto_tx_off;
    TEST_ASSERT_TRUE(sent_after_first > 0);
    TEST_ASSERT_TRUE(sent_after_first < hs_flight_len);

    g_qc.address_validated = PROTO_TRUE;
    size_t total = sent_after_first;
    for (int i = 0; i < 8 && total < hs_flight_len; i++)
    {
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.send_args.out = out;
        QuicConnV.send_args.cap = sizeof out;
        QuicConnV.send(g_qc_ctx);
        TEST_ASSERT_TRUE(QuicConnV.n > 0);
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
    QuicFrameV.build_stream_args.out = fr;
    QuicFrameV.build_stream_args.cap = sizeof fr;
    QuicFrameV.build_stream_args.id = 0;
    QuicFrameV.build_stream_args.offset = 0;
    QuicFrameV.build_stream_args.data = d;
    QuicFrameV.build_stream_args.len = 2;
    QuicFrameV.build_stream_args.fin = PROTO_FALSE;
    QuicFrame.build_stream(quic_frame_work);
    size_t fl = QuicFrameV.n;
    uint8_t sdg[256];
    size_t sl = build_short(sdg, sizeof sdg, SERVER_SCID, sizeof(SERVER_SCID), 1, &apc, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = sdg;
    QuicConnV.recv_args.len = sl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    (void)QuicConnV.n;

    static uint8_t big[PROTOCORE_QUIC_STREAM_TX - 64];
    memset(big, 0x5A, sizeof(big));
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.stream_send_args.stream_id = 0;
    QuicConnV.stream_send_args.data = big;
    QuicConnV.stream_send_args.len = sizeof(big);
    QuicConnV.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(g_qc_ctx);
    size_t queued = QuicConnV.n;
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

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    (void)QuicConnV.n;
    TEST_ASSERT_TRUE(st->tx_sent > 0);
    TEST_ASSERT_TRUE(st->tx_sent < st->tx_have);
    TEST_ASSERT_EQUAL_UINT64(st->tx_sent, st->tx_off);
    TEST_ASSERT_FALSE(st->tx_fin_sent);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);
    TEST_ASSERT_EQUAL_UINT(st->tx_have, st->tx_sent);
    TEST_ASSERT_TRUE(st->tx_fin_sent);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.stream_send_args.stream_id = 0;
    QuicConnV.stream_send_args.data = d;
    QuicConnV.stream_send_args.len = 2;
    QuicConnV.stream_send_args.fin = PROTO_FALSE;
    QuicConn.stream_send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(2, QuicConnV.n);
    size_t before = st->tx_sent;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);
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
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.stream_send_args.stream_id = 0;
    QuicConnV.stream_send_args.data = (const uint8_t *)"BODY";
    QuicConnV.stream_send_args.len = 4;
    QuicConnV.stream_send_args.fin = PROTO_FALSE;
    QuicConn.stream_send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(4, QuicConnV.n);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);
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

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.stream_send_args.stream_id = 0;
    QuicConnV.stream_send_args.data = (const uint8_t *)"";
    QuicConnV.stream_send_args.len = 0;
    QuicConnV.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
    TEST_ASSERT_TRUE(st->tx_fin);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    size_t n = QuicConnV.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(st->tx_fin_sent);

    uint8_t plain[PROTOCORE_QUIC_MAX_DATAGRAM];
    size_t pt = open_short(out, n, sizeof(CLIENT_SCID), &aps, plain);
    TEST_ASSERT_NOT_EQUAL(SIZE_MAX, pt);
    QuicFrameHeader f;
    QuicFrameV.parse_args.buf = plain;
    QuicFrameV.parse_args.len = pt;
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    size_t got = QuicFrameV.n;
    TEST_ASSERT_TRUE(got > 0);
    TEST_ASSERT_TRUE(f.type >= QUIC_FT_STREAM);
    TEST_ASSERT_EQUAL_UINT64(0, f.stream.length);
    TEST_ASSERT_TRUE(f.stream.fin);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
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
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.stream_send_args.stream_id = ids[i];
        QuicConnV.stream_send_args.data = payload;
        QuicConnV.stream_send_args.len = sizeof(payload);
        QuicConnV.stream_send_args.fin = PROTO_FALSE;
        QuicConn.stream_send(g_qc_ctx);
        TEST_ASSERT_EQUAL_UINT(sizeof(payload), QuicConnV.n);
    }

    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    (void)QuicConnV.n;

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
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.send_args.out = out;
        QuicConnV.send_args.cap = sizeof out;
        QuicConnV.send(g_qc_ctx);
        (void)QuicConnV.n;
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
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
}

void test_quic_conn_stream_send_clamped()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);

    static uint8_t huge[PROTOCORE_QUIC_STREAM_TX + 512];
    memset(huge, 0x2B, sizeof(huge));
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.stream_send_args.stream_id = 0;
    QuicConnV.stream_send_args.data = huge;
    QuicConnV.stream_send_args.len = sizeof(huge);
    QuicConnV.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(g_qc_ctx);
    size_t took = QuicConnV.n;
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

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.stream_send_args.stream_id = UINT64_MAX;
    QuicConnV.stream_send_args.data = (const uint8_t *)"hi";
    QuicConnV.stream_send_args.len = 2;
    QuicConnV.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(2, QuicConnV.n);
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, g_qc.streams[i].id);
    }

    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
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
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.n > 0);

    uint32_t now = 1000;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = now;
    QuicConn.on_timeout(g_qc_ctx);
    TEST_ASSERT_TRUE(g_qc.pto_armed);

    for (int i = 0; i < 12; i++)
    {
        now = g_qc.pto_deadline_ms + 1;
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.timeout_args.now_ms = now;
        QuicConn.on_timeout(g_qc_ctx);
    }
    TEST_ASSERT_EQUAL_UINT8(8, g_qc.pto_count);

    g_qc.pto_count = 40;
    g_qc.pto_armed = PROTO_FALSE;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 0;
    QuicConn.on_timeout(g_qc_ctx);
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
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);

    g_qc.space[QUIC_ENC_APP].ack_eliciting_rx = PROTO_TRUE;
    g_qc.space[QUIC_ENC_APP].have_rx = PROTO_FALSE;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    TEST_ASSERT_EQUAL_UINT(0, QuicConnV.n);
    TEST_ASSERT_TRUE(g_qc.space[QUIC_ENC_APP].ack_eliciting_rx);
}

void test_quic_conn_close_level_without_keys()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    init_conn(&g_qc, &cb);
    QuicInitialSecrets init;
    QuicCryptoV.derive_initial_secrets_args.keys_work = tw;
    QuicCryptoV.derive_initial_secrets_args.dcid = ODCID;
    QuicCryptoV.derive_initial_secrets_args.dcid_len = sizeof(ODCID);
    QuicCryptoV.derive_initial_secrets_args.out = &init;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    uint8_t fr[64] = {QUIC_FT_PING};
    uint8_t dg[512];
    size_t dl = build_long(dg, sizeof dg, QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID, sizeof(CLIENT_SCID), 0,
                           &init.client, fr, sizeof fr);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    TEST_ASSERT_EQUAL_UINT8(QTLS_START, g_qc.tls.state);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.close_args.error_code = QUIC_ERR_NO_ERROR;
    QuicConn.close(g_qc_ctx);
    TEST_ASSERT_TRUE(g_qc.close_queued);
    g_qc.close_level = QUIC_ENC_APP;
    TEST_ASSERT_FALSE(g_qc.space[QUIC_ENC_APP].discarded);

    uint8_t out[512];
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.send_args.out = out;
    QuicConnV.send_args.cap = sizeof out;
    QuicConnV.send(g_qc_ctx);
    size_t n = QuicConnV.n;
    TEST_ASSERT_TRUE(n > 0);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.closed);

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
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);

    g_qc.draining = PROTO_TRUE;
    TEST_ASSERT_FALSE(g_qc.closed);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.closed);
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
        QuicConnV.bind.b = g_qc_b;
        QuicConnV.timeout_args.now_ms = 1000;
        QuicConn.on_timeout(g_qc_ctx);
        TEST_ASSERT_TRUE(g_qc.pto_armed);
        TEST_ASSERT_EQUAL_UINT32(1000 + PROTOCORE_QUIC_PTO_MS, g_qc.pto_deadline_ms);
    }

    init_conn(&g_qc, &cb);
    g_qc.pto_armed = PROTO_TRUE;
    g_qc.pto_count = 3;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 1000;
    QuicConn.on_timeout(g_qc_ctx);
    TEST_ASSERT_FALSE(g_qc.pto_armed);
    TEST_ASSERT_EQUAL_UINT8(0, g_qc.pto_count);

    init_conn(&g_qc, &cb);
    g_qc.space[QUIC_ENC_APP].last_ae_pn = 3;
    g_qc.space[QUIC_ENC_APP].largest_acked = 2;
    g_qc.space[QUIC_ENC_APP].discarded = PROTO_TRUE;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 1000;
    QuicConn.on_timeout(g_qc_ctx);
    TEST_ASSERT_FALSE(g_qc.pto_armed);
}

void test_quic_conn_pto_disarms_when_all_acked()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 1000;
    QuicConn.on_timeout(g_qc_ctx);
    TEST_ASSERT_TRUE(g_qc.pto_armed);

    uint8_t fr[32];
    QuicFrameV.build_ack_args.out = fr;
    QuicFrameV.build_ack_args.cap = sizeof fr;
    QuicFrameV.build_ack_args.largest = 0;
    QuicFrameV.build_ack_args.delay = 0;
    QuicFrameV.build_ack_args.first_range = 0;
    QuicFrame.build_ack(quic_frame_work);
    size_t fl = QuicFrameV.n;
    uint8_t dg[256];
    size_t dl = build_short(dg, sizeof dg, SERVER_SCID, sizeof(SERVER_SCID), 0, &apc, fr, fl);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.recv_args.datagram = dg;
    QuicConnV.recv_args.len = dl;
    QuicConn.recv(g_qc_ctx);
    TEST_ASSERT_TRUE(QuicConnV.ok);
    TEST_ASSERT_EQUAL_INT64(0, g_qc.space[QUIC_ENC_APP].largest_acked);

    g_qc.pto_armed = PROTO_TRUE;
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 2000;
    QuicConn.on_timeout(g_qc_ctx);
    TEST_ASSERT_FALSE(g_qc.pto_armed);
    TEST_ASSERT_EQUAL_UINT8(0, g_qc.pto_count);
    QuicConn.is_closed(g_qc_ctx);
    TEST_ASSERT_FALSE(QuicConnV.closed);
}

void test_quic_conn_pto_requeues_handshake_done_once()
{
    fill();
    QuicConnCallbacks cb = {on_stream_data, on_hs_done, NULL};
    QuicInitialSecrets init;
    QuicPacketKeys apc, aps;
    complete_handshake(&g_qc, &cb, &init, &apc, &aps, sizeof(CLIENT_SCID));
    TEST_ASSERT_TRUE(g_qc.handshake_done_sent);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 1000;
    QuicConn.on_timeout(g_qc_ctx);
    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = 1000 + PROTOCORE_QUIC_PTO_MS + 1;
    QuicConn.on_timeout(g_qc_ctx);
    TEST_ASSERT_TRUE(g_qc.handshake_done_queued);
    TEST_ASSERT_FALSE(g_qc.handshake_done_sent);

    QuicConnV.bind.b = g_qc_b;
    QuicConnV.timeout_args.now_ms = g_qc.pto_deadline_ms + 1;
    QuicConn.on_timeout(g_qc_ctx);
    TEST_ASSERT_TRUE(g_qc.handshake_done_queued);
    TEST_ASSERT_FALSE(g_qc.handshake_done_sent);
}
