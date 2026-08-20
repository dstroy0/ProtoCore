// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the RFC 7250 RawPublicKey credential
// (network_drivers/presentation/http/http3/tls13_rpk.h).
//
// RFC 8410 sec 10.1 prints an Ed25519 public key as a base64 SubjectPublicKeyInfo, so
// test_rfc8410_ed25519_spki rebuilds those 44 octets from the bare key and compares them octet for
// octet. The rest drive the credential through the Certificate message it rides in: build one, parse
// it back with Tls13Msg, and read the key out again.

#include "crypto/asymmetric/ed25519/ed25519.h"
#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"
#include "network_drivers/presentation/http/http3/tls13_rpk/tls13_rpk.h"
#include <string.h>

#include <unity.h>

static uint8_t tls13_rpk_work[16]; // the borrow an entry takes; Tls13Rpk never reads it

static uint8_t tls13_msg_work[16]; // the borrow an entry takes; Tls13Msg never reads it

// Ed25519.pubkey is what this suite calls, so the span is its.
static uint8_t g_work[PROTOCORE_ED25519_BORROW] __attribute__((aligned(8)));

static uint8_t g_out[128];

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 8032 sec 7.1 TEST 1 secret key.
static const uint8_t RFC8032_SEED[32] = {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a,
                                         0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32,
                                         0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};

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
    Tls13RpkV.ed25519_spki_args.out = g_out;
    Tls13RpkV.ed25519_spki_args.cap = sizeof(g_out);
    Tls13RpkV.ed25519_spki_args.pub = RFC8410_SPKI + 12;
    Tls13Rpk.ed25519_spki(tls13_rpk_work);
    TEST_ASSERT_EQUAL_UINT(44u, Tls13RpkV.n);
    TEST_ASSERT_EQUAL_MEMORY(RFC8410_SPKI, g_out, 44);

    Tls13RpkV.ed25519_spki_args.out = g_out;
    Tls13RpkV.ed25519_spki_args.cap = 43;
    Tls13RpkV.ed25519_spki_args.pub = RFC8410_SPKI + 12;
    Tls13Rpk.ed25519_spki(tls13_rpk_work);
    TEST_ASSERT_EQUAL_UINT(0u, Tls13RpkV.n);
}

// RFC 7250 Figure 1: for certificate_type RawPublicKey the Certificate payload carries the
// SubjectPublicKeyInfo where the X.509 chain would go, so a build and a parse round trip back to the
// same 32 octets.
void test_rpk_certificate_round_trip(void)
{
    uint8_t pub[PROTOCORE_ED25519_PUBKEY_LEN];
    Ed25519.pubkey_args.pub = pub;
    Ed25519.pubkey_args.seed = RFC8032_SEED;
    Ed25519.pubkey(g_work);

    uint8_t msg[128];
    Tls13RpkV.build_certificate_args.out = msg;
    Tls13RpkV.build_certificate_args.cap = sizeof(msg);
    Tls13RpkV.build_certificate_args.ed25519_pub = pub;
    Tls13Rpk.build_certificate(tls13_rpk_work);
    size_t n = Tls13RpkV.n;
    TEST_ASSERT_NOT_EQUAL(0u, n);

    const uint8_t *cert = NULL;
    size_t cert_len = 0;
    Tls13MsgV.parse_certificate_args.msg = msg;
    Tls13MsgV.parse_certificate_args.len = n;
    Tls13MsgV.parse_certificate_args.cert = &cert;
    Tls13MsgV.parse_certificate_args.cert_len = &cert_len;
    Tls13Msg.parse_certificate(tls13_msg_work);
    TEST_ASSERT_TRUE(Tls13MsgV.ok);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_TLS13_ED25519_SPKI_LEN, cert_len);

    const uint8_t *got = NULL;
    Tls13RpkV.ed25519_from_spki_args.spki = cert;
    Tls13RpkV.ed25519_from_spki_args.len = cert_len;
    Tls13RpkV.ed25519_from_spki_args.pub = &got;
    Tls13Rpk.ed25519_from_spki(tls13_rpk_work);
    TEST_ASSERT_TRUE(Tls13RpkV.ok);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pub, got, 32);
}

// An X.509 Certificate parses as a Certificate; only the SubjectPublicKeyInfo reader refuses it,
// because a DER chain is not the RFC 8410 encoding an RPK credential has.
void test_x509_certificate_is_not_read_as_a_raw_public_key(void)
{
    static const uint8_t DER[40] = {0x30, 0x26, 0x02, 0x01, 0x02, 0x30, 0x0d, 0x06, 0x09, 0x2a};
    uint8_t msg[128];
    Tls13MsgV.build_certificate_args.out = msg;
    Tls13MsgV.build_certificate_args.cap = sizeof(msg);
    Tls13MsgV.build_certificate_args.cert_der = DER;
    Tls13MsgV.build_certificate_args.cert_len = sizeof(DER);
    Tls13Msg.build_certificate(tls13_msg_work);
    size_t n = Tls13MsgV.n;
    TEST_ASSERT_NOT_EQUAL(0u, n);

    const uint8_t *cert = NULL;
    size_t cert_len = 0;
    Tls13MsgV.parse_certificate_args.msg = msg;
    Tls13MsgV.parse_certificate_args.len = n;
    Tls13MsgV.parse_certificate_args.cert = &cert;
    Tls13MsgV.parse_certificate_args.cert_len = &cert_len;
    Tls13Msg.parse_certificate(tls13_msg_work);
    TEST_ASSERT_TRUE(Tls13MsgV.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(DER), cert_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DER, cert, sizeof(DER));

    const uint8_t *got = NULL;
    Tls13RpkV.ed25519_from_spki_args.spki = cert;
    Tls13RpkV.ed25519_from_spki_args.len = cert_len;
    Tls13RpkV.ed25519_from_spki_args.pub = &got;
    Tls13Rpk.ed25519_from_spki(tls13_rpk_work);
    TEST_ASSERT_FALSE(Tls13RpkV.ok);
}

// A SubjectPublicKeyInfo whose prefix is not id-Ed25519 is refused rather than read past.
void test_spki_reader_refuses_a_wrong_prefix(void)
{
    uint8_t spki[PROTOCORE_TLS13_ED25519_SPKI_LEN];
    uint8_t pub[32];
    memset(pub, 0x5a, sizeof(pub));
    Tls13RpkV.ed25519_spki_args.out = spki;
    Tls13RpkV.ed25519_spki_args.cap = sizeof(spki);
    Tls13RpkV.ed25519_spki_args.pub = pub;
    Tls13Rpk.ed25519_spki(tls13_rpk_work);
    TEST_ASSERT_NOT_EQUAL(0u, Tls13RpkV.n);

    const uint8_t *got = NULL;
    Tls13RpkV.ed25519_from_spki_args.spki = spki;
    Tls13RpkV.ed25519_from_spki_args.len = sizeof(spki);
    Tls13RpkV.ed25519_from_spki_args.pub = &got;
    Tls13Rpk.ed25519_from_spki(tls13_rpk_work);
    TEST_ASSERT_TRUE(Tls13RpkV.ok);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pub, got, 32);

    spki[6] ^= 0x01; // the OID body
    Tls13RpkV.ed25519_from_spki_args.spki = spki;
    Tls13RpkV.ed25519_from_spki_args.len = sizeof(spki);
    Tls13RpkV.ed25519_from_spki_args.pub = &got;
    Tls13Rpk.ed25519_from_spki(tls13_rpk_work);
    TEST_ASSERT_FALSE(Tls13RpkV.ok);
    spki[6] ^= 0x01;
    Tls13RpkV.ed25519_from_spki_args.spki = spki;
    Tls13RpkV.ed25519_from_spki_args.len = sizeof(spki) - 1;
    Tls13RpkV.ed25519_from_spki_args.pub = &got;
    Tls13Rpk.ed25519_from_spki(tls13_rpk_work);
    TEST_ASSERT_FALSE(Tls13RpkV.ok);
}
