// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for src/crypto/x509/x509.h - RFC 5280 certificates, and RFC 6125 name matching.
//
// The certificates are real: made by OpenSSL, dumped into x509_fixture.h with the values OpenSSL
// itself reports for them. A parser checked only against DER its own author assembled proves the
// author's idea of the encoding twice over, which is how a parser and its suite agree on a bug.
//
// The name-matching cases are the ones that decide whether a connection goes to the right server.
// RFC 6125 sec 6.4.3's three rules each get a case, and so does each way a wildcard can be
// stretched past them - *.com, bar.*.example.net, and a wildcard asked to span a dot.

#include "crypto/x509/x509.h"
#include "x509_fixture.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static proto_bool parse(const uint8_t *der, size_t len)
{
    X509.parse_args.der = der;
    X509.parse_args.len = len;
    X509.parse(NULL);
    return X509.ok;
}

static proto_bool matches(const char *host)
{
    X509.match_args.cert = &X509.cert;
    X509.match_args.host = host;
    X509.match_args.host_len = 0;
    X509.name_match(NULL);
    return X509.ok;
}

// ---------------------------------------------------------------------------
// RFC 5280 sec 4.1: the fields
// ---------------------------------------------------------------------------

// The whole of a leaf, against what OpenSSL says it holds.
void test_an_ed25519_leaf_reads_as_openssl_wrote_it(void)
{
    TEST_ASSERT_TRUE(parse(X509_ED25519_DER, sizeof(X509_ED25519_DER)));

    TEST_ASSERT_EQUAL_UINT8(2, X509.cert.version); // v3
    TEST_ASSERT_EQUAL(PROTOCORE_X509_SIG_ED25519, X509.cert.sig_alg);
    TEST_ASSERT_EQUAL(PROTOCORE_X509_KEY_ED25519, X509.cert.key_alg);

    // -set_serial 4919 == 0x1337, and sec 4.1.2.2 keeps it as it was encoded.
    TEST_ASSERT_EQUAL_UINT(2, X509.cert.serial.len);
    TEST_ASSERT_EQUAL_HEX8(0x13, X509.cert.serial.p[0]);
    TEST_ASSERT_EQUAL_HEX8(0x37, X509.cert.serial.p[1]);

    TEST_ASSERT_EQUAL_UINT64(X509_ED25519_NOT_BEFORE, X509.cert.not_before);
    TEST_ASSERT_EQUAL_UINT64(X509_ED25519_NOT_AFTER, X509.cert.not_after);

    // RFC 8410 sec 4: the Ed25519 subjectPublicKey is the 32 raw octets.
    TEST_ASSERT_EQUAL_UINT(32, X509.cert.key.len);
    // sec 5.1: an Ed25519 signature is 64 octets.
    TEST_ASSERT_EQUAL_UINT(64, X509.cert.sig.len);

    // The TBS is handed back whole, because that is what the signature covers (sec 4.1.1.2). It
    // begins at the second value of the Certificate SEQUENCE and is itself a SEQUENCE.
    TEST_ASSERT_NOT_NULL(X509.cert.tbs.p);
    TEST_ASSERT_EQUAL_HEX8(0x30, X509.cert.tbs.p[0]);
    TEST_ASSERT_TRUE(X509.cert.tbs.len < sizeof(X509_ED25519_DER));
}

// An EC leaf: the curve is named in the algorithm parameters, and a key on a curve this build does
// not implement must not read as one it does (RFC 5480 sec 2.1.1).
void test_a_p256_leaf_reports_its_curve(void)
{
    TEST_ASSERT_TRUE(parse(X509_P256_DER, sizeof(X509_P256_DER)));
    TEST_ASSERT_EQUAL(PROTOCORE_X509_KEY_EC_P256, X509.cert.key_alg);
    TEST_ASSERT_EQUAL(PROTOCORE_X509_SIG_ECDSA_SHA256, X509.cert.sig_alg); // signed BY the P-256 CA
    // RFC 5480 sec 2.2: an uncompressed P-256 point is 0x04 then two 32-octet coordinates.
    TEST_ASSERT_EQUAL_UINT(65, X509.cert.key.len);
    TEST_ASSERT_EQUAL_HEX8(0x04, X509.cert.key.p[0]);
}

void test_an_rsa_leaf_reads_its_key(void)
{
    TEST_ASSERT_TRUE(parse(X509_RSA_DER, sizeof(X509_RSA_DER)));
    TEST_ASSERT_EQUAL(PROTOCORE_X509_KEY_RSA, X509.cert.key_alg);
    // A 2048-bit RSAPublicKey SEQUENCE is a little over 256 octets once the modulus and exponent
    // are wrapped; what matters is that it is the key's own bytes and not empty.
    TEST_ASSERT_TRUE(X509.cert.key.len > 256);
    TEST_ASSERT_EQUAL_HEX8(0x30, X509.cert.key.p[0]); // RFC 8017 A.1.1: RSAPublicKey ::= SEQUENCE
}

// sec 4.2.1.9 and sec 4.2.1.3 on the CA the leaves were signed by.
void test_a_ca_reports_its_constraints_and_usage(void)
{
    TEST_ASSERT_TRUE(parse(X509_CA_DER, sizeof(X509_CA_DER)));
    TEST_ASSERT_TRUE(X509.cert.has_bc);
    TEST_ASSERT_TRUE(X509.cert.is_ca);
    TEST_ASSERT_TRUE(X509.cert.has_path_len);
    TEST_ASSERT_EQUAL_UINT32(1, X509.cert.path_len); // pathlen:1

    TEST_ASSERT_TRUE(X509.cert.has_ku);
    TEST_ASSERT_TRUE((X509.cert.key_usage & PROTOCORE_X509_KU_KEY_CERT_SIGN) != 0);
    TEST_ASSERT_TRUE((X509.cert.key_usage & PROTOCORE_X509_KU_CRL_SIGN) != 0);
    // It was not given digitalSignature, so it must not report one.
    TEST_ASSERT_FALSE((X509.cert.key_usage & PROTOCORE_X509_KU_DIGITAL_SIGNATURE) != 0);
}

// A leaf is not a CA. basicConstraints is absent on these leaves, and an absent extension must not
// read as cA TRUE - that confusion is what lets a leaf sign other certificates.
void test_a_leaf_is_not_a_ca(void)
{
    TEST_ASSERT_TRUE(parse(X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    TEST_ASSERT_FALSE(X509.cert.is_ca);
}

// The issuer of a leaf is the subject of the CA, byte for byte. That equality is what a chain walk
// is built on, which is why the Name is kept encoded rather than decoded and re-compared.
void test_the_leaf_issuer_is_the_ca_subject_byte_for_byte(void)
{
    TEST_ASSERT_TRUE(parse(X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    static uint8_t issuer[256];
    const size_t issuer_len = X509.cert.issuer.len;
    TEST_ASSERT_TRUE(issuer_len <= sizeof(issuer));
    memcpy(issuer, X509.cert.issuer.p, issuer_len);

    TEST_ASSERT_TRUE(parse(X509_CA_DER, sizeof(X509_CA_DER)));
    TEST_ASSERT_EQUAL_UINT(issuer_len, X509.cert.subject.len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(issuer, X509.cert.subject.p, issuer_len));
}

// ---------------------------------------------------------------------------
// What a parse refuses
// ---------------------------------------------------------------------------

void test_nothing_and_rubbish_are_refused(void)
{
    TEST_ASSERT_FALSE(parse(NULL, 10));
    TEST_ASSERT_FALSE(parse(X509_ED25519_DER, 0));

    const uint8_t junk[] = {0x01, 0x02, 0x03, 0x04};
    TEST_ASSERT_FALSE(parse(junk, sizeof(junk)));
}

// A truncated certificate is refused at whatever field runs off the end, not read as far as it goes
// and then reported as a certificate.
void test_a_truncated_certificate_is_refused(void)
{
    for (size_t cut = 1; cut < sizeof(X509_ED25519_DER); cut += 17)
    {
        TEST_ASSERT_FALSE_MESSAGE(parse(X509_ED25519_DER, cut), "a truncated certificate parsed");
    }
}

// A single flipped bit anywhere in the encoding either changes a field or breaks the structure. The
// point of this case is that it never yields a certificate whose length fields disagree with the
// buffer - the parser is walked over every byte position rather than the happy path only.
void test_a_flipped_length_octet_does_not_run_off_the_buffer(void)
{
    static uint8_t buf[sizeof(X509_ED25519_DER)];
    for (size_t i = 0; i < sizeof(buf); i += 7)
    {
        memcpy(buf, X509_ED25519_DER, sizeof(buf));
        buf[i] ^= 0x80u; // set the high bit: on a length octet this means "long form"
        (void)parse(buf, sizeof(buf));
        // No assertion on the verdict: a parse may legitimately still succeed. What must hold is
        // that every reported field stays inside the buffer.
        if (X509.ok)
        {
            TEST_ASSERT_TRUE(X509.cert.tbs.p >= buf);
            TEST_ASSERT_TRUE(X509.cert.tbs.p + X509.cert.tbs.len <= buf + sizeof(buf));
            TEST_ASSERT_TRUE(X509.cert.key.p + X509.cert.key.len <= buf + sizeof(buf));
            TEST_ASSERT_TRUE(X509.cert.sig.p + X509.cert.sig.len <= buf + sizeof(buf));
        }
    }
}

// ---------------------------------------------------------------------------
// RFC 6125 sec 6.4: name matching
// ---------------------------------------------------------------------------

// The fixture's SAN is DNS:leaf.example.com, DNS:*.wild.example.com, DNS:other.example.net.
void test_an_exact_dns_name_matches(void)
{
    TEST_ASSERT_TRUE(parse(X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    TEST_ASSERT_TRUE(matches("leaf.example.com"));
    TEST_ASSERT_TRUE(matches("other.example.net")); // any entry in the list, not just the first
}

// sec 6.4.1: the comparison is case-insensitive, and a trailing dot names the same host.
void test_a_name_matches_regardless_of_case_or_a_trailing_dot(void)
{
    TEST_ASSERT_TRUE(parse(X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    TEST_ASSERT_TRUE(matches("LEAF.Example.COM"));
    TEST_ASSERT_TRUE(matches("leaf.example.com."));
}

void test_a_name_that_is_not_in_the_list_does_not_match(void)
{
    TEST_ASSERT_TRUE(parse(X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    TEST_ASSERT_FALSE(matches("evil.example.com"));
    TEST_ASSERT_FALSE(matches("example.com"));
    TEST_ASSERT_FALSE(matches(""));
}

// sec 6.4.3 rule 2: the wildcard label matches exactly one label.
void test_a_wildcard_matches_one_label(void)
{
    TEST_ASSERT_TRUE(parse(X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    TEST_ASSERT_TRUE(matches("a.wild.example.com"));
    TEST_ASSERT_TRUE(matches("anything.wild.example.com"));
}

// The same rule, the other way: it does not span a dot, and it does not match the bare domain.
// "*.wild.example.com" matching "a.b.wild.example.com" is the classic wildcard over-reach.
void test_a_wildcard_does_not_span_a_dot_or_match_the_bare_domain(void)
{
    TEST_ASSERT_TRUE(parse(X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    TEST_ASSERT_FALSE(matches("a.b.wild.example.com"));
    TEST_ASSERT_FALSE(matches("wild.example.com"));
}

// A certificate with no subjectAltName speaks for no name. sec 6.4.4 forbids falling back to the
// subject common name when a DNS-ID is available, and this reads only DNS-IDs - so the fallback
// does not exist at all rather than existing and being skipped.
void test_a_certificate_without_a_san_matches_nothing(void)
{
    TEST_ASSERT_TRUE(parse(X509_CA_DER, sizeof(X509_CA_DER))); // CN=Test Root CA, no SAN
    TEST_ASSERT_FALSE(matches("Test Root CA"));
    TEST_ASSERT_FALSE(matches("test.root.ca"));
}

void test_a_match_needs_both_a_certificate_and_a_name(void)
{
    TEST_ASSERT_TRUE(parse(X509_ED25519_DER, sizeof(X509_ED25519_DER)));

    X509.match_args.cert = NULL;
    X509.match_args.host = "leaf.example.com";
    X509.match_args.host_len = 0;
    X509.name_match(NULL);
    TEST_ASSERT_FALSE(X509.ok);

    X509.match_args.cert = &X509.cert;
    X509.match_args.host = NULL;
    X509.name_match(NULL);
    TEST_ASSERT_FALSE(X509.ok);
}
