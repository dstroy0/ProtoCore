// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the TLS 1.3 handshake message codec
// (network_drivers/presentation/http/http3/tls13_msg.h).
//
// Two published byte strings carry this file. RFC 8448 sec 3 prints a complete TLS 1.3 exchange, so
// test_rfc8448_server_hello_bytes rebuilds its 90-octet ServerHello octet for octet from the same
// random, key share and empty session id, and test_rfc8448_client_hello_parse feeds its 196-octet
// ClientHello to the parser and reads back the fields the RFC's own hex says are in it. RFC 8446
// sec 4.4.3 prints the signed content for a 32-byte-0x01 transcript hash, which pins the 64-space
// pad, the exact context string and the 0x00 separator without trusting our own assembler.

#include "crypto/asymmetric/ed25519/ed25519.h"
#include "crypto/hash/sha512/sha512.h"
#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"
#include <string.h>

#include <unity.h>

// Ed25519.sign, .verify and .pubkey are what this suite calls, directly and through
// protocore_tls13_build_cert_verify, so the span is theirs. PROTOCORE_ED25519_BORROW already
// carries the SHA-512 context they hash with.
static uint8_t g_work[PROTOCORE_ED25519_BORROW] __attribute__((aligned(8)));

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 8448 sec 3, "{server} create an ephemeral x25519 key pair": the server's public key.
static const uint8_t RFC8448_SERVER_SHARE[32] = {0xc9, 0x82, 0x88, 0x76, 0x11, 0x20, 0x95, 0xfe, 0x66, 0x76, 0x2b,
                                                 0xdb, 0xf7, 0xc6, 0x72, 0xe1, 0x56, 0xd6, 0xcc, 0x25, 0x3b, 0x83,
                                                 0x3d, 0xf1, 0xdd, 0x69, 0xb1, 0xb0, 0x4e, 0x75, 0x1f, 0x0f};

// The 32-byte Random of the same trace's ServerHello.
static const uint8_t RFC8448_SERVER_RANDOM[32] = {0xa6, 0xaf, 0x06, 0xa4, 0x12, 0x18, 0x60, 0xdc, 0x5e, 0x6e, 0x60,
                                                  0x24, 0x9c, 0xd3, 0x4c, 0x95, 0x93, 0x0c, 0x8a, 0xc5, 0xcb, 0x14,
                                                  0x34, 0xda, 0xc1, 0x55, 0x77, 0x2e, 0xd3, 0xe2, 0x69, 0x28};

// RFC 8448 sec 3: "ServerHello (90 octets)", copied from the trace verbatim.
static const uint8_t RFC8448_SERVER_HELLO[90] = {
    0x02, 0x00, 0x00, 0x56,                                                                         // ServerHello, 86
    0x03, 0x03,                                                                                     // legacy_version
    0xa6, 0xaf, 0x06, 0xa4, 0x12, 0x18, 0x60, 0xdc, 0x5e, 0x6e, 0x60, 0x24, 0x9c, 0xd3, 0x4c, 0x95, // Random
    0x93, 0x0c, 0x8a, 0xc5, 0xcb, 0x14, 0x34, 0xda, 0xc1, 0x55, 0x77, 0x2e, 0xd3, 0xe2, 0x69, 0x28,
    0x00,                                           // session_id echo
    0x13, 0x01,                                     // AES_128_GCM
    0x00,                                           // compression
    0x00, 0x2e,                                     // extensions
    0x00, 0x33, 0x00, 0x24, 0x00, 0x1d, 0x00, 0x20, // key_share x25519
    0xc9, 0x82, 0x88, 0x76, 0x11, 0x20, 0x95, 0xfe, 0x66, 0x76, 0x2b, 0xdb, 0xf7, 0xc6, 0x72, 0xe1,
    0x56, 0xd6, 0xcc, 0x25, 0x3b, 0x83, 0x3d, 0xf1, 0xdd, 0x69, 0xb1, 0xb0, 0x4e, 0x75, 0x1f, 0x0f,
    0x00, 0x2b, 0x00, 0x02, 0x03, 0x04, // supported_versions = TLS 1.3
};

// RFC 8448 sec 3: "ClientHello (196 octets)", copied from the trace verbatim.
static const uint8_t RFC8448_CLIENT_HELLO[196] = {
    0x01, 0x00, 0x00, 0xc0, 0x03, 0x03, 0xcb, 0x34, 0xec, 0xb1, 0xe7, 0x81, 0x63, 0xba, 0x1c, 0x38, 0xc6, 0xda,
    0xcb, 0x19, 0x6a, 0x6d, 0xff, 0xa2, 0x1a, 0x8d, 0x99, 0x12, 0xec, 0x18, 0xa2, 0xef, 0x62, 0x83, 0x02, 0x4d,
    0xec, 0xe7, 0x00, 0x00, 0x06, 0x13, 0x01, 0x13, 0x03, 0x13, 0x02, 0x01, 0x00, 0x00, 0x91, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x09, 0x00, 0x00, 0x06, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0xff, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x0a, 0x00, 0x14, 0x00, 0x12, 0x00, 0x1d, 0x00, 0x17, 0x00, 0x18, 0x00, 0x19, 0x01, 0x00, 0x01, 0x01, 0x01,
    0x02, 0x01, 0x03, 0x01, 0x04, 0x00, 0x23, 0x00, 0x00, 0x00, 0x33, 0x00, 0x26, 0x00, 0x24, 0x00, 0x1d, 0x00,
    0x20, 0x99, 0x38, 0x1d, 0xe5, 0x60, 0xe4, 0xbd, 0x43, 0xd2, 0x3d, 0x8e, 0x43, 0x5a, 0x7d, 0xba, 0xfe, 0xb3,
    0xc0, 0x6e, 0x51, 0xc1, 0x3c, 0xae, 0x4d, 0x54, 0x13, 0x69, 0x1e, 0x52, 0x9a, 0xaf, 0x2c, 0x00, 0x2b, 0x00,
    0x03, 0x02, 0x03, 0x04, 0x00, 0x0d, 0x00, 0x20, 0x00, 0x1e, 0x04, 0x03, 0x05, 0x03, 0x06, 0x03, 0x02, 0x03,
    0x08, 0x04, 0x08, 0x05, 0x08, 0x06, 0x04, 0x01, 0x05, 0x01, 0x06, 0x01, 0x02, 0x01, 0x04, 0x02, 0x05, 0x02,
    0x06, 0x02, 0x02, 0x02, 0x00, 0x2d, 0x00, 0x02, 0x01, 0x01, 0x00, 0x1c, 0x00, 0x02, 0x40, 0x01,
};

// RFC 8448 sec 3: the same trace's client x25519 key_share, inside the ClientHello above.
static const uint8_t RFC8448_CLIENT_SHARE[32] = {0x99, 0x38, 0x1d, 0xe5, 0x60, 0xe4, 0xbd, 0x43, 0xd2, 0x3d, 0x8e,
                                                 0x43, 0x5a, 0x7d, 0xba, 0xfe, 0xb3, 0xc0, 0x6e, 0x51, 0xc1, 0x3c,
                                                 0xae, 0x4d, 0x54, 0x13, 0x69, 0x1e, 0x52, 0x9a, 0xaf, 0x2c};

// Rebuilding RFC 8448's ServerHello from its own inputs must reproduce its own octets: same random,
// same 32-byte X25519 share, empty legacy_session_id_echo, TLS_AES_128_GCM_SHA256, TLS 1.3.
void test_rfc8448_server_hello_bytes(void)
{
    uint8_t out[128];
    size_t n = protocore_tls13_build_server_hello(out, sizeof(out), RFC8448_SERVER_RANDOM, NULL, 0,
                                                  RFC8448_SERVER_SHARE, sizeof(RFC8448_SERVER_SHARE), TLS_GROUP_X25519,
                                                  PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, PROTO_FALSE, NULL, 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(RFC8448_SERVER_HELLO), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_SERVER_HELLO, out, sizeof(RFC8448_SERVER_HELLO));
}

// RFC 8446 sec 4.1.3: "legacy_session_id_echo: The contents of the client's legacy_session_id
// field", echoed verbatim, and the 24-bit handshake length grows by exactly what was echoed.
void test_server_hello_echoes_the_session_id(void)
{
    static const uint8_t SID[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    uint8_t out[160];
    size_t n = protocore_tls13_build_server_hello(out, sizeof(out), RFC8448_SERVER_RANDOM, SID, sizeof(SID),
                                                  RFC8448_SERVER_SHARE, 32, TLS_GROUP_X25519,
                                                  PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, PROTO_FALSE, NULL, 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(RFC8448_SERVER_HELLO) + sizeof(SID), n);

    // Handshake header: msg_type then a 24-bit length of everything after it.
    TEST_ASSERT_EQUAL_HEX8(TLS_HS_SERVER_HELLO, out[0]);
    uint32_t hs_len = ((uint32_t)out[1] << 16) | ((uint32_t)out[2] << 8) | out[3];
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(n - 4), hs_len);

    TEST_ASSERT_EQUAL_UINT8(sizeof(SID), out[4 + 2 + 32]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SID, out + 4 + 2 + 32 + 1, sizeof(SID));
}

// The RFC's own ClientHello, read back field by field from the hex above:
//   legacy_session_id is length 0; cipher_suites is 13 01 13 03 13 02, which contains 0x1301;
//   supported_groups lists 001d (x25519); supported_versions lists 0304; key_share carries an
//   x25519 entry with the 32-byte share; server_name carries "server"; signature_algorithms lists
//   fifteen schemes and 0807 (ed25519) is not one of them; there is no ALPN and no transport params.
void test_rfc8448_client_hello_parse(void)
{
    Tls13ClientHello ch;
    TEST_ASSERT_TRUE(
        protocore_tls13_parse_client_hello(RFC8448_CLIENT_HELLO, sizeof(RFC8448_CLIENT_HELLO), &ch, PROTO_FALSE));

    TEST_ASSERT_EQUAL_UINT8(0u, ch.session_id_len);
    TEST_ASSERT_TRUE(ch.offers_aes128gcm_sha256);
    TEST_ASSERT_TRUE(ch.offers_tls13);
    TEST_ASSERT_TRUE(ch.offers_x25519);
    TEST_ASSERT_TRUE(ch.has_key_share);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_CLIENT_SHARE, ch.client_x25519, 32);

    TEST_ASSERT_FALSE(ch.offers_ed25519); // 0x0807 is absent from this trace's list
    TEST_ASSERT_FALSE(ch.offers_h3_alpn);
    TEST_ASSERT_NULL(ch.quic_tp);
    TEST_ASSERT_FALSE(ch.has_server_cert_type);
    TEST_ASSERT_FALSE(ch.has_conn_id);

    TEST_ASSERT_NOT_NULL(ch.sni);
    TEST_ASSERT_EQUAL_UINT(6u, ch.sni_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("server", ch.sni, 6);
}

// RFC 8446 sec 4.1.2: "legacy_compression_methods: ... this vector MUST contain exactly one byte,
// set to zero"; sec 4.1.3 fixes legacy_session_id at 0..32 octets; and a truncated or mistyped
// message is not a ClientHello at all.
void test_malformed_client_hello_is_refused(void)
{
    uint8_t buf[sizeof(RFC8448_CLIENT_HELLO)];
    Tls13ClientHello ch;

    // Wrong handshake type.
    memcpy(buf, RFC8448_CLIENT_HELLO, sizeof(buf));
    buf[0] = TLS_HS_SERVER_HELLO;
    TEST_ASSERT_FALSE(protocore_tls13_parse_client_hello(buf, sizeof(buf), &ch, PROTO_FALSE));

    // The 24-bit length claims more body than the buffer holds.
    memcpy(buf, RFC8448_CLIENT_HELLO, sizeof(buf));
    buf[3] = 0xff;
    TEST_ASSERT_FALSE(protocore_tls13_parse_client_hello(buf, sizeof(buf), &ch, PROTO_FALSE));

    // legacy_compression_methods with a non-zero method.
    memcpy(buf, RFC8448_CLIENT_HELLO, sizeof(buf));
    buf[4 + 2 + 32 + 1 + 2 + 6 + 1] = 0x01; // the single compression method octet
    TEST_ASSERT_FALSE(protocore_tls13_parse_client_hello(buf, sizeof(buf), &ch, PROTO_FALSE));

    // A session id longer than the 32 the struct permits.
    memcpy(buf, RFC8448_CLIENT_HELLO, sizeof(buf));
    buf[4 + 2 + 32] = 33;
    TEST_ASSERT_FALSE(protocore_tls13_parse_client_hello(buf, sizeof(buf), &ch, PROTO_FALSE));

    // Nothing at all, and just a header.
    TEST_ASSERT_FALSE(protocore_tls13_parse_client_hello(RFC8448_CLIENT_HELLO, 0, &ch, PROTO_FALSE));
    TEST_ASSERT_FALSE(protocore_tls13_parse_client_hello(RFC8448_CLIENT_HELLO, 4, &ch, PROTO_FALSE));

    // An odd cipher_suites length: sec 4.1.2 makes it a vector of 2-byte CipherSuites.
    memcpy(buf, RFC8448_CLIENT_HELLO, sizeof(buf));
    buf[4 + 2 + 32 + 1 + 1] = 0x07;
    TEST_ASSERT_FALSE(protocore_tls13_parse_client_hello(buf, sizeof(buf), &ch, PROTO_FALSE));
}

// RFC 8446 sec 4.4.3 prints the content covered by a server CertificateVerify signature "if the
// transcript hash was 32 bytes of 01":
//   64 octets of 0x20, then 544c5320312e332c20736572766572204365727469666963617465566572696679
//   ("TLS 1.3, server CertificateVerify"), then 00, then the 32 hash octets.
void test_rfc8446_4_4_3_signed_content(void)
{
    static const uint8_t HASH[32] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                                     0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                                     0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
    static const uint8_t WANT[130] = {
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x54, 0x4c, 0x53, 0x20,
        0x31, 0x2e, 0x33, 0x2c, 0x20, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x20, 0x43, 0x65, 0x72, 0x74, 0x69,
        0x66, 0x69, 0x63, 0x61, 0x74, 0x65, 0x56, 0x65, 0x72, 0x69, 0x66, 0x79, 0x00, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    };
    uint8_t out[160];
    size_t n = protocore_tls13_cert_verify_content(out, sizeof(out), HASH, 32, PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, out, sizeof(WANT));

    // sec 4.4.3: "The context string for a client signature is 'TLS 1.3, client CertificateVerify'",
    // so the two contexts differ in exactly one word and never produce the same signed content.
    uint8_t cli[160];
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), protocore_tls13_cert_verify_content(cli, sizeof(cli), HASH, 32, PROTO_FALSE));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(cli, out, sizeof(WANT)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, cli, 64);             // the pad is the same
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT + 97, cli + 97, 33);   // separator + hash are the same
    TEST_ASSERT_EQUAL_UINT8_ARRAY("client", cli + 64 + 9, 6); // only the role word differs

    // A destination too small writes nothing rather than a partial content.
    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_cert_verify_content(out, sizeof(WANT) - 1, HASH, 32, PROTO_TRUE));
}

// RFC 8446 sec 4.4.3: "algorithm: The signature algorithm used ... signature: The signature". The
// message carries algorithm = ed25519 (0x0807) and a 64-byte RFC 8032 signature, and that signature
// must verify against the sec 4.4.3 content under the public key of the same seed.
void test_cert_verify_signature_round_trip(void)
{
    static const uint8_t SEED[32] = {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a,
                                     0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32,
                                     0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
    static const uint8_t HASH[32] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                     0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x23,
                                     0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x0f, 0x1e, 0x2d, 0x3c};
    uint8_t msg[128];
    size_t n = protocore_tls13_build_cert_verify(g_work, msg, sizeof(msg), HASH, 32, SEED);
    TEST_ASSERT_EQUAL_UINT(4u + 2u + 2u + (size_t)PROTOCORE_ED25519_SIG_LEN, n);

    TEST_ASSERT_EQUAL_HEX8(TLS_HS_CERTIFICATE_VERIFY, msg[0]);
    uint32_t hs_len = ((uint32_t)msg[1] << 16) | ((uint32_t)msg[2] << 8) | msg[3];
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(n - 4), hs_len);
    TEST_ASSERT_EQUAL_HEX16(TLS_SIG_ED25519, (uint16_t)((msg[4] << 8) | msg[5]));
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_ED25519_SIG_LEN, (uint16_t)((msg[6] << 8) | msg[7]));

    uint8_t content[160];
    size_t clen = protocore_tls13_cert_verify_content(content, sizeof(content), HASH, 32, PROTO_TRUE);
    uint8_t pub[PROTOCORE_ED25519_PUBKEY_LEN];
    Ed25519.pubkey_args.pub = pub;
    Ed25519.pubkey_args.seed = SEED;
    Ed25519.pubkey(g_work);
    Ed25519.verify_args.pub = pub;
    Ed25519.verify_args.msg = content;
    Ed25519.verify_args.msg_len = clen;
    Ed25519.verify_args.sig = msg + 8;
    Ed25519.verify(g_work);
    TEST_ASSERT_TRUE(Ed25519.ok);

    // The signature is over that content and nothing else: a different transcript hash does not
    // verify under the same signature.
    uint8_t other[32];
    memcpy(other, HASH, 32);
    other[0] ^= 0x01;
    size_t olen = protocore_tls13_cert_verify_content(content, sizeof(content), other, 32, PROTO_TRUE);
    Ed25519.verify_args.pub = pub;
    Ed25519.verify_args.msg = content;
    Ed25519.verify_args.msg_len = olen;
    Ed25519.verify_args.sig = msg + 8;
    Ed25519.verify(g_work);
    TEST_ASSERT_FALSE(Ed25519.ok);
}

// RFC 8446 sec 4.4.2: "Certificate { opaque certificate_request_context<0..2^8-1>;
// CertificateEntry certificate_list<0..2^24-1>; }", each entry being cert_data<1..2^24-1> followed
// by extensions<0..2^16-1>. One DER certificate, empty context, no entry extensions.
void test_rfc8446_4_4_2_certificate_layout(void)
{
    static const uint8_t DER[5] = {0x30, 0x03, 0x02, 0x01, 0x00};
    uint8_t out[64];
    size_t n = protocore_tls13_build_certificate(out, sizeof(out), DER, sizeof(DER));

    // 4 header + 1 context + 3 list length + 3 cert length + 5 cert + 2 extensions.
    TEST_ASSERT_EQUAL_UINT(4u + 1u + 3u + 3u + sizeof(DER) + 2u, n);
    TEST_ASSERT_EQUAL_HEX8(TLS_HS_CERTIFICATE, out[0]);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(n - 4), ((uint32_t)out[1] << 16) | ((uint32_t)out[2] << 8) | out[3]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[4]); // empty certificate_request_context
    uint32_t list_len = ((uint32_t)out[5] << 16) | ((uint32_t)out[6] << 8) | out[7];
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(3 + sizeof(DER) + 2), list_len);
    uint32_t cert_len = ((uint32_t)out[8] << 16) | ((uint32_t)out[9] << 8) | out[10];
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(DER), cert_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DER, out + 11, sizeof(DER));
    TEST_ASSERT_EQUAL_HEX16(0, (uint16_t)((out[11 + sizeof(DER)] << 8) | out[12 + sizeof(DER)]));
}

// RFC 8446 sec 4.4.4: "Finished { opaque verify_data[Hash.length]; }". For SHA-256 that is 32
// octets, and the message is those octets behind the 4-byte handshake header, nothing more.
void test_rfc8446_4_4_4_finished(void)
{
    static const uint8_t VD[32] = {0xa8, 0xec, 0x43, 0x6d, 0x67, 0x76, 0x34, 0xae, 0x52, 0x5a, 0xc1,
                                   0xfc, 0xeb, 0xe1, 0x1a, 0x03, 0x9e, 0xc1, 0x76, 0x94, 0xfa, 0xc6,
                                   0xe9, 0x85, 0x27, 0xb6, 0x42, 0xf2, 0xed, 0xd5, 0xce, 0x61};
    uint8_t out[64];
    size_t n = protocore_tls13_build_finished(out, sizeof(out), VD, 32);
    TEST_ASSERT_EQUAL_UINT(4u + 32u, n);
    TEST_ASSERT_EQUAL_HEX8(TLS_HS_FINISHED, out[0]);
    TEST_ASSERT_EQUAL_UINT32(32u, ((uint32_t)out[1] << 16) | ((uint32_t)out[2] << 8) | out[3]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(VD, out + 4, 32);
}

// RFC 8446 sec 4.4.1: when a HelloRetryRequest is used, ClientHello1 is replaced in the transcript
// by "message_hash || 00 00 Hash.length || Hash(ClientHello1)", where message_hash is 254.
void test_rfc8446_4_4_1_message_hash(void)
{
    static const uint8_t CH1[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                    0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                    0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};
    uint8_t out[48];
    size_t n = protocore_tls13_build_message_hash(out, sizeof(out), CH1);
    TEST_ASSERT_EQUAL_UINT(36u, n);
    TEST_ASSERT_EQUAL_HEX8(254, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
    TEST_ASSERT_EQUAL_HEX8(32, out[3]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CH1, out + 4, 32);
}

// RFC 8446 sec 4.1.3: "the HelloRetryRequest message uses the same structure as the ServerHello, but
// with Random set to the special value of the SHA-256 of 'HelloRetryRequest':
//   CF 21 AD 74 E5 9A 61 11 BE 1D 8C 02 1E 65 B8 91
//   C2 A2 11 16 7A BB 8C 5E 07 9E 09 E2 C8 A8 33 9C"
void test_rfc8446_4_1_3_hello_retry_request(void)
{
    static const uint8_t HRR_RANDOM[32] = {0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C,
                                           0x02, 0x1E, 0x65, 0xB8, 0x91, 0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB,
                                           0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(HRR_RANDOM, protocore_tls13_hrr_random, 32);

    static const uint8_t COOKIE[8] = {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7};
    uint8_t out[128];
    size_t n = protocore_tls13_build_hello_retry_request(out, sizeof(out), NULL, 0, TLS_GROUP_X25519,
                                                         PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, COOKIE, sizeof(COOKIE),
                                                         PROTO_FALSE);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8(TLS_HS_SERVER_HELLO, out[0]); // sec 4.1.4: it IS a ServerHello on the wire
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(n - 4), ((uint32_t)out[1] << 16) | ((uint32_t)out[2] << 8) | out[3]);
    TEST_ASSERT_EQUAL_HEX16(0x0303, (uint16_t)((out[4] << 8) | out[5]));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(HRR_RANDOM, out + 6, 32);
    TEST_ASSERT_EQUAL_UINT8(0u, out[38]); // legacy_session_id_echo
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, (uint16_t)((out[39] << 8) | out[40]));
    TEST_ASSERT_EQUAL_UINT8(0u, out[41]); // legacy_compression_method

    // A cookie wider than a 16-bit extension body can name is refused rather than truncated.
    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_build_hello_retry_request(out, sizeof(out), NULL, 0, TLS_GROUP_X25519,
                                                                         PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, COOKIE,
                                                                         0xFFFE, PROTO_FALSE));
}

// RFC 9001 sec 8.2 gives the transport parameters extension codepoint 0x0039, and RFC 7301 defines
// ALPN. EncryptedExtensions carries both; a round trip through the ClientHello parser is what proves
// the extension bodies are well formed, since the parser reads the same extension vector.
void test_encrypted_extensions_carries_alpn_and_transport_params(void)
{
    static const uint8_t TP[6] = {0x0b, 0x01, 0x0a, 0x0e, 0x01, 0x02};
    uint8_t out[128];
    size_t n = protocore_tls13_build_encrypted_extensions(out, sizeof(out), TP, sizeof(TP), PROTO_FALSE);

    // 4 header + 2 extensions length + (4 + 5) ALPN + (4 + 6) transport params.
    TEST_ASSERT_EQUAL_UINT(4u + 2u + 9u + 4u + sizeof(TP), n);
    TEST_ASSERT_EQUAL_HEX8(TLS_HS_ENCRYPTED_EXTENSIONS, out[0]);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(n - 4), ((uint32_t)out[1] << 16) | ((uint32_t)out[2] << 8) | out[3]);
    TEST_ASSERT_EQUAL_HEX16((uint16_t)(n - 6), (uint16_t)((out[4] << 8) | out[5]));

    // ALPN (0x0010) with a ProtocolNameList holding one 2-octet name, "h3".
    TEST_ASSERT_EQUAL_HEX16(0x0010, (uint16_t)((out[6] << 8) | out[7]));
    TEST_ASSERT_EQUAL_HEX16(5, (uint16_t)((out[8] << 8) | out[9]));
    TEST_ASSERT_EQUAL_HEX16(3, (uint16_t)((out[10] << 8) | out[11]));
    TEST_ASSERT_EQUAL_UINT8(2u, out[12]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("h3", out + 13, 2);

    // quic_transport_parameters (RFC 9001 sec 8.2, codepoint 0x39) carrying the bytes handed in.
    TEST_ASSERT_EQUAL_HEX16(TLS_EXT_QUIC_TRANSPORT_PARAMS, (uint16_t)((out[15] << 8) | out[16]));
    TEST_ASSERT_EQUAL_HEX16((uint16_t)sizeof(TP), (uint16_t)((out[17] << 8) | out[18]));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TP, out + 19, sizeof(TP));

    // The DTLS profile of the same message carries no extensions at all.
    size_t m = protocore_tls13_build_encrypted_extensions_empty(out, sizeof(out), PROTO_FALSE, NULL);
    TEST_ASSERT_EQUAL_UINT(6u, m);
    TEST_ASSERT_EQUAL_HEX8(TLS_HS_ENCRYPTED_EXTENSIONS, out[0]);
    TEST_ASSERT_EQUAL_UINT32(2u, ((uint32_t)out[1] << 16) | ((uint32_t)out[2] << 8) | out[3]);
    TEST_ASSERT_EQUAL_HEX16(0, (uint16_t)((out[4] << 8) | out[5]));
}

// A ClientHello carrying the extensions a QUIC/HTTP3 server needs: the parser must surface ALPN
// "h3" (RFC 7301), the quic_transport_parameters body (RFC 9001 sec 8.2), and ed25519 in
// signature_algorithms (RFC 8446 sec 4.2.3, scheme 0x0807).
void test_quic_client_hello_extensions(void)
{
    static const uint8_t CH[] = {
        0x01, 0x00, 0x00, 0x5a,                         // ClientHello, body 90 octets
        0x03, 0x03,                                     // legacy_version
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Random
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
        0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x00,                                                            // legacy_session_id: empty
        0x00, 0x02, 0x13, 0x01,                                          // cipher_suites: TLS_AES_128_GCM_SHA256
        0x01, 0x00,                                                      // legacy_compression_methods: { 0 }
        0x00, 0x2f,                                                      // extensions, 47 octets
        0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04,                        // supported_versions: TLS 1.3
        0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d,                  // supported_groups: x25519
        0x00, 0x0d, 0x00, 0x04, 0x00, 0x02, 0x08, 0x07,                  // signature_algorithms: ed25519
        0x00, 0x10, 0x00, 0x07, 0x00, 0x05, 0x02, 'h',  '3',  0x01, 'x', // ALPN: "h3", "x"
        0x00, 0x39, 0x00, 0x03, 0x0b, 0x01, 0x0a,                        // quic_transport_parameters
        0x00, 0x14, 0x00, 0x02, 0x01, 0x00,                              // server_certificate_type: X509
    };
    Tls13ClientHello ch;
    TEST_ASSERT_TRUE(protocore_tls13_parse_client_hello(CH, sizeof(CH), &ch, PROTO_FALSE));
    TEST_ASSERT_TRUE(ch.offers_tls13);
    TEST_ASSERT_TRUE(ch.offers_aes128gcm_sha256);
    TEST_ASSERT_TRUE(ch.offers_x25519);
    TEST_ASSERT_TRUE(ch.offers_ed25519);
    TEST_ASSERT_TRUE(ch.offers_h3_alpn);
    TEST_ASSERT_TRUE(ch.has_server_cert_type);
    TEST_ASSERT_TRUE(ch.offers_x509_server_cert);
    TEST_ASSERT_NOT_NULL(ch.quic_tp);
    TEST_ASSERT_EQUAL_UINT(3u, ch.quic_tp_len);
    TEST_ASSERT_EQUAL_HEX8(0x0b, ch.quic_tp[0]);

    // Offering the x25519 group is not the same as sending a share for it: this message has no
    // key_share extension, so the parser reports the group but no share.
    TEST_ASSERT_FALSE(ch.has_key_share);
}

// Every builder reports 0 rather than writing a truncated handshake message: half a message would be
// fed to the transcript hash and to the peer as if it were whole.
void test_builders_refuse_a_short_buffer(void)
{
    uint8_t small[8];
    uint8_t vd[32];
    uint8_t der[8];
    memset(vd, 0, sizeof(vd));
    memset(der, 0, sizeof(der));

    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_build_server_hello(
                                   small, sizeof(small), RFC8448_SERVER_RANDOM, NULL, 0, RFC8448_SERVER_SHARE, 32,
                                   TLS_GROUP_X25519, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, PROTO_FALSE, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_build_finished(small, sizeof(small), vd, 32));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_build_certificate(small, sizeof(small), der, sizeof(der)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_build_message_hash(small, sizeof(small), vd));
    TEST_ASSERT_EQUAL_UINT(
        0u, protocore_tls13_build_encrypted_extensions(small, sizeof(small), der, sizeof(der), PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_build_cert_verify(g_work, small, sizeof(small), vd, 32, vd));
    TEST_ASSERT_EQUAL_UINT(
        0u, protocore_tls13_build_hello_retry_request(small, sizeof(small), NULL, 0, TLS_GROUP_X25519,
                                                      PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, NULL, 0, PROTO_FALSE));

    // Exactly the message's own length is enough.
    uint8_t exact[40];
    TEST_ASSERT_EQUAL_UINT(36u, protocore_tls13_build_finished(exact, 36, vd, 32));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_build_finished(exact, 35, vd, 32));
}

// ---------------------------------------------------------------------------
// The client half: a ClientHello builder and a ServerHello parser (RFC 8446
// sec 4.1.2 / 4.1.3). The parser is checked against the RFC 8448 sec 3 trace's
// own ServerHello octets, and the builder by feeding what it wrote to the
// server-side parser the rest of this file already pins to that trace.
// ---------------------------------------------------------------------------

void test_rfc8448_server_hello_parse(void)
{
    Tls13ServerHello sh;
    TEST_ASSERT_TRUE(
        protocore_tls13_parse_server_hello(RFC8448_SERVER_HELLO, sizeof(RFC8448_SERVER_HELLO), &sh, PROTO_FALSE));
    TEST_ASSERT_FALSE(sh.is_hrr);
    TEST_ASSERT_TRUE(sh.selected_tls13);
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, sh.cipher_suite);
    TEST_ASSERT_EQUAL_UINT8(0u, sh.session_id_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_SERVER_RANDOM, sh.random, 32);
    TEST_ASSERT_TRUE(sh.has_key_share);
    TEST_ASSERT_EQUAL_HEX16(TLS_GROUP_X25519, sh.group);
    TEST_ASSERT_EQUAL_UINT(32u, sh.share_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_SERVER_SHARE, sh.share, 32);
}

// A truncated ServerHello is refused rather than read past.
void test_server_hello_parse_refuses_truncation(void)
{
    Tls13ServerHello sh;
    for (size_t n = 0; n < sizeof(RFC8448_SERVER_HELLO); n++)
    {
        TEST_ASSERT_FALSE(protocore_tls13_parse_server_hello(RFC8448_SERVER_HELLO, n, &sh, PROTO_FALSE));
    }
}

// A HelloRetryRequest is a ServerHello carrying the fixed Random, so it comes back through the same
// parser with is_hrr set, the selected_group in place of a share, and the cookie to echo.
void test_hello_retry_request_parses_as_a_server_hello(void)
{
    static const uint8_t COOKIE[19] = {0xc0, 0x01, 0xc0, 0x02, 0xc0, 0x03, 0xc0, 0x04, 0xc0, 0x05,
                                       0xc0, 0x06, 0xc0, 0x07, 0xc0, 0x08, 0xc0, 0x09, 0xc0};
    uint8_t out[160];
    size_t n = protocore_tls13_build_hello_retry_request(out, sizeof(out), NULL, 0, TLS_GROUP_X25519,
                                                         PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, COOKIE, sizeof(COOKIE),
                                                         PROTO_FALSE);
    TEST_ASSERT_NOT_EQUAL(0u, n);

    Tls13ServerHello sh;
    TEST_ASSERT_TRUE(protocore_tls13_parse_server_hello(out, n, &sh, PROTO_FALSE));
    TEST_ASSERT_TRUE(sh.is_hrr);
    TEST_ASSERT_TRUE(sh.selected_tls13);
    TEST_ASSERT_TRUE(sh.has_key_share);
    TEST_ASSERT_EQUAL_HEX16(TLS_GROUP_X25519, sh.group);
    TEST_ASSERT_NULL(sh.share); // the HRR form carries selected_group only
    TEST_ASSERT_EQUAL_UINT(sizeof(COOKIE), sh.cookie_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(COOKIE, sh.cookie, sizeof(COOKIE));
}

// What the builder writes is what the server-side parser reads: the two are mirrors, so the
// round trip pins the wire layout without restating it.
void test_client_hello_round_trips_through_the_server_parser(void)
{
    static const uint8_t CLIENT_RANDOM[32] = {0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b,
                                              0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
                                              0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0};
    static const uint8_t SHARE[32] = {0x99, 0x38, 0x1d, 0xe5, 0x60, 0xe4, 0xbd, 0x43, 0xd2, 0x3d, 0x8e,
                                      0x43, 0x5a, 0x7d, 0xba, 0xfe, 0xb3, 0xc0, 0x6e, 0x51, 0xc1, 0x3c,
                                      0xae, 0x4d, 0x54, 0x13, 0x69, 0x1e, 0x52, 0x9a, 0xaf, 0x2c};
    uint8_t out[512];
    size_t n = protocore_tls13_build_client_hello(out, sizeof(out), CLIENT_RANDOM, NULL, 0, SHARE, sizeof(SHARE),
                                                  TLS_GROUP_X25519, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256,
                                                  "example.com", "h2", NULL, 0, PROTO_TRUE, PROTO_FALSE);
    TEST_ASSERT_NOT_EQUAL(0u, n);

    // Handshake header: msg_type then a 24-bit length of everything after it.
    TEST_ASSERT_EQUAL_HEX8(TLS_HS_CLIENT_HELLO, out[0]);
    uint32_t hs_len = ((uint32_t)out[1] << 16) | ((uint32_t)out[2] << 8) | out[3];
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(n - 4), hs_len);

    Tls13ClientHello ch;
    TEST_ASSERT_TRUE(protocore_tls13_parse_client_hello(out, n, &ch, PROTO_FALSE));
    TEST_ASSERT_TRUE(ch.offers_tls13);
    TEST_ASSERT_TRUE(ch.offers_aes128gcm_sha256);
    TEST_ASSERT_TRUE(ch.offers_x25519);
    TEST_ASSERT_TRUE(ch.offers_ed25519);
    TEST_ASSERT_TRUE(ch.has_key_share);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SHARE, ch.client_x25519, 32);
    TEST_ASSERT_TRUE(ch.offers_h3_alpn == PROTO_FALSE); // "h2" was offered, not "h3"
    TEST_ASSERT_EQUAL_UINT(11u, ch.sni_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("example.com", ch.sni, 11);
    TEST_ASSERT_TRUE(ch.has_server_cert_type);
}

// The ALPN the builder writes is the one the server reads back.
void test_client_hello_offers_the_alpn_it_was_given(void)
{
    static const uint8_t R[32] = {0};
    static const uint8_t SHARE[32] = {0};
    uint8_t out[512];
    size_t n = protocore_tls13_build_client_hello(out, sizeof(out), R, NULL, 0, SHARE, sizeof(SHARE), TLS_GROUP_X25519,
                                                  PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, NULL, "h3", NULL, 0,
                                                  PROTO_FALSE, PROTO_FALSE);
    TEST_ASSERT_NOT_EQUAL(0u, n);

    Tls13ClientHello ch;
    TEST_ASSERT_TRUE(protocore_tls13_parse_client_hello(out, n, &ch, PROTO_FALSE));
    TEST_ASSERT_TRUE(ch.offers_h3_alpn);
    TEST_ASSERT_EQUAL_UINT(0u, ch.sni_len);
    TEST_ASSERT_FALSE(ch.has_server_cert_type);
}

// A cookie from a HelloRetryRequest rides the retried ClientHello (sec 4.1.2).
void test_client_hello_echoes_a_cookie(void)
{
    static const uint8_t R[32] = {0};
    static const uint8_t SHARE[32] = {0};
    static const uint8_t COOKIE[7] = {0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03};
    uint8_t out[512];
    size_t n = protocore_tls13_build_client_hello(out, sizeof(out), R, NULL, 0, SHARE, sizeof(SHARE), TLS_GROUP_X25519,
                                                  PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, NULL, NULL, COOKIE,
                                                  sizeof(COOKIE), PROTO_FALSE, PROTO_FALSE);
    TEST_ASSERT_NOT_EQUAL(0u, n);

    Tls13ClientHello ch;
    TEST_ASSERT_TRUE(protocore_tls13_parse_client_hello(out, n, &ch, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT(sizeof(COOKIE), ch.cookie_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(COOKIE, ch.cookie, sizeof(COOKIE));
}

// A buffer one byte short of the message writes nothing rather than a truncated one.
void test_client_hello_refuses_a_short_buffer(void)
{
    static const uint8_t R[32] = {0};
    static const uint8_t SHARE[32] = {0};
    uint8_t out[512];
    size_t n = protocore_tls13_build_client_hello(out, sizeof(out), R, NULL, 0, SHARE, sizeof(SHARE), TLS_GROUP_X25519,
                                                  PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, NULL, NULL, NULL, 0,
                                                  PROTO_FALSE, PROTO_FALSE);
    TEST_ASSERT_NOT_EQUAL(0u, n);
    uint8_t small[512];
    TEST_ASSERT_EQUAL_UINT(0u,
                           protocore_tls13_build_client_hello(small, n - 1, R, NULL, 0, SHARE, sizeof(SHARE),
                                                              TLS_GROUP_X25519, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256,
                                                              NULL, NULL, NULL, 0, PROTO_FALSE, PROTO_FALSE));
}

// The DTLS ClientHello carries the extra zero-length legacy_cookie (RFC 9147 sec 5.3), which the
// DTLS-mode parser expects and the TLS-mode one does not.
void test_dtls_client_hello_carries_the_legacy_cookie(void)
{
    static const uint8_t R[32] = {0};
    static const uint8_t SHARE[32] = {0};
    uint8_t out[512];
    size_t n = protocore_tls13_build_client_hello(out, sizeof(out), R, NULL, 0, SHARE, sizeof(SHARE), TLS_GROUP_X25519,
                                                  PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, NULL, NULL, NULL, 0,
                                                  PROTO_FALSE, PROTO_TRUE);
    TEST_ASSERT_NOT_EQUAL(0u, n);

    Tls13ClientHello ch;
    TEST_ASSERT_TRUE(protocore_tls13_parse_client_hello(out, n, &ch, PROTO_TRUE));
    TEST_ASSERT_TRUE(ch.offers_x25519);
    // The same bytes read as TLS are one field out of step, so the parse must not succeed.
    Tls13ClientHello as_tls;
    TEST_ASSERT_FALSE(protocore_tls13_parse_client_hello(out, n, &as_tls, PROTO_FALSE));
}

// ---------------------------------------------------------------------------
// The flight parsers the client reads the server's answer with. Each is the
// inverse of a builder this file already pins, so the round trip is the check.
// ---------------------------------------------------------------------------

// RFC 8032 sec 7.1 TEST 1 secret key, the same seed the CertificateVerify test signs with.
static const uint8_t RFC8032_SEED[32] = {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a,
                                         0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32,
                                         0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};

void test_rpk_certificate_round_trip(void)
{
    uint8_t pub[PROTOCORE_ED25519_PUBKEY_LEN];
    Ed25519.pubkey_args.pub = pub;
    Ed25519.pubkey_args.seed = RFC8032_SEED;
    Ed25519.pubkey(g_work);

    uint8_t msg[128];
    size_t n = protocore_tls13_build_certificate_rpk(msg, sizeof(msg), pub);
    TEST_ASSERT_NOT_EQUAL(0u, n);

    const uint8_t *cert = NULL;
    size_t cert_len = 0;
    TEST_ASSERT_TRUE(protocore_tls13_parse_certificate(msg, n, &cert, &cert_len));
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_TLS13_ED25519_SPKI_LEN, cert_len);

    const uint8_t *got = NULL;
    TEST_ASSERT_TRUE(protocore_tls13_ed25519_from_spki(cert, cert_len, &got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pub, got, 32);
}

// An X.509 Certificate parses as a Certificate; only the SubjectPublicKeyInfo reader refuses it,
// because a DER chain is not the RFC 8410 encoding an RPK credential has.
void test_x509_certificate_is_not_read_as_a_raw_public_key(void)
{
    static const uint8_t DER[40] = {0x30, 0x26, 0x02, 0x01, 0x02, 0x30, 0x0d, 0x06, 0x09, 0x2a};
    uint8_t msg[128];
    size_t n = protocore_tls13_build_certificate(msg, sizeof(msg), DER, sizeof(DER));
    TEST_ASSERT_NOT_EQUAL(0u, n);

    const uint8_t *cert = NULL;
    size_t cert_len = 0;
    TEST_ASSERT_TRUE(protocore_tls13_parse_certificate(msg, n, &cert, &cert_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(DER), cert_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DER, cert, sizeof(DER));

    const uint8_t *got = NULL;
    TEST_ASSERT_FALSE(protocore_tls13_ed25519_from_spki(cert, cert_len, &got));
}

// A SubjectPublicKeyInfo whose prefix is not id-Ed25519 is refused rather than read past.
void test_spki_reader_refuses_a_wrong_prefix(void)
{
    uint8_t spki[PROTOCORE_TLS13_ED25519_SPKI_LEN];
    uint8_t pub[32];
    memset(pub, 0x5a, sizeof(pub));
    TEST_ASSERT_NOT_EQUAL(0u, protocore_tls13_ed25519_spki(spki, sizeof(spki), pub));

    const uint8_t *got = NULL;
    TEST_ASSERT_TRUE(protocore_tls13_ed25519_from_spki(spki, sizeof(spki), &got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pub, got, 32);

    spki[6] ^= 0x01; // the OID body
    TEST_ASSERT_FALSE(protocore_tls13_ed25519_from_spki(spki, sizeof(spki), &got));
    spki[6] ^= 0x01;
    TEST_ASSERT_FALSE(protocore_tls13_ed25519_from_spki(spki, sizeof(spki) - 1, &got));
}

void test_cert_verify_round_trip(void)
{
    static const uint8_t HASH[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                     0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                     0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};
    uint8_t msg[128];
    size_t n = protocore_tls13_build_cert_verify(g_work, msg, sizeof(msg), HASH, 32, RFC8032_SEED);
    TEST_ASSERT_NOT_EQUAL(0u, n);

    uint16_t scheme = 0;
    const uint8_t *sig = NULL;
    size_t sig_len = 0;
    TEST_ASSERT_TRUE(protocore_tls13_parse_cert_verify(msg, n, &scheme, &sig, &sig_len));
    TEST_ASSERT_EQUAL_HEX16(TLS_SIG_ED25519, scheme);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_ED25519_SIG_LEN, sig_len);

    // What the parser handed back verifies over the sec 4.4.3 content, so it is the real signature
    // and not a view of the wrong bytes.
    uint8_t pub[PROTOCORE_ED25519_PUBKEY_LEN];
    Ed25519.pubkey_args.pub = pub;
    Ed25519.pubkey_args.seed = RFC8032_SEED;
    Ed25519.pubkey(g_work);
    uint8_t content[160];
    size_t clen = protocore_tls13_cert_verify_content(content, sizeof(content), HASH, 32, PROTO_TRUE);
    Ed25519.verify_args.pub = pub;
    Ed25519.verify_args.msg = content;
    Ed25519.verify_args.msg_len = clen;
    Ed25519.verify_args.sig = sig;
    Ed25519.verify(g_work);
    TEST_ASSERT_TRUE(Ed25519.ok);
}

void test_finished_round_trip(void)
{
    static const uint8_t VD[32] = {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
                                   0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5,
                                   0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf};
    uint8_t msg[64];
    size_t n = protocore_tls13_build_finished(msg, sizeof(msg), VD, 32);
    TEST_ASSERT_EQUAL_UINT(36u, n);

    const uint8_t *vd = NULL;
    TEST_ASSERT_TRUE(protocore_tls13_parse_finished(msg, n, &vd, 32));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(VD, vd, 32);

    // A body that is not exactly Hash.length is refused, and so is the wrong msg_type.
    TEST_ASSERT_FALSE(protocore_tls13_parse_finished(msg, n - 1, &vd, 32));
    msg[0] = TLS_HS_CERTIFICATE;
    TEST_ASSERT_FALSE(protocore_tls13_parse_finished(msg, n, &vd, 32));
}

// Every flight parser refuses each truncation rather than reading past its own buffer.
void test_flight_parsers_refuse_truncation(void)
{
    uint8_t pub[PROTOCORE_ED25519_PUBKEY_LEN];
    Ed25519.pubkey_args.pub = pub;
    Ed25519.pubkey_args.seed = RFC8032_SEED;
    Ed25519.pubkey(g_work);

    uint8_t cert_msg[128];
    size_t cn = protocore_tls13_build_certificate_rpk(cert_msg, sizeof(cert_msg), pub);
    static const uint8_t HASH[32] = {0};
    uint8_t cv_msg[128];
    size_t vn = protocore_tls13_build_cert_verify(g_work, cv_msg, sizeof(cv_msg), HASH, 32, RFC8032_SEED);

    const uint8_t *p = NULL;
    size_t plen = 0;
    uint16_t scheme = 0;
    for (size_t k = 0; k < cn; k++)
    {
        TEST_ASSERT_FALSE(protocore_tls13_parse_certificate(cert_msg, k, &p, &plen));
    }
    for (size_t k = 0; k < vn; k++)
    {
        TEST_ASSERT_FALSE(protocore_tls13_parse_cert_verify(cv_msg, k, &scheme, &p, &plen));
    }
}
