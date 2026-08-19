// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the software AES block (crypto/cipher/aes_block.h).
//
// This is the primitive every software AES mode in the library sits on - CTR, GCM's GCTR, CCM's
// CBC-MAC, CMAC's subkeys - so it is pinned to the published numbers rather than to a round trip:
// FIPS 197 Appendix C gives one plaintext block and its ciphertext at each key length, which fixes
// the key schedule and all ten/twelve/fourteen rounds at once.
//
// The key schedule is checked separately at its own published values (FIPS 197 Appendix A), because
// a schedule that is wrong only in a late round key still produces a correct-looking first round.

#include "crypto/cipher/aes_block/aes_block.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The entries read nothing out of the borrow - the schedule is the caller's - but they take one.
static uint8_t g_ws[64] __attribute__((aligned(8)));

// FIPS 197 Appendix C: the one plaintext block every vector uses.
static const uint8_t PT[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                               0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};

static void expand(const uint8_t *key, int nk, uint32_t *rk)
{
    AesBlock.key_expand_args.key = key;
    AesBlock.key_expand_args.nk = nk;
    AesBlock.key_expand_args.rk = rk;
    AesBlock.key_expand(g_ws);
}

static void encrypt(const uint32_t *rk, int nr, const uint8_t *in, uint8_t *out)
{
    AesBlock.encrypt_block_args.rk = rk;
    AesBlock.encrypt_block_args.nr = nr;
    AesBlock.encrypt_block_args.in = in;
    AesBlock.encrypt_block_args.out = out;
    AesBlock.encrypt_block(g_ws);
}

void test_fips197_c1_aes128(void)
{
    static const uint8_t KEY[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t WANT[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                     0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    uint32_t rk[44];
    uint8_t got[16];
    expand(KEY, 4, rk);
    encrypt(rk, 10, PT, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, got, 16);
}

void test_fips197_c3_aes256(void)
{
    static const uint8_t KEY[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    static const uint8_t WANT[16] = {0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
                                     0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89};
    uint32_t rk[60];
    uint8_t got[16];
    expand(KEY, 8, rk);
    encrypt(rk, 14, PT, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, got, 16);
}

// FIPS 197 Appendix A.1: the AES-128 schedule's first and last round keys, which pins both ends of
// the expansion - the straight copy at the start and the last RCON application.
void test_fips197_a1_key_schedule(void)
{
    static const uint8_t KEY[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                                    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
    uint32_t rk[44];
    expand(KEY, 4, rk);
    // w0..w3 are the key itself, big-endian per word.
    TEST_ASSERT_EQUAL_HEX32(0x2b7e1516u, rk[0]);
    TEST_ASSERT_EQUAL_HEX32(0x28aed2a6u, rk[1]);
    TEST_ASSERT_EQUAL_HEX32(0xabf71588u, rk[2]);
    TEST_ASSERT_EQUAL_HEX32(0x09cf4f3cu, rk[3]);
    // w40..w43, the final round key.
    TEST_ASSERT_EQUAL_HEX32(0xd014f9a8u, rk[40]);
    TEST_ASSERT_EQUAL_HEX32(0xc9ee2589u, rk[41]);
    TEST_ASSERT_EQUAL_HEX32(0xe13f0cc8u, rk[42]);
    TEST_ASSERT_EQUAL_HEX32(0xb6630ca6u, rk[43]);
}

// in and out may alias, which is what the CTR and CBC-MAC loops above this rely on.
void test_in_and_out_may_alias(void)
{
    static const uint8_t KEY[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t WANT[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                     0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    uint32_t rk[44];
    uint8_t buf[16];
    expand(KEY, 4, rk);
    memcpy(buf, PT, 16);
    encrypt(rk, 10, buf, buf);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 16);
}

// The schedule is a pure function of the key, so expanding twice gives the same words, and a
// different key gives different ones.
void test_schedule_is_a_function_of_the_key(void)
{
    static const uint8_t K1[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                   0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t K2[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                   0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x10};
    uint32_t a[44], b[44], c[44];
    expand(K1, 4, a);
    expand(K1, 4, b);
    expand(K2, 4, c);
    TEST_ASSERT_EQUAL_HEX32_ARRAY(a, b, 44);
    TEST_ASSERT_TRUE(memcmp(a, c, sizeof(a)) != 0);
}

// The inlines the AES modes call directly and the namespace entries must be the same functions.
void test_inline_and_namespace_agree(void)
{
    static const uint8_t KEY[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    uint32_t rk_ns[44], rk_inline[44];
    uint8_t via_ns[16], via_inline[16];

    expand(KEY, 4, rk_ns);
    protocore_aes_key_expand(KEY, 4, rk_inline);
    TEST_ASSERT_EQUAL_HEX32_ARRAY(rk_inline, rk_ns, 44);

    encrypt(rk_ns, 10, PT, via_ns);
    protocore_aes_encrypt_block(rk_inline, 10, PT, via_inline);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(via_inline, via_ns, 16);
}

// A null operand is refused rather than dereferenced.
void test_null_operands_are_refused(void)
{
    uint32_t rk[44];
    uint8_t out[16];
    memset(out, 0x5A, sizeof(out));

    AesBlock.key_expand_args.key = NULL;
    AesBlock.key_expand_args.nk = 4;
    AesBlock.key_expand_args.rk = rk;
    AesBlock.key_expand(g_ws);
    TEST_ASSERT_FALSE(AesBlock.ok);

    AesBlock.encrypt_block_args.rk = NULL;
    AesBlock.encrypt_block_args.nr = 10;
    AesBlock.encrypt_block_args.in = PT;
    AesBlock.encrypt_block_args.out = out;
    AesBlock.encrypt_block(g_ws);
    TEST_ASSERT_FALSE(AesBlock.ok);
    for (unsigned i = 0; i < 16u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x5A, out[i]);
    }
}
