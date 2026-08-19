// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/aead/aes128gcm/aes128gcm.h" // Aes128Gcm: the per-epoch AEAD context this suite compares
#include "crypto/asymmetric/curve25519/curve25519.h"
#include "crypto/asymmetric/ed25519/ed25519.h"
#include "crypto/hash/sha256/sha256.h"
#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"
#include "network_drivers/presentation/security/dtls/dtls_conn/dtls_conn.h"
#include "network_drivers/presentation/security/dtls/dtls_handshake/dtls_handshake.h"
#include "network_drivers/presentation/security/dtls/dtls_record/dtls_record.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include "server/clock/clock.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

static uint8_t dtls_server_work[16]; // the borrow an entry takes; DtlsServer never reads it

static uint8_t dtls_handshake_work[16]; // the borrow an entry takes; DtlsHandshake never reads it

static uint8_t dtls_record_work[16]; // the borrow an entry takes; DtlsRecord never reads it

static uint8_t tw[4096];
static uint8_t tw_h1[4096];
static uint8_t tw_t[4096];
static uint8_t tw_tr[4096];

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
    Aes128Gcm.seal_args.nonce = n12;
    Aes128Gcm.seal_args.aad = NULL;
    Aes128Gcm.seal_args.aad_len = 0;
    Aes128Gcm.seal_args.pt = zpt;
    Aes128Gcm.seal_args.pt_len = sizeof zpt;
    Aes128Gcm.seal_args.ct_out = c2;
    Aes128Gcm.seal_args.tag_out = t2;
    Aes128Gcm.seal(b);
    TEST_ASSERT_EQUAL_MEMORY(c1, c2, sizeof c1);
    TEST_ASSERT_EQUAL_MEMORY(t1, t2, sizeof t1);
}

static uint32_t g_ms = 0;
static uint32_t test_clock()
{
    return g_ms;
}

// Move time the way a service pass does: the source advances, then ONE read stamps Clock.ms, which
// is what every reader in the library sees. Clock.set_ms installs the source and does NOT stamp, so
// advancing g_ms alone leaves the connection polling the same instant and no PTO ever elapses.
static void advance_ms(uint32_t by)
{
    g_ms += by;
    Clock.millis(Clock.internal);
}

void setUp()
{
    g_ms = 0;
    Clock.src.fn = test_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    Clock.millis(Clock.internal); // set_ms installs the source; this is what stamps Clock.ms from it
}
void tearDown()
{
}

static const uint8_t SERVER_ED_SEED[32] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                           17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
static const uint8_t SERVER_X25519_PRIV[32] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                                               0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                                               0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
static const uint8_t SERVER_RANDOM[32] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA,
                                          0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5,
                                          0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF};
static const uint8_t CLIENT_X25519_PRIV[32] = {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                                               0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                                               0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22};
static const uint8_t SERVER_COOKIE_KEY[32] = {0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
                                              0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71,
                                              0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b};
static const uint8_t TEST_PEER_ADDR[6] = {192, 168, 1, 50, 0xC3, 0x50};

typedef struct
{
    uint8_t *p;
    size_t n;
} Buf;
static void b8(Buf *b, uint8_t v)
{
    b->p[b->n++] = v;
}
static void b16(Buf *b, uint16_t v)
{
    b8(b, (uint8_t)(v >> 8));
    b8(b, (uint8_t)v);
}
static void bmem(Buf *b, const uint8_t *m, size_t k)
{
    memcpy(b->p + b->n, m, k);
    b->n += k;
}

static size_t build_client_hello_ex(uint8_t *out, const uint8_t client_pub[32], proto_bool with_keyshare,
                                    const uint8_t *cookie, size_t cookie_len, const uint8_t *cid, size_t cid_len,
                                    proto_bool offer_rpk, uint16_t group, uint16_t sigalg)
{
    Buf b = {out, 0};
    b8(&b, 0x01);
    size_t len_at = b.n;
    b8(&b, 0);
    b8(&b, 0);
    b8(&b, 0);
    b16(&b, 0x0303);
    for (int i = 0; i < 32; i++)
    {
        b8(&b, (uint8_t)(0x30 + i));
    }
    b8(&b, 0x00);
    b8(&b, 0x00);
    b16(&b, 0x0002);
    b16(&b, 0x1301);
    b8(&b, 0x01);
    b8(&b, 0x00);
    size_t ext_len_at = b.n;
    b16(&b, 0);

    b16(&b, 0x002b);
    b16(&b, 0x0003);
    b8(&b, 0x02);
    b16(&b, 0xFEFC);

    b16(&b, 0x000a);
    b16(&b, 0x0004);
    b16(&b, 0x0002);
    b16(&b, group);

    b16(&b, 0x000d);
    b16(&b, 0x0004);
    b16(&b, 0x0002);
    b16(&b, sigalg);
    if (with_keyshare)
    {

        b16(&b, 0x0033);
        b16(&b, 0x0026);
        b16(&b, 0x0024);
        b16(&b, 0x001d);
        b16(&b, 0x0020);
        bmem(&b, client_pub, 32);
    }
    if (cookie && cookie_len)
    {

        b16(&b, 0x002c);
        b16(&b, (uint16_t)(cookie_len + 2));
        b16(&b, (uint16_t)cookie_len);
        bmem(&b, cookie, cookie_len);
    }
    if (cid)
    {

        b16(&b, 0x0036);
        b16(&b, (uint16_t)(1 + cid_len));
        b8(&b, (uint8_t)cid_len);
        bmem(&b, cid, cid_len);
    }
    if (offer_rpk)
    {

        b16(&b, 0x0014);
        b16(&b, 0x0003);
        b8(&b, 0x02);
        b8(&b, 0x00);
        b8(&b, 0x02);
    }
    uint16_t ext_len = (uint16_t)(b.n - ext_len_at - 2);
    out[ext_len_at] = (uint8_t)(ext_len >> 8);
    out[ext_len_at + 1] = (uint8_t)ext_len;
    uint32_t body = (uint32_t)(b.n - len_at - 3);
    out[len_at] = (uint8_t)(body >> 16);
    out[len_at + 1] = (uint8_t)(body >> 8);
    out[len_at + 2] = (uint8_t)body;
    return b.n;
}

static size_t build_client_hello(uint8_t *out, const uint8_t client_pub[32])
{
    return build_client_hello_ex(out, client_pub, PROTO_TRUE, NULL, 0, NULL, 0, PROTO_FALSE, TLS_GROUP_X25519,
                                 TLS_SIG_ED25519);
}

static proto_bool sh_keyshare(const uint8_t *sh, size_t len, uint8_t pub[32])
{
    if (len < 44)
    {
        return PROTO_FALSE;
    }
    size_t o = 4 + 2 + 32;
    uint8_t sid = sh[o++];
    o += sid;
    o += 2 + 1;
    if (o + 2 > len)
    {
        return PROTO_FALSE;
    }
    size_t ext_end = o + 2 + ((sh[o] << 8) | sh[o + 1]);
    o += 2;
    while (o + 4 <= ext_end && ext_end <= len)
    {
        uint16_t et = (uint16_t)((sh[o] << 8) | sh[o + 1]);
        uint16_t el = (uint16_t)((sh[o + 2] << 8) | sh[o + 3]);
        o += 4;
        if (et == 0x0033 && el >= 4 + 32)
        {
            memcpy(pub, sh + o + 4, 32);
            return PROTO_TRUE;
        }
        o += el;
    }
    return PROTO_FALSE;
}

static proto_bool hrr_cookie(const uint8_t *sh, size_t len, uint8_t *cookie_out, size_t *cookie_len)
{
    if (len < 44)
    {
        return PROTO_FALSE;
    }
    size_t o = 4 + 2 + 32;
    uint8_t sid = sh[o++];
    o += sid;
    o += 2 + 1;
    if (o + 2 > len)
    {
        return PROTO_FALSE;
    }
    size_t ext_end = o + 2 + ((sh[o] << 8) | sh[o + 1]);
    o += 2;
    while (o + 4 <= ext_end && ext_end <= len)
    {
        uint16_t et = (uint16_t)((sh[o] << 8) | sh[o + 1]);
        uint16_t el = (uint16_t)((sh[o + 2] << 8) | sh[o + 3]);
        o += 4;
        if (et == 0x002c && el >= 2)
        {
            size_t cl = (size_t)((sh[o] << 8) | sh[o + 1]);
            if (cl + 2 > el || o + 2 + cl > len)
            {
                return PROTO_FALSE;
            }
            memcpy(cookie_out, sh + o + 2, cl);
            *cookie_len = cl;
            return PROTO_TRUE;
        }
        o += el;
    }
    return PROTO_FALSE;
}

static size_t frag_to_tls(const uint8_t *payload, size_t plen, uint8_t *tls_out)
{
    DtlsHsHeader hh;
    DtlsHandshake.header_parse_args.p = payload;
    DtlsHandshake.header_parse_args.len = plen;
    DtlsHandshake.header_parse_args.out = &hh;
    DtlsHandshake.header_parse(dtls_handshake_work);
    if (!DtlsHandshake.n || hh.frag_offset != 0 || hh.frag_length != hh.length)
    {
        return 0;
    }
    tls_out[0] = hh.msg_type;
    tls_out[1] = (uint8_t)(hh.length >> 16);
    tls_out[2] = (uint8_t)(hh.length >> 8);
    tls_out[3] = (uint8_t)hh.length;
    memcpy(tls_out + 4, hh.fragment, hh.length);
    return 4 + hh.length;
}

static size_t ct_record_len(const uint8_t *rec, size_t avail, size_t cid_len)
{
    size_t pre = 1 + ((rec[0] & 0x10) ? cid_len : 0);
    size_t seq_len = (rec[0] & 0x08) ? 2 : 1;
    size_t len_off = pre + seq_len;
    size_t o = len_off + 2;
    size_t enc = ((size_t)rec[len_off] << 8) | rec[len_off + 1];
    return (o + enc <= avail) ? o + enc : 0;
}

static size_t sh_conn_id(const uint8_t *sh, size_t len, uint8_t *cid_out)
{
    if (len < 44)
    {
        return 0;
    }
    size_t o = 4 + 2 + 32;
    uint8_t sid = sh[o++];
    o += sid;
    o += 2 + 1;
    if (o + 2 > len)
    {
        return 0;
    }
    size_t ext_end = o + 2 + ((sh[o] << 8) | sh[o + 1]);
    o += 2;
    while (o + 4 <= ext_end && ext_end <= len)
    {
        uint16_t et = (uint16_t)((sh[o] << 8) | sh[o + 1]);
        uint16_t el = (uint16_t)((sh[o + 2] << 8) | sh[o + 3]);
        o += 4;
        if (et == 0x0036 && el >= 1)
        {
            size_t cl = sh[o];
            if (1 + cl > el || o + 1 + cl > len)
            {
                return 0;
            }
            memcpy(cid_out, sh + o + 1, cl);
            return cl;
        }
        o += el;
    }
    return 0;
}

static proto_bool ee_has_rpk(const uint8_t *msg, size_t mlen)
{
    if (mlen < 6)
    {
        return PROTO_FALSE;
    }
    size_t o = 4;
    size_t ext_end = o + 2 + ((msg[o] << 8) | msg[o + 1]);
    o += 2;
    while (o + 4 <= ext_end && ext_end <= mlen)
    {
        uint16_t et = (uint16_t)((msg[o] << 8) | msg[o + 1]);
        uint16_t el = (uint16_t)((msg[o + 2] << 8) | msg[o + 3]);
        o += 4;
        if (et == 0x0014 && el == 1 && msg[o] == 0x02)
        {
            return PROTO_TRUE;
        }
        o += el;
    }
    return PROTO_FALSE;
}

static const uint8_t SPKI_PREFIX[12] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};

static DtlsConn g_dtls;
static DtlsConn g_dtls2;

// Takes the caller's transcript and keeps feeding it. No caller reads it back afterwards, so this
// runs on the context itself rather than the hand-rebased copy it used to fork.
static void complete_handshake_from_flight(DtlsConn *conn, uint8_t *tr, uint16_t cfin_msg_seq, const uint8_t *flight,
                                           size_t fl, const uint8_t *client_cid, size_t client_cid_len,
                                           proto_bool expect_rpk, uint64_t first_seq)
{
    size_t off = 0;

    DtlsPlaintext pt;
    DtlsRecord.plaintext_parse_args.rec = flight + off;
    DtlsRecord.plaintext_parse_args.rec_len = fl - off;
    DtlsRecord.plaintext_parse_args.out = &pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    size_t rl = DtlsRecord.n;
    TEST_ASSERT_TRUE(rl > 0);
    off += rl;
    uint8_t sh[512];
    size_t sh_len = frag_to_tls(pt.fragment, pt.frag_len, sh);
    TEST_ASSERT_TRUE(sh_len > 0);
    Sha256.update_args.data = sh;
    Sha256.update_args.len = sh_len;
    Sha256.update(tr);

    uint8_t server_pub[32];
    TEST_ASSERT_TRUE(sh_keyshare(sh, sh_len, server_pub));

    uint8_t scid[PROTOCORE_DTLS_CID_MAX];
    size_t scid_len = client_cid_len ? sh_conn_id(sh, sh_len, scid) : 0;
    if (client_cid_len)
    {
        TEST_ASSERT_TRUE(scid_len > 0);
    }

    uint8_t ecdhe[32];
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    Tls13KeySchedule cks;
    uint8_t h[32];
    Sha256.final_args.out = h;
    Sha256.final(tr);
    static uint8_t ks_store_392[PROTOCORE_TLS13_KS_BORROW];
    Tls13Ks.bind.kdf = &DTLS13_KDF;
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.bind.s = ks_store_392;
    Tls13Ks.early(NULL);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = 32;
    Tls13Ks.step.ch_sh_hash = h;
    Tls13Ks.handshake(NULL);

    DtlsRecordKeys srv_read;
    DtlsRecord.keys_derive_args.out = &srv_read;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 2;
    DtlsRecord.keys_derive_args.secret = cks.s + TLS13_KS_SERVER_HS;
    DtlsRecord.keys_derive(dtls_record_work);

    uint8_t cert_pub[32];
    proto_bool have_cert = PROTO_FALSE;
    uint64_t exp_seq = first_seq;
    int seen_fin = 0;
    while (off < fl)
    {
        size_t crl = ct_record_len(flight + off, fl - off, client_cid_len);
        TEST_ASSERT_TRUE(crl > 0);
        uint8_t inner[512];
        DtlsCiphertext info;
        DtlsRecord.unprotect_args.keys = &srv_read;
        DtlsRecord.unprotect_args.next_seq = exp_seq;
        DtlsRecord.unprotect_args.rec = flight + off;
        DtlsRecord.unprotect_args.rec_len = crl;
        DtlsRecord.unprotect_args.out = inner;
        DtlsRecord.unprotect_args.out_cap = sizeof(inner);
        DtlsRecord.unprotect_args.info = &info;
        DtlsRecord.unprotect_args.expected_cid = client_cid;
        DtlsRecord.unprotect_args.expected_cid_len = client_cid_len;
        DtlsRecord.unprotect(dtls_record_work);
        TEST_ASSERT_TRUE(DtlsRecord.ok);
        exp_seq = info.seq + 1;
        off += crl;
        TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DTLS_CT_HANDSHAKE, info.content_type);

        uint8_t msg[512];
        size_t mlen = frag_to_tls(inner, info.pt_len, msg);
        TEST_ASSERT_TRUE(mlen > 0);

        if (msg[0] == 15)
        {
            TEST_ASSERT_TRUE(have_cert);
            uint8_t h_ch_cert[32];
            Sha256.final_args.out = h_ch_cert;
            Sha256.final(tr);
            uint8_t content[160];
            size_t clen = protocore_tls13_cert_verify_content(content, sizeof(content), h_ch_cert, 32, PROTO_TRUE);
            TEST_ASSERT_TRUE(clen > 0);
            const uint8_t *sig = msg + 4 + 2 + 2;
            Ed25519.verify_args.pub = cert_pub;
            Ed25519.verify_args.msg = content;
            Ed25519.verify_args.msg_len = clen;
            Ed25519.verify_args.sig = sig;
            Ed25519.verify(tw);
            TEST_ASSERT_TRUE(Ed25519.ok);
        }
        if (msg[0] == 20)
        {
            uint8_t hcv[32];
            Sha256.final_args.out = hcv;
            Sha256.final(tr);
            uint8_t expect[32];
            Tls13Ks.bind.ks = &cks;
            Tls13Ks.finished_args.base_secret = cks.s + TLS13_KS_SERVER_HS;
            Tls13Ks.finished_args.transcript_hash = hcv;
            Tls13Ks.finished_args.out = expect;
            Tls13Ks.finished_mac(NULL);
            TEST_ASSERT_EQUAL_MEMORY(expect, msg + 4, 32);
            seen_fin = 1;
        }

        if (msg[0] == 8)
        {
            TEST_ASSERT_EQUAL(expect_rpk, ee_has_rpk(msg, mlen));
        }

        Sha256.update_args.data = msg;
        Sha256.update_args.len = mlen;
        Sha256.update(tr);

        if (msg[0] == 11)
        {
            const uint8_t *cert_data = msg + 4 + 1 + 3 + 3;
            uint32_t entry_len = (uint32_t)((msg[4 + 1 + 3] << 16) | (msg[4 + 1 + 3 + 1] << 8) | msg[4 + 1 + 3 + 2]);
            if (expect_rpk)
            {
                TEST_ASSERT_EQUAL_UINT32(44, entry_len);
                TEST_ASSERT_EQUAL_MEMORY(SPKI_PREFIX, cert_data, sizeof(SPKI_PREFIX));
                memcpy(cert_pub, cert_data + 12, 32);
            }
            else
            {
                TEST_ASSERT_EQUAL_UINT32(32, entry_len);
                memcpy(cert_pub, cert_data, 32);
            }
            have_cert = PROTO_TRUE;
        }
    }
    TEST_ASSERT_TRUE(seen_fin);

    uint8_t h_sfin[32];
    Sha256.final_args.out = h_sfin;
    Sha256.final(tr);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ch_sfin_hash = h_sfin;
    Tls13Ks.master(NULL);

    uint8_t cfin_verify[32];
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.finished_args.base_secret = cks.s + TLS13_KS_CLIENT_HS;
    Tls13Ks.finished_args.transcript_hash = h_sfin;
    Tls13Ks.finished_args.out = cfin_verify;
    Tls13Ks.finished_mac(NULL);
    uint8_t cfin[64];
    size_t cfin_len = protocore_tls13_build_finished(cfin, sizeof(cfin), cfin_verify, 32);

    DtlsRecordKeys cli_write;
    DtlsRecord.keys_derive_args.out = &cli_write;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 2;
    DtlsRecord.keys_derive_args.secret = cks.s + TLS13_KS_CLIENT_HS;
    DtlsRecord.keys_derive(dtls_record_work);

    uint8_t cfin_frag[80];
    DtlsHandshake.frag_build_args.msg_type = cfin[0];
    DtlsHandshake.frag_build_args.msg_seq = cfin_msg_seq;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(cfin_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = cfin + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(cfin_len - 4);
    DtlsHandshake.frag_build_args.out = cfin_frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(cfin_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t cff = DtlsHandshake.n;
    uint8_t cfin_rec[128];
    DtlsRecord.protect_args.keys = &cli_write;
    DtlsRecord.protect_args.seq = 0;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.protect_args.plaintext = cfin_frag;
    DtlsRecord.protect_args.pt_len = cff;
    DtlsRecord.protect_args.out = cfin_rec;
    DtlsRecord.protect_args.out_cap = sizeof(cfin_rec);
    DtlsRecord.protect_args.cid = scid_len ? scid : NULL;
    DtlsRecord.protect_args.cid_len = scid_len;
    DtlsRecord.protect(dtls_record_work);
    size_t cfr = DtlsRecord.n;

    uint8_t out2[64];
    DtlsServer.process_args.c = conn;
    DtlsServer.process_args.dgram = cfin_rec;
    DtlsServer.process_args.len = cfr;
    DtlsServer.process_args.out = out2;
    DtlsServer.process_args.out_cap = sizeof(out2);
    DtlsServer.process(dtls_server_work);
    int r2 = DtlsServer.n;
    TEST_ASSERT_TRUE(r2 > 0);
    DtlsServer.established_args.c = conn;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);

    DtlsRecordKeys cli_app_read;
    DtlsRecordKeys cli_app_write;
    DtlsRecord.keys_derive_args.out = &cli_app_read;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 3;
    DtlsRecord.keys_derive_args.secret = cks.s + TLS13_KS_SERVER_AP;
    DtlsRecord.keys_derive(dtls_record_work);
    DtlsRecord.keys_derive_args.out = &cli_app_write;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 3;
    DtlsRecord.keys_derive_args.secret = cks.s + TLS13_KS_CLIENT_AP;
    DtlsRecord.keys_derive(dtls_record_work);

    uint8_t ack_pt[64];
    DtlsCiphertext ackinfo;
    DtlsRecord.unprotect_args.keys = &cli_app_read;
    DtlsRecord.unprotect_args.next_seq = 0;
    DtlsRecord.unprotect_args.rec = out2;
    DtlsRecord.unprotect_args.rec_len = (size_t)r2;
    DtlsRecord.unprotect_args.out = ack_pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(ack_pt);
    DtlsRecord.unprotect_args.info = &ackinfo;
    DtlsRecord.unprotect_args.expected_cid = client_cid;
    DtlsRecord.unprotect_args.expected_cid_len = client_cid_len;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DTLS_CT_ACK, ackinfo.content_type);

    DtlsServer.app_write_keys_args.c = conn;
    DtlsServer.app_write_keys(dtls_server_work);
    DtlsRecordKeys *const srv_app_write = DtlsServer.ptr;
    DtlsServer.app_read_keys_args.c = conn;
    DtlsServer.app_read_keys(dtls_server_work);
    DtlsRecordKeys *const srv_app_read = DtlsServer.ptr;
    TEST_ASSERT_NOT_NULL(srv_app_write);
    TEST_ASSERT_NOT_NULL(srv_app_read);

    TEST_ASSERT_EQUAL_MEMORY(cli_app_read.iv, srv_app_write->iv, 12);
    TEST_ASSERT_EQUAL_MEMORY(cli_app_write.iv, srv_app_read->iv, 12);
    assert_ctx_match(cli_app_read.gcm, srv_app_write->gcm);
    assert_ctx_match(cli_app_write.gcm, srv_app_read->gcm);

    uint8_t cfin_rec2[128];
    DtlsRecord.protect_args.keys = &cli_write;
    DtlsRecord.protect_args.seq = 1;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.protect_args.plaintext = cfin_frag;
    DtlsRecord.protect_args.pt_len = cff;
    DtlsRecord.protect_args.out = cfin_rec2;
    DtlsRecord.protect_args.out_cap = sizeof(cfin_rec2);
    DtlsRecord.protect_args.cid = scid_len ? scid : NULL;
    DtlsRecord.protect_args.cid_len = scid_len;
    DtlsRecord.protect(dtls_record_work);
    size_t cfr2 = DtlsRecord.n;
    uint8_t out3[64];
    DtlsServer.process_args.c = conn;
    DtlsServer.process_args.dgram = cfin_rec2;
    DtlsServer.process_args.len = cfr2;
    DtlsServer.process_args.out = out3;
    DtlsServer.process_args.out_cap = sizeof(out3);
    DtlsServer.process(dtls_server_work);
    int r3 = DtlsServer.n;
    TEST_ASSERT_TRUE(r3 > 0);
    DtlsServer.established_args.c = conn;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);
    uint8_t ack_pt3[64];
    DtlsCiphertext ackinfo3;
    DtlsRecord.unprotect_args.keys = &cli_app_read;
    DtlsRecord.unprotect_args.next_seq = 1;
    DtlsRecord.unprotect_args.rec = out3;
    DtlsRecord.unprotect_args.rec_len = (size_t)r3;
    DtlsRecord.unprotect_args.out = ack_pt3;
    DtlsRecord.unprotect_args.out_cap = sizeof(ack_pt3);
    DtlsRecord.unprotect_args.info = &ackinfo3;
    DtlsRecord.unprotect_args.expected_cid = client_cid;
    DtlsRecord.unprotect_args.expected_cid_len = client_cid_len;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DTLS_CT_ACK, ackinfo3.content_type);
}

static void server_cfg(DtlsServerConfig *cfg, const uint8_t server_ed_pub[32])
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->cert_der = server_ed_pub;
    cfg->cert_len = 32;
    cfg->ed25519_seed = SERVER_ED_SEED;
    cfg->ephemeral_priv = SERVER_X25519_PRIV;
    cfg->server_random = SERVER_RANDOM;
    cfg->cookie_key = SERVER_COOKIE_KEY;
}

void test_full_handshake(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);

    uint8_t *tr;
    tr = tw_tr;
    Sha256.init(tr);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = ch_len;
    Sha256.update(tr);

    uint8_t ch_frag[300];
    DtlsHandshake.frag_build_args.msg_type = ch[0];
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.out = ch_frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(ch_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t ch_fl = DtlsHandshake.n;
    uint8_t ch_rec[320];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = ch_frag;
    DtlsRecord.plaintext_build_args.frag_len = ch_fl;
    DtlsRecord.plaintext_build_args.out = ch_rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(ch_rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t ch_rl = DtlsRecord.n;

    uint8_t flight[2048];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = ch_rec;
    DtlsServer.process_args.len = ch_rl;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    int fl = DtlsServer.n;
    TEST_ASSERT_TRUE(fl > 0);

    complete_handshake_from_flight(&g_dtls, tr, 1, flight, (size_t)fl, NULL, 0, PROTO_FALSE, 0);
}

void test_full_handshake_rpk(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    uint8_t ch[256];
    size_t ch_len = build_client_hello_ex(ch, client_pub, PROTO_TRUE, NULL, 0, NULL, 0, PROTO_TRUE, TLS_GROUP_X25519,
                                          TLS_SIG_ED25519);

    uint8_t *tr;
    tr = tw_tr;
    Sha256.init(tr);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = ch_len;
    Sha256.update(tr);

    uint8_t ch_frag[300];
    DtlsHandshake.frag_build_args.msg_type = ch[0];
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.out = ch_frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(ch_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t ch_fl = DtlsHandshake.n;
    uint8_t ch_rec[320];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = ch_frag;
    DtlsRecord.plaintext_build_args.frag_len = ch_fl;
    DtlsRecord.plaintext_build_args.out = ch_rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(ch_rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t ch_rl = DtlsRecord.n;

    uint8_t flight[2048];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = ch_rec;
    DtlsServer.process_args.len = ch_rl;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    int fl = DtlsServer.n;
    TEST_ASSERT_TRUE(fl > 0);

    complete_handshake_from_flight(&g_dtls, tr, 1, flight, (size_t)fl, NULL, 0, PROTO_TRUE, 0);
}

void test_cid_handshake(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    const uint8_t client_cid[3] = {0xC1, 0xC2, 0xC3};
    uint8_t ch[256];
    size_t ch_len = build_client_hello_ex(ch, client_pub, PROTO_TRUE, NULL, 0, client_cid, sizeof(client_cid),
                                          PROTO_FALSE, TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t *tr;
    tr = tw_tr;
    Sha256.init(tr);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = ch_len;
    Sha256.update(tr);

    uint8_t ch_frag[300];
    DtlsHandshake.frag_build_args.msg_type = ch[0];
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.out = ch_frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(ch_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t ch_fl = DtlsHandshake.n;
    uint8_t ch_rec[320];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = ch_frag;
    DtlsRecord.plaintext_build_args.frag_len = ch_fl;
    DtlsRecord.plaintext_build_args.out = ch_rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(ch_rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t ch_rl = DtlsRecord.n;

    uint8_t flight[2048];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = ch_rec;
    DtlsServer.process_args.len = ch_rl;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    int fl = DtlsServer.n;
    TEST_ASSERT_TRUE(fl > 0);

    DtlsPlaintext sh_pt;
    DtlsRecord.plaintext_parse_args.rec = flight;
    DtlsRecord.plaintext_parse_args.rec_len = (size_t)fl;
    DtlsRecord.plaintext_parse_args.out = &sh_pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    size_t shrl = DtlsRecord.n;
    TEST_ASSERT_TRUE(shrl > 0);
    const uint8_t *ep2 = flight + shrl;
    TEST_ASSERT_TRUE((ep2[0] & 0x10) != 0);
    TEST_ASSERT_EQUAL_MEMORY(client_cid, ep2 + 1, sizeof(client_cid));

    complete_handshake_from_flight(&g_dtls, tr, 1, flight, (size_t)fl, client_cid, sizeof(client_cid), PROTO_FALSE, 0);
}

void test_hrr_group_renegotiation(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = TEST_PEER_ADDR;
    DtlsServer.init_args.peer_addr_len = sizeof(TEST_PEER_ADDR);
    DtlsServer.init(dtls_server_work);

    uint8_t ch1[256];
    size_t ch1_len = build_client_hello_ex(ch1, client_pub, PROTO_FALSE, NULL, 0, NULL, 0, PROTO_FALSE,
                                           TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t f1[300];
    DtlsHandshake.frag_build_args.msg_type = ch1[0];
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch1_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch1 + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch1_len - 4);
    DtlsHandshake.frag_build_args.out = f1;
    DtlsHandshake.frag_build_args.out_cap = sizeof(f1);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t f1l = DtlsHandshake.n;
    uint8_t r1[320];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = f1;
    DtlsRecord.plaintext_build_args.frag_len = f1l;
    DtlsRecord.plaintext_build_args.out = r1;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(r1);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t r1l = DtlsRecord.n;

    uint8_t hrr_flight[512];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = r1;
    DtlsServer.process_args.len = r1l;
    DtlsServer.process_args.out = hrr_flight;
    DtlsServer.process_args.out_cap = sizeof(hrr_flight);
    DtlsServer.process(dtls_server_work);
    int hf = DtlsServer.n;
    TEST_ASSERT_TRUE(hf > 0);
    DtlsServer.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_FALSE(DtlsServer.ok);

    DtlsPlaintext pt;
    DtlsRecord.plaintext_parse_args.rec = hrr_flight;
    DtlsRecord.plaintext_parse_args.rec_len = (size_t)hf;
    DtlsRecord.plaintext_parse_args.out = &pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    size_t rl = DtlsRecord.n;
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t hrr[512];
    size_t hrr_len = frag_to_tls(pt.fragment, pt.frag_len, hrr);
    TEST_ASSERT_TRUE(hrr_len > 0);
    TEST_ASSERT_EQUAL_UINT8(0x02, hrr[0]);
    TEST_ASSERT_EQUAL_MEMORY(protocore_tls13_hrr_random, hrr + 4 + 2, 32);
    uint8_t cookie[PROTOCORE_DTLS_COOKIE_MAX];
    size_t cookie_len = 0;
    TEST_ASSERT_TRUE(hrr_cookie(hrr, hrr_len, cookie, &cookie_len));
    TEST_ASSERT_TRUE(cookie_len > 0);

    uint8_t ch1_hash[32];
    uint8_t *h1;
    h1 = tw_h1;
    Sha256.init(h1);
    Sha256.update_args.data = ch1;
    Sha256.update_args.len = ch1_len;
    Sha256.update(h1);
    Sha256.final_args.out = ch1_hash;
    Sha256.final(h1);

    uint8_t *tr;
    tr = tw_tr;
    Sha256.init(tr);
    uint8_t mh[36];
    size_t mhl = protocore_tls13_build_message_hash(mh, sizeof(mh), ch1_hash);
    TEST_ASSERT_TRUE(mhl > 0);
    Sha256.update_args.data = mh;
    Sha256.update_args.len = mhl;
    Sha256.update(tr);
    Sha256.update_args.data = hrr;
    Sha256.update_args.len = hrr_len;
    Sha256.update(tr);

    uint8_t ch2[320];
    size_t ch2_len = build_client_hello_ex(ch2, client_pub, PROTO_TRUE, cookie, cookie_len, NULL, 0, PROTO_FALSE,
                                           TLS_GROUP_X25519, TLS_SIG_ED25519);
    Sha256.update_args.data = ch2;
    Sha256.update_args.len = ch2_len;
    Sha256.update(tr);

    uint8_t f2[380];
    DtlsHandshake.frag_build_args.msg_type = ch2[0];
    DtlsHandshake.frag_build_args.msg_seq = 1;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch2_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch2 + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch2_len - 4);
    DtlsHandshake.frag_build_args.out = f2;
    DtlsHandshake.frag_build_args.out_cap = sizeof(f2);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t f2l = DtlsHandshake.n;
    uint8_t r2[420];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 1;
    DtlsRecord.plaintext_build_args.fragment = f2;
    DtlsRecord.plaintext_build_args.frag_len = f2l;
    DtlsRecord.plaintext_build_args.out = r2;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(r2);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t r2l = DtlsRecord.n;

    uint8_t flight[2048];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = r2;
    DtlsServer.process_args.len = r2l;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    int fl = DtlsServer.n;
    TEST_ASSERT_TRUE(fl > 0);

    complete_handshake_from_flight(&g_dtls, tr, 2, flight, (size_t)fl, NULL, 0, PROTO_FALSE, 0);
}

void test_hrr_retry_without_cookie_rejected(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = TEST_PEER_ADDR;
    DtlsServer.init_args.peer_addr_len = sizeof(TEST_PEER_ADDR);
    DtlsServer.init(dtls_server_work);

    uint8_t ch1[256];
    size_t ch1_len = build_client_hello_ex(ch1, client_pub, PROTO_FALSE, NULL, 0, NULL, 0, PROTO_FALSE,
                                           TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t f1[300];
    DtlsHandshake.frag_build_args.msg_type = ch1[0];
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch1_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch1 + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch1_len - 4);
    DtlsHandshake.frag_build_args.out = f1;
    DtlsHandshake.frag_build_args.out_cap = sizeof(f1);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t f1l = DtlsHandshake.n;
    uint8_t r1[320];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = f1;
    DtlsRecord.plaintext_build_args.frag_len = f1l;
    DtlsRecord.plaintext_build_args.out = r1;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(r1);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t r1l = DtlsRecord.n;
    uint8_t hrr_flight[512];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = r1;
    DtlsServer.process_args.len = r1l;
    DtlsServer.process_args.out = hrr_flight;
    DtlsServer.process_args.out_cap = sizeof(hrr_flight);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.n > 0);

    uint8_t ch2[320];
    size_t ch2_len = build_client_hello_ex(ch2, client_pub, PROTO_TRUE, NULL, 0, NULL, 0, PROTO_FALSE, TLS_GROUP_X25519,
                                           TLS_SIG_ED25519);
    uint8_t f2[380];
    DtlsHandshake.frag_build_args.msg_type = ch2[0];
    DtlsHandshake.frag_build_args.msg_seq = 1;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch2_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch2 + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch2_len - 4);
    DtlsHandshake.frag_build_args.out = f2;
    DtlsHandshake.frag_build_args.out_cap = sizeof(f2);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t f2l = DtlsHandshake.n;
    uint8_t r2[420];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 1;
    DtlsRecord.plaintext_build_args.fragment = f2;
    DtlsRecord.plaintext_build_args.frag_len = f2l;
    DtlsRecord.plaintext_build_args.out = r2;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(r2);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t r2l = DtlsRecord.n;
    uint8_t out[2048];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = r2;
    DtlsServer.process_args.len = r2l;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(47, DtlsServer.value);
}

void test_reject_no_tls13(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);

    for (size_t i = 0; i + 1 < ch_len; i++)
    {
        if (ch[i] == 0xFE && ch[i + 1] == 0xFC)
        {
            ch[i + 1] = 0xFD;
            break;
        }
    }
    uint8_t frag[300], rec[320], out[1024];
    DtlsHandshake.frag_build_args.msg_type = ch[0];
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.out = frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t fl = DtlsHandshake.n;
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = frag;
    DtlsRecord.plaintext_build_args.frag_len = fl;
    DtlsRecord.plaintext_build_args.out = rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t rl = DtlsRecord.n;
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(70, DtlsServer.value);
}

static int drive_server_flight(DtlsConn *conn, DtlsServerConfig *cfg, uint8_t **tr, uint8_t *flight, size_t flight_cap)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    DtlsServer.init_args.c = conn;
    DtlsServer.init_args.cfg = cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    *tr = tw_tr; // NOT tw: that is the shared work borrow every other call here is handed, and
                 // the next one to take it would overwrite the running transcript
    Sha256.init(*tr);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = ch_len;
    Sha256.update(*tr);
    uint8_t ch_frag[300];
    DtlsHandshake.frag_build_args.msg_type = ch[0];
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.out = ch_frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(ch_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t ch_fl = DtlsHandshake.n;
    uint8_t ch_rec[320];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = ch_frag;
    DtlsRecord.plaintext_build_args.frag_len = ch_fl;
    DtlsRecord.plaintext_build_args.out = ch_rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(ch_rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t ch_rl = DtlsRecord.n;
    DtlsServer.process_args.c = conn;
    DtlsServer.process_args.dgram = ch_rec;
    DtlsServer.process_args.len = ch_rl;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = flight_cap;
    DtlsServer.process(dtls_server_work);
    return DtlsServer.n;
}

void test_pto_retransmit_and_recovery(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t *tr;
    uint8_t flight[2048];
    int fl = drive_server_flight(&g_dtls, &cfg, &tr, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fl > 0);

    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_DTLS_PTO_INITIAL_MS, DtlsServer.n);

    uint8_t rflight[2048];
    DtlsServer.on_timeout_args.c = &g_dtls;
    DtlsServer.on_timeout_args.out = rflight;
    DtlsServer.on_timeout_args.out_cap = sizeof(rflight);
    DtlsServer.on_timeout(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);

    advance_ms(PROTOCORE_DTLS_PTO_INITIAL_MS);
    DtlsServer.on_timeout_args.c = &g_dtls;
    DtlsServer.on_timeout_args.out = rflight;
    DtlsServer.on_timeout_args.out_cap = sizeof(rflight);
    DtlsServer.on_timeout(dtls_server_work);
    int rfl = DtlsServer.n;
    TEST_ASSERT_TRUE(rfl > 0);
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT((int)(PROTOCORE_DTLS_PTO_INITIAL_MS * 2), DtlsServer.n);

    // RFC 9147 sec 4.2 computes the per-record nonce from the sequence number, so a retransmission
    // carries NEW sequence numbers rather than repeating the first flight's - repeating them would
    // reuse an AES-GCM nonce under the same key. Count what the first flight already spent, and the
    // walk below then proves the retransmit continues past it rather than starting over.
    DtlsPlaintext fpt;
    DtlsRecord.plaintext_parse_args.rec = flight;
    DtlsRecord.plaintext_parse_args.rec_len = (size_t)fl;
    DtlsRecord.plaintext_parse_args.out = &fpt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    size_t fskip = DtlsRecord.n; // the ServerHello goes out in the clear
    TEST_ASSERT_TRUE(fskip > 0);
    uint64_t sent = 0;
    for (size_t o = fskip; o < (size_t)fl;)
    {
        size_t rl = ct_record_len(flight + o, (size_t)fl - o, 0);
        TEST_ASSERT_TRUE(rl > 0);
        sent++;
        o += rl;
    }
    TEST_ASSERT_TRUE(sent > 0);
    complete_handshake_from_flight(&g_dtls, tr, 1, rflight, (size_t)rfl, NULL, 0, PROTO_FALSE, sent);
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
}

void test_pto_backoff_and_giveup(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t *tr;
    uint8_t flight[2048], rflight[2048];
    TEST_ASSERT_TRUE(drive_server_flight(&g_dtls, &cfg, &tr, flight, sizeof(flight)) > 0);

    static const int PTO_MS[PROTOCORE_DTLS_MAX_RETRANSMITS] = {2000, 4000, 8000, 16000, 32000, 60000, 60000, 60000};
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(1000, DtlsServer.n);
    for (int i = 0; i < PROTOCORE_DTLS_MAX_RETRANSMITS; i++)
    {
        advance_ms(PROTOCORE_DTLS_PTO_MAX_MS + 1000);
        DtlsServer.on_timeout_args.c = &g_dtls;
        DtlsServer.on_timeout_args.out = rflight;
        DtlsServer.on_timeout_args.out_cap = sizeof(rflight);
        DtlsServer.on_timeout(dtls_server_work);
        TEST_ASSERT_TRUE(DtlsServer.n > 0);
        DtlsServer.timeout_ms_args.c = &g_dtls;
        DtlsServer.timeout_ms(dtls_server_work);
        TEST_ASSERT_EQUAL_INT(PTO_MS[i], DtlsServer.n);
    }
    advance_ms(PROTOCORE_DTLS_PTO_MAX_MS + 1000);
    DtlsServer.on_timeout_args.c = &g_dtls;
    DtlsServer.on_timeout_args.out = rflight;
    DtlsServer.on_timeout_args.out_cap = sizeof(rflight);
    DtlsServer.on_timeout(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rflight;
    DtlsServer.process_args.len = 1;
    DtlsServer.process_args.out = rflight;
    DtlsServer.process_args.out_cap = sizeof(rflight);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
}

void test_pto_ack_cancels_retransmit(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t *tr;
    uint8_t flight[2048];
    int fl = drive_server_flight(&g_dtls, &cfg, &tr, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fl > 0);
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.n >= 0);

    DtlsPlaintext pt;
    DtlsRecord.plaintext_parse_args.rec = flight;
    DtlsRecord.plaintext_parse_args.rec_len = (size_t)fl;
    DtlsRecord.plaintext_parse_args.out = &pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.n > 0);
    uint8_t sh[512];
    size_t sh_len = frag_to_tls(pt.fragment, pt.frag_len, sh);
    TEST_ASSERT_TRUE(sh_len > 0);
    uint8_t server_pub[32];
    TEST_ASSERT_TRUE(sh_keyshare(sh, sh_len, server_pub));
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t *t;
    t = tw_t;
    Sha256.init(t);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = ch_len;
    Sha256.update(t);
    Sha256.update_args.data = sh;
    Sha256.update_args.len = sh_len;
    Sha256.update(t);
    uint8_t h[32];
    Sha256.final_args.out = h;
    Sha256.final(t);
    uint8_t ecdhe[32];
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    Tls13KeySchedule cks;
    static uint8_t ks_store_929[PROTOCORE_TLS13_KS_BORROW];
    Tls13Ks.bind.kdf = &DTLS13_KDF;
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.bind.s = ks_store_929;
    Tls13Ks.early(NULL);
    Tls13Ks.bind.ks = &cks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = 32;
    Tls13Ks.step.ch_sh_hash = h;
    Tls13Ks.handshake(NULL);
    DtlsRecordKeys cli_write;
    DtlsRecord.keys_derive_args.out = &cli_write;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 2;
    DtlsRecord.keys_derive_args.secret = cks.s + TLS13_KS_CLIENT_HS;
    DtlsRecord.keys_derive(dtls_record_work);

    DtlsRecordNumber rns[5] = {{0, 0}, {2, 0}, {2, 1}, {2, 2}, {2, 3}};
    uint8_t ack_body[2 + 5 * 16];
    DtlsHandshake.ack_build_args.nums = rns;
    DtlsHandshake.ack_build_args.count = 5;
    DtlsHandshake.ack_build_args.out = ack_body;
    DtlsHandshake.ack_build_args.out_cap = sizeof(ack_body);
    DtlsHandshake.ack_build(dtls_handshake_work);
    size_t bl = DtlsHandshake.n;
    TEST_ASSERT_TRUE(bl > 0);
    uint8_t ack_rec[160];
    DtlsRecord.protect_args.keys = &cli_write;
    DtlsRecord.protect_args.seq = 0;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_ACK;
    DtlsRecord.protect_args.plaintext = ack_body;
    DtlsRecord.protect_args.pt_len = bl;
    DtlsRecord.protect_args.out = ack_rec;
    DtlsRecord.protect_args.out_cap = sizeof(ack_rec);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t ar = DtlsRecord.n;
    TEST_ASSERT_TRUE(ar > 0);

    uint8_t out[64];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = ack_rec;
    DtlsServer.process_args.len = ar;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
}

static size_t plain_hs_record(uint8_t *out, size_t out_cap, const uint8_t *tls_msg, size_t tls_len, uint16_t msg_seq,
                              uint64_t rec_seq)
{
    uint8_t frag[512];
    DtlsHandshake.frag_build_args.msg_type = tls_msg[0];
    DtlsHandshake.frag_build_args.msg_seq = msg_seq;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(tls_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = tls_msg + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(tls_len - 4);
    DtlsHandshake.frag_build_args.out = frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t fl = DtlsHandshake.n;
    if (!fl)
    {
        return 0;
    }
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = rec_seq;
    DtlsRecord.plaintext_build_args.fragment = frag;
    DtlsRecord.plaintext_build_args.frag_len = fl;
    DtlsRecord.plaintext_build_args.out = out;
    DtlsRecord.plaintext_build_args.out_cap = out_cap;
    DtlsRecord.plaintext_build(dtls_record_work);
    return DtlsRecord.n;
}

typedef struct
{
    Tls13KeySchedule cks;
    DtlsRecordKeys cli_hs_write;
    DtlsRecordKeys srv_hs_read;
    DtlsRecordKeys cli_app_write;
    DtlsRecordKeys cli_app_read;
    uint8_t cfin_frag[80];
    size_t cfin_frag_len;
} ClientSession;

static proto_bool run_to_finished(DtlsConn *conn, DtlsServerConfig *cfg, ClientSession *st)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    DtlsServer.init_args.c = conn;
    DtlsServer.init_args.cfg = cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t *tr;
    tr = tw_tr;
    Sha256.init(tr);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = ch_len;
    Sha256.update(tr);

    uint8_t rec[320];
    size_t rl = plain_hs_record(rec, sizeof(rec), ch, ch_len, 0, 0);
    if (!rl)
    {
        return PROTO_FALSE;
    }
    uint8_t flight[2048];
    DtlsServer.process_args.c = conn;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    int fl = DtlsServer.n;
    if (fl <= 0)
    {
        return PROTO_FALSE;
    }

    DtlsPlaintext pt;
    DtlsRecord.plaintext_parse_args.rec = flight;
    DtlsRecord.plaintext_parse_args.rec_len = (size_t)fl;
    DtlsRecord.plaintext_parse_args.out = &pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    size_t off = DtlsRecord.n;
    if (!off)
    {
        return PROTO_FALSE;
    }
    uint8_t sh[512];
    size_t sh_len = frag_to_tls(pt.fragment, pt.frag_len, sh);
    if (!sh_len)
    {
        return PROTO_FALSE;
    }
    Sha256.update_args.data = sh;
    Sha256.update_args.len = sh_len;
    Sha256.update(tr);
    uint8_t server_pub[32];
    if (!sh_keyshare(sh, sh_len, server_pub))
    {
        return PROTO_FALSE;
    }

    uint8_t ecdhe[32];
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_args.point = server_pub;
    Curve25519.x25519(tw);
    uint8_t h[32];
    Sha256.final_args.out = h;
    Sha256.final(tr);
    static uint8_t ks_store_1033[PROTOCORE_TLS13_KS_BORROW];
    Tls13Ks.bind.kdf = &DTLS13_KDF;
    Tls13Ks.bind.ks = &st->cks;
    Tls13Ks.bind.s = ks_store_1033;
    Tls13Ks.early(NULL);
    Tls13Ks.bind.ks = &st->cks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = 32;
    Tls13Ks.step.ch_sh_hash = h;
    Tls13Ks.handshake(NULL);
    DtlsRecord.keys_derive_args.out = &st->srv_hs_read;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 2;
    DtlsRecord.keys_derive_args.secret = st->cks.s + TLS13_KS_SERVER_HS;
    DtlsRecord.keys_derive(dtls_record_work);
    DtlsRecord.keys_derive_args.out = &st->cli_hs_write;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 2;
    DtlsRecord.keys_derive_args.secret = st->cks.s + TLS13_KS_CLIENT_HS;
    DtlsRecord.keys_derive(dtls_record_work);

    uint64_t exp_seq = 0;
    while (off < (size_t)fl)
    {
        size_t crl = ct_record_len(flight + off, (size_t)fl - off, 0);
        if (!crl)
        {
            return PROTO_FALSE;
        }
        uint8_t inner[512];
        DtlsCiphertext info;
        DtlsRecord.unprotect_args.keys = &st->srv_hs_read;
        DtlsRecord.unprotect_args.next_seq = exp_seq;
        DtlsRecord.unprotect_args.rec = flight + off;
        DtlsRecord.unprotect_args.rec_len = crl;
        DtlsRecord.unprotect_args.out = inner;
        DtlsRecord.unprotect_args.out_cap = sizeof(inner);
        DtlsRecord.unprotect_args.info = &info;
        DtlsRecord.unprotect_args.expected_cid = NULL;
        DtlsRecord.unprotect_args.expected_cid_len = 0;
        DtlsRecord.unprotect(dtls_record_work);
        if (!DtlsRecord.ok)
        {
            return PROTO_FALSE;
        }
        exp_seq = info.seq + 1;
        off += crl;
        uint8_t msg[512];
        size_t mlen = frag_to_tls(inner, info.pt_len, msg);
        if (!mlen)
        {
            return PROTO_FALSE;
        }
        Sha256.update_args.data = msg;
        Sha256.update_args.len = mlen;
        Sha256.update(tr);
    }

    uint8_t h_sfin[32];
    Sha256.final_args.out = h_sfin;
    Sha256.final(tr);
    Tls13Ks.bind.ks = &st->cks;
    Tls13Ks.step.ch_sfin_hash = h_sfin;
    Tls13Ks.master(NULL);
    DtlsRecord.keys_derive_args.out = &st->cli_app_write;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 3;
    DtlsRecord.keys_derive_args.secret = st->cks.s + TLS13_KS_CLIENT_AP;
    DtlsRecord.keys_derive(dtls_record_work);
    DtlsRecord.keys_derive_args.out = &st->cli_app_read;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 3;
    DtlsRecord.keys_derive_args.secret = st->cks.s + TLS13_KS_SERVER_AP;
    DtlsRecord.keys_derive(dtls_record_work);

    uint8_t verify[32];
    Tls13Ks.bind.ks = &st->cks;
    Tls13Ks.finished_args.base_secret = st->cks.s + TLS13_KS_CLIENT_HS;
    Tls13Ks.finished_args.transcript_hash = h_sfin;
    Tls13Ks.finished_args.out = verify;
    Tls13Ks.finished_mac(NULL);
    uint8_t cfin[64];
    size_t cfin_len = protocore_tls13_build_finished(cfin, sizeof(cfin), verify, 32);
    if (cfin_len < 4)
    {
        return PROTO_FALSE;
    }
    DtlsHandshake.frag_build_args.msg_type = cfin[0];
    DtlsHandshake.frag_build_args.msg_seq = 1;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(cfin_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = cfin + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(cfin_len - 4);
    DtlsHandshake.frag_build_args.out = st->cfin_frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(st->cfin_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    st->cfin_frag_len = DtlsHandshake.n;
    return st->cfin_frag_len > 0;
}

static int feed_client_finished(DtlsConn *conn, ClientSession *st, uint64_t seq, uint8_t *out, size_t out_cap)
{
    uint8_t rec[128];
    DtlsRecord.protect_args.keys = &st->cli_hs_write;
    DtlsRecord.protect_args.seq = seq;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.protect_args.plaintext = st->cfin_frag;
    DtlsRecord.protect_args.pt_len = st->cfin_frag_len;
    DtlsRecord.protect_args.out = rec;
    DtlsRecord.protect_args.out_cap = sizeof(rec);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t rl = DtlsRecord.n;
    if (!rl)
    {
        return -2;
    }
    DtlsServer.process_args.c = conn;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = out_cap;
    DtlsServer.process(dtls_server_work);
    return DtlsServer.n;
}

static int feed_epoch2_msg(DtlsConn *conn, ClientSession *st, uint64_t seq, uint16_t msg_seq, const uint8_t *tls_msg,
                           size_t tls_len, uint8_t *out, size_t out_cap)
{
    uint8_t frag[128];
    DtlsHandshake.frag_build_args.msg_type = tls_msg[0];
    DtlsHandshake.frag_build_args.msg_seq = msg_seq;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(tls_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = tls_msg + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(tls_len - 4);
    DtlsHandshake.frag_build_args.out = frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t fl = DtlsHandshake.n;
    if (!fl)
    {
        return -2;
    }
    uint8_t rec[192];
    DtlsRecord.protect_args.keys = &st->cli_hs_write;
    DtlsRecord.protect_args.seq = seq;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.protect_args.plaintext = frag;
    DtlsRecord.protect_args.pt_len = fl;
    DtlsRecord.protect_args.out = rec;
    DtlsRecord.protect_args.out_cap = sizeof(rec);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t rl = DtlsRecord.n;
    if (!rl)
    {
        return -2;
    }
    DtlsServer.process_args.c = conn;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = out_cap;
    DtlsServer.process(dtls_server_work);
    return DtlsServer.n;
}

static int feed_epoch2_ack(DtlsConn *conn, ClientSession *st, uint64_t seq, const uint8_t *body, size_t blen,
                           uint8_t *out, size_t out_cap)
{
    uint8_t rec[192];
    DtlsRecord.protect_args.keys = &st->cli_hs_write;
    DtlsRecord.protect_args.seq = seq;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_ACK;
    DtlsRecord.protect_args.plaintext = body;
    DtlsRecord.protect_args.pt_len = blen;
    DtlsRecord.protect_args.out = rec;
    DtlsRecord.protect_args.out_cap = sizeof(rec);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t rl = DtlsRecord.n;
    if (!rl)
    {
        return -2;
    }
    DtlsServer.process_args.c = conn;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = out_cap;
    DtlsServer.process(dtls_server_work);
    return DtlsServer.n;
}

void test_ciphertext_truncated_header_stops_walk(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t out[64];

    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);
    const uint8_t short_len[3] = {0x2C, 0x00, 0x01};
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = short_len;
    DtlsServer.process_args.len = sizeof(short_len);
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);

    DtlsServer.init_args.c = &g_dtls2;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);
    const uint8_t long_len[5] = {0x2C, 0x00, 0x01, 0x00, 0xFF};
    DtlsServer.process_args.c = &g_dtls2;
    DtlsServer.process_args.dgram = long_len;
    DtlsServer.process_args.len = sizeof(long_len);
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls2;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);
}

void test_ciphertext_before_keys_is_discarded(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t out[64];

    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);
    const uint8_t with_len[7] = {0x2C, 0x00, 0x01, 0x00, 0x02, 0xAA, 0xBB};
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = with_len;
    DtlsServer.process_args.len = sizeof(with_len);
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);

    DtlsServer.init_args.c = &g_dtls2;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);
    const uint8_t no_len[4] = {0x20, 0x01, 0xAA, 0xBB};
    DtlsServer.process_args.c = &g_dtls2;
    DtlsServer.process_args.dgram = no_len;
    DtlsServer.process_args.len = sizeof(no_len);
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls2;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);
}

void test_plaintext_non_handshake_record_ignored(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    const uint8_t alert_body[2] = {0x01, 0x00};
    uint8_t rec[32];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_ALERT;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = alert_body;
    DtlsRecord.plaintext_build_args.frag_len = sizeof(alert_body);
    DtlsRecord.plaintext_build_args.out = rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t rl = DtlsRecord.n;
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[2048];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t ch_rec[320];
    size_t ch_rl = plain_hs_record(ch_rec, sizeof(ch_rec), ch, ch_len, 0, 1);
    TEST_ASSERT_TRUE(ch_rl > 0);
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = ch_rec;
    DtlsServer.process_args.len = ch_rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.n > 0);
}

void test_truncated_handshake_fragment_ignored(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    const uint8_t stub[5] = {0x01, 0x00, 0x00, 0x20, 0x00};
    uint8_t rec[32];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = stub;
    DtlsRecord.plaintext_build_args.frag_len = sizeof(stub);
    DtlsRecord.plaintext_build_args.out = rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t rl = DtlsRecord.n;
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[64];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);
}

void test_fragment_for_other_msg_seq_ignored(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t rec[320];
    size_t rl = plain_hs_record(rec, sizeof(rec), ch, ch_len, 7, 0);
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[2048];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);

    size_t rl0 = plain_hs_record(rec, sizeof(rec), ch, ch_len, 0, 1);
    TEST_ASSERT_TRUE(rl0 > 0);
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl0;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.n > 0);
}

void test_oversize_handshake_message_rejected(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    const uint8_t body[8] = {0};
    uint8_t frag[32];
    DtlsHandshake.frag_build_args.msg_type = TLS_HS_CLIENT_HELLO;
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(PROTOCORE_DTLS_CONN_REASM_CAP + 1);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = body;
    DtlsHandshake.frag_build_args.frag_len = sizeof(body);
    DtlsHandshake.frag_build_args.out = frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t fl = DtlsHandshake.n;
    TEST_ASSERT_TRUE(fl > 0);
    uint8_t rec[64];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = frag;
    DtlsRecord.plaintext_build_args.frag_len = fl;
    DtlsRecord.plaintext_build_args.out = rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t rl = DtlsRecord.n;
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[64];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(50, DtlsServer.value);
}

void test_unexpected_message_in_start_rejected(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    uint8_t fin[36];
    fin[0] = TLS_HS_FINISHED;
    fin[1] = 0;
    fin[2] = 0;
    fin[3] = 32;
    memset(fin + 4, 0xAA, 32);
    uint8_t rec[64];
    size_t rl = plain_hs_record(rec, sizeof(rec), fin, sizeof(fin), 0, 0);
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[64];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(10, DtlsServer.value);
}

void test_client_hello_missing_algorithms_rejected(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t rec[320];
    uint8_t out[2048];

    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);
    uint8_t ch_a[256];
    size_t la =
        build_client_hello_ex(ch_a, client_pub, PROTO_TRUE, NULL, 0, NULL, 0, PROTO_FALSE, TLS_GROUP_X25519, 0x0804);
    size_t ra = plain_hs_record(rec, sizeof(rec), ch_a, la, 0, 0);
    TEST_ASSERT_TRUE(ra > 0);
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = ra;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(40, DtlsServer.value);

    DtlsServer.init_args.c = &g_dtls2;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);
    uint8_t ch_b[256];
    size_t lb =
        build_client_hello_ex(ch_b, client_pub, PROTO_TRUE, NULL, 0, NULL, 0, PROTO_FALSE, 0x0017, TLS_SIG_ED25519);
    size_t rb = plain_hs_record(rec, sizeof(rec), ch_b, lb, 0, 0);
    TEST_ASSERT_TRUE(rb > 0);
    DtlsServer.process_args.c = &g_dtls2;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rb;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls2;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(40, DtlsServer.value);
}

void test_oversize_certificate_is_internal_error(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    static uint8_t big_cert[PROTOCORE_DTLS_CONN_MSG_CAP + 200];
    memset(big_cert, 0xAB, sizeof(big_cert));

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    cfg.cert_der = big_cert;
    cfg.cert_len = sizeof(big_cert);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t rec[320];
    size_t rl = plain_hs_record(rec, sizeof(rec), ch, ch_len, 0, 0);
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[4096];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(80, DtlsServer.value);
}

void test_flight_out_cap_too_small_is_internal_error(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t rec[320];
    uint8_t tiny[64];

    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    size_t rl = plain_hs_record(rec, sizeof(rec), ch, ch_len, 0, 0);
    TEST_ASSERT_TRUE(rl > 0);
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = tiny;
    DtlsServer.process_args.out_cap = sizeof(tiny);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(80, DtlsServer.value);

    DtlsServer.init_args.c = &g_dtls2;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = TEST_PEER_ADDR;
    DtlsServer.init_args.peer_addr_len = sizeof(TEST_PEER_ADDR);
    DtlsServer.init(dtls_server_work);
    uint8_t ch1[256];
    size_t ch1_len = build_client_hello_ex(ch1, client_pub, PROTO_FALSE, NULL, 0, NULL, 0, PROTO_FALSE,
                                           TLS_GROUP_X25519, TLS_SIG_ED25519);
    size_t r1 = plain_hs_record(rec, sizeof(rec), ch1, ch1_len, 0, 0);
    TEST_ASSERT_TRUE(r1 > 0);
    DtlsServer.process_args.c = &g_dtls2;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = r1;
    DtlsServer.process_args.out = tiny;
    DtlsServer.process_args.out_cap = sizeof(tiny);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls2;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(80, DtlsServer.value);
}

void test_retransmit_out_cap_too_small(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t *tr;
    uint8_t flight[2048];
    TEST_ASSERT_TRUE(drive_server_flight(&g_dtls, &cfg, &tr, flight, sizeof(flight)) > 0);

    advance_ms(PROTOCORE_DTLS_PTO_INITIAL_MS);
    uint8_t tiny[32];
    DtlsServer.on_timeout_args.c = &g_dtls;
    DtlsServer.on_timeout_args.out = tiny;
    DtlsServer.on_timeout_args.out_cap = sizeof(tiny);
    DtlsServer.on_timeout(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
}

void test_timer_idle_when_done_or_failed(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t out[256];

    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls, &cfg, &st));
    TEST_ASSERT_TRUE(feed_client_finished(&g_dtls, &st, 0, out, sizeof(out)) > 0);
    DtlsServer.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);
    advance_ms(PROTOCORE_DTLS_PTO_MAX_MS);
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.on_timeout_args.c = &g_dtls;
    DtlsServer.on_timeout_args.out = out;
    DtlsServer.on_timeout_args.out_cap = sizeof(out);
    DtlsServer.on_timeout(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);

    uint8_t *tr;
    uint8_t flight[2048];
    TEST_ASSERT_TRUE(drive_server_flight(&g_dtls2, &cfg, &tr, flight, sizeof(flight)) > 0);
    DtlsServer.timeout_ms_args.c = &g_dtls2;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.n >= 0);
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t rec[320];
    size_t rl = plain_hs_record(rec, sizeof(rec), ch, ch_len, 1, 1);
    TEST_ASSERT_TRUE(rl > 0);
    DtlsServer.process_args.c = &g_dtls2;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls2;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(10, DtlsServer.value);
    DtlsServer.timeout_ms_args.c = &g_dtls2;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.on_timeout_args.c = &g_dtls2;
    DtlsServer.on_timeout_args.out = out;
    DtlsServer.on_timeout_args.out_cap = sizeof(out);
    DtlsServer.on_timeout(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
}

void test_client_finished_error_paths(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t out[256];

    ClientSession sa;
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls, &cfg, &sa));
    uint8_t shortfin[35];
    shortfin[0] = TLS_HS_FINISHED;
    shortfin[1] = 0;
    shortfin[2] = 0;
    shortfin[3] = 31;
    memset(shortfin + 4, 0x5A, 31);
    TEST_ASSERT_EQUAL_INT(-1, feed_epoch2_msg(&g_dtls, &sa, 0, 1, shortfin, sizeof(shortfin), out, sizeof(out)));
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(50, DtlsServer.value);

    ClientSession sb;
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls2, &cfg, &sb));
    uint8_t badfin[36];
    badfin[0] = TLS_HS_FINISHED;
    badfin[1] = 0;
    badfin[2] = 0;
    badfin[3] = 32;
    memset(badfin + 4, 0xAA, 32);
    TEST_ASSERT_EQUAL_INT(-1, feed_epoch2_msg(&g_dtls2, &sb, 0, 1, badfin, sizeof(badfin), out, sizeof(out)));
    DtlsServer.alert_args.c = &g_dtls2;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(51, DtlsServer.value);

    ClientSession sc;
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls2, &cfg, &sc));
    uint8_t stray[14];
    stray[0] = TLS_HS_CLIENT_HELLO;
    stray[1] = 0;
    stray[2] = 0;
    stray[3] = 10;
    memset(stray + 4, 0x11, 10);
    TEST_ASSERT_EQUAL_INT(-1, feed_epoch2_msg(&g_dtls2, &sc, 0, 1, stray, sizeof(stray), out, sizeof(out)));
    DtlsServer.alert_args.c = &g_dtls2;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(10, DtlsServer.value);
}

void test_ack_malformed_and_partial_keep_timer(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls, &cfg, &st));
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_DTLS_PTO_INITIAL_MS, DtlsServer.n);
    uint8_t out[64];

    const uint8_t malformed[2] = {0x00, 0x10};
    TEST_ASSERT_EQUAL_INT(0, feed_epoch2_ack(&g_dtls, &st, 0, malformed, sizeof(malformed), out, sizeof(out)));
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_DTLS_PTO_INITIAL_MS, DtlsServer.n);

    DtlsRecordNumber one = {0, 0};
    uint8_t body[2 + 16];
    DtlsHandshake.ack_build_args.nums = &one;
    DtlsHandshake.ack_build_args.count = 1;
    DtlsHandshake.ack_build_args.out = body;
    DtlsHandshake.ack_build_args.out_cap = sizeof(body);
    DtlsHandshake.ack_build(dtls_handshake_work);
    size_t bl = DtlsHandshake.n;
    TEST_ASSERT_TRUE(bl > 0);
    TEST_ASSERT_EQUAL_INT(0, feed_epoch2_ack(&g_dtls, &st, 1, body, bl, out, sizeof(out)));
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_DTLS_PTO_INITIAL_MS, DtlsServer.n);
}

void test_ack_replay_and_late_ack_ignored(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls, &cfg, &st));
    uint8_t out[256];

    DtlsRecordNumber rns[5] = {{0, 0}, {2, 0}, {2, 1}, {2, 2}, {2, 3}};
    uint8_t body[2 + 5 * 16];
    DtlsHandshake.ack_build_args.nums = rns;
    DtlsHandshake.ack_build_args.count = 5;
    DtlsHandshake.ack_build_args.out = body;
    DtlsHandshake.ack_build_args.out_cap = sizeof(body);
    DtlsHandshake.ack_build(dtls_handshake_work);
    size_t bl = DtlsHandshake.n;
    TEST_ASSERT_TRUE(bl > 0);
    uint8_t rec[192];
    DtlsRecord.protect_args.keys = &st.cli_hs_write;
    DtlsRecord.protect_args.seq = 0;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_ACK;
    DtlsRecord.protect_args.plaintext = body;
    DtlsRecord.protect_args.pt_len = bl;
    DtlsRecord.protect_args.out = rec;
    DtlsRecord.protect_args.out_cap = sizeof(rec);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t rl = DtlsRecord.n;
    TEST_ASSERT_TRUE(rl > 0);
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);

    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);

    TEST_ASSERT_EQUAL_INT(0, feed_epoch2_ack(&g_dtls, &st, 1, body, bl, out, sizeof(out)));
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);

    TEST_ASSERT_TRUE(feed_client_finished(&g_dtls, &st, 2, out, sizeof(out)) > 0);
    DtlsServer.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);
}

void test_completion_ack_deferred_when_out_full(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls, &cfg, &st));

    uint8_t tiny[16];
    TEST_ASSERT_EQUAL_INT(0, feed_client_finished(&g_dtls, &st, 0, tiny, sizeof(tiny)));
    DtlsServer.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);

    uint8_t out[128];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = tiny;
    DtlsServer.process_args.len = 0;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    int n = DtlsServer.n;
    TEST_ASSERT_TRUE(n > 0);
    uint8_t pt[64];
    DtlsCiphertext info;
    DtlsRecord.unprotect_args.keys = &st.cli_app_read;
    DtlsRecord.unprotect_args.next_seq = 0;
    DtlsRecord.unprotect_args.rec = out;
    DtlsRecord.unprotect_args.rec_len = (size_t)n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DTLS_CT_ACK, info.content_type);
}

void test_forged_record_does_not_end_the_association(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);

    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls, &cfg, &st));
    uint8_t out[512];
    TEST_ASSERT_TRUE(feed_client_finished(&g_dtls, &st, 0, out, sizeof(out)) > 0);
    DtlsServer.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);

    uint8_t junk[48];
    memset(junk, 0xA5, sizeof(junk));
    junk[0] = 0x2F;
    junk[3] = 0x00;
    junk[4] = 0x2B;
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = junk;
    DtlsServer.process_args.len = sizeof(junk);
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
    DtlsServer.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);

    const uint8_t payload[5] = {'h', 'e', 'l', 'l', 'o'};
    uint8_t apprec[64];
    DtlsRecord.protect_args.keys = &st.cli_app_write;
    DtlsRecord.protect_args.seq = 0;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = payload;
    DtlsRecord.protect_args.pt_len = sizeof(payload);
    DtlsRecord.protect_args.out = apprec;
    DtlsRecord.protect_args.out_cap = sizeof(apprec);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t pl = DtlsRecord.n;
    TEST_ASSERT_TRUE(pl > 0);
    uint8_t plain[64];
    size_t plen = 0;
    DtlsServer.open_app_args.c = &g_dtls;
    DtlsServer.open_app_args.rec = apprec;
    DtlsServer.open_app_args.rec_len = pl;
    DtlsServer.open_app_args.out = plain;
    DtlsServer.open_app_args.out_cap = sizeof(plain);
    DtlsServer.open_app_args.out_len = &plen;
    DtlsServer.open_app(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), (uint32_t)plen);
    TEST_ASSERT_EQUAL_MEMORY(payload, plain, sizeof(payload));
}

void test_app_records_before_and_after_established(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    const uint8_t payload[5] = {'h', 'e', 'l', 'l', 'o'};
    uint8_t plain[64];
    size_t plen = 0;

    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);
    uint8_t dummy[32];
    memset(dummy, 0x2F, sizeof(dummy));
    DtlsServer.open_app_args.c = &g_dtls;
    DtlsServer.open_app_args.rec = dummy;
    DtlsServer.open_app_args.rec_len = sizeof(dummy);
    DtlsServer.open_app_args.out = plain;
    DtlsServer.open_app_args.out_cap = sizeof(plain);
    DtlsServer.open_app_args.out_len = &plen;
    DtlsServer.open_app(dtls_server_work);
    TEST_ASSERT_FALSE(DtlsServer.ok);
    DtlsServer.seal_app_args.c = &g_dtls;
    DtlsServer.seal_app_args.data = payload;
    DtlsServer.seal_app_args.len = sizeof(payload);
    DtlsServer.seal_app_args.out = plain;
    DtlsServer.seal_app_args.out_cap = sizeof(plain);
    DtlsServer.seal_app(dtls_server_work);
    size_t sealed = DtlsServer.n;
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)sealed);
    DtlsServer.app_write_keys_args.c = &g_dtls;
    DtlsServer.app_write_keys(dtls_server_work);
    TEST_ASSERT_NULL(DtlsServer.ptr);
    DtlsServer.app_read_keys_args.c = &g_dtls;
    DtlsServer.app_read_keys(dtls_server_work);
    TEST_ASSERT_NULL(DtlsServer.ptr);
    uint8_t cid_out[PROTOCORE_DTLS_CID_MAX];
    DtlsServer.local_cid_args.c = &g_dtls;
    DtlsServer.local_cid_args.out = cid_out;
    DtlsServer.local_cid(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)DtlsServer.n);

    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls2, &cfg, &st));
    uint8_t out[256];
    TEST_ASSERT_TRUE(feed_client_finished(&g_dtls2, &st, 0, out, sizeof(out)) > 0);
    DtlsServer.established_args.c = &g_dtls2;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);
    DtlsServer.app_write_keys_args.c = &g_dtls2;
    DtlsServer.app_write_keys(dtls_server_work);
    TEST_ASSERT_NOT_NULL(DtlsServer.ptr);

    uint8_t junk[48];
    memset(junk, 0xA5, sizeof(junk));
    junk[0] = 0x2F;
    junk[3] = 0x00;
    junk[4] = 0x2B;
    DtlsServer.open_app_args.c = &g_dtls2;
    DtlsServer.open_app_args.rec = junk;
    DtlsServer.open_app_args.rec_len = sizeof(junk);
    DtlsServer.open_app_args.out = plain;
    DtlsServer.open_app_args.out_cap = sizeof(plain);
    DtlsServer.open_app_args.out_len = &plen;
    DtlsServer.open_app(dtls_server_work);
    TEST_ASSERT_FALSE(DtlsServer.ok);

    uint8_t ackrec[64];
    DtlsRecord.protect_args.keys = &st.cli_app_write;
    DtlsRecord.protect_args.seq = 0;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_ACK;
    DtlsRecord.protect_args.plaintext = payload;
    DtlsRecord.protect_args.pt_len = sizeof(payload);
    DtlsRecord.protect_args.out = ackrec;
    DtlsRecord.protect_args.out_cap = sizeof(ackrec);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t al = DtlsRecord.n;
    TEST_ASSERT_TRUE(al > 0);
    DtlsServer.open_app_args.c = &g_dtls2;
    DtlsServer.open_app_args.rec = ackrec;
    DtlsServer.open_app_args.rec_len = al;
    DtlsServer.open_app_args.out = plain;
    DtlsServer.open_app_args.out_cap = sizeof(plain);
    DtlsServer.open_app_args.out_len = &plen;
    DtlsServer.open_app(dtls_server_work);
    TEST_ASSERT_FALSE(DtlsServer.ok);

    uint8_t apprec[64];
    DtlsRecord.protect_args.keys = &st.cli_app_write;
    DtlsRecord.protect_args.seq = 1;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = payload;
    DtlsRecord.protect_args.pt_len = sizeof(payload);
    DtlsRecord.protect_args.out = apprec;
    DtlsRecord.protect_args.out_cap = sizeof(apprec);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t pl = DtlsRecord.n;
    TEST_ASSERT_TRUE(pl > 0);
    DtlsServer.open_app_args.c = &g_dtls2;
    DtlsServer.open_app_args.rec = apprec;
    DtlsServer.open_app_args.rec_len = pl;
    DtlsServer.open_app_args.out = plain;
    DtlsServer.open_app_args.out_cap = sizeof(plain);
    DtlsServer.open_app_args.out_len = &plen;
    DtlsServer.open_app(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), (uint32_t)plen);
    TEST_ASSERT_EQUAL_MEMORY(payload, plain, sizeof(payload));
    DtlsServer.open_app_args.c = &g_dtls2;
    DtlsServer.open_app_args.rec = apprec;
    DtlsServer.open_app_args.rec_len = pl;
    DtlsServer.open_app_args.out = plain;
    DtlsServer.open_app_args.out_cap = sizeof(plain);
    DtlsServer.open_app_args.out_len = &plen;
    DtlsServer.open_app(dtls_server_work);
    TEST_ASSERT_FALSE(DtlsServer.ok);

    uint8_t srec[64];
    DtlsServer.seal_app_args.c = &g_dtls2;
    DtlsServer.seal_app_args.data = payload;
    DtlsServer.seal_app_args.len = sizeof(payload);
    DtlsServer.seal_app_args.out = srec;
    DtlsServer.seal_app_args.out_cap = sizeof(srec);
    DtlsServer.seal_app(dtls_server_work);
    size_t sl = DtlsServer.n;
    TEST_ASSERT_TRUE(sl > 0);
    DtlsCiphertext info;
    DtlsRecord.unprotect_args.keys = &st.cli_app_read;
    DtlsRecord.unprotect_args.next_seq = 1;
    DtlsRecord.unprotect_args.rec = srec;
    DtlsRecord.unprotect_args.rec_len = sl;
    DtlsRecord.unprotect_args.out = plain;
    DtlsRecord.unprotect_args.out_cap = sizeof(plain);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_DTLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), (uint32_t)info.pt_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, plain, sizeof(payload));
}

void test_conn_id_edge_cases(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t rec[320];
    uint8_t flight[2048];
    uint8_t cid_out[PROTOCORE_DTLS_CID_MAX];

    uint8_t big_cid[PROTOCORE_DTLS_CID_MAX + 1];
    memset(big_cid, 0xD1, sizeof(big_cid));
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);
    uint8_t ch_a[256];
    size_t la = build_client_hello_ex(ch_a, client_pub, PROTO_TRUE, NULL, 0, big_cid, sizeof(big_cid), PROTO_FALSE,
                                      TLS_GROUP_X25519, TLS_SIG_ED25519);
    size_t ra = plain_hs_record(rec, sizeof(rec), ch_a, la, 0, 0);
    TEST_ASSERT_TRUE(ra > 0);
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = ra;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    int fa = DtlsServer.n;
    TEST_ASSERT_TRUE(fa > 0);
    DtlsServer.local_cid_args.c = &g_dtls;
    DtlsServer.local_cid_args.out = cid_out;
    DtlsServer.local_cid(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)DtlsServer.n);
    DtlsPlaintext pt;
    DtlsRecord.plaintext_parse_args.rec = flight;
    DtlsRecord.plaintext_parse_args.rec_len = (size_t)fa;
    DtlsRecord.plaintext_parse_args.out = &pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    size_t shl = DtlsRecord.n;
    TEST_ASSERT_TRUE(shl > 0);
    TEST_ASSERT_TRUE((flight[shl] & 0x10) == 0);

    const uint8_t empty_cid[1] = {0};
    DtlsServer.init_args.c = &g_dtls2;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);
    uint8_t ch_b[256];
    size_t lb = build_client_hello_ex(ch_b, client_pub, PROTO_TRUE, NULL, 0, empty_cid, 0, PROTO_FALSE,
                                      TLS_GROUP_X25519, TLS_SIG_ED25519);
    size_t rb = plain_hs_record(rec, sizeof(rec), ch_b, lb, 0, 0);
    TEST_ASSERT_TRUE(rb > 0);
    DtlsServer.process_args.c = &g_dtls2;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rb;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    int fb = DtlsServer.n;
    TEST_ASSERT_TRUE(fb > 0);
    DtlsServer.local_cid_args.c = &g_dtls2;
    DtlsServer.local_cid_args.out = cid_out;
    DtlsServer.local_cid(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_DTLS_CONN_LOCAL_CID_LEN, (uint32_t)DtlsServer.n);
    TEST_ASSERT_EQUAL_MEMORY(SERVER_RANDOM, cid_out, PROTOCORE_DTLS_CONN_LOCAL_CID_LEN);
    DtlsRecord.plaintext_parse_args.rec = flight;
    DtlsRecord.plaintext_parse_args.rec_len = (size_t)fb;
    DtlsRecord.plaintext_parse_args.out = &pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    size_t shl_b = DtlsRecord.n;
    TEST_ASSERT_TRUE(shl_b > 0);
    TEST_ASSERT_TRUE((flight[shl_b] & 0x10) == 0);
}

static proto_bool hrr_roundtrip_accepted(const uint8_t *addr, size_t addr_len)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = addr;
    DtlsServer.init_args.peer_addr_len = addr_len;
    DtlsServer.init(dtls_server_work);

    uint8_t ch1[256];
    size_t ch1_len = build_client_hello_ex(ch1, client_pub, PROTO_FALSE, NULL, 0, NULL, 0, PROTO_FALSE,
                                           TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t rec[420];
    size_t r1 = plain_hs_record(rec, sizeof(rec), ch1, ch1_len, 0, 0);
    if (!r1)
    {
        return PROTO_FALSE;
    }
    uint8_t hrr_flight[512];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = r1;
    DtlsServer.process_args.out = hrr_flight;
    DtlsServer.process_args.out_cap = sizeof(hrr_flight);
    DtlsServer.process(dtls_server_work);
    int hf = DtlsServer.n;
    if (hf <= 0)
    {
        return PROTO_FALSE;
    }

    DtlsPlaintext pt;
    DtlsRecord.plaintext_parse_args.rec = hrr_flight;
    DtlsRecord.plaintext_parse_args.rec_len = (size_t)hf;
    DtlsRecord.plaintext_parse_args.out = &pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    if (!DtlsRecord.n)
    {
        return PROTO_FALSE;
    }
    uint8_t hrr[512];
    size_t hrr_len = frag_to_tls(pt.fragment, pt.frag_len, hrr);
    uint8_t cookie[PROTOCORE_DTLS_COOKIE_MAX];
    size_t cookie_len = 0;
    if (!hrr_len || !hrr_cookie(hrr, hrr_len, cookie, &cookie_len))
    {
        return PROTO_FALSE;
    }

    uint8_t ch2[320];
    size_t ch2_len = build_client_hello_ex(ch2, client_pub, PROTO_TRUE, cookie, cookie_len, NULL, 0, PROTO_FALSE,
                                           TLS_GROUP_X25519, TLS_SIG_ED25519);
    size_t r2 = plain_hs_record(rec, sizeof(rec), ch2, ch2_len, 1, 1);
    if (!r2)
    {
        return PROTO_FALSE;
    }
    uint8_t flight[2048];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = r2;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    return DtlsServer.n > 0;
}

void test_flight_fragments_to_the_pmtu(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    cfg.pmtu = 96;

    uint8_t *tr;
    uint8_t flight[2048];
    int fl = drive_server_flight(&g_dtls, &cfg, &tr, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fl > 0);

    TEST_ASSERT_TRUE(g_dtls.flight_count > 5);

    for (uint8_t i = 0; i < g_dtls.flight_count; i++)
    {
        TEST_ASSERT_TRUE(g_dtls.flight_msgs[i].len + PROTOCORE_DTLS_REC_OVERHEAD_MAX <= cfg.pmtu);
    }

    DtlsServerConfig wide;
    server_cfg(&wide, server_ed_pub);
    uint8_t *tr2;
    uint8_t flight2[2048];
    TEST_ASSERT_TRUE(drive_server_flight(&g_dtls2, &wide, &tr2, flight2, sizeof(flight2)) > 0);
    TEST_ASSERT_TRUE(g_dtls2.flight_count < g_dtls.flight_count);
}

void test_cookie_is_worthless_to_another_peer(void)
{
    static const uint8_t OTHER_ADDR[] = {10, 0, 0, 9, 0x30, 0x39};

    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);

    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = TEST_PEER_ADDR;
    DtlsServer.init_args.peer_addr_len = sizeof(TEST_PEER_ADDR);
    DtlsServer.init(dtls_server_work);
    uint8_t ch1[256];
    size_t ch1_len = build_client_hello_ex(ch1, client_pub, PROTO_FALSE, NULL, 0, NULL, 0, PROTO_FALSE,
                                           TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t rec[420];
    size_t r1 = plain_hs_record(rec, sizeof(rec), ch1, ch1_len, 0, 0);
    TEST_ASSERT_TRUE(r1 > 0);
    uint8_t hrr_flight[512];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = r1;
    DtlsServer.process_args.out = hrr_flight;
    DtlsServer.process_args.out_cap = sizeof(hrr_flight);
    DtlsServer.process(dtls_server_work);
    int hf = DtlsServer.n;
    TEST_ASSERT_TRUE(hf > 0);

    DtlsPlaintext pt;
    DtlsRecord.plaintext_parse_args.rec = hrr_flight;
    DtlsRecord.plaintext_parse_args.rec_len = (size_t)hf;
    DtlsRecord.plaintext_parse_args.out = &pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.n > 0);
    uint8_t hrr[512];
    size_t hrr_len = frag_to_tls(pt.fragment, pt.frag_len, hrr);
    TEST_ASSERT_TRUE(hrr_len > 0);
    uint8_t cookie[PROTOCORE_DTLS_COOKIE_MAX];
    size_t cookie_len = 0;
    TEST_ASSERT_TRUE(hrr_cookie(hrr, hrr_len, cookie, &cookie_len));

    uint8_t ch2[320];
    size_t ch2_len = build_client_hello_ex(ch2, client_pub, PROTO_TRUE, cookie, cookie_len, NULL, 0, PROTO_FALSE,
                                           TLS_GROUP_X25519, TLS_SIG_ED25519);
    size_t r2 = plain_hs_record(rec, sizeof(rec), ch2, ch2_len, 1, 1);
    TEST_ASSERT_TRUE(r2 > 0);

    uint8_t rec1[420];
    size_t r1b = plain_hs_record(rec1, sizeof(rec1), ch1, ch1_len, 0, 0);
    TEST_ASSERT_TRUE(r1b > 0);
    DtlsServer.init_args.c = &g_dtls2;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = OTHER_ADDR;
    DtlsServer.init_args.peer_addr_len = sizeof(OTHER_ADDR);
    DtlsServer.init(dtls_server_work);
    uint8_t flight[2048];
    DtlsServer.process_args.c = &g_dtls2;
    DtlsServer.process_args.dgram = rec1;
    DtlsServer.process_args.len = r1b;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.n > 0);

    DtlsServer.process_args.c = &g_dtls2;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = r2;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls2;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(47, DtlsServer.value);

    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = r2;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.n > 0);
}

void test_peer_addr_zero_length_and_clamped(void)
{

    TEST_ASSERT_TRUE(hrr_roundtrip_accepted(TEST_PEER_ADDR, 0));
    uint8_t big_addr[PROTOCORE_DTLS_PEER_ADDR_MAX + 14];
    for (size_t i = 0; i < sizeof(big_addr); i++)
    {
        big_addr[i] = (uint8_t)(0x40 + i);
    }
    TEST_ASSERT_TRUE(hrr_roundtrip_accepted(big_addr, sizeof(big_addr)));
}

void test_hrr_retry_without_keyshare_rejected(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = TEST_PEER_ADDR;
    DtlsServer.init_args.peer_addr_len = sizeof(TEST_PEER_ADDR);
    DtlsServer.init(dtls_server_work);

    uint8_t ch[256];
    size_t ch_len = build_client_hello_ex(ch, client_pub, PROTO_FALSE, NULL, 0, NULL, 0, PROTO_FALSE, TLS_GROUP_X25519,
                                          TLS_SIG_ED25519);
    uint8_t rec[320];
    size_t r1 = plain_hs_record(rec, sizeof(rec), ch, ch_len, 0, 0);
    TEST_ASSERT_TRUE(r1 > 0);
    uint8_t out[1024];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = r1;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.n > 0);

    size_t r2 = plain_hs_record(rec, sizeof(rec), ch, ch_len, 1, 1);
    TEST_ASSERT_TRUE(r2 > 0);
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = r2;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(40, DtlsServer.value);
}

void test_hrr_retry_with_corrupt_cookie_rejected(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = TEST_PEER_ADDR;
    DtlsServer.init_args.peer_addr_len = sizeof(TEST_PEER_ADDR);
    DtlsServer.init(dtls_server_work);

    uint8_t ch1[256];
    size_t ch1_len = build_client_hello_ex(ch1, client_pub, PROTO_FALSE, NULL, 0, NULL, 0, PROTO_FALSE,
                                           TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t rec[420];
    size_t r1 = plain_hs_record(rec, sizeof(rec), ch1, ch1_len, 0, 0);
    TEST_ASSERT_TRUE(r1 > 0);
    uint8_t hrr_flight[512];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = r1;
    DtlsServer.process_args.out = hrr_flight;
    DtlsServer.process_args.out_cap = sizeof(hrr_flight);
    DtlsServer.process(dtls_server_work);
    int hf = DtlsServer.n;
    TEST_ASSERT_TRUE(hf > 0);

    DtlsPlaintext pt;
    DtlsRecord.plaintext_parse_args.rec = hrr_flight;
    DtlsRecord.plaintext_parse_args.rec_len = (size_t)hf;
    DtlsRecord.plaintext_parse_args.out = &pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.n > 0);
    uint8_t hrr[512];
    size_t hrr_len = frag_to_tls(pt.fragment, pt.frag_len, hrr);
    TEST_ASSERT_TRUE(hrr_len > 0);
    uint8_t cookie[PROTOCORE_DTLS_COOKIE_MAX];
    size_t cookie_len = 0;
    TEST_ASSERT_TRUE(hrr_cookie(hrr, hrr_len, cookie, &cookie_len));
    TEST_ASSERT_TRUE(cookie_len > 0);
    cookie[cookie_len - 1] ^= 0xFF;

    uint8_t ch2[320];
    size_t ch2_len = build_client_hello_ex(ch2, client_pub, PROTO_TRUE, cookie, cookie_len, NULL, 0, PROTO_FALSE,
                                           TLS_GROUP_X25519, TLS_SIG_ED25519);
    size_t r2 = plain_hs_record(rec, sizeof(rec), ch2, ch2_len, 1, 1);
    TEST_ASSERT_TRUE(r2 > 0);
    uint8_t out[2048];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = r2;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(47, DtlsServer.value);
}

void test_non_finished_message_after_done_rejected(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls, &cfg, &st));
    uint8_t out[256];
    TEST_ASSERT_TRUE(feed_client_finished(&g_dtls, &st, 0, out, sizeof(out)) > 0);
    DtlsServer.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);

    uint8_t stray[14];
    stray[0] = TLS_HS_CLIENT_HELLO;
    stray[1] = 0;
    stray[2] = 0;
    stray[3] = 10;
    memset(stray + 4, 0x33, 10);
    TEST_ASSERT_EQUAL_INT(-1, feed_epoch2_msg(&g_dtls, &st, 1, 1, stray, sizeof(stray), out, sizeof(out)));
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(10, DtlsServer.value);
}

void test_epoch2_other_content_type_ignored(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls, &cfg, &st));

    const uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t rec[128];
    DtlsRecord.protect_args.keys = &st.cli_hs_write;
    DtlsRecord.protect_args.seq = 0;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = payload;
    DtlsRecord.protect_args.pt_len = sizeof(payload);
    DtlsRecord.protect_args.out = rec;
    DtlsRecord.protect_args.out_cap = sizeof(rec);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t rl = DtlsRecord.n;
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[128];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);
    DtlsServer.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_FALSE(DtlsServer.ok);
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_DTLS_PTO_INITIAL_MS, DtlsServer.n);

    TEST_ASSERT_TRUE(feed_client_finished(&g_dtls, &st, 1, out, sizeof(out)) > 0);
    DtlsServer.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);
}

void test_timer_stopped_by_done_state(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    ClientSession st;
    uint8_t out[512];
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls, &cfg, &st));
    TEST_ASSERT_TRUE(feed_client_finished(&g_dtls, &st, 0, out, sizeof(out)) > 0);
    DtlsServer.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);

    g_dtls.awaiting_reply = PROTO_TRUE;
    advance_ms(PROTOCORE_DTLS_PTO_MAX_MS);
    DtlsServer.timeout_ms_args.c = &g_dtls;
    DtlsServer.timeout_ms(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(-1, DtlsServer.n);
    DtlsServer.on_timeout_args.c = &g_dtls;
    DtlsServer.on_timeout_args.out = out;
    DtlsServer.on_timeout_args.out_cap = sizeof(out);
    DtlsServer.on_timeout(dtls_server_work);
    TEST_ASSERT_EQUAL_INT(0, DtlsServer.n);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8(0, DtlsServer.value);
}

void test_established_requires_app_keys(void)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    ClientSession st;
    uint8_t out[512];
    TEST_ASSERT_TRUE(run_to_finished(&g_dtls, &cfg, &st));
    TEST_ASSERT_TRUE(feed_client_finished(&g_dtls, &st, 0, out, sizeof(out)) > 0);
    DtlsServer.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.ok);

    g_dtls.ep3_ready = PROTO_FALSE;
    DtlsServer.established_args.c = &g_dtls;
    DtlsServer.established(dtls_server_work);
    TEST_ASSERT_FALSE(DtlsServer.ok);
    DtlsServer.app_write_keys_args.c = &g_dtls;
    DtlsServer.app_write_keys(dtls_server_work);
    TEST_ASSERT_NULL(DtlsServer.ptr);
    DtlsServer.app_read_keys_args.c = &g_dtls;
    DtlsServer.app_read_keys(dtls_server_work);
    TEST_ASSERT_NULL(DtlsServer.ptr);

    uint8_t app[64];
    size_t app_len = 0;
    DtlsServer.open_app_args.c = &g_dtls;
    DtlsServer.open_app_args.rec = out;
    DtlsServer.open_app_args.rec_len = 16;
    DtlsServer.open_app_args.out = app;
    DtlsServer.open_app_args.out_cap = sizeof(app);
    DtlsServer.open_app_args.out_len = &app_len;
    DtlsServer.open_app(dtls_server_work);
    TEST_ASSERT_FALSE(DtlsServer.ok);
}

void test_local_cid_requires_nonempty_id(void)
{
    uint8_t client_pub[32];
    Curve25519.x25519_base_args.out = client_pub;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(tw);
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);

    const uint8_t peer_cid[2] = {0x77, 0x88};
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);
    uint8_t ch[256];
    size_t chl = build_client_hello_ex(ch, client_pub, PROTO_TRUE, NULL, 0, peer_cid, sizeof(peer_cid), PROTO_FALSE,
                                       TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t rec[320];
    size_t rl = plain_hs_record(rec, sizeof(rec), ch, chl, 0, 0);
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t flight[2048];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = rec;
    DtlsServer.process_args.len = rl;
    DtlsServer.process_args.out = flight;
    DtlsServer.process_args.out_cap = sizeof(flight);
    DtlsServer.process(dtls_server_work);
    TEST_ASSERT_TRUE(DtlsServer.n > 0);
    TEST_ASSERT_TRUE(g_dtls.cid_negotiated);

    uint8_t cid_out[PROTOCORE_DTLS_CID_MAX];
    DtlsServer.local_cid_args.c = &g_dtls;
    DtlsServer.local_cid_args.out = cid_out;
    DtlsServer.local_cid(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_DTLS_CONN_LOCAL_CID_LEN, (uint32_t)DtlsServer.n);

    g_dtls.local_cid_len = 0;
    memset(cid_out, 0xEE, sizeof(cid_out));
    DtlsServer.local_cid_args.c = &g_dtls;
    DtlsServer.local_cid_args.out = cid_out;
    DtlsServer.local_cid(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)DtlsServer.n);
    TEST_ASSERT_EQUAL_UINT8(0xEE, cid_out[0]);
}

static void low_order_share_is_refused(const uint8_t client_pub[32], const char *what)
{
    uint8_t server_ed_pub[32];
    Ed25519.pubkey_args.pub = server_ed_pub;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(tw);

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsServer.init_args.c = &g_dtls;
    DtlsServer.init_args.cfg = &cfg;
    DtlsServer.init_args.peer_addr = NULL;
    DtlsServer.init_args.peer_addr_len = 0;
    DtlsServer.init(dtls_server_work);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);

    uint8_t ch_frag[300];
    DtlsHandshake.frag_build_args.msg_type = ch[0];
    DtlsHandshake.frag_build_args.msg_seq = 0;
    DtlsHandshake.frag_build_args.full_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.frag_offset = 0;
    DtlsHandshake.frag_build_args.frag = ch + 4;
    DtlsHandshake.frag_build_args.frag_len = (uint32_t)(ch_len - 4);
    DtlsHandshake.frag_build_args.out = ch_frag;
    DtlsHandshake.frag_build_args.out_cap = sizeof(ch_frag);
    DtlsHandshake.frag_build(dtls_handshake_work);
    size_t ch_fl = DtlsHandshake.n;
    uint8_t ch_rec[320];
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = ch_frag;
    DtlsRecord.plaintext_build_args.frag_len = ch_fl;
    DtlsRecord.plaintext_build_args.out = ch_rec;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(ch_rec);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t ch_rl = DtlsRecord.n;

    uint8_t out[2048];
    DtlsServer.process_args.c = &g_dtls;
    DtlsServer.process_args.dgram = ch_rec;
    DtlsServer.process_args.len = ch_rl;
    DtlsServer.process_args.out = out;
    DtlsServer.process_args.out_cap = sizeof(out);
    DtlsServer.process(dtls_server_work);
    int rc = DtlsServer.n;
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, rc, what);
    DtlsServer.alert_args.c = &g_dtls;
    DtlsServer.alert(dtls_server_work);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(47, DtlsServer.value, what);
}

void test_low_order_keyshare_all_zero_is_refused(void)
{
    uint8_t pub[32];
    memset(pub, 0, sizeof(pub));
    low_order_share_is_refused(pub, "all-zero key share");
}

void test_low_order_keyshare_one_is_refused(void)
{
    uint8_t pub[32];
    memset(pub, 0, sizeof(pub));
    pub[0] = 0x01;
    low_order_share_is_refused(pub, "key share u=1");
}
