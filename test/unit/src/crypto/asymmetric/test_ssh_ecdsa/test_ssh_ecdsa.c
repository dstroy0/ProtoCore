// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NIST P-256 primitives (crypto/asymmetric/ecdsa.h) behind
// ecdsa-sha2-nistp256 and ecdh-sha2-nistp256 (RFC 5656).
//
// The load-bearing case is test_rfc6979_deterministic_signatures. RFC 6979 Appendix A.2.5 publishes,
// for P-256 with SHA-256, the private scalar x, the public point (Ux, Uy), and the exact (r, s) for
// the messages "sample" and "test". A deterministic signature that reproduces those bytes exercises
// the whole stack at once - field and scalar reduction, Jacobian point math, the scalar ladder, and
// the HMAC-SHA256 nonce generation - against numbers nothing in this tree produced.
//
// ECDH is pinned to RFC 5903 sec 8.1 (256-bit Random ECP Group), which publishes both private keys,
// both public points, and the single shared X coordinate the two sides must agree on.

#include "crypto/asymmetric/ecdsa.h"
#include <string.h>

#include <unity.h>

static uint8_t g_work[PROTOCORE_ECDSA_BORROW] __attribute__((aligned(8)));

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t nib(char c)
{
    return (uint8_t)(c <= '9' ? c - '0' : ((c | 0x20) - 'a' + 10));
}

static void unhex(const char *h, uint8_t *out)
{
    for (size_t i = 0; h[2 * i] && h[2 * i + 1]; i++)
    {
        out[i] = (uint8_t)((nib(h[2 * i]) << 4) | nib(h[2 * i + 1]));
    }
}

// RFC 6979 A.2.5, curve NIST P-256.
static const char *const X = "C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721";
static const char *const UX = "60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6";
static const char *const UY = "7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299";
// With SHA-256, message = "sample"
static const char *const SAMPLE_R = "EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716";
static const char *const SAMPLE_S = "F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8";
// With SHA-256, message = "test"
static const char *const TEST_R = "F1ABB023518351CD71D881567B1EA663ED3EFCF6C5132B354F28D3B0B7D38367";
static const char *const TEST_S = "019F4113742A2B14BD25926B49C649155F267E60D3814B4C0CC84250E46F0083";
// The group order, printed as q in the same appendix.
static const char *const N = "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551";

// RFC 5903 sec 8.1: initiator and responder private keys, their public points, and the shared X.
static const char *const I_PRIV = "C88F01F510D9AC3F70A292DAA2316DE544E9AAB8AFE84049C62A9C57862D1433";
static const char *const GIX = "DAD0B65394221CF9B051E1FECA5787D098DFE637FC90B9EF945D0C3772581180";
static const char *const GIY = "5271A0461CDB8252D61F1C456FA3E59AB1F45B33ACCF5F58389E0577B8990BB3";
static const char *const R_PRIV = "C6EF9C5D78AE012A011164ACB397CE2088685D8F06BF9BE0B283AB46476BEE53";
static const char *const GRX = "D12DFB5289C8D4F81208B70270398C342296970A0BCCB74C736FC7554494BF63";
static const char *const GRY = "56FBF3CA366CC23E8157854C13C58D6AAC23F046ADA30F8353E74F33039872AB";
static const char *const GIRX = "D6840F6B42F6EDAFD13116E0E12565202FEF8E9ECE7DCE03812464D04B9442DE";

// Assemble the uncompressed point 0x04 || X || Y from two hex coordinates.
static void mkpub(uint8_t pub[65], const char *xh, const char *yh)
{
    pub[0] = 0x04;
    unhex(xh, pub + 1);
    unhex(yh, pub + 33);
}

// d*G must land on the point the appendix prints, in the uncompressed encoding RFC 5656 sec 3.1 uses.
void test_rfc6979_public_point(void)
{
    uint8_t priv[32], pub[65], want[32];
    unhex(X, priv);
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_pubkey(pub, priv));
    TEST_ASSERT_EQUAL_UINT8(0x04, pub[0]);
    unhex(UX, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, pub + 1, 32);
    unhex(UY, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, pub + 33, 32);
}

// RFC 6979 A.2.5, both SHA-256 rows: the signature is a function of (x, message) alone.
void test_rfc6979_deterministic_signatures(void)
{
    uint8_t priv[32], sig[64], want[64];
    unhex(X, priv);

    TEST_ASSERT_TRUE(protocore_ecdsa_p256_sign(sig, g_work, (const uint8_t *)"sample", 6, priv));
    unhex(SAMPLE_R, want);
    unhex(SAMPLE_S, want + 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, sig, 64);

    TEST_ASSERT_TRUE(protocore_ecdsa_p256_sign(sig, g_work, (const uint8_t *)"test", 4, priv));
    unhex(TEST_R, want);
    unhex(TEST_S, want + 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, sig, 64);
}

// The published (r, s) must verify under the published point.
void test_rfc6979_signatures_verify(void)
{
    uint8_t priv[32], pub[65], sig[64];
    unhex(X, priv);
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_pubkey(pub, priv));

    unhex(SAMPLE_R, sig);
    unhex(SAMPLE_S, sig + 32);
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_verify(pub, g_work, (const uint8_t *)"sample", 6, sig));

    unhex(TEST_R, sig);
    unhex(TEST_S, sig + 32);
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_verify(pub, g_work, (const uint8_t *)"test", 4, sig));
}

// The two published signatures are bound to their own messages: neither verifies under the other's.
void test_verify_binds_signature_to_message(void)
{
    uint8_t priv[32], pub[65], sig[64];
    unhex(X, priv);
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_pubkey(pub, priv));

    unhex(TEST_R, sig);
    unhex(TEST_S, sig + 32);
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(pub, g_work, (const uint8_t *)"sample", 6, sig));

    unhex(SAMPLE_R, sig);
    unhex(SAMPLE_S, sig + 32);
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(pub, g_work, (const uint8_t *)"test", 4, sig));
}

// A one-bit change in r, in s, or in the public point breaks the verification.
void test_verify_refuses_tampering(void)
{
    uint8_t priv[32], pub[65], sig[64], bad[64];
    unhex(X, priv);
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_pubkey(pub, priv));
    unhex(SAMPLE_R, sig);
    unhex(SAMPLE_S, sig + 32);

    memcpy(bad, sig, 64);
    bad[0] ^= 0x01;
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(pub, g_work, (const uint8_t *)"sample", 6, bad));

    memcpy(bad, sig, 64);
    bad[63] ^= 0x01;
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(pub, g_work, (const uint8_t *)"sample", 6, bad));

    uint8_t badpub[65];
    memcpy(badpub, pub, 65);
    badpub[1] ^= 0x01;
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(badpub, g_work, (const uint8_t *)"sample", 6, sig));
}

// RFC 5656 sec 3.1 carries Q as 0x04 || X || Y. A compressed-point prefix is a form this module does
// not accept, so it must be refused rather than read as if the 0x04 were there.
void test_verify_refuses_a_non_uncompressed_point(void)
{
    uint8_t priv[32], pub[65], sig[64];
    unhex(X, priv);
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_pubkey(pub, priv));
    unhex(SAMPLE_R, sig);
    unhex(SAMPLE_S, sig + 32);

    pub[0] = 0x02;
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(pub, g_work, (const uint8_t *)"sample", 6, sig));
    pub[0] = 0x00;
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(pub, g_work, (const uint8_t *)"sample", 6, sig));
}

// A coordinate of 2^256-1 is above the field prime, so the point cannot be decoded at all - a
// different refusal from an in-range coordinate that is simply off the curve.
void test_verify_refuses_an_out_of_field_coordinate(void)
{
    uint8_t priv[32], pub[65], sig[64], bad[65];
    unhex(X, priv);
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_pubkey(pub, priv));
    unhex(SAMPLE_R, sig);
    unhex(SAMPLE_S, sig + 32);

    memcpy(bad, pub, 65);
    memset(bad + 1, 0xFF, 32);
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(bad, g_work, (const uint8_t *)"sample", 6, sig));

    memcpy(bad, pub, 65);
    memset(bad + 33, 0xFF, 32);
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(bad, g_work, (const uint8_t *)"sample", 6, sig));
}

// ECDSA verification requires 1 <= r < n and 1 <= s < n. Zero and n itself sit either side of that
// window, so both must be refused before any curve math runs.
void test_verify_refuses_r_or_s_outside_one_to_n_minus_one(void)
{
    uint8_t priv[32], pub[65], base[64], bad[64], n[32];
    unhex(X, priv);
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_pubkey(pub, priv));
    unhex(SAMPLE_R, base);
    unhex(SAMPLE_S, base + 32);
    unhex(N, n);

    memcpy(bad, base, 64);
    memset(bad, 0, 32); // r = 0
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(pub, g_work, (const uint8_t *)"sample", 6, bad));

    memcpy(bad, base, 64);
    memcpy(bad, n, 32); // r = n
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(pub, g_work, (const uint8_t *)"sample", 6, bad));

    memcpy(bad, base, 64);
    memset(bad + 32, 0, 32); // s = 0
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(pub, g_work, (const uint8_t *)"sample", 6, bad));

    memcpy(bad, base, 64);
    memcpy(bad + 32, n, 32); // s = n
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(pub, g_work, (const uint8_t *)"sample", 6, bad));
}

// A private scalar must satisfy 1 <= d < n. Every entry point validates it for itself.
void test_private_scalar_bounds_are_enforced(void)
{
    uint8_t priv[32], pub[65], sig[64], shared[32], peer[65];
    mkpub(peer, GRX, GRY);

    memset(priv, 0, 32); // d = 0
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_pubkey(pub, priv));
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_sign(sig, g_work, (const uint8_t *)"x", 1, priv));
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_ecdh(shared, peer, priv));

    memset(priv, 0xFF, 32); // d = 2^256-1, above n
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_pubkey(pub, priv));
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_sign(sig, g_work, (const uint8_t *)"x", 1, priv));
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_ecdh(shared, peer, priv));

    unhex(N, priv); // d = n exactly, the first value outside the window
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_pubkey(pub, priv));
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_sign(sig, g_work, (const uint8_t *)"x", 1, priv));
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_ecdh(shared, peer, priv));
}

// RFC 5903 sec 8.1: g^i and g^r derived from the printed private keys.
void test_rfc5903_public_points(void)
{
    uint8_t priv[32], pub[65], want[32];

    unhex(I_PRIV, priv);
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_pubkey(pub, priv));
    unhex(GIX, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, pub + 1, 32);
    unhex(GIY, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, pub + 33, 32);

    unhex(R_PRIV, priv);
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_pubkey(pub, priv));
    unhex(GRX, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, pub + 1, 32);
    unhex(GRY, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, pub + 33, 32);
}

// RFC 5903 sec 8.1: "The Diffie-Hellman shared secret value is girx" - and both sides reach it.
void test_rfc5903_shared_secret(void)
{
    uint8_t ipriv[32], rpriv[32], ipub[65], rpub[65], want[32], got[32];
    unhex(I_PRIV, ipriv);
    unhex(R_PRIV, rpriv);
    mkpub(ipub, GIX, GIY);
    mkpub(rpub, GRX, GRY);
    unhex(GIRX, want);

    TEST_ASSERT_TRUE(protocore_ecdsa_p256_ecdh(got, rpub, ipriv));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_ecdh(got, ipub, rpriv));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

// A peer point that is not on the curve, or not in the uncompressed encoding, is refused before the
// scalar multiplication: an invalid-curve point is how a peer extracts the private scalar.
void test_ecdh_refuses_an_invalid_peer_point(void)
{
    uint8_t priv[32], bad[65], out[32];
    unhex(R_PRIV, priv);

    mkpub(bad, GIX, GIY);
    bad[1] ^= 0x01; // X moved off the curve
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_ecdh(out, bad, priv));

    mkpub(bad, GIX, GIY);
    bad[64] ^= 0x01; // Y moved off the curve
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_ecdh(out, bad, priv));

    mkpub(bad, GIX, GIY);
    bad[0] = 0x02; // compressed-point prefix
    TEST_ASSERT_FALSE(protocore_ecdsa_p256_ecdh(out, bad, priv));
}

// Sign then verify over many distinct scalars and message lengths: a windowed ladder has table-index
// and identity edges that two published vectors alone never reach, and each flip must be caught.
void test_sign_verify_round_trip_over_many_keys(void)
{
    uint64_t st = 0x9e3779b97f4a7c15ULL;
    for (int iter = 0; iter < 24; iter++)
    {
        uint8_t priv[32], pub[65], msg[32], sig[64];
        for (int i = 0; i < 32; i++)
        {
            st ^= st << 13;
            st ^= st >> 7;
            st ^= st << 17;
            priv[i] = (uint8_t)(st >> 24);
        }
        priv[0] &= 0x7F; // n has 0xFF as its top octet, so clearing the high bit keeps d < n
        priv[31] |= 0x01;

        TEST_ASSERT_TRUE(protocore_ecdsa_p256_pubkey(pub, priv));
        size_t mlen = (size_t)(iter + 1);
        for (size_t i = 0; i < mlen; i++)
        {
            msg[i] = (uint8_t)(i * 13 + iter);
        }
        TEST_ASSERT_TRUE(protocore_ecdsa_p256_sign(sig, g_work, msg, mlen, priv));
        TEST_ASSERT_TRUE(protocore_ecdsa_p256_verify(pub, g_work, msg, mlen, sig));
        sig[iter % 64] ^= (uint8_t)(1u << (iter % 8));
        TEST_ASSERT_FALSE(protocore_ecdsa_p256_verify(pub, g_work, msg, mlen, sig));
    }
}

// ECDH between two freshly derived keys agrees, which is the only property the KEX depends on.
void test_ecdh_agrees_for_derived_keys(void)
{
    uint8_t a[32], b[32], apub[65], bpub[65], s1[32], s2[32];
    for (int i = 0; i < 32; i++)
    {
        a[i] = (uint8_t)(i * 5 + 3);
        b[i] = (uint8_t)(i * 11 + 7);
    }
    a[0] &= 0x7F;
    b[0] &= 0x7F;
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_pubkey(apub, a));
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_pubkey(bpub, b));
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_ecdh(s1, bpub, a));
    TEST_ASSERT_TRUE(protocore_ecdsa_p256_ecdh(s2, apub, b));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(s1, s2, 32);
}
