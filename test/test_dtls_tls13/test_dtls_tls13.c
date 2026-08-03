// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TLS 1.3 messages the DTLS 1.3 handshake adds to pc_tls13_msg (RFC 8446 §4.1.4 / §4.4.1): the
// HelloRetryRequest builder, the cookie extension parse, the empty EncryptedExtensions, and the
// message_hash transcript wrapper. Compiled with PC_ENABLE_DTLS (not HTTP/3) to prove the module
// builds and works for the DTLS path. The HelloRetryRequest is pinned byte-for-byte, anchored on the
// RFC 8446 §4.1.3 magic random.

#include "crypto/asymmetric/ed25519.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// The HelloRetryRequest magic random is SHA-256("HelloRetryRequest") (RFC 8446 §4.1.3).
static const uint8_t HRR_MAGIC[32] = {0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C,
                                      0x02, 0x1E, 0x65, 0xB8, 0x91, 0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB,
                                      0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};

// The whole DTLS 1.3 HelloRetryRequest for (session_id="", group=X25519, cookie=AA BB CC DD),
// byte-for-byte. DTLS carries the 0xFEFD / 0xFEFC version codepoints (RFC 9147 §5.3), not 0x0303 / 0x0304.
static const uint8_t HRR_WIRE[66] = {0x02, 0x00, 0x00, 0x3e,                         // server_hello, length 62
                                     0xFE, 0xFD,                                     // legacy_version (DTLS 1.2)
                                     0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, // \_ HRR magic random ...
                                     0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91, //
                                     0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E, //
                                     0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C, // ... /
                                     0x00,                                           // legacy_session_id_echo: empty
                                     0x13, 0x01,                         // cipher_suite TLS_AES_128_GCM_SHA256
                                     0x00,                               // legacy_compression_method
                                     0x00, 0x16,                         // extensions length 22
                                     0x00, 0x2b, 0x00, 0x02, 0xFE, 0xFC, // supported_versions -> DTLS 1.3 (0xFEFC)
                                     0x00, 0x33, 0x00, 0x02, 0x00, 0x1d, // key_share (HRR) -> selected_group X25519
                                     0x00, 0x2c, 0x00, 0x06, 0x00, 0x04, 0xAA, 0xBB,
                                     0xCC, 0xDD}; // cookie -> AA BB CC DD

static void test_hrr_magic_symbol(void)
{
    // The builder and the RFC constant agree.
    TEST_ASSERT_EQUAL_MEMORY(HRR_MAGIC, pc_tls13_hrr_random, 32);
}

static void test_hrr_build_kat(void)
{
    const uint8_t cookie[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t out[128];
    size_t n =
        pc_tls13_build_hello_retry_request(out, sizeof(out), NULL, 0, TLS_GROUP_X25519, cookie, sizeof(cookie),
                                           /*dtls=*/true);
    TEST_ASSERT_EQUAL_size_t(sizeof(HRR_WIRE), n);
    TEST_ASSERT_EQUAL_MEMORY(HRR_WIRE, out, sizeof(HRR_WIRE));
    // The random field carries the magic, i.e. this ServerHello is a HelloRetryRequest.
    TEST_ASSERT_EQUAL_MEMORY(HRR_MAGIC, out + 6, 32);
}

static void test_hrr_echoes_session_id(void)
{
    const uint8_t sid[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    const uint8_t cookie[2] = {0x01, 0x02};
    uint8_t out[128];
    size_t n = pc_tls13_build_hello_retry_request(out, sizeof(out), sid, sizeof(sid), TLS_GROUP_X25519, cookie,
                                                  sizeof(cookie), /*dtls=*/true);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT8(sizeof(sid), out[38]);        // legacy_session_id_echo length
    TEST_ASSERT_EQUAL_MEMORY(sid, out + 39, sizeof(sid)); // echoed verbatim
}

static void test_message_hash(void)
{
    uint8_t h[32];
    for (unsigned i = 0; i < 32; i++)
    {
        h[i] = (uint8_t)(0x80 + i);
    }
    uint8_t out[40];
    size_t n = pc_tls13_build_message_hash(out, sizeof(out), h);
    TEST_ASSERT_EQUAL_size_t(36, n);
    // message_hash(254) || uint24(32) || Hash(CH1)
    TEST_ASSERT_EQUAL_UINT8(254, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0x20, out[3]);
    TEST_ASSERT_EQUAL_MEMORY(h, out + 4, 32);
}

static void test_empty_encrypted_extensions(void)
{
    uint8_t out[16];
    size_t n = pc_tls13_build_encrypted_extensions_empty(out, sizeof(out));
    const uint8_t want[6] = {0x08, 0x00, 0x00, 0x02, 0x00, 0x00}; // EE, len 2, extensions length 0
    TEST_ASSERT_EQUAL_size_t(sizeof(want), n);
    TEST_ASSERT_EQUAL_MEMORY(want, out, sizeof(want));
}

// A ClientHello carrying a cookie extension (the client's retry) is parsed and the cookie recovered.
static void test_client_hello_cookie_parse(void)
{
    const uint8_t cookie[3] = {0x11, 0x22, 0x33};
    // Assemble a minimal but well-formed ClientHello with exactly one extension (cookie).
    uint8_t ch[128];
    size_t p = 0;
    ch[p++] = 0x01; // client_hello
    size_t body_len_at = p;
    p += 3; // 24-bit body length (patched below)
    ch[p++] = 0x03;
    ch[p++] = 0x03; // legacy_version
    for (int i = 0; i < 32; i++)
    {
        ch[p++] = (uint8_t)i; // random
    }
    ch[p++] = 0x00; // session_id length 0
    ch[p++] = 0x00;
    ch[p++] = 0x02;
    ch[p++] = 0x13;
    ch[p++] = 0x01; // cipher_suites: TLS_AES_128_GCM_SHA256
    ch[p++] = 0x01;
    ch[p++] = 0x00; // compression_methods: [null]
    // extensions: total length, then cookie extension.
    uint16_t ext_body = (uint16_t)(2 + sizeof(cookie)); // cookie<2> length prefix + cookie
    uint16_t ext_total = (uint16_t)(4 + ext_body);      // ext type(2) + ext len(2) + body
    ch[p++] = (uint8_t)(ext_total >> 8);
    ch[p++] = (uint8_t)ext_total;
    ch[p++] = 0x00;
    ch[p++] = 0x2c; // cookie extension type
    ch[p++] = (uint8_t)(ext_body >> 8);
    ch[p++] = (uint8_t)ext_body;
    ch[p++] = (uint8_t)(sizeof(cookie) >> 8);
    ch[p++] = (uint8_t)sizeof(cookie); // cookie length
    for (unsigned i = 0; i < sizeof(cookie); i++)
    {
        ch[p++] = cookie[i];
    }
    uint32_t body_len = (uint32_t)(p - body_len_at - 3);
    ch[body_len_at] = (uint8_t)(body_len >> 16);
    ch[body_len_at + 1] = (uint8_t)(body_len >> 8);
    ch[body_len_at + 2] = (uint8_t)body_len;

    Tls13ClientHello hello;
    TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(ch, p, &hello));
    TEST_ASSERT_NOT_NULL(hello.cookie);
    TEST_ASSERT_EQUAL_size_t(sizeof(cookie), hello.cookie_len);
    TEST_ASSERT_EQUAL_MEMORY(cookie, hello.cookie, sizeof(cookie));
}

// ---------------------------------------------------------------------------
// RFC 7250 Raw Public Keys (PC_ENABLE_TLS_RPK)
// ---------------------------------------------------------------------------

// The fixed 12-byte DER prefix of an Ed25519 SubjectPublicKeyInfo (RFC 8410 §4): SEQUENCE {
// SEQUENCE { OID 1.3.101.112 }, BIT STRING(33, 0 unused) }. Confirmed against `openssl ... -pubout`.
static const uint8_t SPKI_PREFIX[12] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};

static void test_ed25519_spki(void)
{
    uint8_t pub[32];
    for (int i = 0; i < 32; i++)
    {
        pub[i] = (uint8_t)(0xA0 + i);
    }
    uint8_t spki[PC_TLS13_ED25519_SPKI_LEN];
    size_t n = pc_tls13_ed25519_spki(spki, sizeof(spki), pub);
    TEST_ASSERT_EQUAL_size_t(44, n);
    TEST_ASSERT_EQUAL_MEMORY(SPKI_PREFIX, spki, sizeof(SPKI_PREFIX)); // fixed AlgorithmIdentifier + BIT STRING head
    TEST_ASSERT_EQUAL_MEMORY(pub, spki + 12, 32);                     // then the raw 32-byte key
    // Too small a buffer fails cleanly.
    uint8_t small[43];
    TEST_ASSERT_EQUAL_size_t(0, pc_tls13_ed25519_spki(small, sizeof(small), pub));
}

static void test_build_certificate_rpk(void)
{
    // Derive a real public key from a seed, so the test spans seed -> pubkey -> SPKI -> Certificate.
    uint8_t seed[32];
    for (int i = 0; i < 32; i++)
    {
        seed[i] = (uint8_t)(i * 7 + 1);
    }
    uint8_t pub[32];
    pc_ed25519_pubkey(pub, seed);

    uint8_t out[80];
    size_t n = pc_tls13_build_certificate_rpk(out, sizeof(out), pub);
    // Certificate: type(11) len24 | ctx_len(0) | list_len24 | entry_len24(44) | SPKI(44) | entry_ext(0000)
    TEST_ASSERT_EQUAL_size_t(4 + 1 + 3 + 3 + 44 + 2, n);
    TEST_ASSERT_EQUAL_UINT8(0x0b, out[0]); // TLS_HS_CERTIFICATE
    TEST_ASSERT_EQUAL_UINT8(0x00, out[4]); // empty certificate_request_context
    TEST_ASSERT_EQUAL_UINT8(44, out[10]);  // CertificateEntry cert_data length low byte (44)
    TEST_ASSERT_EQUAL_MEMORY(SPKI_PREFIX, out + 11, sizeof(SPKI_PREFIX)); // the SPKI DER prefix
    TEST_ASSERT_EQUAL_MEMORY(pub, out + 11 + 12, 32);                     // carrying the derived public key
    TEST_ASSERT_EQUAL_UINT8(0x00, out[11 + 44]);                          // entry extensions length = 0
    TEST_ASSERT_EQUAL_UINT8(0x00, out[11 + 44 + 1]);
}

static void test_ee_rpk_extension(void)
{
    // The empty (DTLS-profile) EncryptedExtensions with RPK selected carries server_certificate_type.
    uint8_t out[32];
    size_t n = pc_tls13_build_encrypted_extensions_empty(out, sizeof(out), /*rpk_server_cert=*/true);
    const uint8_t want[11] = {0x08, 0x00, 0x00, 0x07,        // EncryptedExtensions, length 7
                              0x00, 0x05,                    // extensions length 5
                              0x00, 0x14, 0x00, 0x01, 0x02}; // server_certificate_type(20) -> RawPublicKey(2)
    TEST_ASSERT_EQUAL_size_t(sizeof(want), n);
    TEST_ASSERT_EQUAL_MEMORY(want, out, sizeof(want));
    // Default (no RPK) stays byte-identical to the historical empty EE.
    n = pc_tls13_build_encrypted_extensions_empty(out, sizeof(out));
    const uint8_t empty[6] = {0x08, 0x00, 0x00, 0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(empty), n);
    TEST_ASSERT_EQUAL_MEMORY(empty, out, sizeof(empty));
}

// Assemble a minimal ClientHello carrying exactly one extension (type @p etype, body @p ebody).
static size_t build_ch_one_ext(uint8_t *ch, uint16_t etype, const uint8_t *ebody, uint16_t ebody_len)
{
    size_t p = 0;
    ch[p++] = 0x01; // client_hello
    size_t body_len_at = p;
    p += 3;
    ch[p++] = 0x03;
    ch[p++] = 0x03; // legacy_version
    for (int i = 0; i < 32; i++)
    {
        ch[p++] = (uint8_t)i; // random
    }
    ch[p++] = 0x00; // session_id length 0
    ch[p++] = 0x00;
    ch[p++] = 0x02;
    ch[p++] = 0x13;
    ch[p++] = 0x01; // cipher_suites: TLS_AES_128_GCM_SHA256
    ch[p++] = 0x01;
    ch[p++] = 0x00;                                 // compression_methods: [null]
    uint16_t ext_total = (uint16_t)(4 + ebody_len); // type(2) + len(2) + body
    ch[p++] = (uint8_t)(ext_total >> 8);
    ch[p++] = (uint8_t)ext_total;
    ch[p++] = (uint8_t)(etype >> 8);
    ch[p++] = (uint8_t)etype;
    ch[p++] = (uint8_t)(ebody_len >> 8);
    ch[p++] = (uint8_t)ebody_len;
    for (unsigned i = 0; i < ebody_len; i++)
    {
        ch[p++] = ebody[i];
    }
    uint32_t body_len = (uint32_t)(p - body_len_at - 3);
    ch[body_len_at] = (uint8_t)(body_len >> 16);
    ch[body_len_at + 1] = (uint8_t)(body_len >> 8);
    ch[body_len_at + 2] = (uint8_t)body_len;
    return p;
}

static void test_parse_server_cert_type_rpk(void)
{
    // server_certificate_type list [X509(0), RawPublicKey(2)]: the client accepts a RawPublicKey from us.
    const uint8_t ebody[3] = {0x02, 0x00, 0x02}; // list length 2, then {0, 2}
    uint8_t ch[128];
    size_t n = build_ch_one_ext(ch, 0x0014, ebody, sizeof(ebody));
    Tls13ClientHello hello;
    TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(ch, n, &hello));
    TEST_ASSERT_TRUE(hello.offers_rpk_server_cert);
}

static void test_parse_server_cert_type_x509_only(void)
{
    // A list with only X509(0): no RPK offer.
    const uint8_t ebody[2] = {0x01, 0x00}; // list length 1, then {0}
    uint8_t ch[128];
    size_t n = build_ch_one_ext(ch, 0x0014, ebody, sizeof(ebody));
    Tls13ClientHello hello;
    TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(ch, n, &hello));
    TEST_ASSERT_FALSE(hello.offers_rpk_server_cert);
    // A ClientHello with no server_certificate_type at all also leaves the flag clear.
    const uint8_t cookie[1] = {0x00};
    n = build_ch_one_ext(ch, 0x002c, cookie, sizeof(cookie));
    TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(ch, n, &hello));
    TEST_ASSERT_FALSE(hello.offers_rpk_server_cert);
}

// A malformed server_certificate_type extension is skipped without failing the parse and leaves the
// RawPublicKey flag clear: an empty body (no list-length byte) and a list length that overruns.
static void test_parse_server_cert_type_malformed(void)
{
    uint8_t ch[128];
    Tls13ClientHello hello;

    // Empty extension body: there is not even a list-length byte.
    size_t n = build_ch_one_ext(ch, 0x0014, NULL, 0);
    TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(ch, n, &hello));
    TEST_ASSERT_FALSE(hello.offers_rpk_server_cert);

    // The declared list length runs past the extension body.
    const uint8_t overrun[1] = {0x05}; // list length 5, but no entries follow
    n = build_ch_one_ext(ch, 0x0014, overrun, sizeof(overrun));
    TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(ch, n, &hello));
    TEST_ASSERT_FALSE(hello.offers_rpk_server_cert);
}

// The QUIC EncryptedExtensions (ALPN + transport parameters) also carries the negotiated
// server_certificate_type = RawPublicKey when it was selected (RFC 7250 sec 4.2) - the same
// extension the DTLS-profile builder appends, on top of the ALPN / transport-params body.
static void test_quic_encrypted_extensions_rpk(void)
{
    const uint8_t tp[3] = {0x04, 0x01, 0x20};
    uint8_t plain[64], rpk[64];
    size_t pn = pc_tls13_build_encrypted_extensions(plain, sizeof(plain), tp, sizeof(tp), /*rpk=*/false);
    size_t rn = pc_tls13_build_encrypted_extensions(rpk, sizeof(rpk), tp, sizeof(tp), /*rpk=*/true);
    TEST_ASSERT_TRUE(pn > 0);
    TEST_ASSERT_EQUAL_size_t(pn + 5, rn); // ext type(2) + length(2) + CertificateType(1)

    // Everything up to the extra extension is byte-identical; the RPK form appends
    // server_certificate_type(20) -> RawPublicKey(2).
    TEST_ASSERT_EQUAL_MEMORY(plain + 6, rpk + 6, pn - 6);
    const uint8_t want[5] = {0x00, 0x14, 0x00, 0x01, 0x02};
    TEST_ASSERT_EQUAL_MEMORY(want, rpk + rn - 5, sizeof(want));
    // The two length prefixes grew by exactly the appended extension.
    TEST_ASSERT_EQUAL_UINT(rn - 4, (unsigned)((rpk[1] << 16) | (rpk[2] << 8) | rpk[3]));
    TEST_ASSERT_EQUAL_UINT(rn - 6, (unsigned)((rpk[4] << 8) | rpk[5]));
}

// Every ClientHello extension the parser dispatches on, fed one at a time, so the whole switch is
// walked in the RawPublicKey build (which has one more arm than the non-RPK one).
static void test_parse_every_extension_arm(void)
{
    struct Ext
    {
        uint16_t type;
        uint8_t body[12];
        uint16_t len;
    };
    static const Ext exts[] = {
        {0x002b, {0x02, 0x03, 0x04}, 3},                            // supported_versions
        {0x000a, {0x00, 0x02, 0x00, 0x1d}, 4},                      // supported_groups
        {0x000d, {0x00, 0x02, 0x08, 0x07}, 4},                      // signature_algorithms
        {0x0033, {0x00, 0x00}, 2},                                  // key_share (empty client_shares)
        {0x0010, {0x00, 0x03, 0x02, 'h', '3'}, 5},                  // ALPN
        {0x0014, {0x02, 0x00, 0x02}, 3},                            // server_certificate_type (RFC 7250)
        {0x0039, {0x04, 0x01, 0x20}, 3},                            // quic_transport_parameters
        {0x002c, {0x00, 0x02, 0x11, 0x22}, 4},                      // cookie
        {0x0036, {0x02, 0xAA, 0xBB}, 3},                            // connection_id
        {0x0000, {0x00, 0x06, 0x00, 0x00, 0x03, 'a', 'b', 'c'}, 8}, // server_name
        {0xFFAA, {0x00}, 1},                                        // an unhandled type -> default
    };
    uint8_t ch[128];
    Tls13ClientHello hello;
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
    {
        size_t n = build_ch_one_ext(ch, exts[i].type, exts[i].body, exts[i].len);
        TEST_ASSERT_TRUE(pc_tls13_parse_client_hello(ch, n, &hello));
    }
    // The last one (an unknown type) is ignored outright, leaving every flag clear.
    TEST_ASSERT_FALSE(hello.offers_tls13);
    TEST_ASSERT_FALSE(hello.has_conn_id);
    TEST_ASSERT_NULL(hello.pc_quic_tp);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_server_cert_type_malformed);
    RUN_TEST(test_quic_encrypted_extensions_rpk);
    RUN_TEST(test_parse_every_extension_arm);
    RUN_TEST(test_hrr_magic_symbol);
    RUN_TEST(test_hrr_build_kat);
    RUN_TEST(test_hrr_echoes_session_id);
    RUN_TEST(test_message_hash);
    RUN_TEST(test_empty_encrypted_extensions);
    RUN_TEST(test_client_hello_cookie_parse);
    RUN_TEST(test_ed25519_spki);
    RUN_TEST(test_build_certificate_rpk);
    RUN_TEST(test_ee_rpk_extension);
    RUN_TEST(test_parse_server_cert_type_rpk);
    RUN_TEST(test_parse_server_cert_type_x509_only);
    return UNITY_END();
}
