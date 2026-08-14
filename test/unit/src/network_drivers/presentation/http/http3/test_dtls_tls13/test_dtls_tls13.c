// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the TLS 1.3 messages the DTLS 1.3 handshake adds to the shared message codec
// (network_drivers/presentation/http/http3/tls13_msg.h), built for the DTLS path.
//
// Two published constants carry this suite. RFC 8446 sec 4.1.3 prints the HelloRetryRequest random -
// the SHA-256 of "HelloRetryRequest" - as 32 hex octets, and a ServerHello carrying it IS a
// HelloRetryRequest, so a wrong byte turns a retry into a real ServerHello. RFC 8410 sec 10.1 prints
// a complete Ed25519 SubjectPublicKeyInfo, which is the RFC 7250 RawPublicKey credential this module
// emits. test_rfc8446_hello_retry_request_random and test_rfc8410_ed25519_spki are those two.

#include "crypto/asymmetric/ed25519.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_out[256];

// RFC 8446 sec 4.1.3: "the HelloRetryRequest message uses the same structure as the ServerHello, but
// with Random set to the special value of the SHA-256 of "HelloRetryRequest":
//     CF 21 AD 74 E5 9A 61 11 BE 1D 8C 02 1E 65 B8 91
//     C2 A2 11 16 7A BB 8C 5E 07 9E 09 E2 C8 A8 33 9C"
void test_rfc8446_hello_retry_request_random(void)
{
    static const uint8_t HRR[32] = {0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C,
                                    0x02, 0x1E, 0x65, 0xB8, 0x91, 0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB,
                                    0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};
    TEST_ASSERT_EQUAL_MEMORY(HRR, protocore_tls13_hrr_random, 32);
}

// RFC 8446 sec 4.1.3 gives the ServerHello field order and sec 4.1.4 makes a HelloRetryRequest one of
// them; RFC 9147 sec 5.3 replaces the two version codepoints for DTLS 1.3 (legacy 0xFEFD on the wire,
// 0xFEFC selected). The extensions are supported_versions (43), the key_share HelloRetryRequest form
// of sec 4.2.8 carrying only the selected group, and the sec 4.2.2 cookie (44).
//
// Octet count, from those field lists:
//   legacy_version 2 + random 32 + session_id 1 + cipher_suite 2 + compression 1
//   + extensions length 2 + extensions (6 + 6 + 10) = 62 body octets behind the 4-octet header.
void test_dtls_hello_retry_request_bytes(void)
{
    static const uint8_t COOKIE[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    static const uint8_t WANT[66] = {
        0x02, 0x00, 0x00, 0x3E,                                     // server_hello, 62 body octets
        0xFE, 0xFD,                                                 // legacy_version = DTLS 1.2
        0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11,             // the HelloRetryRequest random
        0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91,             //
        0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E,             //
        0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C,             //
        0x00,                                                       // legacy_session_id_echo: empty
        0x13, 0x01,                                                 // cipher_suite TLS_AES_128_GCM_SHA256
        0x00,                                                       // legacy_compression_method
        0x00, 0x16,                                                 // extensions: 22 octets
        0x00, 0x2B, 0x00, 0x02, 0xFE, 0xFC,                         // supported_versions -> DTLS 1.3
        0x00, 0x33, 0x00, 0x02, 0x00, 0x1D,                         // key_share -> selected_group x25519
        0x00, 0x2C, 0x00, 0x06, 0x00, 0x04, 0xAA, 0xBB, 0xCC, 0xDD, // cookie
    };
    size_t n = protocore_tls13_build_hello_retry_request(g_out, sizeof(g_out), NULL, 0, TLS_GROUP_X25519, COOKIE,
                                                         sizeof(COOKIE), PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT(66u, n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 66);
}

// The same message on the TLS path carries 0x0303 / 0x0304 instead (RFC 8446 sec 4.1.3 and sec
// 4.2.1), and echoes the client's legacy_session_id verbatim.
void test_tls_hello_retry_request_codepoints(void)
{
    static const uint8_t SID[4] = {0x11, 0x22, 0x33, 0x44};
    size_t n = protocore_tls13_build_hello_retry_request(g_out, sizeof(g_out), SID, sizeof(SID), TLS_GROUP_X25519, NULL,
                                                         0, PROTO_FALSE);
    // no cookie extension this time: 22 - 10 = 12 extension octets, and 4 more of session id
    TEST_ASSERT_EQUAL_UINT(66u - 10u + 4u, n);
    TEST_ASSERT_EQUAL_HEX8(0x03, g_out[4]); // legacy_version 0x0303
    TEST_ASSERT_EQUAL_HEX8(0x03, g_out[5]);
    TEST_ASSERT_EQUAL_MEMORY(protocore_tls13_hrr_random, g_out + 6, 32);
    TEST_ASSERT_EQUAL_HEX8(0x04, g_out[38]); // session_id length
    TEST_ASSERT_EQUAL_MEMORY(SID, g_out + 39, 4);

    // supported_versions -> 0x0304, then the key_share group, and nothing after it
    static const uint8_t TAIL[14] = {0x00, 0x0C, 0x00, 0x2B, 0x00, 0x02, 0x03,
                                     0x04, 0x00, 0x33, 0x00, 0x02, 0x00, 0x1D};
    TEST_ASSERT_EQUAL_MEMORY(TAIL, g_out + n - 14, 14);
}

// RFC 8446 sec 4.4.1: when a HelloRetryRequest is used, ClientHello1 is replaced in the transcript by
// "message_hash || 00 00 Hash.length || Hash(ClientHello1)". message_hash is handshake type 254 and
// Hash.length is 32 for SHA-256, so the whole synthetic message is 36 octets.
void test_rfc8446_message_hash(void)
{
    uint8_t ch1[32];
    memset(ch1, 0x5A, sizeof(ch1));
    size_t n = protocore_tls13_build_message_hash(g_out, sizeof(g_out), ch1);
    TEST_ASSERT_EQUAL_UINT(36u, n);
    TEST_ASSERT_EQUAL_HEX8(254, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x20, g_out[3]);
    TEST_ASSERT_EQUAL_MEMORY(ch1, g_out + 4, 32);
}

// RFC 8446 sec 4.3.1: EncryptedExtensions is handshake type 8 wrapping an Extension list. The DTLS
// profile carries no ALPN and no transport parameters, so the list is empty and the whole message is
// the 4-octet header plus a zero length.
void test_rfc8446_empty_encrypted_extensions(void)
{
    static const uint8_t WANT[6] = {0x08, 0x00, 0x00, 0x02, 0x00, 0x00};
    size_t n = protocore_tls13_build_encrypted_extensions_empty(g_out, sizeof(g_out), PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT(6u, n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 6);
}

// RFC 7250 sec 4.2: the server answers server_certificate_type (extension type 20) with the single
// CertificateType it selected, and IANA registers RawPublicKey as value 2. The extension is 5 octets
// - type, a length of 1, and the value - inside the EncryptedExtensions list.
void test_rfc7250_negotiated_server_certificate_type(void)
{
    static const uint8_t WANT[11] = {0x08, 0x00, 0x00, 0x07, 0x00, 0x05, 0x00, 0x14, 0x00, 0x01, 0x02};
    size_t n = protocore_tls13_build_encrypted_extensions_empty(g_out, sizeof(g_out), PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT(11u, n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 11);
    TEST_ASSERT_EQUAL_INT(2, TLS_CERT_TYPE_RAW_PUBLIC_KEY);
    TEST_ASSERT_EQUAL_INT(0, TLS_CERT_TYPE_X509);
}

// RFC 8410 sec 10.1 prints an example Ed25519 public key as a base64 SubjectPublicKeyInfo,
// "MCowBQYDK2VwAyEAGb9ECWmEzf6FQbrBZ9w7lshQhqowtrbLDFw4rXAxZuE=", which decodes to the 44 octets
// below: the fixed 12-octet DER header of sec 4 (SEQUENCE, AlgorithmIdentifier with OID 1.3.101.112,
// BIT STRING of 33 octets with 0 unused bits) followed by the 32-octet key.
void test_rfc8410_ed25519_spki(void)
{
    static const uint8_t RFC8410_SPKI[44] = {0x30, 0x2A, 0x30, 0x05, 0x06, 0x03, 0x2B, 0x65, 0x70, 0x03, 0x21,
                                             0x00, 0x19, 0xBF, 0x44, 0x09, 0x69, 0x84, 0xCD, 0xFE, 0x85, 0x41,
                                             0xBA, 0xC1, 0x67, 0xDC, 0x3B, 0x96, 0xC8, 0x50, 0x86, 0xAA, 0x30,
                                             0xB6, 0xB6, 0xCB, 0x0C, 0x5C, 0x38, 0xAD, 0x70, 0x31, 0x66, 0xE1};
    TEST_ASSERT_EQUAL_UINT(44u, (unsigned)PROTOCORE_TLS13_ED25519_SPKI_LEN);
    size_t n = protocore_tls13_ed25519_spki(g_out, sizeof(g_out), RFC8410_SPKI + 12);
    TEST_ASSERT_EQUAL_UINT(44u, n);
    TEST_ASSERT_EQUAL_MEMORY(RFC8410_SPKI, g_out, 44);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_ed25519_spki(g_out, 43, RFC8410_SPKI + 12));
}

// RFC 7250 Figure 1: for certificate_type RawPublicKey the Certificate payload carries
// "opaque ASN.1_subjectPublicKeyInfo<1..2^24-1>" in place of the X.509 chain. RFC 8446 sec 4.4.2
// wraps that in an empty certificate_request_context, a 24-bit CertificateList length, one
// CertificateEntry of 24-bit cert_data length, and an empty per-entry extension list.
//
// Octet count: 1 context + 3 list length + (3 cert_data length + 44 SPKI + 2 extensions) = 53 body
// octets behind the 4-octet handshake header.
void test_rfc7250_raw_public_key_certificate(void)
{
    static const uint8_t RFC8410_KEY[32] = {0x19, 0xBF, 0x44, 0x09, 0x69, 0x84, 0xCD, 0xFE, 0x85, 0x41, 0xBA,
                                            0xC1, 0x67, 0xDC, 0x3B, 0x96, 0xC8, 0x50, 0x86, 0xAA, 0x30, 0xB6,
                                            0xB6, 0xCB, 0x0C, 0x5C, 0x38, 0xAD, 0x70, 0x31, 0x66, 0xE1};
    size_t n = protocore_tls13_build_certificate_rpk(g_out, sizeof(g_out), RFC8410_KEY);
    TEST_ASSERT_EQUAL_UINT(57u, n);
    TEST_ASSERT_EQUAL_HEX8(11, g_out[0]); // handshake type certificate
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x35, g_out[3]); // 53 body octets
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[4]); // empty certificate_request_context
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[6]);
    TEST_ASSERT_EQUAL_HEX8(0x31, g_out[7]); // CertificateList: 49 octets
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[8]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[9]);
    TEST_ASSERT_EQUAL_HEX8(0x2C, g_out[10]); // cert_data: 44 octets of SubjectPublicKeyInfo
    TEST_ASSERT_EQUAL_HEX8(0x30, g_out[11]); // the DER SEQUENCE the SPKI opens with
    TEST_ASSERT_EQUAL_MEMORY(RFC8410_KEY, g_out + 11 + 12, 32);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[55]); // empty per-entry extensions
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[56]);
}

// A DTLS 1.3 ClientHello (RFC 9147 sec 5.3), which differs from the TLS one by the legacy_cookie
// field between legacy_session_id and cipher_suites and by advertising 0xFEFC in supported_versions.
// It carries the sec 4.2.2 cookie the server asked for in its HelloRetryRequest, an RFC 7250
// server_certificate_type list, and an RFC 9146 connection_id.
static const uint8_t DTLS_CH[79] = {
    0x01, 0x00, 0x00, 0x4B, // client_hello, 75 body octets
    0xFE, 0xFD,             // legacy_version = DTLS 1.2
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x00,                                                       // legacy_session_id: empty
    0x00,                                                       // legacy_cookie: empty (DTLS only)
    0x00, 0x02, 0x13, 0x01,                                     // cipher_suites
    0x01, 0x00,                                                 // legacy_compression_methods
    0x00, 0x1F,                                                 // extensions: 31 octets
    0x00, 0x2B, 0x00, 0x03, 0x02, 0xFE, 0xFC,                   // supported_versions -> DTLS 1.3
    0x00, 0x2C, 0x00, 0x06, 0x00, 0x04, 0xAA, 0xBB, 0xCC, 0xDD, // cookie
    0x00, 0x14, 0x00, 0x03, 0x02, 0x02, 0x00,                   // server_certificate_type: RPK, X.509
    0x00, 0x36, 0x00, 0x03, 0x02, 0xC1, 0xD5,                   // connection_id
};

// RFC 8446 sec 4.2.2: the client echoes the cookie the HelloRetryRequest sent, in a Cookie struct
// whose body is a 2-octet length followed by the cookie itself.
void test_rfc8446_cookie_extension_is_parsed(void)
{
    static const uint8_t COOKIE[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    Tls13ClientHello ch;
    TEST_ASSERT_TRUE(protocore_tls13_parse_client_hello(DTLS_CH, sizeof(DTLS_CH), &ch, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT(4u, ch.cookie_len);
    TEST_ASSERT_NOT_NULL(ch.cookie);
    TEST_ASSERT_EQUAL_MEMORY(COOKIE, ch.cookie, 4);
    TEST_ASSERT_TRUE(ch.offers_tls13); // supported_versions carried the DTLS 1.3 codepoint
    TEST_ASSERT_TRUE(ch.offers_aes128gcm_sha256);
}

// RFC 7250 sec 3: the client's server_certificate_type extension is a list of CertificateType
// values, and sec 4.2 has the server read the whole list to decide what to answer with.
void test_rfc7250_server_certificate_type_is_parsed(void)
{
    Tls13ClientHello ch;
    TEST_ASSERT_TRUE(protocore_tls13_parse_client_hello(DTLS_CH, sizeof(DTLS_CH), &ch, PROTO_TRUE));
    TEST_ASSERT_TRUE(ch.has_server_cert_type);
    TEST_ASSERT_TRUE(ch.offers_rpk_server_cert);
    TEST_ASSERT_TRUE(ch.offers_x509_server_cert);
}

// RFC 9146 sec 3: the connection_id extension body is a 1-octet length then the CID the peer must
// place in records it sends. RFC 9147 sec 9 carries it into DTLS 1.3.
void test_rfc9146_connection_id_is_parsed(void)
{
    static const uint8_t CID[2] = {0xC1, 0xD5};
    Tls13ClientHello ch;
    TEST_ASSERT_TRUE(protocore_tls13_parse_client_hello(DTLS_CH, sizeof(DTLS_CH), &ch, PROTO_TRUE));
    TEST_ASSERT_TRUE(ch.has_conn_id);
    TEST_ASSERT_EQUAL_UINT(2u, ch.conn_id_len);
    TEST_ASSERT_EQUAL_MEMORY(CID, ch.conn_id, 2);
}

// RFC 9147 sec 5.3: "A DTLS 1.3-only client MUST set the legacy_cookie field to zero length. If a
// DTLS 1.3 ClientHello is received with any other value in this field, the server MUST abort the
// handshake with an illegal_parameter alert."
void test_rfc9147_legacy_cookie_must_be_empty(void)
{
    uint8_t bad[sizeof(DTLS_CH)];
    Tls13ClientHello ch;
    memcpy(bad, DTLS_CH, sizeof(bad));
    bad[39] = 0x01; // the legacy_cookie length, right after the empty legacy_session_id
    TEST_ASSERT_FALSE(protocore_tls13_parse_client_hello(bad, sizeof(bad), &ch, PROTO_TRUE));

    // read as a TLS ClientHello the same octets are malformed too, since no legacy_cookie belongs
    // there and every field behind it then lands one octet out
    TEST_ASSERT_FALSE(protocore_tls13_parse_client_hello(DTLS_CH, sizeof(DTLS_CH), &ch, PROTO_FALSE));
}

// A destination that cannot hold the whole message yields 0, never a handshake message whose
// back-patched length prefixes disagree with what was written.
void test_builders_refuse_a_short_destination(void)
{
    static const uint8_t COOKIE[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t ch1[32];
    memset(ch1, 0, sizeof(ch1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_build_hello_retry_request(g_out, 65, NULL, 0, TLS_GROUP_X25519, COOKIE,
                                                                         sizeof(COOKIE), PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_build_encrypted_extensions_empty(g_out, 5, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_build_message_hash(g_out, 35, ch1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_tls13_build_certificate_rpk(g_out, 56, ch1));
}
