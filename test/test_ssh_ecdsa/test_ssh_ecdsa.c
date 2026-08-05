// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// NIST P-256 native software-path tests (ecdsa-sha2-nistp256 signatures + ecdh-sha2-nistp256 KEX).
// ECDSA correctness is pinned to the RFC 6979 Appendix A.2.5 (P-256, SHA-256) deterministic
// known-answer vectors: the same private key, public point, and the exact (r, s) for messages
// "sample" and "test". A byte-exact deterministic signature proves the whole stack - field/scalar
// arithmetic, Jacobian point math, scalar multiplication, and RFC 6979 nonce generation. ECDH is
// pinned to the RFC 5903 §8.1 (256-bit Random ECP Group) shared-secret vectors.

#include "crypto/asymmetric/ecdsa.h"
#include <stdint.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// One hex digit to its value.
static int nib(char c)
{
    return c >= 'a' ? c - 'a' + 10 : (c >= 'A' ? c - 'A' + 10 : c - '0');
}

static size_t hexdec(const char *h, uint8_t *out)
{
    size_t n = 0;
    for (; h[0] && h[1]; h += 2)
    {
        out[n++] = (uint8_t)((nib(h[0]) << 4) | nib(h[1]));
    }
    return n;
}

// RFC 6979 A.2.5 (curve P-256).
static const char *PRIV = "C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721";
static const char *UX = "60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6";
static const char *UY = "7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299";
// message "sample", SHA-256
static const char *SAMPLE_R = "EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716";
static const char *SAMPLE_S = "F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8";
// message "test", SHA-256
static const char *TEST_R = "F1ABB023518351CD71D881567B1EA663ED3EFCF6C5132B354F28D3B0B7D38367";
static const char *TEST_S = "019F4113742A2B14BD25926B49C649155F267E60D3814B4C0CC84250E46F0083";

static void test_ecdsa_pubkey_matches_rfc6979(void)
{
    uint8_t priv[32];
    hexdec(PRIV, priv);
    uint8_t pub[65];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(pub, priv));
    TEST_ASSERT_EQUAL_UINT8(0x04, pub[0]);
    uint8_t ux[32], uy[32];
    hexdec(UX, ux);
    hexdec(UY, uy);
    TEST_ASSERT_EQUAL_MEMORY(ux, pub + 1, 32);
    TEST_ASSERT_EQUAL_MEMORY(uy, pub + 33, 32);
}

// The deterministic (RFC 6979) signature must be byte-exact against the published vector.
static void test_ecdsa_sign_deterministic_sample(void)
{
    uint8_t priv[32];
    hexdec(PRIV, priv);
    uint8_t sig[64];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_sign(sig, (const uint8_t *)"sample", 6, priv));
    uint8_t r[32], s[32];
    hexdec(SAMPLE_R, r);
    hexdec(SAMPLE_S, s);
    TEST_ASSERT_EQUAL_MEMORY(r, sig, 32);
    TEST_ASSERT_EQUAL_MEMORY(s, sig + 32, 32);
}

static void test_ecdsa_sign_deterministic_test(void)
{
    uint8_t priv[32];
    hexdec(PRIV, priv);
    uint8_t sig[64];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_sign(sig, (const uint8_t *)"test", 4, priv));
    uint8_t r[32], s[32];
    hexdec(TEST_R, r);
    hexdec(TEST_S, s);
    TEST_ASSERT_EQUAL_MEMORY(r, sig, 32);
    TEST_ASSERT_EQUAL_MEMORY(s, sig + 32, 32);
}

static void test_ecdsa_verify_valid(void)
{
    uint8_t priv[32];
    hexdec(PRIV, priv);
    uint8_t pub[65];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(pub, priv));

    uint8_t sig[64];
    hexdec(SAMPLE_R, sig);
    hexdec(SAMPLE_S, sig + 32);
    TEST_ASSERT_TRUE(pc_ecdsa_p256_verify(pub, (const uint8_t *)"sample", 6, sig));

    hexdec(TEST_R, sig);
    hexdec(TEST_S, sig + 32);
    TEST_ASSERT_TRUE(pc_ecdsa_p256_verify(pub, (const uint8_t *)"test", 4, sig));
}

static void test_ecdsa_verify_rejects_tamper(void)
{
    uint8_t priv[32];
    hexdec(PRIV, priv);
    uint8_t pub[65];
    pc_ecdsa_p256_pubkey(pub, priv);

    uint8_t sig[64];
    hexdec(SAMPLE_R, sig);
    hexdec(SAMPLE_S, sig + 32);

    // Wrong message (the "test" signature under the "sample" message).
    uint8_t sig_test[64];
    hexdec(TEST_R, sig_test);
    hexdec(TEST_S, sig_test + 32);
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(pub, (const uint8_t *)"sample", 6, sig_test));

    // Tampered signature (flip one bit of s).
    sig[63] ^= 0x01;
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(pub, (const uint8_t *)"sample", 6, sig));

    // Tampered public key (flip one bit of X -> off curve / wrong key).
    hexdec(SAMPLE_R, sig);
    hexdec(SAMPLE_S, sig + 32);
    pub[1] ^= 0x01;
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(pub, (const uint8_t *)"sample", 6, sig));
}

// A fresh key round-trips (exercises sign -> verify with a non-vector key).
static void test_ecdsa_roundtrip_other_key(void)
{
    uint8_t priv[32];
    memset(priv, 0, 32);
    priv[31] = 0x42; // d = 0x42
    uint8_t pub[65];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(pub, priv));
    const uint8_t msg[] = "deterministic ecdsa round trip";
    uint8_t sig[64];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_sign(sig, msg, sizeof(msg) - 1, priv));
    TEST_ASSERT_TRUE(pc_ecdsa_p256_verify(pub, msg, sizeof(msg) - 1, sig));
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(pub, (const uint8_t *)"other message", 13, sig));
}

// Stress the scalar multiplication across many distinct secret scalars: pubkey -> sign -> verify must
// round-trip and a one-bit tamper must be rejected, for 48 deterministic pseudo-random keys/messages.
// A fixed-window ladder has index/table-zero edge cases the two KAT vectors alone would not exercise.
// One byte of the deterministic xorshift64 stream, advancing the caller's state.
static uint8_t next_byte(uint64_t *st)
{
    *st ^= *st << 13;
    *st ^= *st >> 7;
    *st ^= *st << 17;
    return (uint8_t)(*st >> 24);
}

static void test_ecdsa_random_roundtrip_stress(void)
{
    uint64_t st = 0x9e3779b97f4a7c15ULL; // deterministic xorshift64 (reproducible, no RNG dependency)
    for (int iter = 0; iter < 48; iter++)
    {
        uint8_t priv[32];
        for (int i = 0; i < 32; i++)
        {
            priv[i] = next_byte(&st);
        }
        priv[0] &= 0x7F; // keep d < n (n's top byte is 0xFF, so clearing the MSB is sufficient and non-zero)
        priv[31] |= 0x01;

        uint8_t pub[65];
        TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(pub, priv));
        uint8_t msg[24];
        for (int i = 0; i < 24; i++)
        {
            msg[i] = next_byte(&st);
        }
        uint8_t sig[64];
        TEST_ASSERT_TRUE(pc_ecdsa_p256_sign(sig, msg, sizeof(msg), priv));
        TEST_ASSERT_TRUE(pc_ecdsa_p256_verify(pub, msg, sizeof(msg), sig));
        sig[iter % 64] ^= (uint8_t)(1u << (iter % 8)); // flip one bit somewhere in r||s
        TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(pub, msg, sizeof(msg), sig));
    }
}

// Invalid keys are rejected.
static void test_ecdsa_pubkey_rejects_bad_scalar(void)
{
    uint8_t priv[32];
    uint8_t pub[65];
    memset(priv, 0, 32);
    TEST_ASSERT_FALSE(pc_ecdsa_p256_pubkey(pub, priv)); // d = 0
    memset(priv, 0xFF, 32);
    TEST_ASSERT_FALSE(pc_ecdsa_p256_pubkey(pub, priv)); // d = 2^256-1 > n
}

// pc_ecdsa_p256_sign() validates its own private scalar independently of pubkey()'s check.
static void test_ecdsa_sign_rejects_bad_scalar(void)
{
    uint8_t priv[32];
    uint8_t sig[64];
    memset(priv, 0, 32);
    TEST_ASSERT_FALSE(pc_ecdsa_p256_sign(sig, (const uint8_t *)"x", 1, priv)); // d = 0
    memset(priv, 0xFF, 32);
    TEST_ASSERT_FALSE(pc_ecdsa_p256_sign(sig, (const uint8_t *)"x", 1, priv)); // d = 2^256-1 > n
}

// verify() rejects a public key with a compressed-point (or any non-0x04) prefix.
static void test_ecdsa_verify_rejects_bad_prefix(void)
{
    uint8_t priv[32];
    hexdec(PRIV, priv);
    uint8_t pub[65];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(pub, priv));
    pub[0] = 0x02; // compressed-point prefix, not accepted

    uint8_t sig[64];
    hexdec(SAMPLE_R, sig);
    hexdec(SAMPLE_S, sig + 32);
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(pub, (const uint8_t *)"sample", 6, sig));
}

// on_curve() must reject a coordinate that is out of the field range [0, p), a distinct failure mode
// from an in-range-but-off-curve coordinate (already exercised by test_ecdsa_verify_rejects_tamper).
static void test_ecdsa_verify_rejects_out_of_range_coord(void)
{
    uint8_t priv[32];
    hexdec(PRIV, priv);
    uint8_t pub[65];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(pub, priv));

    uint8_t sig[64];
    hexdec(SAMPLE_R, sig);
    hexdec(SAMPLE_S, sig + 32);

    uint8_t bad_pub[65];
    memcpy(bad_pub, pub, 65);
    memset(bad_pub + 1, 0xFF, 32); // X = 2^256-1 >= p
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(bad_pub, (const uint8_t *)"sample", 6, sig));

    memcpy(bad_pub, pub, 65);
    memset(bad_pub + 33, 0xFF, 32); // Y = 2^256-1 >= p
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(bad_pub, (const uint8_t *)"sample", 6, sig));
}

// verify() rejects a signature whose r or s is 0 or >= the group order n, before ever touching the
// public key's curve math.
static void test_ecdsa_verify_rejects_out_of_range_sig(void)
{
    uint8_t priv[32];
    hexdec(PRIV, priv);
    uint8_t pub[65];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(pub, priv));

    // The P-256 group order n itself: an out-of-range (>= n) r/s value.
    static const char *N_HEX = "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551";
    uint8_t n_bytes[32];
    hexdec(N_HEX, n_bytes);

    uint8_t base[64];
    hexdec(SAMPLE_R, base);
    hexdec(SAMPLE_S, base + 32);

    uint8_t bad[64];
    memcpy(bad, base, 64);
    memset(bad, 0, 32); // r = 0
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(pub, (const uint8_t *)"sample", 6, bad));

    memcpy(bad, base, 64);
    memcpy(bad, n_bytes, 32); // r = n (>= n)
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(pub, (const uint8_t *)"sample", 6, bad));

    memcpy(bad, base, 64);
    memset(bad + 32, 0, 32); // s = 0
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(pub, (const uint8_t *)"sample", 6, bad));

    memcpy(bad, base, 64);
    memcpy(bad + 32, n_bytes, 32); // s = n (>= n)
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(pub, (const uint8_t *)"sample", 6, bad));
}

// verify() must reject a signature crafted so R = u1*G + u2*Q is the point at infinity: with r = s = 1,
// w = s^-1 = 1, u1 = e, u2 = r = 1, so R = e*G + Q; picking Q = -e*G (i.e. deriving Q from the private
// scalar d' = (n - e) mod n, e = SHA256("forge") mod n) forces R = O. This is a real ECDSA edge case a
// maliciously crafted, otherwise well-formed (pub, sig) pair can hit and must be rejected.
static void test_ecdsa_verify_rejects_forged_infinity(void)
{
    static const char *DPRIME = "8E4BE2912B723A724570A306120CF010574EFCBB21727CC963EE1D77BF129CD4";
    uint8_t dpriv[32];
    hexdec(DPRIME, dpriv);
    uint8_t pub[65];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(pub, dpriv));

    uint8_t sig[64];
    memset(sig, 0, 64);
    sig[31] = 0x01; // r = 1
    sig[63] = 0x01; // s = 1
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(pub, (const uint8_t *)"forge", 5, sig));
}

// ---- ECDH (ecdh-sha2-nistp256) --------------------------------------------
// Pinned to RFC 5903 §8.1 (256-Bit Random ECP Group): two private keys, their public points,
// and the single shared secret X coordinate both sides must agree on.
static const char *ECDH_I_PRIV = "C88F01F510D9AC3F70A292DAA2316DE544E9AAB8AFE84049C62A9C57862D1433";
static const char *ECDH_R_PRIV = "C6EF9C5D78AE012A011164ACB397CE2088685D8F06BF9BE0B283AB46476BEE53";
static const char *ECDH_IX = "DAD0B65394221CF9B051E1FECA5787D098DFE637FC90B9EF945D0C3772581180";
static const char *ECDH_IY = "5271A0461CDB8252D61F1C456FA3E59AB1F45B33ACCF5F58389E0577B8990BB3";
static const char *ECDH_RX = "D12DFB5289C8D4F81208B70270398C342296970A0BCCB74C736FC7554494BF63";
static const char *ECDH_RY = "56FBF3CA366CC23E8157854C13C58D6AAC23F046ADA30F8353E74F33039872AB";
static const char *ECDH_SHARED = "D6840F6B42F6EDAFD13116E0E12565202FEF8E9ECE7DCE03812464D04B9442DE";

// Assemble a 65-byte uncompressed point 0x04 || X || Y from hex.
static void mkpub(uint8_t pub[65], const char *xh, const char *yh)
{
    pub[0] = 0x04;
    hexdec(xh, pub + 1);
    hexdec(yh, pub + 33);
}

// Both parties derive the identical shared secret X coordinate (RFC 5903 §8.1).
static void test_ecdh_rfc5903_shared_secret(void)
{
    uint8_t ipriv[32];
    uint8_t rpriv[32];
    hexdec(ECDH_I_PRIV, ipriv);
    hexdec(ECDH_R_PRIV, rpriv);
    uint8_t ipub[65];
    uint8_t rpub[65];
    mkpub(ipub, ECDH_IX, ECDH_IY);
    mkpub(rpub, ECDH_RX, ECDH_RY);
    uint8_t shared[32];
    hexdec(ECDH_SHARED, shared);

    uint8_t out[32];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_ecdh(out, rpub, ipriv)); // initiator: i * R
    TEST_ASSERT_EQUAL_MEMORY(shared, out, 32);
    TEST_ASSERT_TRUE(pc_ecdsa_p256_ecdh(out, ipub, rpriv)); // responder: r * I
    TEST_ASSERT_EQUAL_MEMORY(shared, out, 32);
}

// The private keys derive exactly the RFC's public points (pubkey cross-check for these keys).
static void test_ecdh_rfc5903_pubkeys(void)
{
    uint8_t ipriv[32];
    uint8_t rpriv[32];
    hexdec(ECDH_I_PRIV, ipriv);
    hexdec(ECDH_R_PRIV, rpriv);
    uint8_t ipub[65];
    uint8_t rpub[65];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(ipub, ipriv));
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(rpub, rpriv));
    uint8_t exp[32];
    hexdec(ECDH_IX, exp);
    TEST_ASSERT_EQUAL_MEMORY(exp, ipub + 1, 32);
    hexdec(ECDH_IY, exp);
    TEST_ASSERT_EQUAL_MEMORY(exp, ipub + 33, 32);
    hexdec(ECDH_RX, exp);
    TEST_ASSERT_EQUAL_MEMORY(exp, rpub + 1, 32);
    hexdec(ECDH_RY, exp);
    TEST_ASSERT_EQUAL_MEMORY(exp, rpub + 33, 32);
}

// ECDH rejects a malformed peer point (off-curve X, or a non-uncompressed prefix).
static void test_ecdh_rejects_bad_point(void)
{
    uint8_t priv[32];
    hexdec(ECDH_R_PRIV, priv);
    uint8_t out[32];

    uint8_t bad[65];
    mkpub(bad, ECDH_IX, ECDH_IY);
    bad[1] ^= 0x01; // corrupt X -> off curve
    TEST_ASSERT_FALSE(pc_ecdsa_p256_ecdh(out, bad, priv));

    uint8_t comp[65];
    mkpub(comp, ECDH_IX, ECDH_IY);
    comp[0] = 0x02; // compressed-point prefix, not accepted
    TEST_ASSERT_FALSE(pc_ecdsa_p256_ecdh(out, comp, priv));
}

// ecdh() validates its own private scalar independently of pubkey()'s / sign()'s checks.
static void test_ecdh_rejects_bad_scalar(void)
{
    uint8_t rpub[65];
    mkpub(rpub, ECDH_RX, ECDH_RY);
    uint8_t priv[32];
    uint8_t out[32];
    memset(priv, 0, 32);
    TEST_ASSERT_FALSE(pc_ecdsa_p256_ecdh(out, rpub, priv)); // d = 0
    memset(priv, 0xFF, 32);
    TEST_ASSERT_FALSE(pc_ecdsa_p256_ecdh(out, rpub, priv)); // d = 2^256-1 > n
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_ecdsa_pubkey_matches_rfc6979);
    RUN_TEST(test_ecdsa_sign_deterministic_sample);
    RUN_TEST(test_ecdsa_sign_deterministic_test);
    RUN_TEST(test_ecdsa_verify_valid);
    RUN_TEST(test_ecdsa_verify_rejects_tamper);
    RUN_TEST(test_ecdsa_roundtrip_other_key);
    RUN_TEST(test_ecdsa_random_roundtrip_stress);
    RUN_TEST(test_ecdsa_pubkey_rejects_bad_scalar);
    RUN_TEST(test_ecdsa_sign_rejects_bad_scalar);
    RUN_TEST(test_ecdsa_verify_rejects_bad_prefix);
    RUN_TEST(test_ecdsa_verify_rejects_out_of_range_coord);
    RUN_TEST(test_ecdsa_verify_rejects_out_of_range_sig);
    RUN_TEST(test_ecdsa_verify_rejects_forged_infinity);
    RUN_TEST(test_ecdh_rfc5903_shared_secret);
    RUN_TEST(test_ecdh_rfc5903_pubkeys);
    RUN_TEST(test_ecdh_rejects_bad_point);
    RUN_TEST(test_ecdh_rejects_bad_scalar);
    return UNITY_END();
}
