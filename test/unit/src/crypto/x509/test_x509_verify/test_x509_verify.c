// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for src/crypto/x509/x509_verify.h - one link of a certification path.
//
// The certificates are real, and each verification path is reached by a certificate the matching
// algorithm actually signed: there is one CA per algorithm in the fixture, because three leaves
// under one Ed25519 CA would exercise the Ed25519 verifier three times and the ECDSA and RSA
// verifiers never, while the suite looked complete.
//
// Two kinds of case here, and the second kind is the one that matters. Accepting a good chain is
// what an implementation does on its first day; refusing every bad one is the whole job. So each
// condition RFC 5280 sec 6.1.3 and sec 6.1.4 states gets a case that violates exactly it, with
// everything else about the certificate left correct.

#include "crypto/x509/x509_verify.h"
#include "x509_fixture.h"
#include <string.h>

#include <unity.h>

// A parsed certificate held across calls: X509.cert is a single handle, so a link needs two copies.
static X509Cert g_leaf;
static X509Cert g_ca;

void setUp(void)
{
    memset(&g_leaf, 0, sizeof(g_leaf));
    memset(&g_ca, 0, sizeof(g_ca));
}
void tearDown(void)
{
}

static proto_bool load(X509Cert *out, const uint8_t *der, size_t len)
{
    X509.parse_args.der = der;
    X509.parse_args.len = len;
    X509.parse(NULL);
    if (X509.ok)
    {
        *out = X509.cert;
    }
    return X509.ok;
}

// A time inside every fixture certificate's validity: the leaves are issued now and last a year.
static uint64_t inside(void)
{
    return X509_ED25519_NOT_BEFORE + 60u;
}

static protocore_x509_status link_of(const X509Cert *cert, const X509Cert *issuer, uint64_t now)
{
    X509Verify.link_args.cert = cert;
    X509Verify.link_args.issuer = issuer;
    X509Verify.time_args.cert = cert;
    X509Verify.time_args.now = now;
    X509Verify.issuer_args.issuer = issuer;
    X509Verify.issuer_args.depth = 0;
    X509Verify.link(protocore_x509_verify_span());
    return X509Verify.status;
}

// ---------------------------------------------------------------------------
// A good link, once per signature algorithm
// ---------------------------------------------------------------------------

void test_an_ed25519_link_verifies(void)
{
    TEST_ASSERT_TRUE(load(&g_leaf, X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    TEST_ASSERT_TRUE(load(&g_ca, X509_CA_ED25519_DER, sizeof(X509_CA_ED25519_DER)));
    TEST_ASSERT_EQUAL(PROTOCORE_X509_SIG_ED25519, g_leaf.sig_alg);
    TEST_ASSERT_EQUAL(PROTOCORE_X509_OK, link_of(&g_leaf, &g_ca, inside()));
}

void test_an_ecdsa_p256_link_verifies(void)
{
    TEST_ASSERT_TRUE(load(&g_leaf, X509_P256_DER, sizeof(X509_P256_DER)));
    TEST_ASSERT_TRUE(load(&g_ca, X509_CA_P256_DER, sizeof(X509_CA_P256_DER)));
    TEST_ASSERT_EQUAL(PROTOCORE_X509_SIG_ECDSA_SHA256, g_leaf.sig_alg);
    // RFC 3279 sec 2.2.3 encodes the signature as SEQUENCE { r, s }, and the verifier takes r || s
    // as fixed fields - so this case also covers that conversion.
    TEST_ASSERT_EQUAL(PROTOCORE_X509_OK, link_of(&g_leaf, &g_ca, inside()));
}

void test_an_rsa_link_verifies(void)
{
    TEST_ASSERT_TRUE(load(&g_leaf, X509_RSA_DER, sizeof(X509_RSA_DER)));
    TEST_ASSERT_TRUE(load(&g_ca, X509_CA_RSA_DER, sizeof(X509_CA_RSA_DER)));
    TEST_ASSERT_EQUAL(PROTOCORE_X509_SIG_RSA_SHA256, g_leaf.sig_alg);
    // RFC 8017 A.1.1 encodes the key as SEQUENCE { modulus, publicExponent }, and the verifier
    // takes fixed fields - so this case also covers that split.
    TEST_ASSERT_EQUAL(PROTOCORE_X509_OK, link_of(&g_leaf, &g_ca, inside()));
}

// ---------------------------------------------------------------------------
// sec 6.1.3 (a)(1): the signature verifies, and only the real one does
// ---------------------------------------------------------------------------

// A single flipped bit in the signature must fail. Anywhere in it, not just the first byte: a
// verifier that checks a prefix passes this at index 0 and fails everywhere else.
void test_a_flipped_signature_bit_fails_wherever_it_is(void)
{
    static uint8_t der[sizeof(X509_ED25519_DER)];
    TEST_ASSERT_TRUE(load(&g_ca, X509_CA_ED25519_DER, sizeof(X509_CA_ED25519_DER)));

    for (size_t bit = 0; bit < 64u * 8u; bit += 37u)
    {
        memcpy(der, X509_ED25519_DER, sizeof(der));
        TEST_ASSERT_TRUE(load(&g_leaf, der, sizeof(der)));
        // The signature is a view into der, so the flip lands in the buffer the check reads.
        uint8_t *sig = (uint8_t *)(uintptr_t)g_leaf.sig.p;
        sig[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        TEST_ASSERT_EQUAL_MESSAGE(PROTOCORE_X509_ERR_BAD_SIGNATURE, link_of(&g_leaf, &g_ca, inside()),
                                  "a corrupted signature verified");
    }
}

// A flipped bit in the signed body must fail too - that is the half a verifier gets wrong when it
// hashes something other than the TBSCertificate's own octets.
void test_a_flipped_body_bit_fails(void)
{
    static uint8_t der[sizeof(X509_ED25519_DER)];
    TEST_ASSERT_TRUE(load(&g_ca, X509_CA_ED25519_DER, sizeof(X509_CA_ED25519_DER)));

    for (size_t off = 8; off < 64u; off += 11u)
    {
        memcpy(der, X509_ED25519_DER, sizeof(der));
        if (!load(&g_leaf, der, sizeof(der)))
        {
            continue; // the flip broke the encoding instead, which is its own refusal
        }
        uint8_t *tbs = (uint8_t *)(uintptr_t)g_leaf.tbs.p;
        tbs[off] ^= 0x01u;
        // Re-parse so the view matches the mutated bytes, then check.
        if (!load(&g_leaf, der, sizeof(der)))
        {
            continue;
        }
        // The signature alone, not the whole link: these offsets fall inside the issuer Name, so a
        // link would refuse on the name match (sec 6.1.3 (a)(4)) before reaching the signature, and
        // the half this case is for is the one that hashes the TBSCertificate's own octets.
        X509Verify.link_args.cert = &g_leaf;
        X509Verify.link_args.issuer = &g_ca;
        X509Verify.signature(protocore_x509_verify_span());
        TEST_ASSERT_EQUAL_MESSAGE(PROTOCORE_X509_ERR_BAD_SIGNATURE, X509Verify.status,
                                  "a modified certificate body verified");
    }
}

// The wrong issuer's key must not verify, even though it is a perfectly good key of the same
// algorithm. This is the case that catches a verifier ignoring the key it was handed.
void test_another_cas_key_does_not_verify_the_leaf(void)
{
    TEST_ASSERT_TRUE(load(&g_leaf, X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    static X509Cert other;
    TEST_ASSERT_TRUE(load(&other, X509_CA_ED25519_DER, sizeof(X509_CA_ED25519_DER)));

    // A different Ed25519 CA: the p256 and rsa CAs are different keys, but of other algorithms, so
    // the one that isolates "wrong key, right algorithm" is the leaf's own key.
    X509Cert leaf_as_issuer = g_leaf;
    X509Verify.link_args.cert = &g_leaf;
    X509Verify.link_args.issuer = &leaf_as_issuer;
    X509Verify.signature(protocore_x509_verify_span());
    TEST_ASSERT_FALSE(X509Verify.ok);
    TEST_ASSERT_EQUAL(PROTOCORE_X509_ERR_BAD_SIGNATURE, X509Verify.status);
}

// A key of the wrong algorithm entirely is refused before any verification is attempted.
void test_an_issuer_key_of_the_wrong_algorithm_is_refused(void)
{
    TEST_ASSERT_TRUE(load(&g_leaf, X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    TEST_ASSERT_TRUE(load(&g_ca, X509_CA_RSA_DER, sizeof(X509_CA_RSA_DER))); // RSA key, Ed25519 signature

    X509Verify.link_args.cert = &g_leaf;
    X509Verify.link_args.issuer = &g_ca;
    X509Verify.signature(protocore_x509_verify_span());
    TEST_ASSERT_FALSE(X509Verify.ok);
    TEST_ASSERT_EQUAL(PROTOCORE_X509_ERR_KEY_MALFORMED, X509Verify.status);
}

// ---------------------------------------------------------------------------
// sec 6.1.3 (a)(2): the validity period includes the current time
// ---------------------------------------------------------------------------

void test_a_certificate_is_valid_inside_its_window_and_at_both_ends(void)
{
    TEST_ASSERT_TRUE(load(&g_leaf, X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    X509Verify.time_args.cert = &g_leaf;

    // sec 4.1.2.5 makes notBefore and notAfter the first and last instant it is valid, so both ends
    // are inside the window rather than outside it.
    X509Verify.time_args.now = g_leaf.not_before;
    X509Verify.validity(NULL);
    TEST_ASSERT_TRUE(X509Verify.ok);

    X509Verify.time_args.now = g_leaf.not_after;
    X509Verify.validity(NULL);
    TEST_ASSERT_TRUE(X509Verify.ok);
}

void test_a_certificate_before_its_window_and_after_it_are_told_apart(void)
{
    TEST_ASSERT_TRUE(load(&g_leaf, X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    X509Verify.time_args.cert = &g_leaf;

    X509Verify.time_args.now = g_leaf.not_before - 1u;
    X509Verify.validity(NULL);
    TEST_ASSERT_FALSE(X509Verify.ok);
    TEST_ASSERT_EQUAL(PROTOCORE_X509_ERR_NOT_YET_VALID, X509Verify.status);

    X509Verify.time_args.now = g_leaf.not_after + 1u;
    X509Verify.validity(NULL);
    TEST_ASSERT_FALSE(X509Verify.ok);
    TEST_ASSERT_EQUAL(PROTOCORE_X509_ERR_EXPIRED, X509Verify.status);
}

// An expired certificate is refused by the whole link, not only by the time check on its own - the
// order the link runs its checks in must not let a good signature stand in for a live certificate.
void test_an_expired_certificate_fails_the_whole_link(void)
{
    TEST_ASSERT_TRUE(load(&g_leaf, X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    TEST_ASSERT_TRUE(load(&g_ca, X509_CA_ED25519_DER, sizeof(X509_CA_ED25519_DER)));
    TEST_ASSERT_EQUAL(PROTOCORE_X509_ERR_EXPIRED, link_of(&g_leaf, &g_ca, g_leaf.not_after + 1u));
}

// ---------------------------------------------------------------------------
// sec 6.1.3 (a)(4) and sec 6.1.4 (k), (n), (m): who may sign
// ---------------------------------------------------------------------------

// The issuer name must be the subject above it. A CA that did not issue this certificate is refused
// before its key is ever consulted.
void test_an_issuer_that_did_not_issue_it_is_refused(void)
{
    TEST_ASSERT_TRUE(load(&g_leaf, X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    static X509Cert stranger;
    TEST_ASSERT_TRUE(load(&stranger, X509_P256_DER, sizeof(X509_P256_DER))); // a leaf, CN=leaf.example.com
    TEST_ASSERT_EQUAL(PROTOCORE_X509_ERR_ISSUER_NAME, link_of(&g_leaf, &stranger, inside()));
}

// sec 6.1.4 (k): an issuer without basicConstraints cA TRUE may not have signed anything. A leaf
// signing another certificate is the failure this prevents.
void test_a_leaf_may_not_sign(void)
{
    TEST_ASSERT_TRUE(load(&g_leaf, X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    X509Verify.issuer_args.issuer = &g_leaf;
    X509Verify.issuer_args.depth = 0;
    X509Verify.may_sign(NULL);
    TEST_ASSERT_FALSE(X509Verify.ok);
    TEST_ASSERT_EQUAL(PROTOCORE_X509_ERR_NOT_A_CA, X509Verify.status);
}

void test_a_ca_may_sign(void)
{
    TEST_ASSERT_TRUE(load(&g_ca, X509_CA_ED25519_DER, sizeof(X509_CA_ED25519_DER)));
    X509Verify.issuer_args.issuer = &g_ca;
    X509Verify.issuer_args.depth = 0;
    X509Verify.may_sign(NULL);
    TEST_ASSERT_TRUE(X509Verify.ok);
}

// sec 6.1.4 (m): pathLenConstraint is how many certificates may follow in the path. The fixture CA
// states 1, so it reaches one below it and not two.
void test_the_path_length_constraint_bounds_the_depth(void)
{
    TEST_ASSERT_TRUE(load(&g_ca, X509_CA_ED25519_DER, sizeof(X509_CA_ED25519_DER)));
    TEST_ASSERT_TRUE(g_ca.has_path_len);
    TEST_ASSERT_EQUAL_UINT32(1, g_ca.path_len);

    X509Verify.issuer_args.issuer = &g_ca;
    X509Verify.issuer_args.depth = 1;
    X509Verify.may_sign(NULL);
    TEST_ASSERT_TRUE(X509Verify.ok);

    X509Verify.issuer_args.depth = 2;
    X509Verify.may_sign(NULL);
    TEST_ASSERT_FALSE(X509Verify.ok);
    TEST_ASSERT_EQUAL(PROTOCORE_X509_ERR_PATH_LEN, X509Verify.status);
}

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------

void test_a_link_needs_both_certificates(void)
{
    TEST_ASSERT_TRUE(load(&g_leaf, X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    TEST_ASSERT_EQUAL(PROTOCORE_X509_ERR_ARGS, link_of(&g_leaf, NULL, inside()));
    TEST_ASSERT_EQUAL(PROTOCORE_X509_ERR_ARGS, link_of(NULL, &g_leaf, inside()));
}

// A null borrow is refused rather than dereferenced: the accessor hands one back when the pool was
// short, and every entry that reaches into it says so.
void test_a_signature_check_without_its_borrow_is_refused(void)
{
    TEST_ASSERT_TRUE(load(&g_leaf, X509_ED25519_DER, sizeof(X509_ED25519_DER)));
    TEST_ASSERT_TRUE(load(&g_ca, X509_CA_ED25519_DER, sizeof(X509_CA_ED25519_DER)));
    X509Verify.link_args.cert = &g_leaf;
    X509Verify.link_args.issuer = &g_ca;
    X509Verify.signature(NULL);
    TEST_ASSERT_FALSE(X509Verify.ok);
    TEST_ASSERT_EQUAL(PROTOCORE_X509_ERR_ARGS, X509Verify.status);
}
