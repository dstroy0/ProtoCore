// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/asymmetric/curve25519.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/h3_conn.h"
#include "network_drivers/presentation/http/http3/h3_frame.h"
#include "network_drivers/presentation/http/http3/qpack.h"
#include "network_drivers/presentation/http/http3/quic_conn.h"
#include "network_drivers/presentation/http/http3/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_tls.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
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
}
void tearDown()
{
}

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

static char g_method[16], g_path[64];
static void app_request(void *app, uint8_t *h3, uint64_t sid, const char *method, const char *path, const char *,
                        const uint8_t *, size_t)
{
    (void)app;
    strncpy(g_method, method, sizeof(g_method) - 1);
    strncpy(g_path, path, sizeof(g_path) - 1);
    H3Conn.bind.b = h3; // the callback is handed the connection's span
    H3Conn.respond_args.stream_id = sid;
    H3Conn.respond_args.status = 200;
    H3Conn.respond_args.content_type = "text/plain";
    H3Conn.respond_args.body = (const uint8_t *)"hello h3";
    H3Conn.respond_args.body_len = 8;
    H3Conn.respond(H3Conn.internal);
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

    QuicTlsConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cert_der = CERT;
    cfg.cert_len = sizeof(CERT);
    memcpy(cfg.ed25519_seed, SERVER_SEED, 32);
    memcpy(cfg.ephemeral_priv, SERVER_PRIV, 32);
    memcpy(cfg.random, SERVER_RANDOM, 32);
    QuicTp.defaults_args.tp = &cfg.params;
    QuicTp.defaults(quic_tp_work);
    cfg.params.initial_max_data = 1048576;
    cfg.params.initial_max_sd_bidi_remote = 262144;
    cfg.params.initial_max_streams_bidi = 8;

    static uint8_t qc_ctx[PROTOCORE_QUIC_CONN_CTX_BORROW];
    static uint8_t qc_b[PROTOCORE_QUIC_CONN_BORROW];
    QuicConn.bind.ctx = qc_ctx;
    QuicConn.bind.b = qc_b;
    QuicConn.init_args.cfg = &cfg;
    QuicConn.init_args.odcid = ODCID;
    QuicConn.init_args.odcid_len = (uint8_t)(sizeof(ODCID));
    QuicConn.init_args.peer_scid = CLIENT_SCID;
    QuicConn.init_args.peer_scid_len = (uint8_t)(sizeof(CLIENT_SCID));
    QuicConn.init_args.our_scid = SERVER_SCID;
    QuicConn.init_args.our_scid_len = (uint8_t)(sizeof(SERVER_SCID));
    QuicConn.init(QuicConn.internal);
    static uint8_t h3_b[PROTOCORE_H3_CONN_BORROW];
    H3Conn.bind.b = h3_b;
    H3Conn.bind.qc = qc_ctx;
    H3Conn.app_args.on_request = app_request;
    H3Conn.app_args.app = NULL;
    H3Conn.init(H3Conn.internal);

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
    QuicConn.bind.ctx = qc_ctx;
    QuicConn.bind.b = qc_b;
    QuicConn.recv_args.datagram = dg;
    QuicConn.recv_args.len = dl;
    QuicConn.recv(QuicConn.internal);

    uint8_t sdg[1500], plain[2048], sh[512], hsf[1024];
    QuicConn.bind.ctx = qc_ctx;
    QuicConn.bind.b = qc_b;
    QuicConn.send_args.out = sdg;
    QuicConn.send_args.cap = sizeof(sdg);
    QuicConn.send(QuicConn.internal);
    size_t sl = QuicConn.n;
    size_t wire = 0;
    uint8_t ty = 0;
    size_t pt = open_long(sdg, sl, &init.server, plain, &wire, &ty);
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
    static uint8_t ks_store_290[PROTOCORE_TLS13_KS_BORROW];
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.bind.s = ks_store_290;
    Tls13Ks.early(NULL);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = 32;
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
    size_t hpt = open_long(sdg + wire, sl - wire, &hs_s, plain, &hw, &hty);
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
    QuicConn.bind.ctx = qc_ctx;
    QuicConn.bind.b = qc_b;
    QuicConn.recv_args.datagram = idg;
    QuicConn.recv_args.len = idl + hdl;
    QuicConn.recv(QuicConn.internal);
    QuicConn.bind.ctx = qc_ctx;
    QuicConn.is_established(QuicConn.internal);
    TEST_ASSERT_TRUE(QuicConn.established);

    QuicConn.bind.ctx = qc_ctx;
    QuicConn.bind.b = qc_b;
    // Drain: send is called until it reports nothing left, so the call is the condition.
    do
    {
        QuicConn.send_args.out = sdg;
        QuicConn.send_args.cap = sizeof(sdg);
        QuicConn.send(QuicConn.internal);
        sl = QuicConn.n;
    } while (sl > 0);

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
    QuicConn.bind.ctx = qc_ctx;
    QuicConn.bind.b = qc_b;
    QuicConn.recv_args.datagram = s1;
    QuicConn.recv_args.len = s1l;
    QuicConn.recv(QuicConn.internal);

    TEST_ASSERT_EQUAL_STRING("GET", g_method);
    TEST_ASSERT_EQUAL_STRING("/hello", g_path);

    proto_bool got = PROTO_FALSE;
    QuicConn.bind.ctx = qc_ctx;
    QuicConn.bind.b = qc_b;
    // Drain: send is called until it reports nothing left, so the call is the condition.
    for (;;)
    {
        QuicConn.send_args.out = sdg;
        QuicConn.send_args.cap = sizeof(sdg);
        QuicConn.send(QuicConn.internal);
        sl = QuicConn.n;
        if (sl == 0)
        {
            break;
        }
        size_t off = 0;
        while (off < sl)
        {
            QuicPacket.is_long_header_args.first = sdg[off];
            QuicPacket.is_long_header(quic_packet_work);
            if (QuicPacket.ok)
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
                if (f.type >= QUIC_FT_STREAM && f.type <= QUIC_FT_STREAM + 7 && f.stream.id == 0)
                {
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
