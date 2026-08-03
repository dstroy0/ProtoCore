// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SSH crypto layer test suite.
//
// Sections:
//   SHA-256       - NIST FIPS 180-4 test vectors
//   HMAC-SHA2-256 - RFC 4231 test vectors
//   AES-256-CTR   - NIST SP 800-38A test vectors
//   BIGNUM        - bn_from_bytes/to_bytes/cmp round-trips + group14 constants
//   DH-GROUP14    - bn_expmod_group14 with small-exponent reference values
//   RSA PKCS#1    - pkcs1v15 pad/unpad + sign/verify with a test key
//   PACKET        - ssh_pkt_send/recv round-trip (unencrypted + encrypted)

#include "crypto/aead/chachapoly.h"
#include "crypto/asymmetric/bignum.h"
#include "crypto/cipher/aes256ctr.h"
#include "crypto/hash/sha256.h"
#include "crypto/mac/ghash.h"
#include "crypto/mac/hmac_sha256.h"
#include "crypto/mac/hmac_sha512.h"
#include "mmgr/plaintext.h"
#include "mmgr/secure.h"
#include "network_drivers/presentation/ssh/transport/ssh_dh.h"
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/tls/ssh_rsa.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

// External test fixture arrays (defined in ssh_rsa.cpp native path).
extern uint8_t _test_rsa_n[256];
extern uint8_t _test_rsa_d[256];
extern uint8_t _test_rsa_e[4];

// ============================================================================
// Helpers
// ============================================================================

static void hex_to_bytes(uint8_t *out, const char *hex, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        unsigned hi = hex[2 * i] >= 'a'   ? hex[2 * i] - 'a' + 10
                      : hex[2 * i] >= 'A' ? hex[2 * i] - 'A' + 10
                                          : hex[2 * i] - '0';
        unsigned lo = hex[2 * i + 1] >= 'a'   ? hex[2 * i + 1] - 'a' + 10
                      : hex[2 * i + 1] >= 'A' ? hex[2 * i + 1] - 'A' + 10
                                              : hex[2 * i + 1] - '0';
        out[i] = (uint8_t)((hi << 4) | lo);
    }
}

static void bytes_to_hex(char *out, const uint8_t *in, size_t n)
{
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++)
    {
        out[2 * i] = H[in[i] >> 4];
        out[2 * i + 1] = H[in[i] & 0x0f];
    }
    out[2 * n] = '\0';
}

// ============================================================================
// SHA-256 tests (NIST FIPS 180-4, §B.1-B.3)
// ============================================================================

static void test_sha256_empty(void)
{
    // SHA256("") = e3b0c44298fc1c149afb...
    uint8_t got[32];
    pc_sha256(NULL, 0, got);
    uint8_t expected[32];
    hex_to_bytes(expected, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 32);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 32);
}

static void test_sha256_abc(void)
{
    // SHA256("abc") = ba7816bf8f01cfea414140de5dae2ec73b00361bbef0469...
    uint8_t got[32];
    const uint8_t *msg = (const uint8_t *)"abc";
    pc_sha256(msg, 3, got);
    uint8_t expected[32];
    hex_to_bytes(expected, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 32);
}

static void test_sha256_448bit(void)
{
    // SHA256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
    const uint8_t *msg = (const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t got[32];
    pc_sha256(msg, 56, got);
    uint8_t expected[32];
    hex_to_bytes(expected, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", 32);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 32);
}

static void test_sha256_streaming(void)
{
    // Same as test_sha256_abc but using the streaming API.
    pc_sha256_ctx ctx;
    pc_sha256_init(&ctx);
    pc_sha256_update(&ctx, (const uint8_t *)"a", 1);
    pc_sha256_update(&ctx, (const uint8_t *)"bc", 2);
    uint8_t got[32];
    pc_sha256_final(&ctx, got);
    uint8_t expected[32];
    hex_to_bytes(expected, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 32);
}

// ============================================================================
// HMAC-SHA2-256 tests (RFC 4231)
// ============================================================================

static void test_hmac_sha256_tc1(void)
{
    // RFC 4231 Test Case 1
    // Key  = 0x0b0b0b0b... (20 bytes of 0x0b)
    // Data = "Hi There"
    // HMAC = b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7
    uint8_t key[20];
    memset(key, 0x0b, 20);
    uint8_t got[32];
    pc_hmac_sha256(key, 20, (const uint8_t *)"Hi There", 8, got);
    uint8_t expected[32];
    hex_to_bytes(expected, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", 32);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 32);
}

static void test_hmac_sha256_tc2(void)
{
    // RFC 4231 Test Case 2
    // Key  = "Jefe"
    // Data = "what do ya want for nothing?"
    // HMAC = 5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843
    uint8_t got[32];
    pc_hmac_sha256((const uint8_t *)"Jefe", 4, (const uint8_t *)"what do ya want for nothing?", 28, got);
    uint8_t expected[32];
    hex_to_bytes(expected, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", 32);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 32);
}

static void test_hmac_sha256_tc3(void)
{
    // RFC 4231 Test Case 3
    // Key  = 0xaa... (20 bytes)
    // Data = 0xdd... (50 bytes)
    // HMAC = 773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe
    uint8_t key[20];
    memset(key, 0xaa, 20);
    uint8_t data[50];
    memset(data, 0xdd, 50);
    uint8_t got[32];
    pc_hmac_sha256(key, 20, data, 50, got);
    uint8_t expected[32];
    hex_to_bytes(expected, "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe", 32);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 32);
}

static void test_hmac_sha256_streaming(void)
{
    // Same as tc1 but via streaming API.
    uint8_t key[20];
    memset(key, 0x0b, 20);
    pc_hmac_sha256_ctx ctx;
    pc_hmac_sha256_init(&ctx, key, 20);
    pc_hmac_sha256_update(&ctx, (const uint8_t *)"Hi ", 3);
    pc_hmac_sha256_update(&ctx, (const uint8_t *)"There", 5);
    uint8_t got[32];
    pc_hmac_sha256_final(&ctx, got);
    uint8_t expected[32];
    hex_to_bytes(expected, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", 32);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 32);
}

static void test_hmac_sha512_tc1(void)
{
    // RFC 4231 Test Case 1: Key = 0x0b x20, Data = "Hi There".
    uint8_t key[20];
    memset(key, 0x0b, 20);
    uint8_t got[64];
    pc_hmac_sha512(key, 20, (const uint8_t *)"Hi There", 8, got);
    uint8_t expected[64];
    hex_to_bytes(expected,
                 "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cded"
                 "aa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854",
                 64);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 64);
}

static void test_hmac_sha512_tc2(void)
{
    // RFC 4231 Test Case 2: Key = "Jefe", Data = "what do ya want for nothing?".
    uint8_t got[64];
    pc_hmac_sha512((const uint8_t *)"Jefe", 4, (const uint8_t *)"what do ya want for nothing?", 28, got);
    uint8_t expected[64];
    hex_to_bytes(expected,
                 "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549"
                 "758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737",
                 64);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 64);
}

static void test_hmac_sha512_streaming(void)
{
    // Same as tc1 but via the streaming API (also exercises the 128-byte block boundary).
    uint8_t key[20];
    memset(key, 0x0b, 20);
    pc_hmac_sha512_ctx ctx;
    pc_hmac_sha512_init(&ctx, key, 20);
    pc_hmac_sha512_update(&ctx, (const uint8_t *)"Hi ", 3);
    pc_hmac_sha512_update(&ctx, (const uint8_t *)"There", 5);
    uint8_t got[64];
    pc_hmac_sha512_final(&ctx, got);
    uint8_t expected[64];
    hex_to_bytes(expected,
                 "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cded"
                 "aa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854",
                 64);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 64);
}

// RFC 4231 Test Case 6: a 131-byte key exceeds the 64-byte SHA-256 block, so build_key_block()
// pre-hashes it (RFC 2104 §2) instead of zero-padding - the key-length branch SSH-derived 32-byte
// keys never take.
static void test_hmac_sha256_tc6_large_key(void)
{
    uint8_t key[131];
    memset(key, 0xaa, sizeof(key));
    const char *data = "Test Using Larger Than Block-Size Key - Hash Key First";
    uint8_t got[32];
    pc_hmac_sha256(key, sizeof(key), (const uint8_t *)data, strlen(data), got);
    uint8_t expected[32];
    hex_to_bytes(expected, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", 32);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 32);
}

// RFC 4231 Test Case 6 for SHA-512: a 131-byte key exceeds the 128-byte SHA-512 block and is
// pre-hashed (exercises the >128-byte key-length branch of the SHA-512 build_key_block).
static void test_hmac_sha512_tc6_large_key(void)
{
    uint8_t key[131];
    memset(key, 0xaa, sizeof(key));
    const char *data = "Test Using Larger Than Block-Size Key - Hash Key First";
    uint8_t got[64];
    pc_hmac_sha512(key, sizeof(key), (const uint8_t *)data, strlen(data), got);
    uint8_t expected[64];
    hex_to_bytes(expected,
                 "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f352"
                 "6b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598",
                 64);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, 64);
}

// ============================================================================
// AES-256-CTR tests (NIST SP 800-38A, F.5.5 + F.5.6)
// ============================================================================

static void test_aes256ctr_encrypt(void)
{
    // NIST SP 800-38A, Section F.5.5
    // Key = 603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4
    // IV  = f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff
    // PT  = 6bc1bee22e409f96e93d7e117393172a (block 1)
    // CT  = 601ec313775789a5b7a7f504bbf3d228 (block 1)

    uint8_t key[32], iv[16];
    hex_to_bytes(key, "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", 32);
    hex_to_bytes(iv, "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", 16);

    uint8_t pt[16], ct[16];
    hex_to_bytes(pt, "6bc1bee22e409f96e93d7e117393172a", 16);

    uint8_t ctr[16];
    memcpy(ctr, iv, 16);
    pc_aes256ctr_crypt(key, ctr, pt, ct, 16);

    uint8_t expected[16];
    hex_to_bytes(expected, "601ec313775789a5b7a7f504bbf3d228", 16);
    TEST_ASSERT_EQUAL_MEMORY(expected, ct, 16);
}

static void test_aes256ctr_decrypt(void)
{
    // AES-256-CTR decrypt is identical to encrypt.
    uint8_t key[32], iv[16];
    hex_to_bytes(key, "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", 32);
    hex_to_bytes(iv, "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", 16);

    uint8_t ct[16], pt[16];
    hex_to_bytes(ct, "601ec313775789a5b7a7f504bbf3d228", 16);

    uint8_t ctr[16];
    memcpy(ctr, iv, 16);
    pc_aes256ctr_crypt(key, ctr, ct, pt, 16);

    uint8_t expected[16];
    hex_to_bytes(expected, "6bc1bee22e409f96e93d7e117393172a", 16);
    TEST_ASSERT_EQUAL_MEMORY(expected, pt, 16);
}

static void test_aes256ctr_multi_block(void)
{
    // NIST F.5.5 blocks 1-4 (64 bytes).
    uint8_t key[32], iv[16];
    hex_to_bytes(key, "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", 32);
    hex_to_bytes(iv, "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", 16);

    uint8_t pt[64], ct[64];
    hex_to_bytes(pt,
                 "6bc1bee22e409f96e93d7e117393172a"
                 "ae2d8a571e03ac9c9eb76fac45af8e51"
                 "30c81c46a35ce411e5fbc1191a0a52ef"
                 "f69f2445df4f9b17ad2b417be66c3710",
                 64);

    uint8_t ctr[16];
    memcpy(ctr, iv, 16);
    pc_aes256ctr_crypt(key, ctr, pt, ct, 64);

    uint8_t expected[64];
    hex_to_bytes(expected,
                 "601ec313775789a5b7a7f504bbf3d228"
                 "f443e3ca4d62b59aca84e990cacaf5c5"
                 "2b0930daa23de94ce87017ba2d84988d"
                 "dfc9c58db67aada613c2dd08457941a6",
                 64);
    TEST_ASSERT_EQUAL_MEMORY(expected, ct, 64);
}

static void test_aes256ctr_scratch_wiped(void)
{
    // Security model: the ephemeral AES key schedule lives in the shared crypto scratch and MUST be wiped
    // after every call, so no expanded key material lingers in the one region an attacker would target.
    uint8_t key[32], ctr[16], in[32] = {0}, out[32];
    hex_to_bytes(key, "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", 32);
    hex_to_bytes(ctr, "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", 16);
    pc_aes256ctr_crypt(key, ctr, in, out, 32);
    // The schedule + keystream were borrowed and released, and the secure pool wipes on release. Observe
    // it the way any caller would: the next borrow of the same size must come back clean, so no expanded
    // key material survives into the next tenant.
    SecureScope scope;
    pc_span reuse = pc_secure_span(256, 16);
    TEST_ASSERT_TRUE(pc_span_ok(reuse));
    uint8_t zeros[256] = {0};
    TEST_ASSERT_EQUAL_MEMORY(zeros, reuse.buf, 256);
}

// ============================================================================
// BigNum tests
// ============================================================================

static void test_bn_roundtrip(void)
{
    // Round-trip: bytes → pc_bignum → bytes.
    uint8_t src[256];
    for (int i = 0; i < 256; i++)
    {
        src[i] = (uint8_t)i;
    }
    pc_bignum bn;
    bn_from_bytes(&bn, src, 256);
    uint8_t dst[256];
    bn_to_bytes(dst, &bn);
    TEST_ASSERT_EQUAL_MEMORY(src, dst, 256);
}

static void test_bn_cmp_equal(void)
{
    pc_bignum a, b;
    memset(a.d, 0x55, sizeof(a.d));
    memset(b.d, 0x55, sizeof(b.d));
    TEST_ASSERT_EQUAL_INT(0, bn_cmp(&a, &b));
}

static void test_bn_cmp_less(void)
{
    pc_bignum a, b;
    memset(a.d, 0, sizeof(a.d));
    memset(b.d, 0, sizeof(b.d));
    b.d[0] = 1;
    TEST_ASSERT_EQUAL_INT(-1, bn_cmp(&a, &b));
}

static void test_bn_cmp_greater(void)
{
    pc_bignum a, b;
    memset(a.d, 0, sizeof(a.d));
    memset(b.d, 0, sizeof(b.d));
    a.d[63] = 1;
    TEST_ASSERT_EQUAL_INT(1, bn_cmp(&a, &b));
}

static void test_bn_is_zero(void)
{
    pc_bignum a;
    memset(a.d, 0, sizeof(a.d));
    TEST_ASSERT_NOT_EQUAL(0, bn_is_zero(&a));
    a.d[0] = 1;
    TEST_ASSERT_EQUAL_INT(0, bn_is_zero(&a));
}

static void test_bn_dh_validate_rejects_zero(void)
{
    pc_bignum zero;
    memset(zero.d, 0, sizeof(zero.d));
    TEST_ASSERT_EQUAL_INT(-1, bn_dh_validate(&zero));
}

static void test_bn_dh_validate_rejects_one(void)
{
    pc_bignum one;
    memset(one.d, 0, sizeof(one.d));
    one.d[0] = 1;
    TEST_ASSERT_EQUAL_INT(-1, bn_dh_validate(&one));
}

static void test_bn_dh_validate_accepts_two(void)
{
    pc_bignum two;
    memset(two.d, 0, sizeof(two.d));
    two.d[0] = 2;
    TEST_ASSERT_EQUAL_INT(0, bn_dh_validate(&two));
}

// ============================================================================
// DH-group14 modular exponentiation - small-exponent reference values
// ============================================================================

// g^1 mod p = g (= 2)
static void test_expmod_exp1(void)
{
    pc_bignum result;
    pc_bignum exp;
    memset(exp.d, 0, sizeof(exp.d));
    exp.d[0] = 1; // exponent = 1
    bn_expmod_group14(&result, &group14_g, &exp);

    // result should equal group14_g (= 2)
    TEST_ASSERT_EQUAL_INT(0, bn_cmp(&result, &group14_g));
}

// g^2 mod p = 4  (g = 2, so 2^2 = 4)
static void test_expmod_exp2(void)
{
    pc_bignum result;
    pc_bignum exp;
    memset(exp.d, 0, sizeof(exp.d));
    exp.d[0] = 2; // exponent = 2
    bn_expmod_group14(&result, &group14_g, &exp);

    pc_bignum four;
    memset(four.d, 0, sizeof(four.d));
    four.d[0] = 4;
    TEST_ASSERT_EQUAL_INT(0, bn_cmp(&result, &four));
}

// g^3 mod p = 8
static void test_expmod_exp3(void)
{
    pc_bignum result;
    pc_bignum exp;
    memset(exp.d, 0, sizeof(exp.d));
    exp.d[0] = 3;
    bn_expmod_group14(&result, &group14_g, &exp);

    pc_bignum eight;
    memset(eight.d, 0, sizeof(eight.d));
    eight.d[0] = 8;
    TEST_ASSERT_EQUAL_INT(0, bn_cmp(&result, &eight));
}

// DH commutativity: g^(ab) == (g^a)^b == (g^b)^a
static void test_expmod_commutative(void)
{
    pc_bignum a_exp, b_exp, ga, gb, gab, gba;
    memset(a_exp.d, 0, sizeof(a_exp.d));
    memset(b_exp.d, 0, sizeof(b_exp.d));
    a_exp.d[0] = 0x000003E7u; // 999
    b_exp.d[0] = 0x0000044Du; // 1101

    bn_expmod_group14(&ga, &group14_g, &a_exp); // g^a
    bn_expmod_group14(&gb, &group14_g, &b_exp); // g^b
    bn_expmod_group14(&gab, &ga, &b_exp);       // (g^a)^b = g^(ab)
    bn_expmod_group14(&gba, &gb, &a_exp);       // (g^b)^a = g^(ab)

    TEST_ASSERT_EQUAL_INT(0, bn_cmp(&gab, &gba));
}

// ============================================================================
// RSA PKCS#1 v1.5 tests
// ============================================================================

// Minimal RSA-512 test key (512-bit for test speed; real SSH uses 2048-bit).
// n, d, e are actual RSA-512 parameters computed offline.
// These are intentionally small; they must never be used in production.
//
// n = 0x00c4f0a4... (see below, 64-byte big-endian = 512-bit)
// e = 65537 (0x00010001)
// d = modular inverse of e mod phi(n)
//
// NOTE: The native ssh_rsa_sign() path uses the pc_bignum type which is
// fixed at 2048 bits (256 bytes).  For a 512-bit key we simply zero-pad.
// The PKCS#1 padding is built for PC_RSA_KEY_BYTES (256) regardless of
// the actual key size in the test; this means the test validates the
// padding and signing logic but uses a shorter effective key.
// For a clean test, we use a small but valid 2048-bit RSA test key below.
//
// RSA-2048 test key (RFC 2313 example, computed offline):
// Only n, e, d are used by the native path; p/q/dp/dq/qinv are not needed.
// This is a KNOWN test key - NEVER use for actual SSH.

static const char TEST_N[] = "00b3510a083aaa7ef840daf1aead98a3"
                             "3bd31ded785f9d5bfe3b4f52ac018c8e"
                             "cc3b07e6b3aa9fb8a8c38a8a5c4af9f5"
                             "e4c89bce1c85b1ae3349c7b9abf6ead9"
                             "3ab2b5e1a6d447eaf4c857b3b4a64893"
                             "eb08f8d86a9a63e8fdef3aab9be4dec7"
                             "16234b4e2e5fd1e3ecf8bf19d95e6b2e"
                             "7f90a67c24a6b7a41b31e7f8b524eee9";

static const char TEST_D[] = "009e4a478869b82befe9a5e1adf5e4ac"
                             "b6f88dd3a9a17f3de5a56d25c8c6d2d4"
                             "0f9c43f4c17d4e1b3ba4d6dd3c4c2e9b"
                             "0f2dae3b49a8c2f2a7d5b2f2e3a5c1b2"
                             "1f2e3d4c5b6a7980aabbccddeeff0011"
                             "2233445566778899aabbccddeeff0011"
                             "2233445566778899aabbccddeeff0011"
                             "2233445566778899aabbccddeeff0001";

static void setup_test_rsa_key(void)
{
    // Use a known 2048-bit n from the hex string above (padded as needed).
    // This is a synthetic key whose correctness we can check structurally.
    hex_to_bytes(_test_rsa_n, TEST_N, 128);
    // For the purpose of PKCS#1 pad + structure tests, d is only needed for
    // the square-and-multiply expmod.  Use a trivially verifiable d=1 so
    // s = m^1 mod n = m (i.e., the "signature" equals the padded message).
    memset(_test_rsa_d, 0, 256);
    _test_rsa_d[255] = 0x01; // d = 1 in big-endian
    _test_rsa_e[0] = 0x00;
    _test_rsa_e[1] = 0x01;
    _test_rsa_e[2] = 0x00;
    _test_rsa_e[3] = 0x01; // e = 65537
}

static void test_rsa_pkcs1_pad_structure(void)
{
    // With d=1, sign(msg) = m^1 mod n = m (the padded message itself).
    // We can verify the PKCS#1 v1.5 padding structure in the output.
    setup_test_rsa_key();
    pc_ssh_rsa_load_pubkey();

    const uint8_t msg[] = "test message for PKCS1 padding check";
    uint8_t sig[256];
    int rc = ssh_rsa_sign(msg, sizeof(msg) - 1, SHA256, sig);
    TEST_ASSERT_EQUAL_INT(0, rc);

    // When d=1 and m < n, sig = m (the padded plaintext).
    // Check padding structure: sig[0]=0x00, sig[1]=0x01, ...0xFF..., 0x00, DigestInfo.
    // Note: big-endian output, so sig[0] is MSB.
    TEST_ASSERT_EQUAL_INT(0x00, sig[0]);
    TEST_ASSERT_EQUAL_INT(0x01, sig[1]);
    // Bytes 2..203 should be 0xFF.
    for (int i = 2; i <= 203; i++)
    {
        TEST_ASSERT_EQUAL_INT(0xFF, sig[i]);
    }
    TEST_ASSERT_EQUAL_INT(0x00, sig[204]);
    // DigestInfo header at sig[205..223].
    TEST_ASSERT_EQUAL_MEMORY(pc_pkcs1_sha256_digestinfo, sig + 205, 19);
}

// Real RSA-2048 keypair (generated offline with `openssl genrsa 2048`).
// Used to prove the native signing path is correct for a genuine private
// exponent d != 1: sign(msg) then verify(msg) must round-trip.  This is a
// KNOWN test key - NEVER use for actual SSH.
static const char RT_N[] = "a855616bbe1ed8ff73463006a1c2e9fcb67d8b3f39e19df514bd17f444697402"
                           "52d7e059497714b436fdabfc56a75a09ce85f9946f2896cd57e705a7d432a89c"
                           "c2296ad8e04b12e26648279c203cbfe4e0784a0dc4be8b370abf02c126f87aa3"
                           "7860267f4b11b49fcc00d830b45fc2fb5ded85ddcbe684201ed7c614de313ead"
                           "2410bc0ef689cc32909159f6c29279075c4bbf43deea939b32901a93c1bcb38c"
                           "528253951433076c1a3b9864e9d95f04c3757634a8626cae7826f501a2f20288"
                           "6eb4b63fc27b1bff84bafb4be5b4d1bc603e1c711112dc318a5213e7c2a71d49"
                           "6935767aec1dcd5e46f3d159f51daf8525e3b78833f561af2e34a2698539c58f";
static const char RT_D[] = "0568149fe10fdd01f242187e5ce4b3435f90d88f98611ccce2fdb4aa2edd2ec8"
                           "792dfb1f7c2bc999850d352991045c95530d254159dea2f0be78614b7abaa617"
                           "511f279206aac4e74a7efea4fa705fec97ea82dcac34888492d7ffdc93e15a7e"
                           "749eacd26f6706adeb8441ffa02ee80ff37c4996c508b4921bd259a322f26264"
                           "204e74612f1e58b1b5c54507a01916751dfcac4ed30aade8a0e458d2d636a35a"
                           "1f309e7bec81b348aa603b14be4c8dfc4dc2b5693fdfa1c1227ad33999c2f4d1"
                           "5f6ecea9d3fdb3337c31a11eb498f30c2b47ad9fcff3a68240e23595e406ceb4"
                           "ba73c3d29a97a67517ca45ae871673ca769cdf627a72ccd8b7bb3d57a195c3b5";

static void test_rsa_sign_verify_roundtrip(void)
{
    // Install the real keypair into the native sign fixture.
    hex_to_bytes(_test_rsa_n, RT_N, 256);
    hex_to_bytes(_test_rsa_d, RT_D, 256);
    _test_rsa_e[0] = 0x00;
    _test_rsa_e[1] = 0x01;
    _test_rsa_e[2] = 0x00;
    _test_rsa_e[3] = 0x01; // e = 65537

    const uint8_t msg[] = "round-trip with a real private exponent";
    uint8_t sig[256];
    int rc = ssh_rsa_sign(msg, sizeof(msg) - 1, SHA256, sig);
    TEST_ASSERT_EQUAL_INT(0, rc);

    // The signature must NOT equal the padded message (d != 1 exercised).
    uint8_t em[256];
    {
        uint8_t digest[PC_SHA256_DIGEST_LEN];
        pc_sha256(msg, sizeof(msg) - 1, digest);
        // Rebuild the expected EM to confirm the modexp actually transformed it.
        em[0] = 0x00;
        em[1] = 0x01;
        memset(em + 2, 0xFF, 256 - 3 - PC_PKCS1_DIGESTINFO_LEN - PC_SHA256_DIGEST_LEN);
        em[256 - 1 - PC_PKCS1_DIGESTINFO_LEN - PC_SHA256_DIGEST_LEN] = 0x00;
        memcpy(em + 256 - PC_PKCS1_DIGESTINFO_LEN - PC_SHA256_DIGEST_LEN, pc_pkcs1_sha256_digestinfo,
               PC_PKCS1_DIGESTINFO_LEN);
        memcpy(em + 256 - PC_SHA256_DIGEST_LEN, digest, PC_SHA256_DIGEST_LEN);
    }
    TEST_ASSERT_NOT_EQUAL(0, memcmp(sig, em, 256));

    // Public verify of our own signature must succeed.
    uint8_t n[256];
    uint8_t e[4] = {0x00, 0x01, 0x00, 0x01};
    hex_to_bytes(n, RT_N, 256);
    TEST_ASSERT_EQUAL_INT(0, pc_rsa_verify(n, e, msg, sizeof(msg) - 1, sig, 256, SHA256));

    // A tampered message must fail verification of the same signature.
    TEST_ASSERT_EQUAL_INT(-1, pc_rsa_verify(n, e, (const uint8_t *)"different", 9, sig, 256, SHA256));
}

static void test_rsa_encode_pubkey(void)
{
    setup_test_rsa_key();
    pc_ssh_rsa_load_pubkey();
    TEST_ASSERT_TRUE(ssh_host_pubkey.loaded);

    uint8_t blob[SSH_RSA_PUBKEY_BLOB_MAX];
    size_t blob_len = 0;
    int rc = ssh_rsa_encode_pubkey(blob, &blob_len, sizeof(blob));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_GREATER_THAN(0, (int)blob_len);

    // First 4 bytes: uint32 length of the key-blob type string = 7 ("ssh-rsa").
    // Per RFC 8332 §3 the RSA public-key blob always uses "ssh-rsa", even
    // though the signature algorithm is "rsa-sha2-256".
    uint32_t alg_len =
        ((uint32_t)blob[0] << 24) | ((uint32_t)blob[1] << 16) | ((uint32_t)blob[2] << 8) | (uint32_t)blob[3];
    TEST_ASSERT_EQUAL_UINT32(7, alg_len);
    // Key-blob type string bytes 4..10: "ssh-rsa"
    TEST_ASSERT_EQUAL_MEMORY("ssh-rsa", blob + 4, 7);
}

// pc_rsa_verify / ssh_rsa_encode_pubkey fail-closed guards, plus the modexp exp==0 fast path.
static void test_rsa_verify_and_encode_guards(void)
{
    uint8_t n[256];
    uint8_t e[4] = {0x00, 0x01, 0x00, 0x01};
    hex_to_bytes(n, RT_N, 256);
    uint8_t sig[256];

    memset(sig, 0, sizeof(sig));
    TEST_ASSERT_EQUAL_INT(-1, pc_rsa_verify(n, e, (const uint8_t *)"m", 1, sig, 255, SHA256)); // sig length mismatch
    memset(sig, 0xFF, sizeof(sig)); // all-ones >= any modulus
    TEST_ASSERT_EQUAL_INT(-1, pc_rsa_verify(n, e, (const uint8_t *)"m", 1, sig, 256, SHA256)); // sig not reduced mod n

    setup_test_rsa_key();
    pc_ssh_rsa_load_pubkey();
    uint8_t blob[SSH_RSA_PUBKEY_BLOB_MAX];
    size_t blob_len = 0;
    TEST_ASSERT_EQUAL_INT(-1, ssh_rsa_encode_pubkey(blob, &blob_len, SSH_RSA_PUBKEY_BLOB_MAX - 1)); // out_cap too small
    ssh_host_pubkey.loaded = PROTO_FALSE;
    TEST_ASSERT_EQUAL_INT(-1, ssh_rsa_encode_pubkey(blob, &blob_len, sizeof(blob))); // no key loaded

    // A zero private exponent takes the modexp exp==0 fast path (result 1), so signing succeeds
    // and yields s == 1 (0x00..01). Degenerate, but it exercises the guard.
    setup_test_rsa_key();
    memset(_test_rsa_d, 0, 256);
    pc_ssh_rsa_load_pubkey();
    uint8_t sig0[256];
    TEST_ASSERT_EQUAL_INT(0, ssh_rsa_sign((const uint8_t *)"x", 1, SHA256, sig0));
    for (int i = 0; i < 255; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, sig0[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(1, sig0[255]);

    setup_test_rsa_key(); // restore the fixture for any later test
    pc_ssh_rsa_load_pubkey();
}

// Real RSA-2048 PKCS#1 v1.5 SHA-256 signature over "hello ssh", produced with
// `openssl dgst -sha256 -sign` (a genuine known-answer vector).
static const char *RSA_KAT_N =
    "932e817a74435fdd1d7c7c5e8ec9b0f2a8b06b6b3db9b03e907d010d2c003985deaeb56d1ac5734116772da333131f8e"
    "b0b5bec0eff6f4dfd612068d2857acefa7dc75e84b0f2ffca57e82297f4f085b8584caebaa5a4be51c6f5887529f84fb"
    "ee5f6940a31307d1224714705ca5cf47ca9e04d2a9faafe7b022be41f426a1868f61410141a0a8b39110107f59d9c5a5"
    "920e8e2921ccaf85672ba0f860644f793f7c38425018c17e915a5f18ba5cfcf002a6a8fc50eeb08bc2f1e81f1df69704"
    "aacfce062bcd9ef678197e9778d411f3f364959637d09d56c6adc598147a6f924d9075a3df5dd2c606a9e963afd49fe5"
    "6b4633e5e905871d0b31093954ec57ff";
static const char *RSA_KAT_SIG =
    "7ee68bd6b7ae465b1927af68eb59fdef8ecf32ad786ca8cd85b71da485610a78d547df0992ed1d95ea6d8d9f077f7f40"
    "0f5113480d74e71d1173a86f2a1363f6168369ea1f65a092c4d3433a75b6a8601a49bcc3920f3d2f868f34ed3dde6f3e"
    "4f0472aa4a4ef212e2e41cbf2eb44684c7e6185627e52cf7527c31edcb96dfd7b4156edd201e3b0acab462dc21ef5a67"
    "db8ad4e3a948eb442bd88add40ba4926f6a401f0c52ae07050bc5c15794b6d86cc1d10ab497724949a827e331b5af733"
    "ccfa3eaf6f91d42734fcad16a80de831e928b651f2de513d5faf7e7798a6d49cdb0b9da548bb47e6097079d5576765065"
    "46b57391271f946e14f8082c3dc0da1";

static void test_rsa_verify_valid_signature(void)
{
    uint8_t n[256], sig[256];
    uint8_t e[4] = {0x00, 0x01, 0x00, 0x01}; // 65537
    hex_to_bytes(n, RSA_KAT_N, 256);
    hex_to_bytes(sig, RSA_KAT_SIG, 256);
    const char *msg = "hello ssh";
    TEST_ASSERT_EQUAL_INT(0, pc_rsa_verify(n, e, (const uint8_t *)msg, 9, sig, 256, SHA256));
}

static void test_rsa_verify_rejects_tampered_signature(void)
{
    uint8_t n[256], sig[256];
    uint8_t e[4] = {0x00, 0x01, 0x00, 0x01};
    hex_to_bytes(n, RSA_KAT_N, 256);
    hex_to_bytes(sig, RSA_KAT_SIG, 256);
    sig[200] ^= 0x01; // flip one bit
    TEST_ASSERT_EQUAL_INT(-1, pc_rsa_verify(n, e, (const uint8_t *)"hello ssh", 9, sig, 256, SHA256));
}

static void test_rsa_verify_rejects_wrong_message(void)
{
    uint8_t n[256], sig[256];
    uint8_t e[4] = {0x00, 0x01, 0x00, 0x01};
    hex_to_bytes(n, RSA_KAT_N, 256);
    hex_to_bytes(sig, RSA_KAT_SIG, 256);
    TEST_ASSERT_EQUAL_INT(-1, pc_rsa_verify(n, e, (const uint8_t *)"goodbye!", 8, sig, 256, SHA256));
}

// Real RSA-2048 rsa-sha2-512 KAT (RFC 8332): a genuine PKCS#1 v1.5 SHA-512
// signature over RSA512_MSG, produced with python cryptography's RSA sign
// (padding.PKCS1v15, hashes.SHA512). Fresh key; NEVER use for actual SSH.
static const char *RSA512_N = "beeda21e84ecc2e3335ce4f4f247ba4847d0bf23cc335effe99cbf54bf7e7428"
                              "8a9a06d130f34b760071146b4689ac0f04abe7cad4c883a163ef98446b28b7ad"
                              "5177c509fd5810b08e1acac05128496bfec0966ad69921366949d7b8b1d7e17f"
                              "35b33b0681fc64afe7d3056b90293f757996648680ec195b1f45fb517f34529b"
                              "ab86a3669afa957e4156820b2405ef560f1da6cd77b6f8a6a4298a03698ac1de"
                              "4bc4884bcc2325eb6b59e3476fa03abd539ebffeadf52da5ecbf8a28ef056aaa"
                              "b157efd5fb2a59d9394a007978a3cdb1e2e8018060537518b6ab0854da88ed25"
                              "4cc63a52b1332a4631522a9a84577acead26bbefab695e5502a9f9e14421b73d";
static const char *RSA512_D = "03a9d89e004bf0b35e556e793abae09aa9721a70cbe6c27063a1a3d432f670b1"
                              "2473af24cd6d25aa067924fca7f6554c56791bf1fae23c1059340c3667ddf8a4"
                              "4537689af7f6fc1eff230977e636c12de6cdf834e5983b98692dc70b5eb2373b"
                              "f32254c41bb36595307c0e9311499153a6391a05b0ac9711f6082839d8987eeb"
                              "4042247dc8f321efc730abf53170b02b55aba49d7e2323c782ebfebb34b3c634"
                              "f34d0fd1cc81088c9c7db441169b1e26a3ad39d5d2e43b0ebe9b6fc6e71931f8"
                              "a255d837862f830a3c82f2fb31ae5b47138bfed232aeeb74ddf766483edea5e1"
                              "60f4dbe3cb313587a642e63caf60dcedddc4b229f072ef1f4cc8e2c5cd5e7401";
static const char *RSA512_SIG = "b09369d7c15b084a780c4db0f1ed03d2831f66c8d5b0143d2ba9e57236756fee"
                                "958510ff01894c07ab776a9c1724b5cf331f124b96e067811a9dc6a3d4d925b8"
                                "2cd63326da20d9d1f75afcac9d6cd55d683f30bea9108139af43c03e76e65fa9"
                                "d1390d031484aba9bce4ce9dce930a04bacdb43488ecd1359df056606651fccf"
                                "e759b3df4f76373b7caf2ac209c96562a99040c07033749e19ab0c0817ad6e12"
                                "fe64d73cdb628794e00c828b34fd9fde35463e1eb590a76185752b66fa37085a"
                                "ddf7b37645b08b844a5090a0e9b9e86d084873b3b233cf030dd6f4069a7d3bc8"
                                "dd26bb6a3bafddd425303f87e507a19f4f97e38ffdad5ea6a929f51429d4ab27";
static const char RSA512_MSG[] = "hello rsa-sha2-512";

// Prove the native SHA-512 sign path is byte-exact against the reference, that the
// signature round-trips under SHA-512, and that the hash algorithm is bound into the
// signature (verifying a rsa-sha2-512 signature as rsa-sha2-256 must fail).
static void test_rsa_sha512_kat_sign_verify(void)
{
    hex_to_bytes(_test_rsa_n, RSA512_N, 256);
    hex_to_bytes(_test_rsa_d, RSA512_D, 256);
    _test_rsa_e[0] = 0x00;
    _test_rsa_e[1] = 0x01;
    _test_rsa_e[2] = 0x00;
    _test_rsa_e[3] = 0x01; // e = 65537

    const size_t mlen = sizeof(RSA512_MSG) - 1;

    // Native SHA-512 sign must byte-match the openssl/cryptography reference.
    uint8_t sig[256];
    TEST_ASSERT_EQUAL_INT(0, ssh_rsa_sign((const uint8_t *)RSA512_MSG, mlen, SHA512, sig));
    uint8_t ref[256];
    hex_to_bytes(ref, RSA512_SIG, 256);
    TEST_ASSERT_EQUAL_MEMORY(ref, sig, 256);

    // Both our signature and the reference verify under SHA-512.
    uint8_t n[256];
    uint8_t e[4] = {0x00, 0x01, 0x00, 0x01};
    hex_to_bytes(n, RSA512_N, 256);
    TEST_ASSERT_EQUAL_INT(0, pc_rsa_verify(n, e, (const uint8_t *)RSA512_MSG, mlen, sig, 256, SHA512));
    TEST_ASSERT_EQUAL_INT(0, pc_rsa_verify(n, e, (const uint8_t *)RSA512_MSG, mlen, ref, 256, SHA512));

    // Hash-algorithm binding: a SHA-512 signature must NOT verify as SHA-256.
    TEST_ASSERT_EQUAL_INT(-1, pc_rsa_verify(n, e, (const uint8_t *)RSA512_MSG, mlen, sig, 256, SHA256));

    // A different message (same length) must fail SHA-512 verification.
    TEST_ASSERT_EQUAL_INT(-1, pc_rsa_verify(n, e, (const uint8_t *)"hello rsa-sha2-256", mlen, sig, 256, SHA512));

    setup_test_rsa_key(); // restore the fixture for any later test
    pc_ssh_rsa_load_pubkey();
}

// ============================================================================
// Packet protocol tests
// ============================================================================

static uint8_t pkt_out_buf[512];
static size_t pkt_out_len;

static uint8_t last_msg_type;
static uint8_t last_payload[512];
static size_t last_payload_len;

static void pkt_handler(uint8_t slot, uint8_t msg_type, const uint8_t *payload, size_t len)
{
    (void)slot;
    last_msg_type = msg_type;
    last_payload_len = (len < sizeof(last_payload)) ? len : sizeof(last_payload);
    memcpy(last_payload, payload, last_payload_len);
}

static void test_pkt_send_recv_unencrypted(void)
{
    ssh_pkt_init(0);
    // Build a small SSH_MSG_IGNORE payload.
    uint8_t payload[] = {SSH_MSG_IGNORE, 'h', 'e', 'l', 'l', 'o'};
    int rc = ssh_pkt_send(0, payload, sizeof(payload), pkt_out_buf, &pkt_out_len, sizeof(pkt_out_buf));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_GREATER_THAN(4, (int)pkt_out_len);

    // Round-trip: feed the encoded bytes back through recv.
    ssh_pkt_init(0); // reset seq numbers
    last_msg_type = 0xFF;
    last_payload_len = 0;
    rc = ssh_pkt_recv(0, pkt_out_buf, pkt_out_len, pkt_handler);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(SSH_MSG_IGNORE, last_msg_type);
    TEST_ASSERT_EQUAL_INT(sizeof(payload), (int)last_payload_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, last_payload, sizeof(payload));
}

static void test_pkt_padding_alignment(void)
{
    // Packet length + padding must be multiple of 16.
    ssh_pkt_init(0);
    uint8_t payload[7] = {SSH_MSG_IGNORE, 1, 2, 3, 4, 5, 6};
    int rc = ssh_pkt_send(0, payload, sizeof(payload), pkt_out_buf, &pkt_out_len, sizeof(pkt_out_buf));
    TEST_ASSERT_EQUAL_INT(0, rc);
    // Wire bytes without MAC: 4 (length) + 1 (pad_len) + 7 (payload) + padding
    // Must be multiple of 16. Total without MAC = pkt_out_len.
    TEST_ASSERT_EQUAL_INT(0, (int)(pkt_out_len % 16));
}

static void test_pkt_seq_increments(void)
{
    ssh_pkt_init(0);
    uint8_t payload[] = {SSH_MSG_IGNORE};
    ssh_pkt_send(0, payload, 1, pkt_out_buf, &pkt_out_len, sizeof(pkt_out_buf));
    ssh_pkt_send(0, payload, 1, pkt_out_buf, &pkt_out_len, sizeof(pkt_out_buf));
    TEST_ASSERT_EQUAL_UINT32(2, ssh_pkt[0].seq_no_send);
}

static void test_pkt_disconnect_zeroes_state(void)
{
    ssh_pkt_init(0);
    // Set a non-zero seq to verify it gets cleared.
    ssh_pkt[0].seq_no_send = 42;
    int rc = ssh_pkt_disconnect(0, SSH_DISCONNECT_PROTOCOL_ERROR, pkt_out_buf, &pkt_out_len, sizeof(pkt_out_buf));
    // After disconnect, state should be reset (seq back to 0).
    TEST_ASSERT_EQUAL_UINT32(0, ssh_pkt[0].seq_no_send);
    TEST_ASSERT_EQUAL_INT(0, rc);
}

// ----------------------------------------------------------------------------
// Encrypted-path round trip (exercises AES-CTR length-peek snapshot/restore)
// ----------------------------------------------------------------------------

// Install deterministic session keys into ssh_keys[0] for the encrypted tests.
// Re-calling this resets both AES-CTR contexts to their initial IV/counter.
static void setup_encrypted_keys(void)
{
    uint8_t K_be[256], H[PC_SHA256_DIGEST_LEN];
    for (int i = 0; i < 256; i++)
    {
        K_be[i] = (uint8_t)(i + 1); // K_be[0]=1, MSB clear (valid mpint)
    }
    for (int i = 0; i < PC_SHA256_DIGEST_LEN; i++)
    {
        H[i] = (uint8_t)(0x40 + i);
    }
    ssh_dh_derive_keys(0, K_be, H);
}

// Build a client→server encrypted packet using ssh_keys[0].c2s_ctx and
// mac_key_c2s - i.e. what the server's recv path must accept.  Deterministic
// (zero) padding so the test is reproducible.  Advances c2s_ctx like a real
// streaming client, so multiple packets can be built back-to-back.
// Returns the total wire length (encrypted body + 32-byte MAC).
static size_t build_client_packet(const uint8_t *payload, size_t payload_len, uint32_t seq, uint8_t *out)
{
    SshKeyMat *km = &ssh_keys[0];

    size_t total = 5 + payload_len;
    size_t rem = total % 16;
    size_t pad = (rem == 0) ? 0 : (16 - rem);
    if (pad < 4)
    {
        pad += 16;
    }
    size_t pkt_len = 1 + payload_len + pad;
    size_t enc_len = 4 + pkt_len;

    out[0] = (uint8_t)(pkt_len >> 24);
    out[1] = (uint8_t)(pkt_len >> 16);
    out[2] = (uint8_t)(pkt_len >> 8);
    out[3] = (uint8_t)(pkt_len);
    out[4] = (uint8_t)pad;
    memcpy(out + 5, payload, payload_len);
    memset(out + 5 + payload_len, 0, pad);

    // MAC over seq_no || plaintext, then encrypt the plaintext body in place.
    uint8_t seq_be[4] = {(uint8_t)(seq >> 24), (uint8_t)(seq >> 16), (uint8_t)(seq >> 8), (uint8_t)seq};
    pc_hmac_sha256_ctx hctx;
    pc_hmac_sha256_init(&hctx, km->mac_key_c2s, 32);
    pc_hmac_sha256_update(&hctx, seq_be, 4);
    pc_hmac_sha256_update(&hctx, out, enc_len);
    pc_hmac_sha256_final(&hctx, out + enc_len);

    pc_aes256ctr_crypt(km->aes_key_c2s, km->aes_iv_c2s, out, out, enc_len);
    return enc_len + PC_HMAC_SHA256_LEN;
}

static void test_pkt_encrypted_roundtrip(void)
{
    setup_encrypted_keys();
    uint8_t payload[] = {SSH_MSG_IGNORE, 'w', 'o', 'r', 'l', 'd'};
    uint8_t wire[256];
    size_t wlen = build_client_packet(payload, sizeof(payload), 0, wire);

    setup_encrypted_keys(); // reset cipher state for the receiver
    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;
    last_msg_type = 0xFF;
    last_payload_len = 0;

    int rc = ssh_pkt_recv(0, wire, wlen, pkt_handler);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(SSH_MSG_IGNORE, last_msg_type);
    TEST_ASSERT_EQUAL_INT(sizeof(payload), (int)last_payload_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, last_payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT32(1, ssh_pkt[0].seq_no_recv);
}

static void test_pkt_chacha20poly1305_roundtrip(void)
{
    // Install a chacha20-poly1305 session with the same key both directions, so ssh_pkt_send()
    // (server encrypts with the s2c key) round-trips through ssh_pkt_recv() (decrypts with c2s).
    ssh_keymat_wipe(0);
    SshKeyMat *km = &ssh_keys[0];
    km->cipher_mode = SSH_CIPHER_CHACHA20POLY1305;
    for (int i = 0; i < PC_CHACHAPOLY_KEY_LEN; i++)
    {
        km->chacha_key_c2s[i] = (uint8_t)(i * 3 + 1);
        km->chacha_key_s2c[i] = (uint8_t)(i * 3 + 1);
    }
    km->active = PROTO_TRUE;

    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;

    uint8_t payload[] = {SSH_MSG_IGNORE, 'c', 'h', 'a', 'c', 'h', 'a', '2', '0'};
    uint8_t wire[256];
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, payload, sizeof(payload), wire, &wlen, sizeof(wire)));
    TEST_ASSERT_TRUE(wlen > 4 + PC_CHACHAPOLY_TAG_LEN); // length + payload + tag
    // The payload region is actually encrypted (not equal to plaintext).
    TEST_ASSERT_TRUE(memcmp(wire + 5, payload + 1, sizeof(payload) - 1) != 0);

    last_msg_type = 0xFF;
    last_payload_len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, wlen, pkt_handler));
    TEST_ASSERT_EQUAL_INT(SSH_MSG_IGNORE, last_msg_type);
    TEST_ASSERT_EQUAL_INT(sizeof(payload), (int)last_payload_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, last_payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT32(1, ssh_pkt[0].seq_no_recv);

    // Tamper with a ciphertext byte -> Poly1305 rejects -> recv returns -1.
    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;
    wire[6] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));

    ssh_keymat_wipe(0); // leave no chacha state for later (aes) tests
}

static void test_pkt_aes256gcm_roundtrip(void)
{
    // Install an aes256-gcm@openssh.com session with the same key/IV both directions, so ssh_pkt_send
    // (server seals with the s2c context) round-trips through ssh_pkt_recv (opens with c2s).
    ssh_keymat_wipe(0);
    SshKeyMat *km = &ssh_keys[0];
    km->cipher_mode = SSH_CIPHER_AES256GCM;
    uint8_t key[32], iv[12];
    for (int i = 0; i < 32; i++)
    {
        key[i] = (uint8_t)(i * 3 + 7);
    }
    for (int i = 0; i < 12; i++)
    {
        iv[i] = (uint8_t)(0x40 + i);
    }
    // Build the keyed contexts exactly as the KEX install path does - the keymat holds no raw GCM key.
    // Writing aes_key_* here instead would leave the contexts zeroed, and a zeroed GHASH table makes the
    // tag a constant: the round trip below would still pass while forged packets were accepted. That is
    // precisely what this test caught during the migration.
    pc_aesgcm_key_init(km->gcm_ctx_c2s, key);
    pc_aesgcm_key_init(km->gcm_ctx_s2c, key);
    memcpy(km->aes_iv_c2s, iv, PC_AESGCM_IV_LEN);
    memcpy(km->aes_iv_s2c, iv, PC_AESGCM_IV_LEN); // same key/IV both directions (GCM reuses aes_iv_*)
    km->active = PROTO_TRUE;

    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;

    uint8_t payload[] = {SSH_MSG_IGNORE, 'a', 'e', 's', 'g', 'c', 'm', '2', '5', '6'};
    uint8_t wire[256];
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, payload, sizeof(payload), wire, &wlen, sizeof(wire)));
    TEST_ASSERT_TRUE(wlen > 4 + PC_AESGCM_TAG_LEN); // length + ciphertext + tag
    // The 4-byte packet_length is in the clear (it is the AEAD's AAD).
    uint32_t pkt_len = (uint32_t)((wire[0] << 24) | (wire[1] << 16) | (wire[2] << 8) | wire[3]);
    TEST_ASSERT_EQUAL_UINT32(wlen - 4 - PC_AESGCM_TAG_LEN, pkt_len);
    TEST_ASSERT_EQUAL_UINT32(0, pkt_len % 16); // whole AES blocks
    // The packet body is actually encrypted (not equal to the plaintext payload).
    TEST_ASSERT_TRUE(memcmp(wire + 5, payload + 1, sizeof(payload) - 1) != 0);

    last_msg_type = 0xFF;
    last_payload_len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, wlen, pkt_handler));
    TEST_ASSERT_EQUAL_INT(SSH_MSG_IGNORE, last_msg_type);
    TEST_ASSERT_EQUAL_INT(sizeof(payload), (int)last_payload_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, last_payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT32(1, ssh_pkt[0].seq_no_recv);

    // Tamper a ciphertext byte -> GCM tag rejects -> recv returns -1, and no plaintext is delivered.
    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;
    memcpy(km->aes_iv_c2s, iv, PC_AESGCM_IV_LEN); // reset receiver nonce to the packet boundary
    wire[6] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));

    ssh_keymat_wipe(0); // leave no gcm state for later tests
}

// aes256-ctr + encrypt-then-MAC round-trip for a given MAC mode. c2s == s2c so ssh_pkt_send
// (encrypts with s2c) round-trips through ssh_pkt_recv (decrypts with c2s).
static void etm_roundtrip_helper(uint8_t mac_mode)
{
    ssh_keymat_wipe(0);
    SshKeyMat *km = &ssh_keys[0];
    km->cipher_mode = SSH_CIPHER_AES256CTR;
    km->mac_mode = mac_mode;
    uint8_t key[32], iv[16];
    for (int i = 0; i < 32; i++)
    {
        key[i] = (uint8_t)(i + 1);
    }
    for (int i = 0; i < 16; i++)
    {
        iv[i] = (uint8_t)(0x80 + i);
    }
    memcpy(km->aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(km->aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN);
    memcpy(km->aes_key_s2c, key, PC_AES256CTR_KEY_LEN);
    memcpy(km->aes_iv_s2c, iv, PC_AES256CTR_CTR_LEN); // same key/IV both directions
    for (int i = 0; i < 64; i++)
    {
        km->mac_key_c2s[i] = km->mac_key_s2c[i] = (uint8_t)(i * 5 + 3);
    }
    km->active = PROTO_TRUE;

    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;
    uint8_t payload[] = {SSH_MSG_IGNORE, 'e', 't', 'm', '!', '1', '2', '3'};
    uint8_t wire[256];
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, payload, sizeof(payload), wire, &wlen, sizeof(wire)));
    // The 4-byte packet_length is in the clear for ETM (not encrypted).
    TEST_ASSERT_EQUAL_UINT32(wlen - 4 - ssh_mac_len(mac_mode),
                             (uint32_t)((wire[0] << 24) | (wire[1] << 16) | (wire[2] << 8) | wire[3]));

    last_msg_type = 0xFF;
    last_payload_len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, wlen, pkt_handler));
    TEST_ASSERT_EQUAL_INT(SSH_MSG_IGNORE, last_msg_type);
    TEST_ASSERT_EQUAL_INT(sizeof(payload), (int)last_payload_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, last_payload, sizeof(payload));

    // Tamper a ciphertext byte -> MAC rejects before any decryption.
    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;
    memcpy(km->aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(km->aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN);
    wire[6] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));
    ssh_keymat_wipe(0);
}

static void test_pkt_aes_etm_sha256_roundtrip(void)
{
    etm_roundtrip_helper(SSH_MAC_HMAC_SHA256_ETM);
}
static void test_pkt_aes_etm_sha512_roundtrip(void)
{
    etm_roundtrip_helper(SSH_MAC_HMAC_SHA512_ETM);
}

static void test_pkt_encrypted_fragmented(void)
{
    setup_encrypted_keys();
    uint8_t payload[40];
    for (int i = 0; i < 40; i++)
    {
        payload[i] = (uint8_t)i;
    }
    payload[0] = SSH_MSG_IGNORE;
    uint8_t wire[256];
    size_t wlen = build_client_packet(payload, sizeof(payload), 0, wire);

    setup_encrypted_keys();
    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;
    last_msg_type = 0xFF;
    last_payload_len = 0;

    // Deliver in awkward fragments; the length-peek must snapshot/restore the
    // CTR state on every incomplete call without desyncing the cipher.
    size_t off = 0;
    size_t chunks[] = {1, 15, 8};
    for (size_t c = 0; c < 3; c++)
    {
        int rc = ssh_pkt_recv(0, wire + off, chunks[c], pkt_handler);
        TEST_ASSERT_EQUAL_INT(0, rc);
        TEST_ASSERT_EQUAL_INT(0xFF, last_msg_type); // nothing delivered yet
        off += chunks[c];
    }
    int rc = ssh_pkt_recv(0, wire + off, wlen - off, pkt_handler);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(SSH_MSG_IGNORE, last_msg_type);
    TEST_ASSERT_EQUAL_INT(sizeof(payload), (int)last_payload_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, last_payload, sizeof(payload));
}

static void test_pkt_encrypted_two_packets(void)
{
    setup_encrypted_keys();
    uint8_t p0[] = {SSH_MSG_IGNORE, 'a', 'b', 'c'};
    uint8_t p1[] = {SSH_MSG_IGNORE, 'x', 'y', 'z', '1', '2'};
    uint8_t wire[512];
    size_t off = 0;
    off += build_client_packet(p0, sizeof(p0), 0, wire + off);
    off += build_client_packet(p1, sizeof(p1), 1, wire + off);

    setup_encrypted_keys();
    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;
    last_msg_type = 0xFF;
    last_payload_len = 0;

    // Both packets in one buffer: the receiver must advance the cipher by
    // exactly one packet at a time and stay aligned for the second.
    int rc = ssh_pkt_recv(0, wire, off, pkt_handler);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(SSH_MSG_IGNORE, last_msg_type);
    TEST_ASSERT_EQUAL_INT(sizeof(p1), (int)last_payload_len);
    TEST_ASSERT_EQUAL_MEMORY(p1, last_payload, sizeof(p1));
    TEST_ASSERT_EQUAL_UINT32(2, ssh_pkt[0].seq_no_recv);
}

// ============================================================================
// setUp / tearDown
// ============================================================================

void setUp(void)
{
    memset(pkt_out_buf, 0, sizeof(pkt_out_buf));
    pkt_out_len = 0;
    last_msg_type = 0;
    memset(last_payload, 0, sizeof(last_payload));
    last_payload_len = 0;
}

void tearDown(void)
{
}

// Regression: the KDF must encode the shared secret K as a CANONICAL mpint
// (leading zero bytes stripped, RFC 4251), exactly like the exchange-hash encoder.
// With a K that has a high-order zero byte, an independent canonical computation
// must match the derived key; the old non-canonical encoder (hashing the full
// 256 bytes) would diverge here - and so would a spec-compliant peer.
static void test_ssh_kdf_canonical_mpint_k(void)
{
    uint8_t K_be[256], H[PC_SHA256_DIGEST_LEN];
    K_be[0] = 0x00; // leading zero -> MUST be stripped
    for (int i = 1; i < 256; i++)
    {
        K_be[i] = (uint8_t)(i * 7 + 1); // K_be[1] = 8 (MSB clear)
    }
    for (int i = 0; i < PC_SHA256_DIGEST_LEN; i++)
    {
        H[i] = (uint8_t)(0x40 + i);
    }

    ssh_dh_derive_keys(0, K_be, H); // session_id == H (first KEX)

    // Independently compute MAC key C->S (label 'E') with a canonical mpint(K):
    // SHA256( mpint(K) || H || 'E' || session_id ).
    size_t off = 0;
    while (off < 256 && K_be[off] == 0x00u)
    {
        off++;
    }
    proto_bool pad = (K_be[off] & 0x80u) != 0;
    uint32_t mlen = (uint32_t)(256 - off) + (pad ? 1u : 0u);
    uint8_t len_be[4] = {(uint8_t)(mlen >> 24), (uint8_t)(mlen >> 16), (uint8_t)(mlen >> 8), (uint8_t)mlen};
    pc_sha256_ctx c;
    pc_sha256_init(&c);
    pc_sha256_update(&c, len_be, 4);
    if (pad)
    {
        uint8_t z = 0x00u;
        pc_sha256_update(&c, &z, 1);
    }
    pc_sha256_update(&c, K_be + off, 256 - off);
    pc_sha256_update(&c, H, PC_SHA256_DIGEST_LEN);
    uint8_t lbl = 'E';
    pc_sha256_update(&c, &lbl, 1);
    pc_sha256_update(&c, H, PC_SHA256_DIGEST_LEN);
    uint8_t expected[PC_SHA256_DIGEST_LEN];
    pc_sha256_final(&c, expected);

    TEST_ASSERT_EQUAL_MEMORY(expected, ssh_keys[0].mac_key_c2s, PC_SHA256_DIGEST_LEN);
}

// Hash a canonical mpint(K) into @p c (mirrors the production encoder; tests may
// use any helpers - only src/ is constrained).
static void kdf_hash_mpint(pc_sha256_ctx *c, const uint8_t K_be[256])
{
    size_t off = 0;
    while (off < 256 && K_be[off] == 0x00u)
    {
        off++;
    }
    proto_bool pad = (K_be[off] & 0x80u) != 0;
    uint32_t mlen = (uint32_t)(256 - off) + (pad ? 1u : 0u);
    uint8_t len_be[4] = {(uint8_t)(mlen >> 24), (uint8_t)(mlen >> 16), (uint8_t)(mlen >> 8), (uint8_t)mlen};
    pc_sha256_update(c, len_be, 4);
    if (pad)
    {
        uint8_t z = 0x00u;
        pc_sha256_update(c, &z, 1);
    }
    pc_sha256_update(c, K_be + off, 256 - off);
}

// RFC 4253 §7.2 length extension: ssh_kdf_derive() past one block must chain
// K2 = HASH(mpint(K) || H || K1), and K1 must equal the single-block derivation.
static void test_ssh_kdf_extension_chain(void)
{
    uint8_t K_be[256], H[PC_SHA256_DIGEST_LEN], sid[PC_SHA256_DIGEST_LEN];
    K_be[0] = 0x00; // leading zero -> stripped by the canonical mpint encoding
    for (int i = 1; i < 256; i++)
    {
        K_be[i] = (uint8_t)(i * 7 + 1);
    }
    for (int i = 0; i < PC_SHA256_DIGEST_LEN; i++)
    {
        H[i] = (uint8_t)(0x40 + i);
        sid[i] = (uint8_t)(0x90 + i);
    }

    uint8_t out[2 * PC_SHA256_DIGEST_LEN];
    ssh_kdf_derive(K_be, H, sid, 'C', out, sizeof(out), PROTO_FALSE, PC_SHA256_DIGEST_LEN, PC_SHA256_DIGEST_LEN,
                   PROTO_FALSE);

    // K1 = HASH(mpint(K) || H || 'C' || sid) - same as a single-block derive.
    uint8_t k1[PC_SHA256_DIGEST_LEN];
    pc_sha256_ctx c;
    pc_sha256_init(&c);
    kdf_hash_mpint(&c, K_be);
    pc_sha256_update(&c, H, PC_SHA256_DIGEST_LEN);
    uint8_t lbl = 'C';
    pc_sha256_update(&c, &lbl, 1);
    pc_sha256_update(&c, sid, PC_SHA256_DIGEST_LEN);
    pc_sha256_final(&c, k1);
    TEST_ASSERT_EQUAL_MEMORY(k1, out, PC_SHA256_DIGEST_LEN);

    // K2 = HASH(mpint(K) || H || K1).
    uint8_t k2[PC_SHA256_DIGEST_LEN];
    pc_sha256_init(&c);
    kdf_hash_mpint(&c, K_be);
    pc_sha256_update(&c, H, PC_SHA256_DIGEST_LEN);
    pc_sha256_update(&c, k1, PC_SHA256_DIGEST_LEN);
    pc_sha256_final(&c, k2);
    TEST_ASSERT_EQUAL_MEMORY(k2, out + PC_SHA256_DIGEST_LEN, PC_SHA256_DIGEST_LEN);
}

// chacha20-poly1305: a payload whose (1+len) mod 8 lands in 5..7 forces the pad<4 -> pad+=8 branch;
// and a truncated packet is held (recv returns 0, consumes nothing).
static void test_pkt_chacha_padding_and_incomplete(void)
{
    ssh_keymat_wipe(0);
    SshKeyMat *km = &ssh_keys[0];
    km->cipher_mode = SSH_CIPHER_CHACHA20POLY1305;
    for (int i = 0; i < PC_CHACHAPOLY_KEY_LEN; i++)
    {
        km->chacha_key_c2s[i] = km->chacha_key_s2c[i] = (uint8_t)(i * 3 + 1);
    }
    km->active = PROTO_TRUE;
    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;

    uint8_t p4[4] = {SSH_MSG_IGNORE, 1, 2, 3}; // 1+4=5 mod 8 -> pad 3 -> +8
    uint8_t wire[256];
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, p4, sizeof(p4), wire, &wlen, sizeof(wire)));
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, wlen, pkt_handler)); // valid round-trip

    // Truncated packet (length still decodes, but the body is incomplete) -> held, returns 0.
    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, p4, sizeof(p4), wire, &wlen, sizeof(wire)));
    ssh_pkt_init(0);
    ssh_pkt[0].enc_in = PROTO_TRUE;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, wlen - 8, pkt_handler));
    ssh_keymat_wipe(0);
}

// aes256-ctr ETM: (1+len) mod 16 in 13..15 forces the pad<4 -> pad+=16 branch; and a truncated
// packet is held.
static void test_pkt_etm_padding_and_incomplete(void)
{
    ssh_keymat_wipe(0);
    SshKeyMat *km = &ssh_keys[0];
    km->cipher_mode = SSH_CIPHER_AES256CTR;
    km->mac_mode = SSH_MAC_HMAC_SHA256_ETM;
    uint8_t key[32], iv[16];
    for (int i = 0; i < 32; i++)
    {
        key[i] = (uint8_t)(i + 1);
    }
    for (int i = 0; i < 16; i++)
    {
        iv[i] = (uint8_t)(0x80 + i);
    }
    memcpy(km->aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(km->aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN);
    memcpy(km->aes_key_s2c, key, PC_AES256CTR_KEY_LEN);
    memcpy(km->aes_iv_s2c, iv, PC_AES256CTR_CTR_LEN);
    for (int i = 0; i < 64; i++)
    {
        km->mac_key_c2s[i] = km->mac_key_s2c[i] = (uint8_t)(i * 5 + 3);
    }
    km->active = PROTO_TRUE;
    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;

    uint8_t p12[12]; // 1+12=13 mod 16 -> pad 3 -> +16
    p12[0] = SSH_MSG_IGNORE;
    for (int i = 1; i < 12; i++)
    {
        p12[i] = (uint8_t)i;
    }
    uint8_t wire[256];
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, p12, sizeof(p12), wire, &wlen, sizeof(wire)));
    memcpy(km->aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(km->aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN);                   // reset the receive cipher
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, wlen, pkt_handler)); // valid round-trip

    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;
    memcpy(km->aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(km->aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN);
    memcpy(km->aes_key_s2c, key, PC_AES256CTR_KEY_LEN);
    memcpy(km->aes_iv_s2c, iv, PC_AES256CTR_CTR_LEN);
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, p12, sizeof(p12), wire, &wlen, sizeof(wire)));
    ssh_pkt_init(0);
    ssh_pkt[0].enc_in = PROTO_TRUE;
    memcpy(km->aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(km->aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN);
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, wlen - 8, pkt_handler)); // truncated -> held
    ssh_keymat_wipe(0);
}

// Forge a chacha20-poly1305 wire packet (valid Poly1305 tag) with a chosen packet_length + padding
// byte at a given sequence number - drives the decrypted-field guards the loopback cannot reach.
static size_t forge_chacha(SshKeyMat *km, uint32_t seq, uint32_t pkt_len, uint8_t pad_byte, uint8_t *out)
{
    memset(out, 0, 4 + pkt_len + PC_CHACHAPOLY_TAG_LEN);
    out[0] = (uint8_t)(pkt_len >> 24);
    out[1] = (uint8_t)(pkt_len >> 16);
    out[2] = (uint8_t)(pkt_len >> 8);
    out[3] = (uint8_t)pkt_len;
    if (pkt_len >= 1)
    {
        out[4] = pad_byte;
    }
    if (pkt_len >= 2)
    {
        out[5] = SSH_MSG_IGNORE;
    }
    pc_chachapoly_encrypt(km->chacha_key_c2s, seq, out, out, pkt_len);
    return 4 + pkt_len + PC_CHACHAPOLY_TAG_LEN;
}

static void chacha_recv_setup(SshKeyMat *km)
{
    ssh_keymat_wipe(0);
    km->cipher_mode = SSH_CIPHER_CHACHA20POLY1305;
    for (int i = 0; i < PC_CHACHAPOLY_KEY_LEN; i++)
    {
        km->chacha_key_c2s[i] = km->chacha_key_s2c[i] = (uint8_t)(i * 3 + 1);
    }
    km->active = PROTO_TRUE;
    ssh_pkt_init(0);
    ssh_pkt[0].enc_in = PROTO_TRUE;
}

// A chacha packet that decrypts cleanly but carries a bad packet_length / padding_length, or arrives
// at the sequence close threshold, is rejected.
static void test_pkt_chacha_forged_rejects(void)
{
    SshKeyMat *km = &ssh_keys[0];
    uint8_t wire[128];

    chacha_recv_setup(km); // packet_length 0 -> below the minimum
    size_t wlen = forge_chacha(km, 0, 0, 0, wire);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));

    chacha_recv_setup(km); // padding_length 2 -> below the RFC 4253 minimum of 4
    wlen = forge_chacha(km, 0, 8, 2, wire);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));

    chacha_recv_setup(km); // decrypts, then the sequence-overflow guard closes the connection
    ssh_pkt[0].seq_no_recv = SSH_SEQ_CLOSE_THRESHOLD;
    wlen = forge_chacha(km, SSH_SEQ_CLOSE_THRESHOLD, 8, 4, wire);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));

    ssh_keymat_wipe(0);
}

// aes256-ctr ETM: the 4-byte packet_length is in the clear, so a length that is not a whole number of
// AES blocks is rejected before any decryption.
static void test_pkt_etm_bad_length(void)
{
    ssh_keymat_wipe(0);
    SshKeyMat *km = &ssh_keys[0];
    km->cipher_mode = SSH_CIPHER_AES256CTR;
    km->mac_mode = SSH_MAC_HMAC_SHA256_ETM;
    uint8_t key[32] = {0}, iv[16] = {0};
    memcpy(km->aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(km->aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN);
    km->active = PROTO_TRUE;
    ssh_pkt_init(0);
    ssh_pkt[0].enc_in = PROTO_TRUE;
    uint8_t wire[8] = {0, 0, 0, 17, 1, 2, 3, 4}; // pkt_len 17 is not a multiple of 16
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, sizeof(wire), pkt_handler));
    ssh_keymat_wipe(0);
}

static void etm_recv_setup(SshKeyMat *km, uint8_t *key, uint8_t *iv)
{
    ssh_keymat_wipe(0);
    km->cipher_mode = SSH_CIPHER_AES256CTR;
    km->mac_mode = SSH_MAC_HMAC_SHA256_ETM;
    for (int i = 0; i < 32; i++)
    {
        key[i] = (uint8_t)(i + 1);
    }
    for (int i = 0; i < 16; i++)
    {
        iv[i] = (uint8_t)(0x80 + i);
    }
    for (int i = 0; i < 64; i++)
    {
        km->mac_key_c2s[i] = (uint8_t)(i * 5 + 3);
    }
    km->active = PROTO_TRUE;
    ssh_pkt_init(0);
    ssh_pkt[0].enc_in = PROTO_TRUE;
}

// Forge an aes256-ctr encrypt-then-MAC wire packet at a given seq with a chosen padding byte, with a
// valid HMAC over (seq || length || ciphertext). Leaves c2s_ctx reset so recv decrypts from the start.
static size_t forge_etm(SshKeyMat *km, const uint8_t *key, const uint8_t *iv, uint32_t seq, uint32_t pkt_len,
                        uint8_t pad_byte, uint8_t *out)
{
    out[0] = (uint8_t)(pkt_len >> 24);
    out[1] = (uint8_t)(pkt_len >> 16);
    out[2] = (uint8_t)(pkt_len >> 8);
    out[3] = (uint8_t)pkt_len;
    uint8_t body[256];
    memset(body, 0, pkt_len);
    body[0] = pad_byte;
    if (pkt_len >= 2)
    {
        body[1] = SSH_MSG_IGNORE;
    }
    memcpy(km->aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(km->aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN);
    pc_aes256ctr_crypt(km->aes_key_c2s, km->aes_iv_c2s, body, body, pkt_len);
    memcpy(out + 4, body, pkt_len);
    uint8_t seq_be[4] = {(uint8_t)(seq >> 24), (uint8_t)(seq >> 16), (uint8_t)(seq >> 8), (uint8_t)seq};
    pc_hmac_sha256_ctx hctx;
    pc_hmac_sha256_init(&hctx, km->mac_key_c2s, 32);
    pc_hmac_sha256_update(&hctx, seq_be, 4);
    pc_hmac_sha256_update(&hctx, out, 4 + pkt_len);
    pc_hmac_sha256_final(&hctx, out + 4 + pkt_len);
    memcpy(km->aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(km->aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN); // reset the receive cipher to the packet boundary
    return 4 + pkt_len + PC_HMAC_SHA256_LEN;
}

// aes256-ctr ETM: a MAC-valid packet whose decrypted padding_length is too small, or that arrives at
// the sequence close threshold, is rejected.
static void test_pkt_etm_forged_rejects(void)
{
    SshKeyMat *km = &ssh_keys[0];
    uint8_t key[32], iv[16], wire[256];

    etm_recv_setup(km, key, iv); // padding_length 2 -> below the minimum of 4
    size_t wlen = forge_etm(km, key, iv, 0, 16, 2, wire);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));

    etm_recv_setup(km, key, iv); // MAC verifies, then the sequence-overflow guard closes the connection
    ssh_pkt[0].seq_no_recv = SSH_SEQ_CLOSE_THRESHOLD;
    wlen = forge_etm(km, key, iv, SSH_SEQ_CLOSE_THRESHOLD, 16, 4, wire);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));

    ssh_keymat_wipe(0);
}

// A valid encrypted packet is discarded (connection closed) when the scratch arena has no room for
// the decrypt buffer - both cipher paths fail closed.
static void test_pkt_scratch_exhausted(void)
{
    SshKeyMat *km = &ssh_keys[0];
    uint8_t wire[256];

    chacha_recv_setup(km);
    size_t wlen = forge_chacha(km, 0, 8, 4, wire); // a valid packet (pad 4)
    pc_plaintext_reset();
    while (pc_plaintext_alloc(8, 1))
        ; // drain the arena so the recv decrypt-buffer alloc fails
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));
    pc_plaintext_reset();

    uint8_t key[32], iv[16];
    etm_recv_setup(km, key, iv);
    wlen = forge_etm(km, key, iv, 0, 16, 4, wire); // valid MAC + padding
    pc_plaintext_reset();
    while (pc_plaintext_alloc(8, 1))
        ;
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));
    pc_plaintext_reset();
    ssh_keymat_wipe(0);
}

// Forge an aes256-ctr encrypt-AND-MAC packet (the length is encrypted too) at a given seq with a
// chosen packet_length + padding byte; MAC over the plaintext. Uses km->c2s_ctx (reset by the caller).
static size_t forge_eam(SshKeyMat *km, uint32_t seq, uint32_t pkt_len, uint8_t pad_byte, uint8_t *out)
{
    size_t enc_len = 4 + pkt_len;
    out[0] = (uint8_t)(pkt_len >> 24);
    out[1] = (uint8_t)(pkt_len >> 16);
    out[2] = (uint8_t)(pkt_len >> 8);
    out[3] = (uint8_t)pkt_len;
    memset(out + 4, 0, pkt_len);
    if (pkt_len >= 1)
    {
        out[4] = pad_byte;
    }
    if (pkt_len >= 2)
    {
        out[5] = SSH_MSG_IGNORE;
    }
    uint8_t seq_be[4] = {(uint8_t)(seq >> 24), (uint8_t)(seq >> 16), (uint8_t)(seq >> 8), (uint8_t)seq};
    pc_hmac_sha256_ctx hctx;
    pc_hmac_sha256_init(&hctx, km->mac_key_c2s, 32);
    pc_hmac_sha256_update(&hctx, seq_be, 4);
    pc_hmac_sha256_update(&hctx, out, enc_len);
    pc_hmac_sha256_final(&hctx, out + enc_len);
    pc_aes256ctr_crypt(km->aes_key_c2s, km->aes_iv_c2s, out, out, enc_len);
    return enc_len + PC_HMAC_SHA256_LEN;
}

// aes256-ctr encrypt-and-MAC error guards: a bad (non-block) length, a bad padding length, arrival at
// the sequence threshold, and an exhausted scratch arena are each rejected.
static void test_pkt_eam_forged_rejects(void)
{
    SshKeyMat *km = &ssh_keys[0];
    uint8_t wire[256];

    setup_encrypted_keys(); // non-block length (4 + 17 = 21) - caught at the length peek
    ssh_pkt_init(0);
    ssh_pkt[0].enc_in = PROTO_TRUE;
    size_t wlen = forge_eam(km, 0, 17, 4, wire);
    setup_encrypted_keys();
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));

    setup_encrypted_keys(); // bad padding length (< 4); 4 + 12 = 16 is a valid block length
    ssh_pkt_init(0);
    ssh_pkt[0].enc_in = PROTO_TRUE;
    wlen = forge_eam(km, 0, 12, 2, wire);
    setup_encrypted_keys();
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));

    setup_encrypted_keys(); // sequence at the close threshold
    ssh_pkt_init(0);
    ssh_pkt[0].enc_in = PROTO_TRUE;
    ssh_pkt[0].seq_no_recv = SSH_SEQ_CLOSE_THRESHOLD;
    wlen = forge_eam(km, SSH_SEQ_CLOSE_THRESHOLD, 12, 4, wire);
    setup_encrypted_keys();
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));

    setup_encrypted_keys(); // exhausted scratch arena on an otherwise-valid packet
    ssh_pkt_init(0);
    ssh_pkt[0].enc_in = PROTO_TRUE;
    wlen = forge_eam(km, 0, 12, 4, wire);
    setup_encrypted_keys();
    pc_plaintext_reset();
    while (pc_plaintext_alloc(8, 1))
        ;
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_handler));
    pc_plaintext_reset();
    ssh_keymat_wipe(0);
}

// ============================================================================
// GHASH 4-bit table vs bitwise reference (proves the optimization is byte-exact)
// ============================================================================

// The pre-optimization reference: textbook 128-iteration bitwise GF(2^128) multiply (NIST SP 800-38D
// sec 6.3), acc *= y. The table method in crypto/mac/ghash.h must match this for all inputs.
static void ghash_gf_mul_bitwise(uint8_t x[16], const uint8_t y[16])
{
    uint8_t z[16] = {0}, v[16];
    memcpy(v, y, 16);
    for (int i = 0; i < 128; i++)
    {
        if ((x[i >> 3] >> (7 - (i & 7))) & 1)
        {
            for (int k = 0; k < 16; k++)
            {
                z[k] ^= v[k];
            }
        }
        uint8_t lsb = v[15] & 1;
        for (int j = 15; j > 0; j--)
        {
            v[j] = (uint8_t)((v[j] >> 1) | (v[j - 1] << 7));
        }
        v[0] >>= 1;
        if (lsb)
        {
            v[0] ^= 0xe1;
        }
    }
    memcpy(x, z, 16);
}

void test_ghash_table_matches_bitwise()
{
    uint32_t s = 0xC0FFEE11u;
    for (int t = 0; t < 4000; t++)
    {
        uint8_t h[16], acc[16], ref[16];
        for (int i = 0; i < 16; i++)
        {
            s = s * 1664525u + 1013904223u;
            h[i] = (uint8_t)(s >> 17);
            s = s * 1664525u + 1013904223u;
            acc[i] = ref[i] = (uint8_t)(s >> 19);
        }
        GhashKey ghk;
        ghash_key_init(&ghk, h);
        ghash_mul(&ghk, acc);         // table method
        ghash_gf_mul_bitwise(ref, h); // reference
        TEST_ASSERT_EQUAL_UINT8_ARRAY(ref, acc, 16);
    }
    // Boundary operands: H = 0, H = 1<<127 (0x80 in byte 0), acc = all-ones.
    uint8_t hz[16] = {0}, one[16] = {0}, allf[16];
    one[0] = 0x80;
    memset(allf, 0xFF, 16);
    for (int c = 0; c < 2; c++)
    {
        const uint8_t *H = c ? one : hz;
        uint8_t a[16], r[16];
        memcpy(a, allf, 16);
        memcpy(r, allf, 16);
        GhashKey g;
        ghash_key_init(&g, H);
        ghash_mul(&g, a);
        ghash_gf_mul_bitwise(r, H);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(r, a, 16);
    }
}

// ssh_keymat_wipe / ssh_dh_wipe guard the slot index against the pool bounds (i < MAX_SSH_CONNS)
// before touching ssh_keys[i] / ssh_dh[i]. Production callers always pass a valid slot, so drive the
// out-of-range arm directly: an index >= MAX_SSH_CONNS (and the UINT8_MAX extreme) must be a safe no-op
// that leaves neighbouring slot 0 untouched.
void test_keymat_wipe_out_of_range_is_noop(void)
{
    memset(&ssh_keys[0], 0xA5, sizeof(SshKeyMat));
    memset(&ssh_dh[0], 0x5A, sizeof(SshDhState));

    ssh_keymat_wipe(MAX_SSH_CONNS); // out of range -> guard rejects, no write
    ssh_keymat_wipe(0xFF);
    ssh_dh_wipe(MAX_SSH_CONNS);
    ssh_dh_wipe(0xFF);

    uint8_t km_ref[sizeof(SshKeyMat)];
    uint8_t dh_ref[sizeof(SshDhState)];
    memset(km_ref, 0xA5, sizeof(km_ref));
    memset(dh_ref, 0x5A, sizeof(dh_ref));
    TEST_ASSERT_EQUAL_MEMORY(km_ref, &ssh_keys[0], sizeof(km_ref));
    TEST_ASSERT_EQUAL_MEMORY(dh_ref, &ssh_dh[0], sizeof(dh_ref));

    // In-range wipe still zeroes the slot (the guard's true arm), leaving no state for later tests.
    ssh_keymat_wipe(0);
    ssh_dh_wipe(0);
}

// ============================================================================
// main
// ============================================================================

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ghash_table_matches_bitwise);

    // SHA-256
    RUN_TEST(test_sha256_empty);
    RUN_TEST(test_sha256_abc);
    RUN_TEST(test_sha256_448bit);
    RUN_TEST(test_sha256_streaming);

    // HMAC-SHA2-256
    RUN_TEST(test_hmac_sha256_tc1);
    RUN_TEST(test_hmac_sha256_tc2);
    RUN_TEST(test_hmac_sha256_tc3);
    RUN_TEST(test_hmac_sha256_streaming);
    RUN_TEST(test_hmac_sha256_tc6_large_key);
    RUN_TEST(test_hmac_sha512_tc1);
    RUN_TEST(test_hmac_sha512_tc2);
    RUN_TEST(test_hmac_sha512_streaming);
    RUN_TEST(test_hmac_sha512_tc6_large_key);

    // AES-256-CTR
    RUN_TEST(test_aes256ctr_encrypt);
    RUN_TEST(test_aes256ctr_decrypt);
    RUN_TEST(test_aes256ctr_multi_block);
    RUN_TEST(test_aes256ctr_scratch_wiped);

    // BigNum
    RUN_TEST(test_bn_roundtrip);
    RUN_TEST(test_bn_cmp_equal);
    RUN_TEST(test_bn_cmp_less);
    RUN_TEST(test_bn_cmp_greater);
    RUN_TEST(test_bn_is_zero);
    RUN_TEST(test_bn_dh_validate_rejects_zero);
    RUN_TEST(test_bn_dh_validate_rejects_one);
    RUN_TEST(test_bn_dh_validate_accepts_two);

    // DH-group14 expmod
    RUN_TEST(test_expmod_exp1);
    RUN_TEST(test_expmod_exp2);
    RUN_TEST(test_expmod_exp3);
    RUN_TEST(test_expmod_commutative);

    // RSA PKCS#1
    RUN_TEST(test_rsa_pkcs1_pad_structure);
    RUN_TEST(test_rsa_sign_verify_roundtrip);
    RUN_TEST(test_rsa_encode_pubkey);
    RUN_TEST(test_rsa_verify_and_encode_guards);
    RUN_TEST(test_rsa_verify_valid_signature);
    RUN_TEST(test_rsa_verify_rejects_tampered_signature);
    RUN_TEST(test_rsa_verify_rejects_wrong_message);
    RUN_TEST(test_rsa_sha512_kat_sign_verify);

    // Packet protocol
    RUN_TEST(test_pkt_send_recv_unencrypted);
    RUN_TEST(test_pkt_padding_alignment);
    RUN_TEST(test_pkt_seq_increments);
    RUN_TEST(test_pkt_disconnect_zeroes_state);
    RUN_TEST(test_pkt_encrypted_roundtrip);
    RUN_TEST(test_pkt_chacha20poly1305_roundtrip);
    RUN_TEST(test_pkt_aes256gcm_roundtrip);
    RUN_TEST(test_pkt_aes_etm_sha256_roundtrip);
    RUN_TEST(test_pkt_aes_etm_sha512_roundtrip);
    RUN_TEST(test_pkt_encrypted_fragmented);
    RUN_TEST(test_pkt_encrypted_two_packets);
    RUN_TEST(test_pkt_chacha_padding_and_incomplete);
    RUN_TEST(test_pkt_etm_padding_and_incomplete);
    RUN_TEST(test_pkt_chacha_forged_rejects);
    RUN_TEST(test_pkt_etm_bad_length);
    RUN_TEST(test_pkt_etm_forged_rejects);
    RUN_TEST(test_pkt_scratch_exhausted);
    RUN_TEST(test_pkt_eam_forged_rejects);
    RUN_TEST(test_ssh_kdf_canonical_mpint_k);
    RUN_TEST(test_ssh_kdf_extension_chain);
    RUN_TEST(test_keymat_wipe_out_of_range_is_noop);

    return UNITY_END();
}
