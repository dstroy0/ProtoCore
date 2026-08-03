// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the TLS 1.3 handshake messages (network_drivers/presentation/http/http3/pc_tls13_msg;
// RFC 8446 sec 4). Byte-exact against the RFC 8448 sec 3 trace where the format is version-agnostic
// (ServerHello, Certificate framing, Finished) and by construction/round-trip elsewhere:
//   - ClientHello parse: extract the client's X25519 key_share and the offers_* capability flags.
//   - ServerHello:  byte-exact vs RFC 8448 (random + empty session id + server X25519 share).
//   - Certificate:  byte-exact vs RFC 8448 (DER reconstructed from the expected message).
//   - Finished:     byte-exact vs RFC 8448 verify_data.
//   - EncryptedExtensions: ALPN "h3" + pc_quic_transport_parameters structure.
//   - CertificateVerify: sec 4.4.3 signed content + an Ed25519 sign/verify round-trip.

#include "crypto/asymmetric/ed25519.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static size_t hx(const char *s, uint8_t *buf, size_t cap)
{
    size_t n = 0;
    int hi = -1;
    for (; *s; s++)
    {
        char c = *s;
        int v;
        if (c >= '0' && c <= '9')
        {
            v = c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            v = c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            v = c - 'A' + 10;
        }
        else
        {
            continue;
        }
        if (hi < 0)
        {
            hi = v;
        }
        else
        {
            TEST_ASSERT_TRUE(n < cap);
            buf[n++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }
    }
    TEST_ASSERT_EQUAL_INT(-1, hi);
    return n;
}

// RFC 8448 sec 3 ClientHello (196 octets).
static const char *CH =
    "01 00 00 c0 03 03 cb 34 ec b1 e7 81 63 ba 1c 38 c6 da cb 19 6a 6d ff a2 1a 8d 99 12 ec 18 a2 ef 62 83"
    "02 4d ec e7 00 00 06 13 01 13 03 13 02 01 00 00 91 00 00 00 0b 00 09 00 00 06 73 65 72 76 65 72 ff 01"
    "00 01 00 00 0a 00 14 00 12 00 1d 00 17 00 18 00 19 01 00 01 01 01 02 01 03 01 04 00 23 00 00 00 33 00"
    "26 00 24 00 1d 00 20 99 38 1d e5 60 e4 bd 43 d2 3d 8e 43 5a 7d ba fe b3 c0 6e 51 c1 3c ae 4d 54 13 69"
    "1e 52 9a af 2c 00 2b 00 03 02 03 04 00 0d 00 20 00 1e 04 03 05 03 06 03 02 03 08 04 08 05 08 06 04 01"
    "05 01 06 01 02 01 04 02 05 02 06 02 02 02 00 2d 00 02 01 01 00 1c 00 02 40 01";
// RFC 8448 sec 3 ServerHello (90 octets).
static const char *SH =
    "02 00 00 56 03 03 a6 af 06 a4 12 18 60 dc 5e 6e 60 24 9c d3 4c 95 93 0c 8a c5 cb 14 34 da c1 55 77 2e"
    "d3 e2 69 28 00 13 01 00 00 2e 00 33 00 24 00 1d 00 20 c9 82 88 76 11 20 95 fe 66 76 2b db f7 c6 72 e1"
    "56 d6 cc 25 3b 83 3d f1 dd 69 b1 b0 4e 75 1f 0f 00 2b 00 02 03 04";
// RFC 8448 sec 3 Certificate (445 octets).
static const char *CERT =
    "0b 00 01 b9 00 00 01 b5 00 01 b0 30 82 01 ac 30 82 01 15 a0 03 02 01 02 02 01 02 30 0d 06 09 2a 86 48"
    "86 f7 0d 01 01 0b 05 00 30 0e 31 0c 30 0a 06 03 55 04 03 13 03 72 73 61 30 1e 17 0d 31 36 30 37 33 30"
    "30 31 32 33 35 39 5a 17 0d 32 36 30 37 33 30 30 31 32 33 35 39 5a 30 0e 31 0c 30 0a 06 03 55 04 03 13"
    "03 72 73 61 30 81 9f 30 0d 06 09 2a 86 48 86 f7 0d 01 01 01 05 00 03 81 8d 00 30 81 89 02 81 81 00 b4"
    "bb 49 8f 82 79 30 3d 98 08 36 39 9b 36 c6 98 8c 0c 68 de 55 e1 bd b8 26 d3 90 1a 24 61 ea fd 2d e4 9a"
    "91 d0 15 ab bc 9a 95 13 7a ce 6c 1a f1 9e aa 6a f9 8c 7c ed 43 12 09 98 e1 87 a8 0e e0 cc b0 52 4b 1b"
    "01 8c 3e 0b 63 26 4d 44 9a 6d 38 e2 2a 5f da 43 08 46 74 80 30 53 0e f0 46 1c 8c a9 d9 ef bf ae 8e a6"
    "d1 d0 3e 2b d1 93 ef f0 ab 9a 80 02 c4 74 28 a6 d3 5a 8d 88 d7 9f 7f 1e 3f 02 03 01 00 01 a3 1a 30 18"
    "30 09 06 03 55 1d 13 04 02 30 00 30 0b 06 03 55 1d 0f 04 04 03 02 05 a0 30 0d 06 09 2a 86 48 86 f7 0d"
    "01 01 0b 05 00 03 81 81 00 85 aa d2 a0 e5 b9 27 6b 90 8c 65 f7 3a 72 67 17 06 18 a5 4c 5f 8a 7b 33 7d"
    "2d f7 a5 94 36 54 17 f2 ea e8 f8 a5 8c 8f 81 72 f9 31 9c f3 6b 7f d6 c5 5b 80 f2 1a 03 01 51 56 72 60"
    "96 fd 33 5e 5e 67 f2 db f1 02 70 2e 60 8c ca e6 be c1 fc 63 a4 2a 99 be 5c 3e b7 10 7c 3c 54 e9 b9 eb"
    "2b d5 20 3b 1c 3b 84 e0 a8 b2 f7 59 40 9b a3 ea c9 d9 1d 40 2d cc 0c c8 f8 96 12 29 ac 91 87 b4 2b 4d"
    "e1 00 00";

void test_parse_client_hello()
{
    uint8_t msg[256];
    size_t n = hx(CH, msg, sizeof(msg));
    Tls13ClientHello ch;
    TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(msg, n, &ch));
    TEST_ASSERT_TRUE(ch.has_key_share);
    uint8_t exp_pub[32];
    hx("99381de560e4bd43d23d8e435a7dbafeb3c06e51c13cae4d5413691e529aaf2c", exp_pub, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_pub, ch.client_x25519, 32);
    TEST_ASSERT_TRUE(ch.offers_tls13);
    TEST_ASSERT_TRUE(ch.offers_x25519);
    TEST_ASSERT_EQUAL_UINT8(0, ch.session_id_len);
    // This plain-TLS trace has no ALPN / ed25519 sig alg / quic params.
    TEST_ASSERT_FALSE(ch.offers_h3_alpn);
    TEST_ASSERT_NULL(ch.pc_quic_tp);
}

void test_build_server_hello()
{
    uint8_t exp[128];
    size_t elen = hx(SH, exp, sizeof(exp));
    uint8_t random[32], pub[32];
    hx("a6af06a4121860dc5e6e60249cd34c95930c8ac5cb1434dac155772ed3e26928", random, 32);
    hx("c9828876112095fe66762bdbf7c672e156d6cc253b833df1dd69b1b04e751f0f", pub, 32);
    uint8_t out[128];
    size_t n = pc_tls13_build_server_hello(out, sizeof(out), random, NULL, 0, pub);
    TEST_ASSERT_EQUAL_UINT(elen, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, out, elen);
}

void test_build_certificate()
{
    uint8_t exp[512];
    size_t elen = hx(CERT, exp, sizeof(exp)); // 445
    // Reconstruct the DER cert from the expected message: strip the 11-byte prefix and 2-byte suffix.
    const uint8_t *der = exp + 11;
    size_t der_len = elen - 13;
    uint8_t out[512];
    size_t n = pc_tls13_build_certificate(out, sizeof(out), der, der_len);
    TEST_ASSERT_EQUAL_UINT(elen, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, out, elen);
}

void test_build_finished()
{
    uint8_t verify[32];
    hx("9b9b141d906337fbd2cbdce71df4deda4ab42c309572cb7fffee5454b78f0718", verify, 32);
    uint8_t out[64];
    size_t n = pc_tls13_build_finished(out, sizeof(out), verify);
    uint8_t exp[64];
    size_t elen = hx("14 00 00 20 9b9b141d906337fbd2cbdce71df4deda4ab42c309572cb7fffee5454b78f0718", exp, sizeof(exp));
    TEST_ASSERT_EQUAL_UINT(elen, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, out, elen);
}

void test_encrypted_extensions()
{
    uint8_t tp[] = {0x04, 0x01, 0x20}; // a tiny transport-params blob
    uint8_t out[64];
    size_t n = pc_tls13_build_encrypted_extensions(out, sizeof(out), tp, sizeof(tp));
    TEST_ASSERT_TRUE(n > 0);
    // Handshake header: type 8, 24-bit length = n - 4.
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_ENCRYPTED_EXTENSIONS, out[0]);
    TEST_ASSERT_EQUAL_UINT(n - 4, (out[1] << 16) | (out[2] << 8) | out[3]);
    // Extensions block length = n - 6, then ALPN ext: 00 10 00 05 00 03 02 'h' '3'.
    TEST_ASSERT_EQUAL_UINT(n - 6, (out[4] << 8) | out[5]);
    static const uint8_t alpn[] = {0x00, 0x10, 0x00, 0x05, 0x00, 0x03, 0x02, 'h', '3'};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(alpn, out + 6, sizeof(alpn));
    // Then pc_quic_transport_parameters ext: 00 39 00 03 <tp>.
    const uint8_t *q = out + 6 + sizeof(alpn);
    TEST_ASSERT_EQUAL_UINT16(TLS_EXT_QUIC_TRANSPORT_PARAMS, (q[0] << 8) | q[1]);
    TEST_ASSERT_EQUAL_UINT16(sizeof(tp), (q[2] << 8) | q[3]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tp, q + 4, sizeof(tp));
}

void test_cert_verify_content()
{
    uint8_t thash[32];
    memset(thash, 0xab, 32);
    uint8_t content[160];
    size_t n = pc_tls13_cert_verify_content(content, sizeof(content), thash, true);
    TEST_ASSERT_EQUAL_UINT(64 + 33 + 1 + 32, n);
    for (int i = 0; i < 64; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0x20, content[i]);
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY("TLS 1.3, server CertificateVerify", content + 64, 33);
    TEST_ASSERT_EQUAL_UINT8(0x00, content[97]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(thash, content + 98, 32);
}

void test_cert_verify_sign_roundtrip()
{
    uint8_t seed[32], pub[32];
    memset(seed, 0x42, 32);
    pc_ed25519_pubkey(pub, seed);
    uint8_t thash[32];
    for (int i = 0; i < 32; i++)
    {
        thash[i] = (uint8_t)i;
    }

    uint8_t out[128];
    size_t n = pc_tls13_build_cert_verify(out, sizeof(out), thash, seed);
    // Header: 0f, len = 2 + 2 + 64 = 68; algorithm ed25519; sig length 64.
    TEST_ASSERT_EQUAL_UINT8(TLS_HS_CERTIFICATE_VERIFY, out[0]);
    TEST_ASSERT_EQUAL_UINT(68, (out[1] << 16) | (out[2] << 8) | out[3]);
    TEST_ASSERT_EQUAL_UINT16(TLS_SIG_ED25519, (out[4] << 8) | out[5]);
    TEST_ASSERT_EQUAL_UINT16(64, (out[6] << 8) | out[7]);
    TEST_ASSERT_EQUAL_UINT(8 + 64, n);

    // Rebuild the signed content and verify the signature.
    uint8_t content[160];
    size_t clen = pc_tls13_cert_verify_content(content, sizeof(content), thash, true);
    TEST_ASSERT_TRUE(pc_ed25519_verify(pub, content, clen, out + 8));
    // A different transcript hash must not verify against the same signature.
    thash[0] ^= 0x01;
    clen = pc_tls13_cert_verify_content(content, sizeof(content), thash, true);
    TEST_ASSERT_FALSE(pc_ed25519_verify(pub, content, clen, out + 8));
}

// Assemble a minimal TLS 1.3 ClientHello wrapping the given extensions block.
static size_t build_ch(uint8_t *msg, const uint8_t *exts, size_t exts_len)
{
    uint8_t body[600];
    size_t b = 0;
    body[b++] = 0x03;
    body[b++] = 0x03; // legacy_version
    for (int j = 0; j < 32; j++)
    {
        body[b++] = 0; // random
    }
    body[b++] = 0; // session_id length 0
    body[b++] = 0x00;
    body[b++] = 0x02;
    body[b++] = 0x13;
    body[b++] = 0x01; // cipher_suites (one)
    body[b++] = 0x01;
    body[b++] = 0x00; // compression_methods (null)
    body[b++] = (uint8_t)(exts_len >> 8);
    body[b++] = (uint8_t)exts_len;
    memcpy(body + b, exts, exts_len);
    b += exts_len;
    size_t m = 0;
    msg[m++] = TLS_HS_CLIENT_HELLO;
    msg[m++] = (uint8_t)(b >> 16);
    msg[m++] = (uint8_t)(b >> 8);
    msg[m++] = (uint8_t)b;
    memcpy(msg + m, body, b);
    return m + b;
}

// A malformed extension body is skipped without failing the overall parse (the guard just returns).
void test_tls13_malformed_extensions()
{
    struct EC
    {
        uint8_t ext[8];
        size_t elen;
    };
    static const EC cases[] = {
        {{0x00, 0x0a, 0x00, 0x01, 0x00}, 5},                   // supported_groups body < 2
        {{0x00, 0x33, 0x00, 0x03, 0x00, 0xFF, 0x00}, 7},       // key_share list len > body
        {{0x00, 0x10, 0x00, 0x03, 0x00, 0xFF, 0x00}, 7},       // ALPN list len > body
        {{0x00, 0x10, 0x00, 0x04, 0x00, 0x02, 0x05, 0x68}, 8}, // ALPN name len > end
        {{0x00, 0x00, 0x00, 0x01, 0x00}, 5},                   // server_name body < 2
        {{0x00, 0x00, 0x00, 0x03, 0x00, 0xFF, 0x00}, 7},       // server_name list len > body
        {{0x00, 0x00, 0x00, 0x02, 0x00, 0x00}, 6},             // server_name too short for an entry
        {{0x00, 0x2c, 0x00, 0x01, 0x00}, 5},                   // cookie body < 2
    };
    uint8_t msg[128];
    Tls13ClientHello ch;
    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++)
    {
        size_t n = build_ch(msg, cases[k].ext, cases[k].elen);
        TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(msg, n, &ch)); // malformed ext skipped, not fatal
    }
}

// ClientHello framing guards: bad type, unreadable/oversized lengths, truncated fields.
void test_tls13_parse_guards()
{
    Tls13ClientHello ch;
    uint8_t bad_type[4] = {0x02, 0, 0, 0};
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(bad_type, sizeof(bad_type), &ch)); // wrong hs type
    uint8_t short_hdr[2] = {TLS_HS_CLIENT_HELLO, 0x00};
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(short_hdr, sizeof(short_hdr), &ch)); // r_u24 body len
    uint8_t big_body[4] = {TLS_HS_CLIENT_HELLO, 0x00, 0x00, 0xFF};
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(big_body, sizeof(big_body), &ch)); // body len > msg
    uint8_t no_ver[5] = {TLS_HS_CLIENT_HELLO, 0x00, 0x00, 0x01, 0x03};
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(no_ver, sizeof(no_ver), &ch)); // r_u16 legacy_version

    // session id length > 32: body_len 35 = version(2)+random(32)+sid_len(1).
    uint8_t sid_big[39] = {TLS_HS_CLIENT_HELLO, 0x00, 0x00, 0x23, 0x03, 0x03};
    sid_big[38] = 33;
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(sid_big, sizeof(sid_big), &ch)); // sid_len > 32

    // session id length 32 but the bytes are not present (r_take fails).
    uint8_t sid_trunc[39] = {TLS_HS_CLIENT_HELLO, 0x00, 0x00, 0x23, 0x03, 0x03};
    sid_trunc[38] = 32;
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(sid_trunc, sizeof(sid_trunc), &ch)); // sid r_take

    // A valid-through-extensions base, then corrupt one internal length field each.
    uint8_t base[64];
    size_t bn = build_ch(base, NULL, 0);
    uint8_t v[64];
    memcpy(v, base, bn);
    v[40] = 3; // cipher_suites length odd
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(v, bn, &ch));
    memcpy(v, base, bn);
    v[43] = 255; // compression_methods length overruns
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(v, bn, &ch));
    memcpy(v, base, bn);
    v[45] = 0xFF;
    v[46] = 0xFF; // extensions_length overruns
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(v, bn, &ch));

    // ext_total unreadable: message ends exactly after compression_methods.
    uint8_t no_ext[44] = {TLS_HS_CLIENT_HELLO, 0x00, 0x00, 0x28, 0x03, 0x03};
    no_ext[38] = 0x00; // sid len 0
    no_ext[39] = 0x00;
    no_ext[40] = 0x02;
    no_ext[41] = 0x13;
    no_ext[42] = 0x01; // cipher_suites
    no_ext[43] = 0x00; // comp_len 0 -> body ends here, no ext_total
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(no_ext, sizeof(no_ext), &ch));

    // An extension whose declared length runs past the buffer.
    uint8_t bad_ext[4] = {0x00, 0x0a, 0x00, 0xFF}; // ext len 255, no body
    uint8_t msg[64];
    size_t mn = build_ch(msg, bad_ext, sizeof(bad_ext));
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(msg, mn, &ch));
}

// Builder capacity guards: a buffer too small for a w_bytes copy, and cert_verify_content overflow.
void test_tls13_builder_cap_guards()
{
    uint8_t out[16];
    uint8_t r32[32] = {0}, pub[32] = {0};
    TEST_ASSERT_EQUAL_UINT(0, pc_tls13_build_server_hello(out, 10, r32, NULL, 0, pub)); // w_bytes(random) overruns
    uint8_t thash[32] = {0};
    TEST_ASSERT_EQUAL_UINT(0, pc_tls13_cert_verify_content(out, 10, thash, true)); // total > cap
}

// Remaining ClientHello parse coverage: the r_u8 compression-length truncation, the
// list16_contains odd-length guard, per-extension short-body guards, a key_share entry
// whose key length overruns, a valid "h3" ALPN, and the pc_quic_transport_parameters extension.
void test_tls13_extension_and_truncation_coverage()
{
    Tls13ClientHello ch;

    // Body ends right after cipher_suites -> r_u8(compression_methods length) truncates.
    uint8_t ct[43] = {TLS_HS_CLIENT_HELLO, 0x00, 0x00, 0x27, 0x03, 0x03};
    ct[38] = 0x00; // session_id length 0
    ct[39] = 0x00;
    ct[40] = 0x02;
    ct[41] = 0x13;
    ct[42] = 0x01; // cipher_suites, then no compression_methods byte
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(ct, sizeof(ct), &ch));

    // One ClientHello carrying malformed and valid extensions. Malformed bodies return
    // from parse_extension without failing the overall parse; the valid ALPN + pc_quic_tp set
    // their flags.
    static const uint8_t ex[] = {
        0x00, 0x2b, 0x00, 0x00,                                     // supported_versions body < 1
        0x00, 0x2b, 0x00, 0x04, 0x03, 0x03, 0x04, 0x00,             // supported_versions odd list length (3)
        0x00, 0x0d, 0x00, 0x01, 0x00,                               // signature_algorithms body < 2
        0x00, 0x33, 0x00, 0x01, 0x00,                               // key_share body < 2
        0x00, 0x33, 0x00, 0x06, 0x00, 0x04, 0x00, 0x1d, 0x00, 0x20, // key_share entry key length overruns
        0x00, 0x10, 0x00, 0x01, 0x00,                               // ALPN body < 2
        0x00, 0x10, 0x00, 0x05, 0x00, 0x03, 0x02, 'h',  '2',        // ALPN "h2" (nl==2, 'h', but not '3')
        0x00, 0x10, 0x00, 0x05, 0x00, 0x03, 0x02, 'h',  '3',        // ALPN offering "h3"
        0x00, 0x39, 0x00, 0x03, 0x04, 0x01, 0x20,                   // pc_quic_transport_parameters
    };
    uint8_t msg[256];
    size_t n = build_ch(msg, ex, sizeof(ex));
    TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(msg, n, &ch));
    TEST_ASSERT_TRUE(ch.offers_h3_alpn);
    TEST_ASSERT_NOT_NULL(ch.pc_quic_tp);
    TEST_ASSERT_EQUAL_UINT(3, (unsigned)ch.pc_quic_tp_len);
}

// Assemble a DTLS-shaped ClientHello (RFC 9147 sec 5.3): identical to build_ch except for the
// legacy_cookie field between legacy_session_id and cipher_suites. @p cookie_len is written as the
// legacy_cookie length and @p cookie_present bytes actually follow it, so a caller can build a
// well-formed hello (equal) or one whose cookie field is truncated (fewer).
static size_t build_ch_dtls(uint8_t *msg, const uint8_t *exts, size_t exts_len, uint8_t cookie_len,
                            uint8_t cookie_present)
{
    uint8_t body[600];
    size_t b = 0;
    body[b++] = 0x03;
    body[b++] = 0x03; // legacy_version
    for (int j = 0; j < 32; j++)
    {
        body[b++] = 0; // random
    }
    body[b++] = 0; // session_id length 0
    body[b++] = cookie_len;
    for (uint8_t j = 0; j < cookie_present; j++)
    {
        body[b++] = (uint8_t)(0xC0 + j); // legacy_cookie bytes
    }
    body[b++] = 0x00;
    body[b++] = 0x02;
    body[b++] = 0x13;
    body[b++] = 0x01; // cipher_suites (one)
    body[b++] = 0x01;
    body[b++] = 0x00; // compression_methods (null)
    body[b++] = (uint8_t)(exts_len >> 8);
    body[b++] = (uint8_t)exts_len;
    if (exts_len)
    {
        memcpy(body + b, exts, exts_len);
    }
    b += exts_len;
    size_t m = 0;
    msg[m++] = TLS_HS_CLIENT_HELLO;
    msg[m++] = (uint8_t)(b >> 16);
    msg[m++] = (uint8_t)(b >> 8);
    msg[m++] = (uint8_t)b;
    memcpy(msg + m, body, b);
    return m + b;
}

// A ClientHello header whose 24-bit body length is @p body_len while only @p body_len bytes of a
// fixed filler body follow - used to cut the parse off at an exact field boundary.
static size_t build_ch_stub(uint8_t *msg, size_t body_len)
{
    msg[0] = TLS_HS_CLIENT_HELLO;
    msg[1] = (uint8_t)(body_len >> 16);
    msg[2] = (uint8_t)(body_len >> 8);
    msg[3] = (uint8_t)body_len;
    msg[4] = 0x03;
    msg[5] = 0x03; // legacy_version, then zero filler
    for (size_t i = 6; i < 4 + body_len; i++)
    {
        msg[i] = 0x00;
    }
    return 4 + body_len;
}

// The DTLS ClientHello shape: the legacy_cookie field is present and consumed (so the extensions
// still parse), and truncating that field - the length byte itself, or the bytes it announces -
// fails the parse. Also pins that DTLS selects the 0xFEFC supported_versions codepoint, not 0x0304.
void test_tls13_dtls_client_hello_shape()
{
    // supported_versions offering DTLS 1.3 (0xFEFC).
    static const uint8_t sv_dtls[] = {0x00, 0x2b, 0x00, 0x03, 0x02, 0xFE, 0xFC};
    uint8_t msg[256];
    Tls13ClientHello ch;

    size_t n = build_ch_dtls(msg, sv_dtls, sizeof(sv_dtls), 0, 0);
    TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(msg, n, &ch, /*dtls=*/true));
    TEST_ASSERT_TRUE(ch.offers_tls13); // matched against PC_TLS_VERSION_DTLS_1_3

    // A non-empty legacy_cookie is skipped just the same.
    n = build_ch_dtls(msg, sv_dtls, sizeof(sv_dtls), 4, 4);
    TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(msg, n, &ch, /*dtls=*/true));
    TEST_ASSERT_TRUE(ch.offers_tls13);

    // The same bytes read as TLS/QUIC (no legacy_cookie field) do NOT yield a DTLS 1.3 offer:
    // the field misaligns everything after it.
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(msg, n, &ch, /*dtls=*/false) && ch.offers_tls13);

    // The legacy_cookie length byte announces more bytes than are present -> parse fails.
    n = build_ch_dtls(msg, sv_dtls, sizeof(sv_dtls), 200, 0);
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(msg, n, &ch, /*dtls=*/true));

    // The message ends exactly after legacy_session_id, so the cookie length byte is absent.
    uint8_t stub[64];
    size_t sn = build_ch_stub(stub, 35); // legacy_version(2) + random(32) + session_id length(1)
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(stub, sn, &ch, /*dtls=*/true));
}

// Every ClientHello field boundary the reader can run off: an empty message, and a body cut short
// at the random, the session-id length, the cipher-suite list, and the extension header fields.
void test_tls13_client_hello_field_truncations()
{
    Tls13ClientHello ch;
    uint8_t msg[64];

    // No bytes at all: even the handshake type cannot be read.
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(msg, 0, &ch));

    // legacy_version present, but fewer than 32 random bytes follow.
    size_t n = build_ch_stub(msg, 10);
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(msg, n, &ch));

    // Body ends exactly after the random: no session-id length byte.
    n = build_ch_stub(msg, 34);
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(msg, n, &ch));

    // Body ends after the (empty) session id: no cipher_suites length.
    n = build_ch_stub(msg, 36);
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(msg, n, &ch));

    // cipher_suites length announces more bytes than the body holds.
    n = build_ch_stub(msg, 40);
    msg[4 + 35] = 0x00;
    msg[4 + 36] = 0x20; // 32 octets of cipher suites, but only 2 follow
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(msg, n, &ch));

    // extensions_length says 1, so the extension type's two bytes cannot be read.
    uint8_t one[1] = {0x00};
    n = build_ch(msg, one, sizeof(one));
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(msg, n, &ch));

    // extensions_length says 3: the type reads, the extension length's two bytes do not.
    uint8_t three[3] = {0x00, 0x0a, 0x00};
    n = build_ch(msg, three, sizeof(three));
    TEST_ASSERT_FALSE(pc_tls13_parse_client_hello(msg, n, &ch));
}

// Extension-body guards that return without failing the overall parse: an oversized supported_groups
// list, a key_share entry whose key length is not 32, ALPN entries that are not "h3", a server_name
// entry that is not a host_name or overruns, and malformed cookie / connection_id bodies.
void test_tls13_extension_body_guards()
{
    struct EC
    {
        uint8_t ext[16];
        size_t elen;
    };
    static const EC cases[] = {
        // supported_groups: declared list length (255) exceeds the extension body.
        {{0x00, 0x0a, 0x00, 0x04, 0x00, 0xFF, 0x00, 0x1d}, 8},
        // key_share: an X25519 entry whose key_exchange is 4 bytes, not 32.
        {{0x00, 0x33, 0x00, 0x0a, 0x00, 0x08, 0x00, 0x1d, 0x00, 0x04, 0xAA, 0xBB, 0xCC, 0xDD}, 14},
        // key_share: an entry for a group that is not X25519 (0x0017); the "group == X25519" check
        // short-circuits false and its key length is never inspected.
        {{0x00, 0x33, 0x00, 0x0a, 0x00, 0x08, 0x00, 0x17, 0x00, 0x04, 0xAA, 0xAA, 0xAA, 0xAA}, 14},
        // ALPN: a 3-byte name, then a 2-byte name that is not "h3".
        {{0x00, 0x10, 0x00, 0x09, 0x00, 0x07, 0x03, 'a', 'b', 'c', 0x02, 'x', '3'}, 13},
        // server_name: a name type that is not host_name(0).
        {{0x00, 0x00, 0x00, 0x08, 0x00, 0x06, 0x01, 0x00, 0x03, 'a', 'b', 'c'}, 12},
        // server_name: host_name whose declared length runs past the list.
        {{0x00, 0x00, 0x00, 0x05, 0x00, 0x03, 0x00, 0x00, 0x10}, 9},
        // cookie: the inner cookie length overruns the extension body.
        {{0x00, 0x2c, 0x00, 0x02, 0xFF, 0xFF}, 6},
        // connection_id: an empty extension body (no length byte).
        {{0x00, 0x36, 0x00, 0x00}, 4},
        // connection_id: the declared id length overruns the body.
        {{0x00, 0x36, 0x00, 0x01, 0x05}, 5},
    };
    uint8_t msg[128];
    Tls13ClientHello ch;
    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++)
    {
        size_t n = build_ch(msg, cases[k].ext, cases[k].elen);
        TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(msg, n, &ch)); // guard returns, parse survives
    }
    // None of the malformed bodies set their flag.
    TEST_ASSERT_FALSE(ch.has_conn_id);

    // The well-formed cookie and connection_id extensions ARE captured (the two switch arms the
    // DTLS handshake relies on).
    static const uint8_t good[] = {
        0x00, 0x2c, 0x00, 0x05, 0x00, 0x03, 0x11, 0x22, 0x33, // cookie -> 11 22 33
        0x00, 0x36, 0x00, 0x03, 0x02, 0xAA, 0xBB,             // connection_id -> AA BB
    };
    size_t n = build_ch(msg, good, sizeof(good));
    TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(msg, n, &ch));
    TEST_ASSERT_NOT_NULL(ch.cookie);
    TEST_ASSERT_EQUAL_UINT(3, (unsigned)ch.cookie_len);
    TEST_ASSERT_EQUAL_UINT8(0x11, ch.cookie[0]);
    TEST_ASSERT_TRUE(ch.has_conn_id);
    TEST_ASSERT_EQUAL_UINT(2, (unsigned)ch.conn_id_len);
    TEST_ASSERT_EQUAL_UINT8(0xAA, ch.conn_id[0]);
    // A valid key_share alongside them still parses (the X25519 arm with the right length).
    TEST_ASSERT_FALSE(ch.has_key_share);
}

// The DTLS version codepoints in the ServerHello / HelloRetryRequest builders (RFC 9147 sec 5.3):
// legacy_version 0xFEFD and supported_versions 0xFEFC, where TLS uses 0x0303 / 0x0304.
void test_tls13_builders_dtls_codepoints()
{
    uint8_t random[32], pub[32];
    memset(random, 0x5A, sizeof(random));
    memset(pub, 0x6B, sizeof(pub));
    uint8_t out[128];

    size_t n = pc_tls13_build_server_hello(out, sizeof(out), random, NULL, 0, pub, 32, TLS_GROUP_X25519,
                                           /*dtls=*/true);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT8(0xFE, out[4]); // legacy_version = DTLS 1.2
    TEST_ASSERT_EQUAL_UINT8(0xFD, out[5]);
    // supported_versions -> 0xFEFC is the last extension for a CID-less ServerHello.
    TEST_ASSERT_EQUAL_UINT8(0xFE, out[n - 2]);
    TEST_ASSERT_EQUAL_UINT8(0xFC, out[n - 1]);

    // The TLS/QUIC form of the same call uses 0x0303 / 0x0304.
    n = pc_tls13_build_server_hello(out, sizeof(out), random, NULL, 0, pub, 32, TLS_GROUP_X25519, /*dtls=*/false);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[4]);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[5]);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[n - 2]);
    TEST_ASSERT_EQUAL_UINT8(0x04, out[n - 1]);

    // HelloRetryRequest, both profiles, with and without a cookie extension.
    const uint8_t cookie[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    size_t with_cookie = pc_tls13_build_hello_retry_request(out, sizeof(out), NULL, 0, TLS_GROUP_X25519, cookie,
                                                            sizeof(cookie), /*dtls=*/true);
    TEST_ASSERT_TRUE(with_cookie > 0);
    TEST_ASSERT_EQUAL_UINT8(0xFE, out[4]);
    TEST_ASSERT_EQUAL_UINT8(0xFD, out[5]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pc_tls13_hrr_random, out + 6, 32);

    // No cookie -> the cookie extension is omitted entirely (a shorter message), and the TLS
    // codepoints are used.
    size_t no_cookie = pc_tls13_build_hello_retry_request(out, sizeof(out), NULL, 0, TLS_GROUP_X25519, NULL, 0,
                                                          /*dtls=*/false);
    TEST_ASSERT_TRUE(no_cookie > 0);
    TEST_ASSERT_TRUE(no_cookie < with_cookie);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[4]);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[5]);
}

// Every builder's capacity guard: a cookie too long for the extension's uint16 length, and an
// output buffer too small for the HelloRetryRequest, either EncryptedExtensions form, and
// message_hash. Each must report 0 rather than emit a truncated message.
void test_tls13_builder_overflow_guards()
{
    uint8_t out[128];
    const uint8_t cookie[4] = {0xAA, 0xBB, 0xCC, 0xDD};

    // cookie_len + 2 must fit a uint16: refused before anything is written.
    TEST_ASSERT_EQUAL_UINT(0, pc_tls13_build_hello_retry_request(out, sizeof(out), NULL, 0, TLS_GROUP_X25519, cookie,
                                                                 0xFFFE, /*dtls=*/false));

    // A HelloRetryRequest that does not fit: sweep every capacity below the needed one.
    size_t need = pc_tls13_build_hello_retry_request(out, sizeof(out), NULL, 0, TLS_GROUP_X25519, cookie,
                                                     sizeof(cookie), /*dtls=*/true);
    TEST_ASSERT_TRUE(need > 0);
    TEST_ASSERT_EQUAL_UINT(0, pc_tls13_build_hello_retry_request(out, need - 1, NULL, 0, TLS_GROUP_X25519, cookie,
                                                                 sizeof(cookie), /*dtls=*/true));

    // The empty (DTLS-profile) EncryptedExtensions is 6 bytes; 5 is not enough.
    TEST_ASSERT_EQUAL_UINT(6, pc_tls13_build_encrypted_extensions_empty(out, 6));
    TEST_ASSERT_EQUAL_UINT(0, pc_tls13_build_encrypted_extensions_empty(out, 5));

    // message_hash is 36 bytes; 35 is not enough.
    uint8_t h[32];
    memset(h, 0x77, sizeof(h));
    TEST_ASSERT_EQUAL_UINT(36, pc_tls13_build_message_hash(out, 36, h));
    TEST_ASSERT_EQUAL_UINT(0, pc_tls13_build_message_hash(out, 35, h));

    // The QUIC EncryptedExtensions (ALPN + transport params) in a buffer one byte short.
    const uint8_t tp[3] = {0x04, 0x01, 0x20};
    need = pc_tls13_build_encrypted_extensions(out, sizeof(out), tp, sizeof(tp));
    TEST_ASSERT_TRUE(need > 0);
    TEST_ASSERT_EQUAL_UINT(0, pc_tls13_build_encrypted_extensions(out, need - 1, tp, sizeof(tp)));

    // Certificate: a buffer one byte short of what a small DER cert needs.
    uint8_t der[8] = {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x02};
    need = pc_tls13_build_certificate(out, sizeof(out), der, sizeof(der));
    TEST_ASSERT_TRUE(need > 0);
    TEST_ASSERT_EQUAL_UINT(0, pc_tls13_build_certificate(out, need - 1, der, sizeof(der)));

    // CertificateVerify: a buffer one byte short of what the Ed25519 signature needs.
    uint8_t thash2[32], seed[32];
    memset(thash2, 0x33, sizeof(thash2));
    memset(seed, 0x44, sizeof(seed));
    need = pc_tls13_build_cert_verify(out, sizeof(out), thash2, seed);
    TEST_ASSERT_TRUE(need > 0);
    TEST_ASSERT_EQUAL_UINT(0, pc_tls13_build_cert_verify(out, need - 1, thash2, seed));

    // Finished is exactly 36 bytes; 35 is not enough.
    uint8_t verify[32];
    memset(verify, 0x55, sizeof(verify));
    TEST_ASSERT_EQUAL_UINT(36, pc_tls13_build_finished(out, 36, verify));
    TEST_ASSERT_EQUAL_UINT(0, pc_tls13_build_finished(out, 35, verify));
}

// The CertificateVerify signed content uses the "client" context string when is_server is false
// (RFC 8446 sec 4.4.3) - the same length, a different label, so the two never cross-verify.
void test_tls13_cert_verify_client_context()
{
    uint8_t thash[32];
    memset(thash, 0x5c, sizeof(thash));
    uint8_t server[160], client[160];
    size_t sn = pc_tls13_cert_verify_content(server, sizeof(server), thash, true);
    size_t cn = pc_tls13_cert_verify_content(client, sizeof(client), thash, false);
    TEST_ASSERT_EQUAL_UINT(sn, cn);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("TLS 1.3, client CertificateVerify", client + 64, 33);
    TEST_ASSERT_EQUAL_UINT8(0x00, client[97]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(thash, client + 98, 32);
    TEST_ASSERT_TRUE(memcmp(server, client, sn) != 0); // the context word differs
}

// pc_tls13_build_server_hello with a non-NULL conn_id emits a trailing connection_id extension
// (RFC 9146 / RFC 9147 sec 9); the default (NULL) call omits it entirely.
void test_tls13_build_server_hello_conn_id()
{
    uint8_t random[32], pub[32];
    memset(random, 0x11, sizeof(random));
    memset(pub, 0x22, sizeof(pub));
    const uint8_t conn_id[3] = {0xC1, 0xC2, 0xC3};

    uint8_t out[128];
    size_t without = pc_tls13_build_server_hello(out, sizeof(out), random, NULL, 0, pub);
    size_t with = pc_tls13_build_server_hello(out, sizeof(out), random, NULL, 0, pub, 32, TLS_GROUP_X25519,
                                              /*dtls=*/false, conn_id, sizeof(conn_id));
    TEST_ASSERT_TRUE(with > without);
    // connection_id ext: type 0036, ext body length 0004, cid length 03, then the CID bytes.
    static const uint8_t tail[] = {0x00, 0x36, 0x00, 0x04, 0x03, 0xC1, 0xC2, 0xC3};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tail, out + with - sizeof(tail), sizeof(tail));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_tls13_extension_and_truncation_coverage);
    RUN_TEST(test_tls13_dtls_client_hello_shape);
    RUN_TEST(test_tls13_client_hello_field_truncations);
    RUN_TEST(test_tls13_extension_body_guards);
    RUN_TEST(test_tls13_builders_dtls_codepoints);
    RUN_TEST(test_tls13_builder_overflow_guards);
    RUN_TEST(test_tls13_cert_verify_client_context);
    RUN_TEST(test_tls13_malformed_extensions);
    RUN_TEST(test_tls13_parse_guards);
    RUN_TEST(test_tls13_builder_cap_guards);
    RUN_TEST(test_parse_client_hello);
    RUN_TEST(test_build_server_hello);
    RUN_TEST(test_tls13_build_server_hello_conn_id);
    RUN_TEST(test_build_certificate);
    RUN_TEST(test_build_finished);
    RUN_TEST(test_encrypted_extensions);
    RUN_TEST(test_cert_verify_content);
    RUN_TEST(test_cert_verify_sign_roundtrip);
    return UNITY_END();
}
