// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// DTLS 1.3 server handshake (RFC 9147 §5-6). A self-consistent proof: the test plays a minimal DTLS
// 1.3 client against pc_dtls_conn - it builds a ClientHello, drives the server, then runs the SAME TLS
// 1.3 key schedule to deprotect the server flight, VERIFIES the server's CertificateVerify signature
// and Finished MAC over the real transcript, sends its own Finished, and confirms both sides install
// identical application-traffic keys. If any transcript byte, epoch, nonce, or key were wrong the
// AEAD open, the signature check, or the key comparison would fail. A second case drives the
// HelloRetryRequest path (RFC 9147 §5.1): a ClientHello with no X25519 key_share triggers an HRR with
// a cookie, and the retry that echoes it completes the same full handshake.

#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ed25519.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/presentation/security/dtls/dtls_conn.h"
#include "network_drivers/presentation/security/dtls/dtls_handshake.h"
#include "network_drivers/presentation/security/dtls/dtls_record.h"
#include "network_drivers/tls/tls13_kdf.h"
#include "server/clock/clock.h"
#include <stdint.h>

#include <unity.h>

// Two keyed AEAD contexts agree iff they seal the same input identically.
static void assert_ctx_match(uint8_t *a, uint8_t *b)
{
    uint8_t n12[12] = {0}, zpt[16] = {0}, c1[16], t1[16], c2[16], t2[16];
    pc_aes128gcm_seal((struct pc_aes128gcm_key *)(a), n12, NULL, 0, zpt, sizeof zpt, c1, t1);
    pc_aes128gcm_seal((struct pc_aes128gcm_key *)(b), n12, NULL, 0, zpt, sizeof zpt, c2, t2);
    TEST_ASSERT_EQUAL_MEMORY(c1, c2, sizeof c1);
    TEST_ASSERT_EQUAL_MEMORY(t1, t2, sizeof t1);
}

// Controllable clock so the retransmission-timer tests can advance time deterministically.
static uint32_t g_ms = 0;
static uint32_t test_clock()
{
    return g_ms;
}

void setUp()
{
    g_ms = 0;
    pc_set_clock(test_clock, 1000); // 1000 ticks/s -> pc_millis() == g_ms
}
void tearDown()
{
}

// ---- fixed test key material (deterministic) ----
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
static const uint8_t TEST_PEER_ADDR[6] = {192, 168, 1, 50, 0xC3, 0x50}; // IPv4 192.168.1.50 : port 50000

// ---- a tiny byte writer ----
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

// Build a ClientHello (TLS message, 4-byte header + body) offering TLS 1.3 / X25519 / Ed25519. When
// @p with_keyshare is true it carries an X25519 key_share for @p client_pub; otherwise it advertises
// X25519 in supported_groups but sends no share (the HelloRetryRequest trigger). A non-NULL @p cookie
// is echoed in a cookie extension (RFC 8446 §4.2.2), as a client does on its post-HRR retry.
// @p group / @p sigalg override the single advertised supported_group / signature_algorithm, so a
// ClientHello that offers neither of the server's one profile can be built (the key_share entry, when
// present, always stays X25519 - the server checks the offered algorithms before the share).
static size_t build_client_hello_ex(uint8_t *out, const uint8_t client_pub[32], proto_bool with_keyshare,
                                    const uint8_t *cookie, size_t cookie_len, const uint8_t *cid, size_t cid_len,
                                    proto_bool offer_rpk, uint16_t group, uint16_t sigalg)
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
    b8(&b, 0x00); // legacy_cookie: empty (DTLS ClientHello, RFC 9147 §5.3)
    b16(&b, 0x0002);
    b16(&b, 0x1301); // cipher_suites: TLS_AES_128_GCM_SHA256
    b8(&b, 0x01);
    b8(&b, 0x00); // compression: null
    size_t ext_len_at = b.n;
    b16(&b, 0); // extensions length (patched)
    // supported_versions: DTLS 1.3 (0xFEFC), RFC 9147
    b16(&b, 0x002b);
    b16(&b, 0x0003);
    b8(&b, 0x02);
    b16(&b, 0xFEFC);
    // supported_groups: x25519 unless overridden
    b16(&b, 0x000a);
    b16(&b, 0x0004);
    b16(&b, 0x0002);
    b16(&b, group);
    // signature_algorithms: ed25519 unless overridden
    b16(&b, 0x000d);
    b16(&b, 0x0004);
    b16(&b, 0x0002);
    b16(&b, sigalg);
    if (with_keyshare)
    {
        // key_share: x25519 entry
        b16(&b, 0x0033);
        b16(&b, 0x0026); // ext body: client_shares(2) + entry(2+2+32)
        b16(&b, 0x0024); // client_shares length
        b16(&b, 0x001d); // group x25519
        b16(&b, 0x0020); // key length 32
        bmem(&b, client_pub, 32);
    }
    if (cookie && cookie_len)
    {
        // cookie { opaque cookie<1..2^16-1> } (RFC 8446 §4.2.2)
        b16(&b, 0x002c);
        b16(&b, (uint16_t)(cookie_len + 2));
        b16(&b, (uint16_t)cookie_len);
        bmem(&b, cookie, cookie_len);
    }
    if (cid)
    {
        // connection_id { opaque cid<0..2^8-1> } (RFC 9146 §3): the CID the server must place in records
        // it sends to this client.
        b16(&b, 0x0036);
        b16(&b, (uint16_t)(1 + cid_len));
        b8(&b, (uint8_t)cid_len);
        bmem(&b, cid, cid_len);
    }
    if (offer_rpk)
    {
        // server_certificate_type (RFC 7250, IANA 20): the client accepts an X.509 or RawPublicKey server
        // credential. CertificateType certificate_types<1..2^8-1> = [X509(0), RawPublicKey(2)].
        b16(&b, 0x0014);
        b16(&b, 0x0003);
        b8(&b, 0x02); // list length 2
        b8(&b, 0x00); // X509(0)
        b8(&b, 0x02); // RawPublicKey(2)
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
    return build_client_hello_ex(out, client_pub, /*with_keyshare=*/PROTO_TRUE, NULL, 0, NULL, 0, PROTO_FALSE,
                                 TLS_GROUP_X25519, TLS_SIG_ED25519);
}

// Pull the 32-byte X25519 key_share out of a ServerHello (walks its extensions for type 0x0033).
static proto_bool sh_keyshare(const uint8_t *sh, size_t len, uint8_t pub[32])
{
    if (len < 44)
    {
        return PROTO_FALSE;
    }
    size_t o = 4 + 2 + 32; // hdr + legacy_version + random
    uint8_t sid = sh[o++];
    o += sid;   // session_id echo
    o += 2 + 1; // cipher_suite + compression
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
            memcpy(pub, sh + o + 4, 32); // group(2) + klen(2) then the key
            return PROTO_TRUE;
        }
        o += el;
    }
    return PROTO_FALSE;
}

// Pull the cookie out of a HelloRetryRequest (a ServerHello walked for the 0x002c cookie extension,
// whose body is opaque cookie<1..2^16-1>). Returns the inner cookie bytes and length.
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
            size_t cl = (size_t)((sh[o] << 8) | sh[o + 1]); // inner cookie length
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

// One DTLS handshake message per record in this phase: strip the 12-byte DTLS handshake header from a
// record payload and rebuild the TLS handshake message (4-byte header + body). Returns TLS msg length.
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

static size_t ct_record_len(const uint8_t *rec, size_t avail, size_t cid_len)
{
    size_t pre = 1 + ((rec[0] & 0x10) ? cid_len : 0); // byte0 + optional connection id
    size_t seq_len = (rec[0] & 0x08) ? 2 : 1;
    size_t len_off = pre + seq_len;
    size_t o = len_off + 2;
    size_t enc = ((size_t)rec[len_off] << 8) | rec[len_off + 1];
    return (o + enc <= avail) ? o + enc : 0;
}

// Pull the server's connection_id (extension 0x0036, RFC 9146) out of a ServerHello. Returns its length
// (0 if absent) and copies the CID bytes to @p cid_out.
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
            size_t cl = sh[o]; // 1-byte CID length
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

// Given the server's flight (its response to the client's final ClientHello) and the client transcript
// @p tr already updated through that final ClientHello, deprotect and verify the whole flight, send the
// client Finished (handshake message_seq @p cfin_msg_seq), and assert both sides install identical
// application-traffic keys. @p tr is taken by value so the caller's copy is untouched. Shared by the
// one-round-trip and HelloRetryRequest paths (they differ only in the transcript prefix and message_seq).
// server_certificate_type(0x0014) = RawPublicKey(2) present in an EncryptedExtensions message?
static proto_bool ee_has_rpk(const uint8_t *msg, size_t mlen)
{
    if (mlen < 6)
    {
        return PROTO_FALSE;
    }
    size_t o = 4; // hs header
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

// The fixed 12-byte DER prefix of an Ed25519 SubjectPublicKeyInfo (RFC 8410 §4).
static const uint8_t SPKI_PREFIX[12] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};

static void complete_handshake_from_flight(DtlsConn *conn, pc_sha256_ctx tr, uint16_t cfin_msg_seq,
                                           const uint8_t *flight, size_t fl, const uint8_t *client_cid,
                                           size_t client_cid_len, proto_bool expect_rpk)
{
    size_t off = 0;
    // record 0: ServerHello (DTLSPlaintext, epoch 0)
    DtlsPlaintext pt;
    size_t rl = pc_dtls_plaintext_parse(flight + off, fl - off, &pt);
    TEST_ASSERT_TRUE(rl > 0);
    off += rl;
    uint8_t sh[512];
    size_t sh_len = frag_to_tls(pt.fragment, pt.frag_len, sh);
    TEST_ASSERT_TRUE(sh_len > 0);
    pc_sha256_update(&tr, sh, sh_len);

    uint8_t server_pub[32];
    TEST_ASSERT_TRUE(sh_keyshare(sh, sh_len, server_pub));

    // Connection ids (RFC 9146 / RFC 9147 §9): when we offered a CID, the ServerHello carries the
    // server's CID (which we place in records we send), and every protected record the server sends us
    // carries our CID (@p client_cid). scid = the server's CID; empty when CID was not negotiated.
    uint8_t scid[PC_DTLS_CID_MAX];
    size_t scid_len = client_cid_len ? sh_conn_id(sh, sh_len, scid) : 0;
    if (client_cid_len)
    {
        TEST_ASSERT_TRUE(scid_len > 0); // the server must echo a connection_id extension
    }

    // client handshake key schedule from Transcript-Hash(..SH)
    uint8_t ecdhe[32];
    pc_x25519(ecdhe, CLIENT_X25519_PRIV, server_pub);
    Tls13KeySchedule cks;
    uint8_t h[32];
    pc_sha256_ctx tmp = tr;
    pc_sha256_final(&tmp, h);
    pc_tls13_ks_early(&DTLS13_KDF, &cks);
    pc_tls13_ks_handshake(&cks, ecdhe, h, 32);

    DtlsRecordKeys srv_read; // client reads the server's epoch-2 records
    pc_dtls_record_keys_derive(&srv_read, DTLS_CIPHER_AES_128_GCM_SHA256, 2, cks.server_hs_traffic);

    // records 1..4: EncryptedExtensions, Certificate, CertificateVerify, Finished (DTLSCiphertext)
    uint8_t cert_pub[32];
    proto_bool have_cert = PROTO_FALSE;
    uint64_t exp_seq = 0;
    int seen_fin = 0;
    while (off < fl)
    {
        size_t crl = ct_record_len(flight + off, fl - off, client_cid_len);
        TEST_ASSERT_TRUE(crl > 0);
        uint8_t inner[512];
        DtlsCiphertext info;
        TEST_ASSERT_TRUE(pc_dtls_ciphertext_unprotect(&srv_read, exp_seq, flight + off, crl, inner, sizeof(inner),
                                                      &info, client_cid, client_cid_len));
        exp_seq = info.seq + 1;
        off += crl;
        TEST_ASSERT_EQUAL_UINT8(PC_DTLS_CT_HANDSHAKE, info.content_type);

        uint8_t msg[512];
        size_t mlen = frag_to_tls(inner, info.pt_len, msg);
        TEST_ASSERT_TRUE(mlen > 0);

        if (msg[0] == 15) // CertificateVerify: verify BEFORE hashing it in, over H(..Certificate)
        {
            TEST_ASSERT_TRUE(have_cert);
            uint8_t h_ch_cert[32];
            pc_sha256_ctx sc = tr;
            pc_sha256_final(&sc, h_ch_cert);
            uint8_t content[160]; // 64*0x20 + 33-byte context + 0x00 + 32-byte hash = 130
            size_t clen = pc_tls13_cert_verify_content(content, sizeof(content), h_ch_cert, PROTO_TRUE);
            TEST_ASSERT_TRUE(clen > 0);
            const uint8_t *sig = msg + 4 + 2 + 2; // algorithm(2) + signature length(2)
            TEST_ASSERT_TRUE(pc_ed25519_verify(cert_pub, content, clen, sig));
        }
        if (msg[0] == 20) // server Finished: verify over H(..CertificateVerify)
        {
            uint8_t hcv[32];
            pc_sha256_ctx s = tr;
            pc_sha256_final(&s, hcv);
            uint8_t expect[32];
            pc_tls13_finished_mac(&DTLS13_KDF, cks.server_hs_traffic, hcv, expect);
            TEST_ASSERT_EQUAL_MEMORY(expect, msg + 4, 32);
            seen_fin = 1;
        }

        if (msg[0] == 8) // EncryptedExtensions: the RPK profile negotiates server_certificate_type here
        {
            TEST_ASSERT_EQUAL(expect_rpk, ee_has_rpk(msg, mlen));
        }

        pc_sha256_update(&tr, msg, mlen);

        if (msg[0] == 11) // Certificate: raw Ed25519 pubkey, or (RFC 7250) an Ed25519 SubjectPublicKeyInfo
        {
            const uint8_t *cert_data = msg + 4 + 1 + 3 + 3; // hdr + ctx_len(1) + list_len(3) + entry_len(3)
            uint32_t entry_len = (uint32_t)((msg[4 + 1 + 3] << 16) | (msg[4 + 1 + 3 + 1] << 8) | msg[4 + 1 + 3 + 2]);
            if (expect_rpk)
            {
                TEST_ASSERT_EQUAL_UINT32(44, entry_len); // 12-byte SPKI prefix + 32-byte key
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

    // --- client Finished over Transcript-Hash(..server Finished) ---
    uint8_t h_sfin[32];
    pc_sha256_ctx s2 = tr;
    pc_sha256_final(&s2, h_sfin);
    pc_tls13_ks_master(&cks, h_sfin);

    uint8_t cfin_verify[32];
    pc_tls13_finished_mac(&DTLS13_KDF, cks.client_hs_traffic, h_sfin, cfin_verify);
    uint8_t cfin[64];
    size_t cfin_len = pc_tls13_build_finished(cfin, sizeof(cfin), cfin_verify);

    DtlsRecordKeys cli_write; // client writes its epoch-2 Finished
    pc_dtls_record_keys_derive(&cli_write, DTLS_CIPHER_AES_128_GCM_SHA256, 2, cks.client_hs_traffic);

    uint8_t cfin_frag[80];
    size_t cff = pc_dtls_hs_frag_build(cfin[0], cfin_msg_seq, (uint32_t)(cfin_len - 4), 0, cfin + 4,
                                       (uint32_t)(cfin_len - 4), cfin_frag, sizeof(cfin_frag));
    uint8_t cfin_rec[128];
    size_t cfr = pc_dtls_ciphertext_protect(&cli_write, 0, PC_DTLS_CT_HANDSHAKE, cfin_frag, cff, cfin_rec,
                                            sizeof(cfin_rec), scid_len ? scid : NULL, scid_len);

    uint8_t out2[64];
    int r2 = pc_dtls_conn_process(conn, cfin_rec, cfr, out2, sizeof(out2));
    TEST_ASSERT_TRUE(r2 > 0); // the server acknowledges the client Finished (RFC 9147 §5.8.3)
    TEST_ASSERT_TRUE(pc_dtls_conn_established(conn));

    // --- both sides agree on the application-traffic keys ---
    DtlsRecordKeys cli_app_read;  // client reads server app data (from server_ap_traffic)
    DtlsRecordKeys cli_app_write; // client writes app data (from client_ap_traffic)
    pc_dtls_record_keys_derive(&cli_app_read, DTLS_CIPHER_AES_128_GCM_SHA256, 3, cks.server_ap_traffic);
    pc_dtls_record_keys_derive(&cli_app_write, DTLS_CIPHER_AES_128_GCM_SHA256, 3, cks.client_ap_traffic);

    // The ACK is an epoch-3 (application) record of content type 26 that the client can decrypt.
    uint8_t ack_pt[64];
    DtlsCiphertext ackinfo;
    TEST_ASSERT_TRUE(pc_dtls_ciphertext_unprotect(&cli_app_read, 0, out2, (size_t)r2, ack_pt, sizeof(ack_pt), &ackinfo,
                                                  client_cid, client_cid_len));
    TEST_ASSERT_EQUAL_UINT8(PC_DTLS_CT_ACK, ackinfo.content_type);

    DtlsRecordKeys *const srv_app_write = pc_dtls_conn_app_write_keys(conn); // server->client
    DtlsRecordKeys *const srv_app_read = pc_dtls_conn_app_read_keys(conn);   // client->server
    TEST_ASSERT_NOT_NULL(srv_app_write);
    TEST_ASSERT_NOT_NULL(srv_app_read);
    // The keys are contexts now, so compare what they produce rather than their bytes: the two sides of
    // a direction must seal identically or the handshake did not agree.
    TEST_ASSERT_EQUAL_MEMORY(cli_app_read.iv, srv_app_write->iv, 12);
    TEST_ASSERT_EQUAL_MEMORY(cli_app_write.iv, srv_app_read->iv, 12);
    assert_ctx_match(cli_app_read.gcm, srv_app_write->gcm);
    assert_ctx_match(cli_app_write.gcm, srv_app_read->gcm);

    // A retransmitted client Finished (its ACK was lost) must draw a fresh ACK, not a fatal error
    // (RFC 9147 §5.8.3): re-send the same Finished in a new record and expect another ACK.
    uint8_t cfin_rec2[128];
    size_t cfr2 = pc_dtls_ciphertext_protect(&cli_write, 1, PC_DTLS_CT_HANDSHAKE, cfin_frag, cff, cfin_rec2,
                                             sizeof(cfin_rec2), scid_len ? scid : NULL, scid_len);
    uint8_t out3[64];
    int r3 = pc_dtls_conn_process(conn, cfin_rec2, cfr2, out3, sizeof(out3));
    TEST_ASSERT_TRUE(r3 > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_established(conn));
    uint8_t ack_pt3[64];
    DtlsCiphertext ackinfo3;
    TEST_ASSERT_TRUE(pc_dtls_ciphertext_unprotect(&cli_app_read, 1, out3, (size_t)r3, ack_pt3, sizeof(ack_pt3),
                                                  &ackinfo3, client_cid, client_cid_len));
    TEST_ASSERT_EQUAL_UINT8(PC_DTLS_CT_ACK, ackinfo3.content_type);
}

static void server_cfg(DtlsServerConfig *cfg, const uint8_t server_ed_pub[32])
{
    cfg->cert_der = server_ed_pub; // the "certificate" is the raw Ed25519 public key for this test
    cfg->cert_len = 32;
    cfg->ed25519_seed = SERVER_ED_SEED;
    cfg->ephemeral_priv = SERVER_X25519_PRIV;
    cfg->server_random = SERVER_RANDOM;
    cfg->cookie_key = SERVER_COOKIE_KEY;
}

// The full one-round-trip handshake, driven from the client side.
static void test_full_handshake(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, NULL, 0); // no HRR expected on the happy path

    // --- client flight 1: ClientHello (epoch 0) ---
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);

    pc_sha256_ctx tr; // client transcript
    pc_sha256_init(&tr);
    pc_sha256_update(&tr, ch, ch_len);

    uint8_t ch_frag[300];
    size_t ch_fl = pc_dtls_hs_frag_build(ch[0], 0, (uint32_t)(ch_len - 4), 0, ch + 4, (uint32_t)(ch_len - 4), ch_frag,
                                         sizeof(ch_frag));
    uint8_t ch_rec[320];
    size_t ch_rl = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, ch_frag, ch_fl, ch_rec, sizeof(ch_rec));

    uint8_t flight[2048];
    int fl = pc_dtls_conn_process(&conn, ch_rec, ch_rl, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fl > 0); // server produced its flight

    complete_handshake_from_flight(&conn, tr, /*cfin_msg_seq=*/1, flight, (size_t)fl, NULL, 0, PROTO_FALSE);
}

// RFC 7250 Raw Public Keys: the ClientHello offers server_certificate_type = RawPublicKey, so the server
// answers with that type in EncryptedExtensions and sends its Ed25519 SubjectPublicKeyInfo (44-byte SPKI)
// as the Certificate instead of an X.509 chain. The client verifies CertificateVerify against the key
// carried in that SPKI, and the whole handshake completes with identical application-traffic keys - so the
// signature really is over the presented raw key.
static void test_full_handshake_rpk(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, NULL, 0);

    uint8_t ch[256];
    size_t ch_len = build_client_hello_ex(ch, client_pub, /*with_keyshare=*/PROTO_TRUE, NULL, 0, NULL, 0,
                                          /*offer_rpk=*/PROTO_TRUE, TLS_GROUP_X25519, TLS_SIG_ED25519);

    pc_sha256_ctx tr;
    pc_sha256_init(&tr);
    pc_sha256_update(&tr, ch, ch_len);

    uint8_t ch_frag[300];
    size_t ch_fl = pc_dtls_hs_frag_build(ch[0], 0, (uint32_t)(ch_len - 4), 0, ch + 4, (uint32_t)(ch_len - 4), ch_frag,
                                         sizeof(ch_frag));
    uint8_t ch_rec[320];
    size_t ch_rl = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, ch_frag, ch_fl, ch_rec, sizeof(ch_rec));

    uint8_t flight[2048];
    int fl = pc_dtls_conn_process(&conn, ch_rec, ch_rl, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fl > 0);

    complete_handshake_from_flight(&conn, tr, /*cfin_msg_seq=*/1, flight, (size_t)fl, NULL, 0,
                                   /*expect_rpk=*/PROTO_TRUE);
}

// Connection id negotiation (RFC 9146 / RFC 9147 §9): the ClientHello offers a connection_id, so the
// server echoes its own CID in the ServerHello, protects its epoch-2 flight with the client's CID, and
// accepts the client's Finished carrying the server's CID - the whole handshake completes with CIDs.
static void test_cid_handshake(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, NULL, 0);

    // ClientHello offering a 3-byte connection id (the CID the server must place in records it sends us).
    const uint8_t client_cid[3] = {0xC1, 0xC2, 0xC3};
    uint8_t ch[256];
    size_t ch_len = build_client_hello_ex(ch, client_pub, /*with_keyshare=*/PROTO_TRUE, NULL, 0, client_cid,
                                          sizeof(client_cid), PROTO_FALSE, TLS_GROUP_X25519, TLS_SIG_ED25519);
    pc_sha256_ctx tr;
    pc_sha256_init(&tr);
    pc_sha256_update(&tr, ch, ch_len);

    uint8_t ch_frag[300];
    size_t ch_fl = pc_dtls_hs_frag_build(ch[0], 0, (uint32_t)(ch_len - 4), 0, ch + 4, (uint32_t)(ch_len - 4), ch_frag,
                                         sizeof(ch_frag));
    uint8_t ch_rec[320];
    size_t ch_rl = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, ch_frag, ch_fl, ch_rec, sizeof(ch_rec));

    uint8_t flight[2048];
    int fl = pc_dtls_conn_process(&conn, ch_rec, ch_rl, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fl > 0);

    // The server's first epoch-2 record (after the plaintext ServerHello) must carry the C bit + our CID.
    DtlsPlaintext sh_pt;
    size_t shrl = pc_dtls_plaintext_parse(flight, (size_t)fl, &sh_pt);
    TEST_ASSERT_TRUE(shrl > 0);
    const uint8_t *ep2 = flight + shrl;
    TEST_ASSERT_TRUE((ep2[0] & 0x10) != 0); // C bit set on the encrypted flight
    TEST_ASSERT_EQUAL_MEMORY(client_cid, ep2 + 1, sizeof(client_cid));

    // The rest completes with CIDs threaded both directions (this also checks the ServerHello carries the
    // server's connection_id, the client's CID-bearing Finished is accepted, and both derive matching keys).
    complete_handshake_from_flight(&conn, tr, /*cfin_msg_seq=*/1, flight, (size_t)fl, client_cid, sizeof(client_cid),
                                   PROTO_FALSE);
}

// The HelloRetryRequest path (RFC 9147 §5.1): a ClientHello that offers X25519 but sends no key_share
// draws an HRR with a cookie; the retry echoes the cookie and its X25519 share, and the same full
// handshake completes over the message_hash || HRR || ClientHello2 || ... transcript (RFC 8446 §4.4.1).
static void test_hrr_group_renegotiation(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, TEST_PEER_ADDR, sizeof(TEST_PEER_ADDR));

    // --- client flight 1: ClientHello with NO key_share (message_seq 0) ---
    uint8_t ch1[256];
    size_t ch1_len = build_client_hello_ex(ch1, client_pub, /*with_keyshare=*/PROTO_FALSE, NULL, 0, NULL, 0,
                                           PROTO_FALSE, TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t f1[300];
    size_t f1l =
        pc_dtls_hs_frag_build(ch1[0], 0, (uint32_t)(ch1_len - 4), 0, ch1 + 4, (uint32_t)(ch1_len - 4), f1, sizeof(f1));
    uint8_t r1[320];
    size_t r1l = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, f1, f1l, r1, sizeof(r1));

    uint8_t hrr_flight[512];
    int hf = pc_dtls_conn_process(&conn, r1, r1l, hrr_flight, sizeof(hrr_flight));
    TEST_ASSERT_TRUE(hf > 0);
    TEST_ASSERT_FALSE(pc_dtls_conn_established(&conn)); // just an HRR so far

    // --- the server flight is a single epoch-0 plaintext HelloRetryRequest ---
    DtlsPlaintext pt;
    size_t rl = pc_dtls_plaintext_parse(hrr_flight, (size_t)hf, &pt);
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t hrr[512];
    size_t hrr_len = frag_to_tls(pt.fragment, pt.frag_len, hrr);
    TEST_ASSERT_TRUE(hrr_len > 0);
    TEST_ASSERT_EQUAL_UINT8(0x02, hrr[0]);                          // ServerHello handshake type
    TEST_ASSERT_EQUAL_MEMORY(pc_tls13_hrr_random, hrr + 4 + 2, 32); // the HRR magic random marks it an HRR
    uint8_t cookie[PC_DTLS_COOKIE_MAX];
    size_t cookie_len = 0;
    TEST_ASSERT_TRUE(hrr_cookie(hrr, hrr_len, cookie, &cookie_len));
    TEST_ASSERT_TRUE(cookie_len > 0);

    // --- client transcript for the HRR path: message_hash(Hash(CH1)) || HRR || CH2 (RFC 8446 §4.4.1) ---
    uint8_t ch1_hash[32];
    pc_sha256_ctx h1;
    pc_sha256_init(&h1);
    pc_sha256_update(&h1, ch1, ch1_len);
    pc_sha256_final(&h1, ch1_hash);

    pc_sha256_ctx tr;
    pc_sha256_init(&tr);
    uint8_t mh[36];
    size_t mhl = pc_tls13_build_message_hash(mh, sizeof(mh), ch1_hash);
    TEST_ASSERT_TRUE(mhl > 0);
    pc_sha256_update(&tr, mh, mhl);
    pc_sha256_update(&tr, hrr, hrr_len);

    // --- client flight 2: ClientHello with the X25519 share and the echoed cookie (message_seq 1) ---
    uint8_t ch2[320];
    size_t ch2_len = build_client_hello_ex(ch2, client_pub, /*with_keyshare=*/PROTO_TRUE, cookie, cookie_len, NULL, 0,
                                           PROTO_FALSE, TLS_GROUP_X25519, TLS_SIG_ED25519);
    pc_sha256_update(&tr, ch2, ch2_len);

    uint8_t f2[380];
    size_t f2l =
        pc_dtls_hs_frag_build(ch2[0], 1, (uint32_t)(ch2_len - 4), 0, ch2 + 4, (uint32_t)(ch2_len - 4), f2, sizeof(f2));
    uint8_t r2[420];
    size_t r2l = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 1, f2, f2l, r2, sizeof(r2));

    uint8_t flight[2048];
    int fl = pc_dtls_conn_process(&conn, r2, r2l, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fl > 0); // the full server flight

    complete_handshake_from_flight(&conn, tr, /*cfin_msg_seq=*/2, flight, (size_t)fl, NULL, 0, PROTO_FALSE);
}

// After an HRR, a retry that carries the X25519 share but omits (or corrupts) the cookie is rejected:
// the server refuses to spend the handshake before the client's address is proven (RFC 9147 §5.1).
static void test_hrr_retry_without_cookie_rejected(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, TEST_PEER_ADDR, sizeof(TEST_PEER_ADDR));

    // CH1 without a key_share -> HRR.
    uint8_t ch1[256];
    size_t ch1_len = build_client_hello_ex(ch1, client_pub, /*with_keyshare=*/PROTO_FALSE, NULL, 0, NULL, 0,
                                           PROTO_FALSE, TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t f1[300];
    size_t f1l =
        pc_dtls_hs_frag_build(ch1[0], 0, (uint32_t)(ch1_len - 4), 0, ch1 + 4, (uint32_t)(ch1_len - 4), f1, sizeof(f1));
    uint8_t r1[320];
    size_t r1l = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, f1, f1l, r1, sizeof(r1));
    uint8_t hrr_flight[512];
    TEST_ASSERT_TRUE(pc_dtls_conn_process(&conn, r1, r1l, hrr_flight, sizeof(hrr_flight)) > 0);

    // CH2 with a key_share but NO cookie (message_seq 1) -> handshake_failure.
    uint8_t ch2[320];
    size_t ch2_len = build_client_hello_ex(ch2, client_pub, /*with_keyshare=*/PROTO_TRUE, NULL, 0, NULL, 0, PROTO_FALSE,
                                           TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t f2[380];
    size_t f2l =
        pc_dtls_hs_frag_build(ch2[0], 1, (uint32_t)(ch2_len - 4), 0, ch2 + 4, (uint32_t)(ch2_len - 4), f2, sizeof(f2));
    uint8_t r2[420];
    size_t r2l = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 1, f2, f2l, r2, sizeof(r2));
    uint8_t out[2048];
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&conn, r2, r2l, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(40, pc_dtls_conn_alert(&conn)); // handshake_failure
}

// A ClientHello that does not offer TLS 1.3 is rejected with a protocol_version alert.
static void test_reject_no_tls13(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, NULL, 0);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    // Corrupt the supported_versions value 0xFEFC (DTLS 1.3) -> 0xFEFD (DTLS 1.2) so offers_tls13 is false.
    for (size_t i = 0; i + 1 < ch_len; i++)
    {
        if (ch[i] == 0xFE && ch[i + 1] == 0xFC)
        {
            ch[i + 1] = 0xFD;
            break;
        }
    }
    uint8_t frag[300], rec[320], out[1024];
    size_t fl =
        pc_dtls_hs_frag_build(ch[0], 0, (uint32_t)(ch_len - 4), 0, ch + 4, (uint32_t)(ch_len - 4), frag, sizeof(frag));
    size_t rl = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, frag, fl, rec, sizeof(rec));
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&conn, rec, rl, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(70, pc_dtls_conn_alert(&conn)); // protocol_version
}

// Drive a fresh connection from ClientHello to WAIT_FINISHED; return the server flight and seed the
// client transcript @p tr through the ClientHello. Shared by the retransmission-timer tests.
static int drive_server_flight(DtlsConn *conn, DtlsServerConfig *cfg, pc_sha256_ctx *tr, uint8_t *flight,
                               size_t flight_cap)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    pc_dtls_conn_init(conn, cfg, NULL, 0);
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    pc_sha256_init(tr);
    pc_sha256_update(tr, ch, ch_len);
    uint8_t ch_frag[300];
    size_t ch_fl = pc_dtls_hs_frag_build(ch[0], 0, (uint32_t)(ch_len - 4), 0, ch + 4, (uint32_t)(ch_len - 4), ch_frag,
                                         sizeof(ch_frag));
    uint8_t ch_rec[320];
    size_t ch_rl = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, ch_frag, ch_fl, ch_rec, sizeof(ch_rec));
    return pc_dtls_conn_process(conn, ch_rec, ch_rl, flight, flight_cap);
}

// The retransmission timer (RFC 9147 §5.8): after the server flight, the timer is armed at the initial
// PTO; once it elapses the whole flight is re-sent with fresh record sequence numbers, and the client
// completes the handshake from that retransmission.
static void test_pto_retransmit_and_recovery(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_sha256_ctx tr;
    uint8_t flight[2048];
    int fl = drive_server_flight(&conn, &cfg, &tr, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fl > 0);

    TEST_ASSERT_EQUAL_INT((int)PC_DTLS_PTO_INITIAL_MS, pc_dtls_conn_timeout_ms(&conn)); // armed at the initial PTO

    uint8_t rflight[2048];
    TEST_ASSERT_EQUAL_INT(0, pc_dtls_conn_on_timeout(&conn, rflight, sizeof(rflight))); // not due yet -> no-op

    g_ms += PC_DTLS_PTO_INITIAL_MS;
    int rfl = pc_dtls_conn_on_timeout(&conn, rflight, sizeof(rflight));
    TEST_ASSERT_TRUE(rfl > 0); // whole flight retransmitted
    TEST_ASSERT_EQUAL_INT((int)(PC_DTLS_PTO_INITIAL_MS * 2), pc_dtls_conn_timeout_ms(&conn)); // backed off to 2x

    // The retransmission is a valid, completable server flight (fresh record seqs); completing it also
    // disarms the timer.
    complete_handshake_from_flight(&conn, tr, 1, rflight, (size_t)rfl, NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_timeout_ms(&conn));
}

// The timer doubles each retransmission up to the cap, and the handshake is abandoned after the
// retransmission ceiling (RFC 9147 §5.8.1).
static void test_pto_backoff_and_giveup(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_sha256_ctx tr;
    uint8_t flight[2048], rflight[2048];
    TEST_ASSERT_TRUE(drive_server_flight(&conn, &cfg, &tr, flight, sizeof(flight)) > 0);

    uint32_t expect = PC_DTLS_PTO_INITIAL_MS;
    TEST_ASSERT_EQUAL_INT((int)expect, pc_dtls_conn_timeout_ms(&conn));
    for (int i = 0; i < PC_DTLS_MAX_RETRANSMITS; i++)
    {
        g_ms += PC_DTLS_PTO_MAX_MS + 1000; // well past any PTO
        TEST_ASSERT_TRUE(pc_dtls_conn_on_timeout(&conn, rflight, sizeof(rflight)) > 0);
        expect = expect >= PC_DTLS_PTO_MAX_MS / 2 ? PC_DTLS_PTO_MAX_MS : expect * 2;
        TEST_ASSERT_EQUAL_INT((int)expect, pc_dtls_conn_timeout_ms(&conn)); // doubled, capped at the max
    }
    g_ms += PC_DTLS_PTO_MAX_MS + 1000;
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_on_timeout(&conn, rflight, sizeof(rflight)));          // ceiling: give up
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_timeout_ms(&conn));                                    // FAILED: no timer
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&conn, rflight, 1, rflight, sizeof(rflight))); // and rejects input
}

// A client ACK covering the whole server flight stops retransmission (RFC 9147 §5.8.3).
static void test_pto_ack_cancels_retransmit(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_sha256_ctx tr;
    uint8_t flight[2048];
    int fl = drive_server_flight(&conn, &cfg, &tr, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fl > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_timeout_ms(&conn) >= 0); // armed

    // Derive the client's epoch-2 write keys from the ServerHello, as a real client would.
    DtlsPlaintext pt;
    TEST_ASSERT_TRUE(pc_dtls_plaintext_parse(flight, (size_t)fl, &pt) > 0);
    uint8_t sh[512];
    size_t sh_len = frag_to_tls(pt.fragment, pt.frag_len, sh);
    TEST_ASSERT_TRUE(sh_len > 0);
    uint8_t server_pub[32];
    TEST_ASSERT_TRUE(sh_keyshare(sh, sh_len, server_pub));
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    pc_sha256_ctx t;
    pc_sha256_init(&t);
    pc_sha256_update(&t, ch, ch_len);
    pc_sha256_update(&t, sh, sh_len);
    uint8_t h[32];
    pc_sha256_final(&t, h);
    uint8_t ecdhe[32];
    pc_x25519(ecdhe, CLIENT_X25519_PRIV, server_pub);
    Tls13KeySchedule cks;
    pc_tls13_ks_early(&DTLS13_KDF, &cks);
    pc_tls13_ks_handshake(&cks, ecdhe, h, 32);
    DtlsRecordKeys cli_write;
    pc_dtls_record_keys_derive(&cli_write, DTLS_CIPHER_AES_128_GCM_SHA256, 2, cks.client_hs_traffic);

    // ACK the whole flight: ServerHello (epoch 0, seq 0) + the four epoch-2 messages (seq 0..3).
    DtlsRecordNumber rns[5] = {{0, 0}, {2, 0}, {2, 1}, {2, 2}, {2, 3}};
    uint8_t ack_body[2 + 5 * 16];
    size_t bl = pc_dtls_ack_build(rns, 5, ack_body, sizeof(ack_body));
    TEST_ASSERT_TRUE(bl > 0);
    uint8_t ack_rec[160];
    size_t ar =
        pc_dtls_ciphertext_protect(&cli_write, 0, PC_DTLS_CT_ACK, ack_body, bl, ack_rec, sizeof(ack_rec), NULL, 0);
    TEST_ASSERT_TRUE(ar > 0);

    uint8_t out[64];
    pc_dtls_conn_process(&conn, ack_rec, ar, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_timeout_ms(&conn)); // the ACK stopped the retransmission timer
}

// ---------------------------------------------------------------------------
// Error and edge paths: record-layer rejects, handshake state-machine faults, the retransmission
// timer's non-running states, and the epoch-3 application record paths.
// ---------------------------------------------------------------------------

// Wrap one TLS handshake message as a single DTLS fragment inside an epoch-0 plaintext record.
static size_t plain_hs_record(uint8_t *out, size_t out_cap, const uint8_t *tls_msg, size_t tls_len, uint16_t msg_seq,
                              uint64_t rec_seq)
{
    uint8_t frag[512];
    size_t fl = pc_dtls_hs_frag_build(tls_msg[0], msg_seq, (uint32_t)(tls_len - 4), 0, tls_msg + 4,
                                      (uint32_t)(tls_len - 4), frag, sizeof(frag));
    if (!fl)
    {
        return 0;
    }
    return pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, rec_seq, frag, fl, out, out_cap);
}

// Everything the minimal test client holds after the server flight: the epoch-2 and epoch-3 record
// keys plus a VALID client Finished fragment left unsent, so a test can deliver it (or something else)
// under its own conditions.
typedef struct
{
    Tls13KeySchedule cks;
    DtlsRecordKeys cli_hs_write;  ///< client -> server, epoch 2
    DtlsRecordKeys srv_hs_read;   ///< server -> client, epoch 2
    DtlsRecordKeys cli_app_write; ///< client -> server, epoch 3
    DtlsRecordKeys cli_app_read;  ///< server -> client, epoch 3
    uint8_t cfin_frag[80];
    size_t cfin_frag_len;
} ClientSession;

// Drive a fresh connection through ClientHello -> server flight (state WAIT_FINISHED) and run the
// client half of the key schedule over the real transcript. No assertions: the caller decides what to
// do next. Returns false if any step of the client side failed.
static proto_bool run_to_finished(DtlsConn *conn, DtlsServerConfig *cfg, ClientSession *st)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    pc_dtls_conn_init(conn, cfg, NULL, 0);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    pc_sha256_ctx tr;
    pc_sha256_init(&tr);
    pc_sha256_update(&tr, ch, ch_len);

    uint8_t rec[320];
    size_t rl = plain_hs_record(rec, sizeof(rec), ch, ch_len, 0, 0);
    if (!rl)
    {
        return PROTO_FALSE;
    }
    uint8_t flight[2048];
    int fl = pc_dtls_conn_process(conn, rec, rl, flight, sizeof(flight));
    if (fl <= 0)
    {
        return PROTO_FALSE;
    }

    DtlsPlaintext pt;
    size_t off = pc_dtls_plaintext_parse(flight, (size_t)fl, &pt);
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
    pc_sha256_update(&tr, sh, sh_len);
    uint8_t server_pub[32];
    if (!sh_keyshare(sh, sh_len, server_pub))
    {
        return PROTO_FALSE;
    }

    uint8_t ecdhe[32];
    pc_x25519(ecdhe, CLIENT_X25519_PRIV, server_pub);
    uint8_t h[32];
    pc_sha256_ctx tmp = tr;
    pc_sha256_final(&tmp, h);
    pc_tls13_ks_early(&DTLS13_KDF, &st->cks);
    pc_tls13_ks_handshake(&st->cks, ecdhe, h, 32);
    pc_dtls_record_keys_derive(&st->srv_hs_read, DTLS_CIPHER_AES_128_GCM_SHA256, 2, st->cks.server_hs_traffic);
    pc_dtls_record_keys_derive(&st->cli_hs_write, DTLS_CIPHER_AES_128_GCM_SHA256, 2, st->cks.client_hs_traffic);

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
        if (!pc_dtls_ciphertext_unprotect(&st->srv_hs_read, exp_seq, flight + off, crl, inner, sizeof(inner), &info,
                                          NULL, 0))
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
        pc_sha256_update(&tr, msg, mlen);
    }

    uint8_t h_sfin[32];
    pc_sha256_ctx s = tr;
    pc_sha256_final(&s, h_sfin);
    pc_tls13_ks_master(&st->cks, h_sfin);
    pc_dtls_record_keys_derive(&st->cli_app_write, DTLS_CIPHER_AES_128_GCM_SHA256, 3, st->cks.client_ap_traffic);
    pc_dtls_record_keys_derive(&st->cli_app_read, DTLS_CIPHER_AES_128_GCM_SHA256, 3, st->cks.server_ap_traffic);

    uint8_t verify[32];
    pc_tls13_finished_mac(&DTLS13_KDF, st->cks.client_hs_traffic, h_sfin, verify);
    uint8_t cfin[64];
    size_t cfin_len = pc_tls13_build_finished(cfin, sizeof(cfin), verify);
    if (cfin_len < 4)
    {
        return PROTO_FALSE;
    }
    st->cfin_frag_len = pc_dtls_hs_frag_build(cfin[0], 1, (uint32_t)(cfin_len - 4), 0, cfin + 4,
                                              (uint32_t)(cfin_len - 4), st->cfin_frag, sizeof(st->cfin_frag));
    return st->cfin_frag_len > 0;
}

// Protect the client's buffered Finished as an epoch-2 record at @p seq and feed it to the server.
static int feed_client_finished(DtlsConn *conn, ClientSession *st, uint64_t seq, uint8_t *out, size_t out_cap)
{
    uint8_t rec[128];
    size_t rl = pc_dtls_ciphertext_protect(&st->cli_hs_write, seq, PC_DTLS_CT_HANDSHAKE, st->cfin_frag,
                                           st->cfin_frag_len, rec, sizeof(rec), NULL, 0);
    if (!rl)
    {
        return -2;
    }
    return pc_dtls_conn_process(conn, rec, rl, out, out_cap);
}

// Wrap an arbitrary TLS handshake message as an epoch-2 client record and feed it to the server.
// Returns -2 when the test itself could not build the record (never a server verdict).
static int feed_epoch2_msg(DtlsConn *conn, ClientSession *st, uint64_t seq, uint16_t msg_seq, const uint8_t *tls_msg,
                           size_t tls_len, uint8_t *out, size_t out_cap)
{
    uint8_t frag[128];
    size_t fl = pc_dtls_hs_frag_build(tls_msg[0], msg_seq, (uint32_t)(tls_len - 4), 0, tls_msg + 4,
                                      (uint32_t)(tls_len - 4), frag, sizeof(frag));
    if (!fl)
    {
        return -2;
    }
    uint8_t rec[192];
    size_t rl =
        pc_dtls_ciphertext_protect(&st->cli_hs_write, seq, PC_DTLS_CT_HANDSHAKE, frag, fl, rec, sizeof(rec), NULL, 0);
    if (!rl)
    {
        return -2;
    }
    return pc_dtls_conn_process(conn, rec, rl, out, out_cap);
}

// Protect an ACK body as an epoch-2 client record and feed it to the server.
static int feed_epoch2_ack(DtlsConn *conn, ClientSession *st, uint64_t seq, const uint8_t *body, size_t blen,
                           uint8_t *out, size_t out_cap)
{
    uint8_t rec[192];
    size_t rl =
        pc_dtls_ciphertext_protect(&st->cli_hs_write, seq, PC_DTLS_CT_ACK, body, blen, rec, sizeof(rec), NULL, 0);
    if (!rl)
    {
        return -2;
    }
    return pc_dtls_conn_process(conn, rec, rl, out, out_cap);
}

// A malformed DTLSCiphertext unified header stops the datagram walk without failing the connection:
// the explicit length field is truncated, or it claims more bytes than the datagram holds.
static void test_ciphertext_truncated_header_stops_walk(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t out[64];

    // 0x2C = 001, C=0, S=1 (16-bit sequence number), L=1 (length present): only 3 bytes, so the
    // 2-byte length field itself does not fit.
    DtlsConn a;
    pc_dtls_conn_init(&a, &cfg, NULL, 0);
    const uint8_t short_len[3] = {0x2C, 0x00, 0x01};
    TEST_ASSERT_EQUAL_INT(0, pc_dtls_conn_process(&a, short_len, sizeof(short_len), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0, pc_dtls_conn_alert(&a)); // stopped, not failed

    // The explicit length (255) runs past the end of the datagram.
    DtlsConn b;
    pc_dtls_conn_init(&b, &cfg, NULL, 0);
    const uint8_t long_len[5] = {0x2C, 0x00, 0x01, 0x00, 0xFF};
    TEST_ASSERT_EQUAL_INT(0, pc_dtls_conn_process(&b, long_len, sizeof(long_len), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0, pc_dtls_conn_alert(&b));
}

// A ciphertext record arriving before any epoch-2 keys exist (no ClientHello yet) is fatal:
// unexpected_message. Covers both header shapes the walker measures - explicit length, and a record
// that runs to the end of the datagram with an 8-bit sequence number.
static void test_ciphertext_before_keys_is_fatal(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t out[64];

    DtlsConn a;
    pc_dtls_conn_init(&a, &cfg, NULL, 0);
    const uint8_t with_len[7] = {0x2C, 0x00, 0x01, 0x00, 0x02, 0xAA, 0xBB};
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&a, with_len, sizeof(with_len), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(10, pc_dtls_conn_alert(&a)); // unexpected_message

    // 0x20 = 001, C=0, S=0 (8-bit sequence number), L=0: the record runs to the end of the datagram.
    DtlsConn b;
    pc_dtls_conn_init(&b, &cfg, NULL, 0);
    const uint8_t no_len[4] = {0x20, 0x01, 0xAA, 0xBB};
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&b, no_len, sizeof(no_len), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(10, pc_dtls_conn_alert(&b));
}

// A well-formed epoch-0 record that is not a handshake record is walked past and ignored: the state
// machine never sees it, and the ClientHello that follows is still accepted.
static void test_plaintext_non_handshake_record_ignored(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, NULL, 0);

    const uint8_t alert_body[2] = {0x01, 0x00}; // warning, close_notify
    uint8_t rec[32];
    size_t rl = pc_dtls_plaintext_build(PC_DTLS_CT_ALERT, 0, 0, alert_body, sizeof(alert_body), rec, sizeof(rec));
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[2048];
    TEST_ASSERT_EQUAL_INT(0, pc_dtls_conn_process(&conn, rec, rl, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0, pc_dtls_conn_alert(&conn));

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t ch_rec[320];
    size_t ch_rl = plain_hs_record(ch_rec, sizeof(ch_rec), ch, ch_len, 0, 1);
    TEST_ASSERT_TRUE(ch_rl > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_process(&conn, ch_rec, ch_rl, out, sizeof(out)) > 0);
}

// A handshake record whose payload is shorter than the 12-byte DTLS handshake header: the fragment
// walk stops and nothing is dispatched (a truncated datagram is not an attack worth an alert).
static void test_truncated_handshake_fragment_ignored(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, NULL, 0);

    const uint8_t stub[5] = {0x01, 0x00, 0x00, 0x20, 0x00}; // first 5 bytes of a handshake header
    uint8_t rec[32];
    size_t rl = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, stub, sizeof(stub), rec, sizeof(rec));
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[64];
    TEST_ASSERT_EQUAL_INT(0, pc_dtls_conn_process(&conn, rec, rl, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0, pc_dtls_conn_alert(&conn));
}

// A fragment for a message_seq the reassembler is not collecting is ignored rather than rejected
// (RFC 9147 §5.4), and the expected ClientHello is still accepted afterwards.
static void test_fragment_for_other_msg_seq_ignored(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, NULL, 0);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t rec[320];
    size_t rl = plain_hs_record(rec, sizeof(rec), ch, ch_len, /*msg_seq=*/7, /*rec_seq=*/0);
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[2048];
    TEST_ASSERT_EQUAL_INT(0, pc_dtls_conn_process(&conn, rec, rl, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0, pc_dtls_conn_alert(&conn));

    size_t rl0 = plain_hs_record(rec, sizeof(rec), ch, ch_len, /*msg_seq=*/0, /*rec_seq=*/1);
    TEST_ASSERT_TRUE(rl0 > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_process(&conn, rec, rl0, out, sizeof(out)) > 0);
}

// A fragment declaring a message body larger than the reassembly buffer is refused by the
// reassembler, and the connection fails with decode_error.
static void test_oversize_handshake_message_rejected(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, NULL, 0);

    const uint8_t body[8] = {0};
    uint8_t frag[32];
    size_t fl = pc_dtls_hs_frag_build(TLS_HS_CLIENT_HELLO, 0, (uint32_t)(PC_DTLS_CONN_REASM_CAP + 1), 0, body,
                                      sizeof(body), frag, sizeof(frag));
    TEST_ASSERT_TRUE(fl > 0);
    uint8_t rec[64];
    size_t rl = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, frag, fl, rec, sizeof(rec));
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[64];
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&conn, rec, rl, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(50, pc_dtls_conn_alert(&conn)); // decode_error
}

// A Finished arriving in START matches no handler: unexpected_message.
static void test_unexpected_message_in_start_rejected(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, NULL, 0);

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
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&conn, rec, rl, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(10, pc_dtls_conn_alert(&conn)); // unexpected_message
}

// A ClientHello that does not offer the server's one profile (Ed25519 signatures and the X25519
// group) is refused with handshake_failure, whichever half is missing.
static void test_client_hello_missing_algorithms_rejected(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t rec[320];
    uint8_t out[2048];

    // signature_algorithms carries only rsa_pss_rsae_sha256 (0x0804): no ed25519.
    DtlsConn a;
    pc_dtls_conn_init(&a, &cfg, NULL, 0);
    uint8_t ch_a[256];
    size_t la =
        build_client_hello_ex(ch_a, client_pub, PROTO_TRUE, NULL, 0, NULL, 0, PROTO_FALSE, TLS_GROUP_X25519, 0x0804);
    size_t ra = plain_hs_record(rec, sizeof(rec), ch_a, la, 0, 0);
    TEST_ASSERT_TRUE(ra > 0);
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&a, rec, ra, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(40, pc_dtls_conn_alert(&a)); // handshake_failure

    // supported_groups carries only secp256r1 (0x0017): no x25519, even though a share is present.
    DtlsConn b;
    pc_dtls_conn_init(&b, &cfg, NULL, 0);
    uint8_t ch_b[256];
    size_t lb =
        build_client_hello_ex(ch_b, client_pub, PROTO_TRUE, NULL, 0, NULL, 0, PROTO_FALSE, 0x0017, TLS_SIG_ED25519);
    size_t rb = plain_hs_record(rec, sizeof(rec), ch_b, lb, 0, 0);
    TEST_ASSERT_TRUE(rb > 0);
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&b, rec, rb, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(40, pc_dtls_conn_alert(&b));
}

// A certificate too large for the outbound message buffer cannot be built into a Certificate message,
// so the handshake is abandoned with internal_error rather than sending a truncated flight.
static void test_oversize_certificate_is_internal_error(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    static uint8_t big_cert[PC_DTLS_CONN_MSG_CAP + 200];
    memset(big_cert, 0xAB, sizeof(big_cert));

    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    cfg.cert_der = big_cert;
    cfg.cert_len = sizeof(big_cert);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, NULL, 0);

    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t rec[320];
    size_t rl = plain_hs_record(rec, sizeof(rec), ch, ch_len, 0, 0);
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[4096];
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&conn, rec, rl, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(80, pc_dtls_conn_alert(&conn)); // internal_error
}

// The response buffer cannot hold even the first record of the flight: the transmit fails and the
// handshake is abandoned with internal_error, for both the full flight and a HelloRetryRequest.
static void test_flight_out_cap_too_small_is_internal_error(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t rec[320];
    uint8_t tiny[64];

    DtlsConn a;
    pc_dtls_conn_init(&a, &cfg, NULL, 0);
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    size_t rl = plain_hs_record(rec, sizeof(rec), ch, ch_len, 0, 0);
    TEST_ASSERT_TRUE(rl > 0);
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&a, rec, rl, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_UINT8(80, pc_dtls_conn_alert(&a)); // internal_error

    // Same for the HelloRetryRequest flight (a ClientHello with no key_share).
    DtlsConn b;
    pc_dtls_conn_init(&b, &cfg, TEST_PEER_ADDR, sizeof(TEST_PEER_ADDR));
    uint8_t ch1[256];
    size_t ch1_len = build_client_hello_ex(ch1, client_pub, /*with_keyshare=*/PROTO_FALSE, NULL, 0, NULL, 0,
                                           PROTO_FALSE, TLS_GROUP_X25519, TLS_SIG_ED25519);
    size_t r1 = plain_hs_record(rec, sizeof(rec), ch1, ch1_len, 0, 0);
    TEST_ASSERT_TRUE(r1 > 0);
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&b, rec, r1, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_UINT8(80, pc_dtls_conn_alert(&b));
}

// A retransmission that cannot be written into the caller's buffer reports failure rather than
// emitting a partial flight.
static void test_retransmit_out_cap_too_small(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_sha256_ctx tr;
    uint8_t flight[2048];
    TEST_ASSERT_TRUE(drive_server_flight(&conn, &cfg, &tr, flight, sizeof(flight)) > 0);

    g_ms += PC_DTLS_PTO_INITIAL_MS;
    uint8_t tiny[32];
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_on_timeout(&conn, tiny, sizeof(tiny)));
}

// The retransmission timer reports no deadline and refuses to fire once the handshake has finished,
// and likewise once it has failed with a flight still outstanding.
static void test_timer_idle_when_done_or_failed(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t out[256];

    DtlsConn done;
    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&done, &cfg, &st));
    TEST_ASSERT_TRUE(feed_client_finished(&done, &st, 0, out, sizeof(out)) > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_established(&done));
    g_ms += PC_DTLS_PTO_MAX_MS;
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_timeout_ms(&done));
    TEST_ASSERT_EQUAL_INT(0, pc_dtls_conn_on_timeout(&done, out, sizeof(out)));

    // A connection that failed while its flight was still outstanding: awaiting_reply is still set,
    // so only the FAILED state stops the timer.
    DtlsConn failed;
    pc_sha256_ctx tr;
    uint8_t flight[2048];
    TEST_ASSERT_TRUE(drive_server_flight(&failed, &cfg, &tr, flight, sizeof(flight)) > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_timeout_ms(&failed) >= 0); // armed
    uint8_t ch[256];
    size_t ch_len = build_client_hello(ch, client_pub);
    uint8_t rec[320];
    size_t rl = plain_hs_record(rec, sizeof(rec), ch, ch_len, /*msg_seq=*/1, /*rec_seq=*/1);
    TEST_ASSERT_TRUE(rl > 0);
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&failed, rec, rl, out, sizeof(out))); // CH in WAIT_FINISHED
    TEST_ASSERT_EQUAL_UINT8(10, pc_dtls_conn_alert(&failed));                            // unexpected_message
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_timeout_ms(&failed));
    TEST_ASSERT_EQUAL_INT(0, pc_dtls_conn_on_timeout(&failed, out, sizeof(out)));
}

// The client Finished error paths: a body of the wrong length is a decode_error, a wrong verify_data
// is a decrypt_error, and any other handshake message in epoch 2 is unexpected_message.
static void test_client_finished_error_paths(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t out[256];

    // (a) Finished with a 31-byte body: not 4 + SHA-256 digest length.
    DtlsConn a;
    ClientSession sa;
    TEST_ASSERT_TRUE(run_to_finished(&a, &cfg, &sa));
    uint8_t shortfin[35];
    shortfin[0] = TLS_HS_FINISHED;
    shortfin[1] = 0;
    shortfin[2] = 0;
    shortfin[3] = 31;
    memset(shortfin + 4, 0x5A, 31);
    TEST_ASSERT_EQUAL_INT(-1, feed_epoch2_msg(&a, &sa, 0, 1, shortfin, sizeof(shortfin), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(50, pc_dtls_conn_alert(&a)); // decode_error

    // (b) Finished of the right shape but the wrong verify_data.
    DtlsConn b;
    ClientSession sb;
    TEST_ASSERT_TRUE(run_to_finished(&b, &cfg, &sb));
    uint8_t badfin[36];
    badfin[0] = TLS_HS_FINISHED;
    badfin[1] = 0;
    badfin[2] = 0;
    badfin[3] = 32;
    memset(badfin + 4, 0xAA, 32);
    TEST_ASSERT_EQUAL_INT(-1, feed_epoch2_msg(&b, &sb, 0, 1, badfin, sizeof(badfin), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(51, pc_dtls_conn_alert(&b)); // decrypt_error

    // (c) a ClientHello inside an epoch-2 record while waiting for the Finished.
    DtlsConn c;
    ClientSession sc;
    TEST_ASSERT_TRUE(run_to_finished(&c, &cfg, &sc));
    uint8_t stray[14];
    stray[0] = TLS_HS_CLIENT_HELLO;
    stray[1] = 0;
    stray[2] = 0;
    stray[3] = 10;
    memset(stray + 4, 0x11, 10);
    TEST_ASSERT_EQUAL_INT(-1, feed_epoch2_msg(&c, &sc, 0, 1, stray, sizeof(stray), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(10, pc_dtls_conn_alert(&c)); // unexpected_message
}

// An ACK that cannot be parsed, and one that covers only part of the outstanding flight, both leave
// the retransmission timer running (RFC 9147 §5.8.3: only a complete acknowledgement disarms it).
static void test_ack_malformed_and_partial_keep_timer(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&conn, &cfg, &st));
    TEST_ASSERT_EQUAL_INT((int)PC_DTLS_PTO_INITIAL_MS, pc_dtls_conn_timeout_ms(&conn));
    uint8_t out[64];

    // The ACK's list length claims one record number that is not there.
    const uint8_t malformed[2] = {0x00, 0x10};
    TEST_ASSERT_EQUAL_INT(0, feed_epoch2_ack(&conn, &st, 0, malformed, sizeof(malformed), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0, pc_dtls_conn_alert(&conn));
    TEST_ASSERT_EQUAL_INT((int)PC_DTLS_PTO_INITIAL_MS, pc_dtls_conn_timeout_ms(&conn)); // still armed

    // A partial ACK: only the epoch-0 ServerHello record, not the four epoch-2 messages.
    DtlsRecordNumber one = {0, 0};
    uint8_t body[2 + 16];
    size_t bl = pc_dtls_ack_build(&one, 1, body, sizeof(body));
    TEST_ASSERT_TRUE(bl > 0);
    TEST_ASSERT_EQUAL_INT(0, feed_epoch2_ack(&conn, &st, 1, body, bl, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT((int)PC_DTLS_PTO_INITIAL_MS, pc_dtls_conn_timeout_ms(&conn)); // still armed
}

// After a complete ACK disarms the timer, a replay of that record is dropped by the anti-replay
// window and a later ACK is ignored (nothing is outstanding) - and the handshake still completes.
static void test_ack_replay_and_late_ack_ignored(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&conn, &cfg, &st));
    uint8_t out[256];

    // ACK the whole flight: ServerHello (epoch 0, seq 0) + the four epoch-2 messages (seq 0..3).
    DtlsRecordNumber rns[5] = {{0, 0}, {2, 0}, {2, 1}, {2, 2}, {2, 3}};
    uint8_t body[2 + 5 * 16];
    size_t bl = pc_dtls_ack_build(rns, 5, body, sizeof(body));
    TEST_ASSERT_TRUE(bl > 0);
    uint8_t rec[192];
    size_t rl = pc_dtls_ciphertext_protect(&st.cli_hs_write, 0, PC_DTLS_CT_ACK, body, bl, rec, sizeof(rec), NULL, 0);
    TEST_ASSERT_TRUE(rl > 0);
    TEST_ASSERT_EQUAL_INT(0, pc_dtls_conn_process(&conn, rec, rl, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_timeout_ms(&conn)); // disarmed

    // The very same record again: dropped as a replay, never re-parsed.
    TEST_ASSERT_EQUAL_INT(0, pc_dtls_conn_process(&conn, rec, rl, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0, pc_dtls_conn_alert(&conn));

    // A fresh ACK now that nothing is outstanding: parsed away without touching the timer.
    TEST_ASSERT_EQUAL_INT(0, feed_epoch2_ack(&conn, &st, 1, body, bl, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0, pc_dtls_conn_alert(&conn));
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_timeout_ms(&conn));

    // None of that disturbed the handshake: the client Finished still completes it.
    TEST_ASSERT_TRUE(feed_client_finished(&conn, &st, 2, out, sizeof(out)) > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_established(&conn));
}

// The client Finished arrives but the caller's buffer cannot hold the completion ACK: the handshake
// still completes and the ACK is emitted on the next call that has room (RFC 9147 §5.8.3).
static void test_completion_ack_deferred_when_out_full(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&conn, &cfg, &st));

    uint8_t tiny[16];
    TEST_ASSERT_EQUAL_INT(0, feed_client_finished(&conn, &st, 0, tiny, sizeof(tiny)));
    TEST_ASSERT_TRUE(pc_dtls_conn_established(&conn));

    // An empty datagram drives nothing but the pending acknowledgement.
    uint8_t out[128];
    int n = pc_dtls_conn_process(&conn, tiny, 0, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    uint8_t pt[64];
    DtlsCiphertext info;
    TEST_ASSERT_TRUE(pc_dtls_ciphertext_unprotect(&st.cli_app_read, 0, out, (size_t)n, pt, sizeof(pt), &info, NULL, 0));
    TEST_ASSERT_EQUAL_UINT8(PC_DTLS_CT_ACK, info.content_type);
}

// The epoch-3 application record paths: nothing is available before the handshake completes, and once
// it has, a record that fails to open, one that is not application data, and a replay are all refused
// while genuine application data round-trips both ways.
static void test_app_records_before_and_after_established(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    const uint8_t payload[5] = {'h', 'e', 'l', 'l', 'o'};
    uint8_t plain[64];
    size_t plen = 0;

    DtlsConn fresh;
    pc_dtls_conn_init(&fresh, &cfg, NULL, 0);
    uint8_t dummy[32];
    memset(dummy, 0x2F, sizeof(dummy));
    TEST_ASSERT_FALSE(pc_dtls_conn_open_app(&fresh, dummy, sizeof(dummy), plain, sizeof(plain), &plen));
    size_t sealed = pc_dtls_conn_seal_app(&fresh, payload, sizeof(payload), plain, sizeof(plain));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)sealed);
    TEST_ASSERT_NULL(pc_dtls_conn_app_write_keys(&fresh));
    TEST_ASSERT_NULL(pc_dtls_conn_app_read_keys(&fresh));
    uint8_t cid_out[PC_DTLS_CID_MAX];
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_dtls_conn_local_cid(&fresh, cid_out));

    DtlsConn conn;
    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&conn, &cfg, &st));
    uint8_t out[256];
    TEST_ASSERT_TRUE(feed_client_finished(&conn, &st, 0, out, sizeof(out)) > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_established(&conn));
    TEST_ASSERT_NOT_NULL(pc_dtls_conn_app_write_keys(&conn));

    // A well-shaped epoch-3 header (0x2F: 001, S=1, L=1, epoch bits 3) over bytes that are not a
    // valid AEAD sealing: the tag check fails.
    uint8_t junk[48];
    memset(junk, 0xA5, sizeof(junk));
    junk[0] = 0x2F;
    junk[3] = 0x00;
    junk[4] = 0x2B; // declared body length 43 = 27 inner bytes + the 16-byte tag
    TEST_ASSERT_FALSE(pc_dtls_conn_open_app(&conn, junk, sizeof(junk), plain, sizeof(plain), &plen));

    // A record that opens cleanly but whose inner content type is not application_data.
    uint8_t ackrec[64];
    size_t al = pc_dtls_ciphertext_protect(&st.cli_app_write, 0, PC_DTLS_CT_ACK, payload, sizeof(payload), ackrec,
                                           sizeof(ackrec), NULL, 0);
    TEST_ASSERT_TRUE(al > 0);
    TEST_ASSERT_FALSE(pc_dtls_conn_open_app(&conn, ackrec, al, plain, sizeof(plain), &plen));

    // Genuine application data opens once, and its replay is refused.
    uint8_t apprec[64];
    size_t pl = pc_dtls_ciphertext_protect(&st.cli_app_write, 1, PC_DTLS_CT_APPLICATION_DATA, payload, sizeof(payload),
                                           apprec, sizeof(apprec), NULL, 0);
    TEST_ASSERT_TRUE(pl > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_open_app(&conn, apprec, pl, plain, sizeof(plain), &plen));
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), (uint32_t)plen);
    TEST_ASSERT_EQUAL_MEMORY(payload, plain, sizeof(payload));
    TEST_ASSERT_FALSE(pc_dtls_conn_open_app(&conn, apprec, pl, plain, sizeof(plain), &plen));

    // The server seals application data the client can open. Its sequence number follows the
    // completion ACK's (both come from the shared epoch-3 send counter).
    uint8_t srec[64];
    size_t sl = pc_dtls_conn_seal_app(&conn, payload, sizeof(payload), srec, sizeof(srec));
    TEST_ASSERT_TRUE(sl > 0);
    DtlsCiphertext info;
    TEST_ASSERT_TRUE(pc_dtls_ciphertext_unprotect(&st.cli_app_read, 1, srec, sl, plain, sizeof(plain), &info, NULL, 0));
    TEST_ASSERT_EQUAL_UINT8(PC_DTLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), (uint32_t)info.pt_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, plain, sizeof(payload));
}

// Connection-id negotiation edge cases (RFC 9146 §3): an id longer than the server can hold is not
// accepted at all, while a zero-length id is legal - the server still picks its own id (from the
// ServerHello random) but places none in the records it sends.
static void test_conn_id_edge_cases(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    uint8_t rec[320];
    uint8_t flight[2048];
    uint8_t cid_out[PC_DTLS_CID_MAX];

    // An over-long connection id: the extension is ignored, so no CID is negotiated.
    uint8_t big_cid[PC_DTLS_CID_MAX + 1];
    memset(big_cid, 0xD1, sizeof(big_cid));
    DtlsConn a;
    pc_dtls_conn_init(&a, &cfg, NULL, 0);
    uint8_t ch_a[256];
    size_t la = build_client_hello_ex(ch_a, client_pub, PROTO_TRUE, NULL, 0, big_cid, sizeof(big_cid), PROTO_FALSE,
                                      TLS_GROUP_X25519, TLS_SIG_ED25519);
    size_t ra = plain_hs_record(rec, sizeof(rec), ch_a, la, 0, 0);
    TEST_ASSERT_TRUE(ra > 0);
    int fa = pc_dtls_conn_process(&a, rec, ra, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fa > 0);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_dtls_conn_local_cid(&a, cid_out));
    DtlsPlaintext pt;
    size_t shl = pc_dtls_plaintext_parse(flight, (size_t)fa, &pt);
    TEST_ASSERT_TRUE(shl > 0);
    TEST_ASSERT_TRUE((flight[shl] & 0x10) == 0); // no C bit on the encrypted flight

    // A zero-length connection id: negotiated, so the server chooses its own 4-byte id.
    const uint8_t empty_cid[1] = {0};
    DtlsConn b;
    pc_dtls_conn_init(&b, &cfg, NULL, 0);
    uint8_t ch_b[256];
    size_t lb = build_client_hello_ex(ch_b, client_pub, PROTO_TRUE, NULL, 0, empty_cid, 0, PROTO_FALSE,
                                      TLS_GROUP_X25519, TLS_SIG_ED25519);
    size_t rb = plain_hs_record(rec, sizeof(rec), ch_b, lb, 0, 0);
    TEST_ASSERT_TRUE(rb > 0);
    int fb = pc_dtls_conn_process(&b, rec, rb, flight, sizeof(flight));
    TEST_ASSERT_TRUE(fb > 0);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PC_DTLS_CONN_LOCAL_CID_LEN, (uint32_t)pc_dtls_conn_local_cid(&b, cid_out));
    TEST_ASSERT_EQUAL_MEMORY(SERVER_RANDOM, cid_out, PC_DTLS_CONN_LOCAL_CID_LEN); // taken from the ServerHello random
    size_t shl_b = pc_dtls_plaintext_parse(flight, (size_t)fb, &pt);
    TEST_ASSERT_TRUE(shl_b > 0);
    TEST_ASSERT_TRUE((flight[shl_b] & 0x10) == 0); // the client's CID is empty, so no C bit
}

// Run the HelloRetryRequest round trip against a connection bound to @p addr / @p addr_len and report
// whether the server accepted the retry (it answered with the full server flight).
static proto_bool hrr_roundtrip_accepted(const uint8_t *addr, size_t addr_len)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, addr, addr_len);

    uint8_t ch1[256];
    size_t ch1_len = build_client_hello_ex(ch1, client_pub, /*with_keyshare=*/PROTO_FALSE, NULL, 0, NULL, 0,
                                           PROTO_FALSE, TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t rec[420];
    size_t r1 = plain_hs_record(rec, sizeof(rec), ch1, ch1_len, 0, 0);
    if (!r1)
    {
        return PROTO_FALSE;
    }
    uint8_t hrr_flight[512];
    int hf = pc_dtls_conn_process(&conn, rec, r1, hrr_flight, sizeof(hrr_flight));
    if (hf <= 0)
    {
        return PROTO_FALSE;
    }

    DtlsPlaintext pt;
    if (!pc_dtls_plaintext_parse(hrr_flight, (size_t)hf, &pt))
    {
        return PROTO_FALSE;
    }
    uint8_t hrr[512];
    size_t hrr_len = frag_to_tls(pt.fragment, pt.frag_len, hrr);
    uint8_t cookie[PC_DTLS_COOKIE_MAX];
    size_t cookie_len = 0;
    if (!hrr_len || !hrr_cookie(hrr, hrr_len, cookie, &cookie_len))
    {
        return PROTO_FALSE;
    }

    uint8_t ch2[320];
    size_t ch2_len = build_client_hello_ex(ch2, client_pub, /*with_keyshare=*/PROTO_TRUE, cookie, cookie_len, NULL, 0,
                                           PROTO_FALSE, TLS_GROUP_X25519, TLS_SIG_ED25519);
    size_t r2 = plain_hs_record(rec, sizeof(rec), ch2, ch2_len, 1, 1);
    if (!r2)
    {
        return PROTO_FALSE;
    }
    uint8_t flight[2048];
    return pc_dtls_conn_process(&conn, rec, r2, flight, sizeof(flight)) > 0;
}

// The bound peer address is optional and is clamped: a non-NULL address of length 0 binds nothing,
// and an over-long address keeps only its first PC_DTLS_PEER_ADDR_MAX bytes. Either way the cookie the
// server mints verifies against what it kept, so the retry is accepted.
static void test_peer_addr_zero_length_and_clamped(void)
{
    TEST_ASSERT_TRUE(hrr_roundtrip_accepted(TEST_PEER_ADDR, 0));
    uint8_t big_addr[PC_DTLS_PEER_ADDR_MAX + 14];
    for (size_t i = 0; i < sizeof(big_addr); i++)
    {
        big_addr[i] = (uint8_t)(0x40 + i);
    }
    TEST_ASSERT_TRUE(hrr_roundtrip_accepted(big_addr, sizeof(big_addr)));
}

// A retry that still carries no key_share is fatal: the server sends at most one HelloRetryRequest,
// so a client cannot loop it (RFC 9147 §5.1).
static void test_hrr_retry_without_keyshare_rejected(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, TEST_PEER_ADDR, sizeof(TEST_PEER_ADDR));

    uint8_t ch[256];
    size_t ch_len = build_client_hello_ex(ch, client_pub, /*with_keyshare=*/PROTO_FALSE, NULL, 0, NULL, 0, PROTO_FALSE,
                                          TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t rec[320];
    size_t r1 = plain_hs_record(rec, sizeof(rec), ch, ch_len, 0, 0);
    TEST_ASSERT_TRUE(r1 > 0);
    uint8_t out[1024];
    TEST_ASSERT_TRUE(pc_dtls_conn_process(&conn, rec, r1, out, sizeof(out)) > 0); // the HelloRetryRequest

    size_t r2 = plain_hs_record(rec, sizeof(rec), ch, ch_len, 1, 1); // the retry, still without a share
    TEST_ASSERT_TRUE(r2 > 0);
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&conn, rec, r2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(40, pc_dtls_conn_alert(&conn)); // handshake_failure
}

// A retry whose cookie is present but does not authenticate (one flipped byte inside its MAC) is
// refused exactly like a missing cookie: the client's address is still unproven (RFC 9147 §5.1).
static void test_hrr_retry_with_corrupt_cookie_rejected(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, TEST_PEER_ADDR, sizeof(TEST_PEER_ADDR));

    uint8_t ch1[256];
    size_t ch1_len = build_client_hello_ex(ch1, client_pub, /*with_keyshare=*/PROTO_FALSE, NULL, 0, NULL, 0,
                                           PROTO_FALSE, TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t rec[420];
    size_t r1 = plain_hs_record(rec, sizeof(rec), ch1, ch1_len, 0, 0);
    TEST_ASSERT_TRUE(r1 > 0);
    uint8_t hrr_flight[512];
    int hf = pc_dtls_conn_process(&conn, rec, r1, hrr_flight, sizeof(hrr_flight));
    TEST_ASSERT_TRUE(hf > 0);

    DtlsPlaintext pt;
    TEST_ASSERT_TRUE(pc_dtls_plaintext_parse(hrr_flight, (size_t)hf, &pt) > 0);
    uint8_t hrr[512];
    size_t hrr_len = frag_to_tls(pt.fragment, pt.frag_len, hrr);
    TEST_ASSERT_TRUE(hrr_len > 0);
    uint8_t cookie[PC_DTLS_COOKIE_MAX];
    size_t cookie_len = 0;
    TEST_ASSERT_TRUE(hrr_cookie(hrr, hrr_len, cookie, &cookie_len));
    TEST_ASSERT_TRUE(cookie_len > 0);
    cookie[cookie_len - 1] ^= 0xFF; // last byte of the HMAC

    uint8_t ch2[320];
    size_t ch2_len = build_client_hello_ex(ch2, client_pub, /*with_keyshare=*/PROTO_TRUE, cookie, cookie_len, NULL, 0,
                                           PROTO_FALSE, TLS_GROUP_X25519, TLS_SIG_ED25519);
    size_t r2 = plain_hs_record(rec, sizeof(rec), ch2, ch2_len, 1, 1);
    TEST_ASSERT_TRUE(r2 > 0);
    uint8_t out[2048];
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_process(&conn, rec, r2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(40, pc_dtls_conn_alert(&conn)); // handshake_failure
}

// Once the handshake is DONE the only handshake message the server still accepts is a retransmitted
// Finished; anything else is an unexpected_message rather than a second pass through the state
// machine.
static void test_non_finished_message_after_done_rejected(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&conn, &cfg, &st));
    uint8_t out[256];
    TEST_ASSERT_TRUE(feed_client_finished(&conn, &st, 0, out, sizeof(out)) > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_established(&conn));

    // A ClientHello in an epoch-2 record, at the message_seq the reassembler is expecting.
    uint8_t stray[14];
    stray[0] = TLS_HS_CLIENT_HELLO;
    stray[1] = 0;
    stray[2] = 0;
    stray[3] = 10;
    memset(stray + 4, 0x33, 10);
    TEST_ASSERT_EQUAL_INT(-1, feed_epoch2_msg(&conn, &st, 1, 1, stray, sizeof(stray), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(10, pc_dtls_conn_alert(&conn)); // unexpected_message
}

// An epoch-2 record whose content type is neither handshake nor ACK decrypts and is consumed, but
// drives nothing: it is not fed to the handshake reassembler and does not become the record number
// the completion ACK covers.
static void test_epoch2_other_content_type_ignored(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    ClientSession st;
    TEST_ASSERT_TRUE(run_to_finished(&conn, &cfg, &st));

    // Application data arriving in epoch 2 (before the handshake finishes): decrypted, replay-marked
    // and dropped, leaving the connection healthy and still awaiting the Finished.
    const uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t rec[128];
    size_t rl = pc_dtls_ciphertext_protect(&st.cli_hs_write, 0, PC_DTLS_CT_APPLICATION_DATA, payload, sizeof(payload),
                                           rec, sizeof(rec), NULL, 0);
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t out[128];
    TEST_ASSERT_EQUAL_INT(0, pc_dtls_conn_process(&conn, rec, rl, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0, pc_dtls_conn_alert(&conn));
    TEST_ASSERT_FALSE(pc_dtls_conn_established(&conn));
    TEST_ASSERT_EQUAL_INT((int)PC_DTLS_PTO_INITIAL_MS, pc_dtls_conn_timeout_ms(&conn)); // still armed

    // The Finished that follows still completes the handshake and is acknowledged.
    TEST_ASSERT_TRUE(feed_client_finished(&conn, &st, 1, out, sizeof(out)) > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_established(&conn));
}

// The retransmission timer is stopped by the DONE state itself, not only by the disarm that normally
// accompanies it: a completed handshake that somehow still has a flight armed never retransmits.
static void test_timer_stopped_by_done_state(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    ClientSession st;
    uint8_t out[512];
    TEST_ASSERT_TRUE(run_to_finished(&conn, &cfg, &st));
    TEST_ASSERT_TRUE(feed_client_finished(&conn, &st, 0, out, sizeof(out)) > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_established(&conn));

    // Re-arm the timer behind the state machine's back: DONE must still veto the retransmission.
    conn.awaiting_reply = PROTO_TRUE;
    g_ms += PC_DTLS_PTO_MAX_MS;
    TEST_ASSERT_EQUAL_INT(-1, pc_dtls_conn_timeout_ms(&conn));
    TEST_ASSERT_EQUAL_INT(0, pc_dtls_conn_on_timeout(&conn, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0, pc_dtls_conn_alert(&conn)); // nothing was disturbed
}

// "Established" means the handshake finished AND the epoch-3 application keys were installed: reaching
// DONE without them is not established, so no application record is ever protected with absent keys.
static void test_established_requires_app_keys(void)
{
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);
    DtlsConn conn;
    ClientSession st;
    uint8_t out[512];
    TEST_ASSERT_TRUE(run_to_finished(&conn, &cfg, &st));
    TEST_ASSERT_TRUE(feed_client_finished(&conn, &st, 0, out, sizeof(out)) > 0);
    TEST_ASSERT_TRUE(pc_dtls_conn_established(&conn));

    conn.ep3_ready = PROTO_FALSE;
    TEST_ASSERT_FALSE(pc_dtls_conn_established(&conn));
    TEST_ASSERT_NULL(pc_dtls_conn_app_write_keys(&conn));
    TEST_ASSERT_NULL(pc_dtls_conn_app_read_keys(&conn));
    // ...and with no keys, an application record cannot be opened either.
    uint8_t app[64];
    size_t app_len = 0;
    TEST_ASSERT_FALSE(pc_dtls_conn_open_app(&conn, out, 16, app, sizeof(app), &app_len));
}

// Reporting the local connection id requires both that a CID was negotiated and that it is non-empty:
// a negotiated-but-empty id yields nothing to place in the peer's records.
static void test_local_cid_requires_nonempty_id(void)
{
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_X25519_PRIV);
    uint8_t server_ed_pub[32];
    pc_ed25519_pubkey(server_ed_pub, SERVER_ED_SEED);
    DtlsServerConfig cfg;
    server_cfg(&cfg, server_ed_pub);

    const uint8_t peer_cid[2] = {0x77, 0x88};
    DtlsConn conn;
    pc_dtls_conn_init(&conn, &cfg, NULL, 0);
    uint8_t ch[256];
    size_t chl = build_client_hello_ex(ch, client_pub, PROTO_TRUE, NULL, 0, peer_cid, sizeof(peer_cid), PROTO_FALSE,
                                       TLS_GROUP_X25519, TLS_SIG_ED25519);
    uint8_t rec[320];
    size_t rl = plain_hs_record(rec, sizeof(rec), ch, chl, 0, 0);
    TEST_ASSERT_TRUE(rl > 0);
    uint8_t flight[2048];
    TEST_ASSERT_TRUE(pc_dtls_conn_process(&conn, rec, rl, flight, sizeof(flight)) > 0);
    TEST_ASSERT_TRUE(conn.cid_negotiated);

    uint8_t cid_out[PC_DTLS_CID_MAX];
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PC_DTLS_CONN_LOCAL_CID_LEN, (uint32_t)pc_dtls_conn_local_cid(&conn, cid_out));

    // Negotiated, but with an empty local id: nothing is reported and nothing is written out.
    conn.local_cid_len = 0;
    memset(cid_out, 0xEE, sizeof(cid_out));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_dtls_conn_local_cid(&conn, cid_out));
    TEST_ASSERT_EQUAL_UINT8(0xEE, cid_out[0]); // the caller's buffer was left untouched
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_handshake);
    RUN_TEST(test_timer_stopped_by_done_state);
    RUN_TEST(test_established_requires_app_keys);
    RUN_TEST(test_local_cid_requires_nonempty_id);
    RUN_TEST(test_non_finished_message_after_done_rejected);
    RUN_TEST(test_epoch2_other_content_type_ignored);
    RUN_TEST(test_full_handshake_rpk);
    RUN_TEST(test_cid_handshake);
    RUN_TEST(test_hrr_group_renegotiation);
    RUN_TEST(test_hrr_retry_without_cookie_rejected);
    RUN_TEST(test_pto_retransmit_and_recovery);
    RUN_TEST(test_pto_backoff_and_giveup);
    RUN_TEST(test_pto_ack_cancels_retransmit);
    RUN_TEST(test_reject_no_tls13);
    RUN_TEST(test_ciphertext_truncated_header_stops_walk);
    RUN_TEST(test_ciphertext_before_keys_is_fatal);
    RUN_TEST(test_plaintext_non_handshake_record_ignored);
    RUN_TEST(test_truncated_handshake_fragment_ignored);
    RUN_TEST(test_fragment_for_other_msg_seq_ignored);
    RUN_TEST(test_oversize_handshake_message_rejected);
    RUN_TEST(test_unexpected_message_in_start_rejected);
    RUN_TEST(test_client_hello_missing_algorithms_rejected);
    RUN_TEST(test_oversize_certificate_is_internal_error);
    RUN_TEST(test_flight_out_cap_too_small_is_internal_error);
    RUN_TEST(test_retransmit_out_cap_too_small);
    RUN_TEST(test_timer_idle_when_done_or_failed);
    RUN_TEST(test_client_finished_error_paths);
    RUN_TEST(test_ack_malformed_and_partial_keep_timer);
    RUN_TEST(test_ack_replay_and_late_ack_ignored);
    RUN_TEST(test_completion_ack_deferred_when_out_full);
    RUN_TEST(test_app_records_before_and_after_established);
    RUN_TEST(test_conn_id_edge_cases);
    RUN_TEST(test_peer_addr_zero_length_and_clamped);
    RUN_TEST(test_hrr_retry_without_keyshare_rejected);
    RUN_TEST(test_hrr_retry_with_corrupt_cookie_rejected);
    return UNITY_END();
}
