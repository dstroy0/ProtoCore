// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// CoAP over DTLS (coaps.h) end-to-end. An in-test DTLS 1.3 client completes the handshake against
// pc_dtls_conn, then sends a CoAP GET inside a DTLS application record; pc_coaps_process decrypts it, runs
// pc_coap_server_process against a registered resource, and returns the CoAP response in a DTLS record
// that the client decrypts and checks. A byte off anywhere - transcript, keys, epoch, CoAP encoding -
// would fail the AEAD open or the response check.

#include "crypto/aead/aes128gcm.h"
#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ed25519.h"
#include "crypto/cipher/aes_block.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/presentation/security/dtls/dtls_conn.h"
#include "network_drivers/presentation/security/dtls/dtls_handshake.h"
#include "network_drivers/presentation/security/dtls/dtls_record.h"
#include "network_drivers/tls/tls13_kdf.h"
#include "services/iot/coap/coap.h"
#include "services/iot/coap/coaps.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

// ---- fixed test key material (deterministic; matches test_dtls_conn) ----
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

// A GET /temp resource that replies 2.05 Content "hi".
static void h_temp(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    resp->code = (uint8_t)COAP_RSP_CONTENT;
    memcpy(resp->payload, "hi", 2);
    resp->payload_len = 2;
    resp->content_format = COAP_CF_TEXT;
}

void setUp()
{
    pc_coap_server_reset();
    pc_coap_server_add_resource("/temp", COAP_ALLOW_GET, h_temp);
}
void tearDown()
{
}

// ---- minimal DTLS 1.3 client helpers (offering X25519 directly, no HRR) ----
struct Buf
{
    uint8_t *p;
    size_t n;
};
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

static size_t build_client_hello(uint8_t *out, const uint8_t client_pub[32])
{
    Buf b = {out, 0};
    b8(&b, 0x01); // client_hello
    size_t len_at = b.n;
    b8(&b, 0);
    b8(&b, 0);
    b8(&b, 0);       // 24-bit body length (patched)
    b16(&b, 0x0303); // legacy_version
    for (int i = 0; i < 32; i++)
    {
        b8(&b, (uint8_t)(0x30 + i)); // random
    }
    b8(&b, 0x00); // session_id: empty
    b8(&b, 0x00); // legacy_cookie: empty (DTLS)
    b16(&b, 0x0002);
    b16(&b, 0x1301); // cipher_suites: TLS_AES_128_GCM_SHA256
    b8(&b, 0x01);
    b8(&b, 0x00); // compression: null
    size_t ext_len_at = b.n;
    b16(&b, 0);
    b16(&b, 0x002b); // supported_versions: DTLS 1.3
    b16(&b, 0x0003);
    b8(&b, 0x02);
    b16(&b, 0xFEFC);
    b16(&b, 0x000a); // supported_groups: x25519
    b16(&b, 0x0004);
    b16(&b, 0x0002);
    b16(&b, 0x001d);
    b16(&b, 0x000d); // signature_algorithms: ed25519
    b16(&b, 0x0004);
    b16(&b, 0x0002);
    b16(&b, 0x0807);
    b16(&b, 0x0033); // key_share: x25519
    b16(&b, 0x0026);
    b16(&b, 0x0024);
    b16(&b, 0x001d);
    b16(&b, 0x0020);
    bmem(&b, client_pub, 32);
    uint16_t ext_len = (uint16_t)(b.n - ext_len_at - 2);
    out[ext_len_at] = (uint8_t)(ext_len >> 8);
    out[ext_len_at + 1] = (uint8_t)ext_len;
    uint32_t body = (uint32_t)(b.n - len_at - 3);
    out[len_at] = (uint8_t)(body >> 16);
    out[len_at + 1] = (uint8_t)(body >> 8);
    out[len_at + 2] = (uint8_t)body;
    return b.n;
}

static bool sh_keyshare(const uint8_t *sh, size_t len, uint8_t pub[32])
{
    if (len < 44)
    {
        return false;
    }
    size_t o = 4 + 2 + 32;
    uint8_t sid = sh[o++];
    o += sid;
    o += 2 + 1;
    if (o + 2 > len)
    {
        return false;
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
            return true;
        }
        o += el;
    }
    return false;
}

static size_t frag_to_tls(const uint8_t *payload, size_t plen, uint8_t *tls_out)
{
    DtlsHsHeader hh;
    if (!pc_dtls_hs_header_parse(payload, plen, &hh) || hh.frag_offset != 0 || hh.frag_length != hh.length)
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

static size_t ct_record_len(const uint8_t *rec, size_t avail)
{
    size_t seq_len = (rec[0] & 0x08) ? 2 : 1;
    size_t o = 1 + seq_len + 2;
    size_t enc = ((size_t)rec[1 + seq_len] << 8) | rec[1 + seq_len + 1];
    return (o + enc <= avail) ? o + enc : 0;
}

// Complete the handshake from the client side and hand back the client's application-traffic keys.
static void handshake(DtlsConn *conn, DtlsRecordKeys *cli_app_write, DtlsRecordKeys *cli_app_read)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);

    DtlsServerConfig cfg;
    cfg.cert_der = server_ed_pub;
    cfg.cert_len = 32;
    cfg.ed25519_seed = SERVER_ED_SEED;
    cfg.ephemeral_priv = SERVER_X25519_PRIV;
    cfg.server_random = SERVER_RANDOM;
    cfg.cookie_key = SERVER_COOKIE_KEY;
    pc_dtls_conn_init(conn, &cfg, NULL, 0);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    pc_sha256_ctx tr;
    pc_sha256_init(&tr);
    pc_sha256_update(&tr, ch, ch_len);
    uint8_t ch_frag[300];
    size_t ch_fl = pc_dtls_hs_frag_build(ch[0], 0, (uint32_t)(ch_len - 4), 0, ch + 4, (uint32_t)(ch_len - 4), ch_frag,
                                         sizeof(ch_frag));
    uint8_t ch_rec[320];
    size_t ch_rl = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, ch_frag, ch_fl, ch_rec, sizeof(ch_rec));

    uint8_t flight[2048];
    int fl = pc_dtls_conn_process(conn, ch_rec, ch_rl, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fl > 0);

    size_t off = 0;
    DtlsPlaintext pt;
    size_t rl = pc_dtls_plaintext_parse(flight, (size_t)fl, &pt);
    TEST_ASSERT_TRUE(rl > 0);
    off += rl;
    uint8_t sh[512];
    size_t sh_len = frag_to_tls(pt.fragment, pt.frag_len, sh);
    TEST_ASSERT_TRUE(sh_len > 0);
    pc_sha256_update(&tr, sh, sh_len);
    uint8_t server_pub[32];
    TEST_ASSERT_TRUE(sh_keyshare(sh, sh_len, server_pub));

    uint8_t ecdhe[32];
    pc_x25519(ecdhe, CLIENT_X25519_PRIV, server_pub);
    Tls13KeySchedule cks;
    uint8_t h[32];
    pc_sha256_ctx tmp = tr;
    pc_sha256_final(&tmp, h);
    pc_tls13_ks_early(&DTLS13_KDF, &cks);
    pc_tls13_ks_handshake(&cks, ecdhe, h, 32);
    DtlsRecordKeys srv_read;
    pc_dtls_record_keys_derive(&srv_read, AES_128_GCM_SHA256, 2, cks.server_hs_traffic);

    uint64_t exp_seq = 0;
    while (off < (size_t)fl)
    {
        size_t crl = ct_record_len(flight + off, (size_t)fl - off);
        TEST_ASSERT_TRUE(crl > 0);
        uint8_t inner[512];
        DtlsCiphertext info;
        TEST_ASSERT_TRUE(
            pc_dtls_ciphertext_unprotect(&srv_read, exp_seq, flight + off, crl, inner, sizeof(inner), &info));
        exp_seq = info.seq + 1;
        off += crl;
        uint8_t msg[512];
        size_t mlen = frag_to_tls(inner, info.pt_len, msg);
        TEST_ASSERT_TRUE(mlen > 0);
        pc_sha256_update(&tr, msg, mlen);
    }

    uint8_t h_sfin[32];
    pc_sha256_ctx s2 = tr;
    pc_sha256_final(&s2, h_sfin);
    pc_tls13_ks_master(&cks, h_sfin);
    uint8_t cfin_verify[32];
    pc_tls13_finished_mac(&DTLS13_KDF, cks.client_hs_traffic, h_sfin, cfin_verify);
    uint8_t cfin[64];
    size_t cfin_len = pc_tls13_build_finished(cfin, sizeof(cfin), cfin_verify);
    DtlsRecordKeys cli_write;
    pc_dtls_record_keys_derive(&cli_write, AES_128_GCM_SHA256, 2, cks.client_hs_traffic);
    uint8_t cfin_frag[80];
    size_t cff = pc_dtls_hs_frag_build(cfin[0], 1, (uint32_t)(cfin_len - 4), 0, cfin + 4, (uint32_t)(cfin_len - 4),
                                       cfin_frag, sizeof(cfin_frag));
    uint8_t cfin_rec[128];
    size_t cfr =
        pc_dtls_ciphertext_protect(&cli_write, 0, PC_DTLS_CT_HANDSHAKE, cfin_frag, cff, cfin_rec, sizeof(cfin_rec));
    uint8_t out2[64];
    TEST_ASSERT_TRUE(pc_dtls_conn_process(conn, cfin_rec, cfr, out2, sizeof(out2)) > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_established(conn));

    pc_dtls_record_keys_derive(cli_app_read, AES_128_GCM_SHA256, 3, cks.server_ap_traffic);
    pc_dtls_record_keys_derive(cli_app_write, AES_128_GCM_SHA256, 3, cks.client_ap_traffic);
}

static void test_coap_over_dtls(void)
{
    DtlsConn conn;
    DtlsRecordKeys cli_app_write, cli_app_read;
    handshake(&conn, &cli_app_write, &cli_app_read);

    // A CoAP CON GET /temp (Ver 1, TKL 0, MID 0x1234; Uri-Path option 11 = "temp").
    const uint8_t coap_get[] = {0x40, 0x01, 0x12, 0x34, 0xB4, 't', 'e', 'm', 'p'};
    uint8_t app_rec[128];
    size_t ar = pc_dtls_ciphertext_protect(&cli_app_write, 0, PC_DTLS_CT_APPLICATION_DATA, coap_get, sizeof(coap_get),
                                           app_rec, sizeof(app_rec));
    TEST_ASSERT_TRUE(ar > 0);

    uint8_t out[256];
    int on = pc_coaps_process(&conn, app_rec, ar, out, sizeof(out));
    TEST_ASSERT_TRUE(on > 0); // a DTLS-wrapped CoAP response came back

    // Decrypt the response: an epoch-3 application record carrying the CoAP answer.
    uint8_t coap_resp[256];
    DtlsCiphertext info;
    TEST_ASSERT_TRUE(
        pc_dtls_ciphertext_unprotect(&cli_app_read, 1, out, (size_t)on, coap_resp, sizeof(coap_resp), &info));
    TEST_ASSERT_EQUAL_UINT8(PC_DTLS_CT_APPLICATION_DATA, info.content_type);

    // CoAP response: piggybacked ACK, code 2.05 Content (0x45), MID echoed, token echoed (none),
    // then payload "hi" after the 0xFF payload marker.
    TEST_ASSERT_TRUE(info.pt_len >= 6);
    TEST_ASSERT_EQUAL_UINT8(0x60, coap_resp[0] & 0xF0); // Ver 1, Type ACK (2)
    TEST_ASSERT_EQUAL_UINT8(0x45, coap_resp[1]);        // 2.05 Content
    TEST_ASSERT_EQUAL_UINT8(0x12, coap_resp[2]);        // MID hi
    TEST_ASSERT_EQUAL_UINT8(0x34, coap_resp[3]);        // MID lo
    // The last two payload bytes are "hi".
    TEST_ASSERT_EQUAL_MEMORY("hi", coap_resp + info.pt_len - 2, 2);
}

// A replayed application record is dropped (RFC 9147 §4.5.1): re-feeding the same record yields no
// second response.
static void test_coap_over_dtls_replay_dropped(void)
{
    DtlsConn conn;
    DtlsRecordKeys cli_app_write, cli_app_read;
    handshake(&conn, &cli_app_write, &cli_app_read);

    const uint8_t coap_get[] = {0x40, 0x01, 0x12, 0x34, 0xB4, 't', 'e', 'm', 'p'};
    uint8_t app_rec[128];
    size_t ar = pc_dtls_ciphertext_protect(&cli_app_write, 0, PC_DTLS_CT_APPLICATION_DATA, coap_get, sizeof(coap_get),
                                           app_rec, sizeof(app_rec));
    uint8_t out[256];
    TEST_ASSERT_TRUE(pc_coaps_process(&conn, app_rec, ar, out, sizeof(out)) > 0);     // first: answered
    TEST_ASSERT_EQUAL_INT(0, pc_coaps_process(&conn, app_rec, ar, out, sizeof(out))); // replay: dropped
}

// An established connection whose decrypted CoAP message yields no response drives the pc_resp_len == 0
// path: pc_coaps_process must return 0 without sealing a record. A CoAP ACK (not a request, RFC 7252
// §4.2) is ignored by pc_coap_server_process, so it produces zero response bytes.
static void test_coaps_no_coap_response(void)
{
    DtlsConn conn;
    DtlsRecordKeys cli_app_write, cli_app_read;
    handshake(&conn, &cli_app_write, &cli_app_read);

    const uint8_t coap_ack[] = {0x60, 0x00, 0x12, 0x34}; // Ver 1, Type ACK (2), TKL 0, MID 0x1234
    uint8_t app_rec[128];
    size_t ar = pc_dtls_ciphertext_protect(&cli_app_write, 0, PC_DTLS_CT_APPLICATION_DATA, coap_ack, sizeof(coap_ack),
                                           app_rec, sizeof(app_rec));
    TEST_ASSERT_TRUE(ar > 0);

    uint8_t out[256];
    TEST_ASSERT_EQUAL_INT(0, pc_coaps_process(&conn, app_rec, ar, out, sizeof(out)));
}

// After establishment a datagram that is not an epoch-3 application record is routed back to the DTLS
// state machine (pc_coaps_process's fall-through return pc_dtls_conn_process). A zero-length datagram (the
// length guard fails first) and a DTLSPlaintext-content-type byte (not a 0b001xxxxx ciphertext header)
// both take that path and, being nothing the established machine needs to answer, produce no output.
static void test_coaps_non_app_record(void)
{
    DtlsConn conn;
    DtlsRecordKeys cli_app_write, cli_app_read;
    handshake(&conn, &cli_app_write, &cli_app_read);

    uint8_t out[256];
    uint8_t byte[1] = {0x16}; // 0x16: a DTLSPlaintext content-type, not a ciphertext unified header
    TEST_ASSERT_EQUAL_INT(0, pc_coaps_process(&conn, byte, 0, out, sizeof(out))); // len < 1
    TEST_ASSERT_EQUAL_INT(0, pc_coaps_process(&conn, byte, 1, out, sizeof(out))); // not (b0 & 0xE0) == 0x20
    TEST_ASSERT_TRUE(pc_dtls_conn_established(&conn));                            // neither disturbed the connection
}

// A DTLSCiphertext record whose epoch is not 3 (0b001xxx with epoch bits != 3) is also routed to the
// state machine. Here its body is garbage, so the record fails to open and the machine reports a fatal
// error (-1), which pc_coaps_process passes through. Covers the epoch (low-two-bits) side of the record test.
static void test_coaps_wrong_epoch_record(void)
{
    DtlsConn conn;
    DtlsRecordKeys cli_app_write, cli_app_read;
    handshake(&conn, &cli_app_write, &cli_app_read);

    uint8_t rec[24];
    memset(rec, 0, sizeof(rec));
    rec[0] = 0x22; // (0x22 & 0xE0) == 0x20 (ciphertext), (0x22 & 0x03) == 2 (epoch 2, not 3)
    uint8_t out[64];
    TEST_ASSERT_EQUAL_INT(-1, pc_coaps_process(&conn, rec, sizeof(rec), out, sizeof(out)));
}

// Before establishment pc_coaps_process forwards the datagram straight to the DTLS handshake state
// machine (the !pc_dtls_conn_established branch). Driving the ClientHello through pc_coaps_process must emit
// the server's flight, exactly as feeding pc_dtls_conn_process directly does.
static void test_coaps_forwards_handshake(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);

    DtlsServerConfig cfg;
    cfg.cert_der = server_ed_pub;
    cfg.cert_len = 32;
    cfg.ed25519_seed = SERVER_ED_SEED;
    cfg.ephemeral_priv = SERVER_X25519_PRIV;
    cfg.server_random = SERVER_RANDOM;
    cfg.cookie_key = SERVER_COOKIE_KEY;
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, NULL, 0);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t ch_frag[300];
    size_t ch_fl = pc_dtls_hs_frag_build(ch[0], 0, (uint32_t)(ch_len - 4), 0, ch + 4, (uint32_t)(ch_len - 4), ch_frag,
                                         sizeof(ch_frag));
    uint8_t ch_rec[320];
    size_t ch_rl = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, ch_frag, ch_fl, ch_rec, sizeof(ch_rec));

    TEST_ASSERT_FALSE(pc_dtls_conn_established(&conn));
    uint8_t flight[2048];
    int fl = pc_coaps_process(&conn, ch_rec, ch_rl, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fl > 0); // the server flight was produced via the handshake-forward path
}

// pc_aes128gcm_open's ct_len < PC_AES128GCM_TAG_LEN guard (RFC 5116 sec 5.1): a ciphertext shorter
// than the 16-byte tag can never carry a valid tag, so open() must reject it before touching AES/GHASH
// at all (ct/out are never dereferenced on this path, so NULL is safe for the zero-length case).
static void test_quic_aead_open_rejects_short_ciphertext(void)
{
    uint8_t key[16] = {0};
    uint8_t nonce[12] = {0};
    uint8_t out[16];
    TEST_ASSERT_FALSE(pc_aes128gcm_open(key, nonce, NULL, 0, NULL, 0, out));
    uint8_t short_ct[PC_AES128GCM_TAG_LEN - 1] = {0};
    TEST_ASSERT_FALSE(pc_aes128gcm_open(key, nonce, NULL, 0, short_ct, sizeof(short_ct), out));
}

// aes_block.h's key schedule is shared by AES-128 (nk=4, used here by QUIC/DTLS) and AES-256 (nk=8, used
// by the SSH ciphers). This build only ever instantiates it with nk=4, so the nk>6 mid-word-SubWord step
// (FIPS 197 sec 5.2) never runs here. Call the shared primitive directly with nk=8/nr=14 against the
// FIPS-197 Appendix C.3 AES-256 KAT to cover that step from this env too.
static void test_aes256_key_expand_kat(void)
{
    static const uint8_t key[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    static const uint8_t pt[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                   0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    static const uint8_t expect_ct[16] = {0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
                                          0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89};
    uint32_t rk[60];
    pc_aes_key_expand(key, 8, rk);
    uint8_t ct[16];
    pc_aes_encrypt_block(rk, 14, pt, ct);
    TEST_ASSERT_EQUAL_MEMORY(expect_ct, ct, 16);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_coap_over_dtls);
    RUN_TEST(test_coap_over_dtls_replay_dropped);
    RUN_TEST(test_coaps_no_coap_response);
    RUN_TEST(test_coaps_non_app_record);
    RUN_TEST(test_coaps_wrong_epoch_record);
    RUN_TEST(test_coaps_forwards_handshake);
    RUN_TEST(test_quic_aead_open_rejects_short_ciphertext);
    RUN_TEST(test_aes256_key_expand_kat);
    return UNITY_END();
}
