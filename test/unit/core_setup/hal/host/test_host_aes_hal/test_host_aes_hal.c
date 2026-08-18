// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the AES accelerator's host arm (test/core_setup/hal/host/host_aes_hal.h).
//
// This arm stands in for a peripheral, so what is asserted is that it answers with the bytes the
// peripheral would: the FIPS 197 appendix C ECB vectors, at all three key lengths. Every mode built
// on the block - CTR, GCM's GCTR, CCM's CBC-MAC, CMAC's subkeys - is software above this and is
// tested in its own suite.
//
// The key bank is state the peripheral holds across blocks, so the ordering cases matter as much as
// the vectors: a block encrypt before any key load must not answer with something a caller could
// mistake for ciphertext, and a second setkey must replace the first rather than accumulate.

#include "test/core_setup/hal/host/host_aes_hal.h"
#include <string.h>

#include <unity.h>

// FIPS 197 appendix C: the one plaintext block every vector uses.
static const uint8_t PT[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                               0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};

void setUp(void)
{
}

void tearDown(void)
{
}

void test_aes128_fips197_c1(void)
{
    static const uint8_t KEY[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t WANT[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                     0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    uint8_t got[16];
    protocore_aes_hw_acquire();
    protocore_aes_hw_setkey(KEY, 16u);
    protocore_aes_hw_block(PT, got);
    protocore_aes_hw_release();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, got, 16);
}

void test_aes192_fips197_c2(void)
{
    static const uint8_t KEY[24] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                                    0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    static const uint8_t WANT[16] = {0xdd, 0xa9, 0x7c, 0xa4, 0x86, 0x4c, 0xdf, 0xe0,
                                     0x6e, 0xaf, 0x70, 0xa0, 0xec, 0x0d, 0x71, 0x91};
    uint8_t got[16];
    protocore_aes_hw_acquire();
    protocore_aes_hw_setkey(KEY, 24u);
    protocore_aes_hw_block(PT, got);
    protocore_aes_hw_release();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, got, 16);
}

void test_aes256_fips197_c3(void)
{
    static const uint8_t KEY[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    static const uint8_t WANT[16] = {0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
                                     0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89};
    uint8_t got[16];
    protocore_aes_hw_acquire();
    protocore_aes_hw_setkey(KEY, 32u);
    protocore_aes_hw_block(PT, got);
    protocore_aes_hw_release();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, got, 16);
}

// The key bank persists across blocks, so a second block under one setkey must repeat the first.
void test_key_persists_across_blocks(void)
{
    static const uint8_t KEY[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    uint8_t a[16], b[16];
    protocore_aes_hw_acquire();
    protocore_aes_hw_setkey(KEY, 16u);
    protocore_aes_hw_block(PT, a);
    protocore_aes_hw_block(PT, b);
    protocore_aes_hw_release();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, b, 16);
}

// A second setkey replaces the bank. Re-loading the 128-bit key after a 256-bit one must answer with
// the 128-bit vector, which a stand-in that kept the wider round count would not.
void test_setkey_replaces_the_bank(void)
{
    static const uint8_t K128[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                     0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t K256[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                     0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                     0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    static const uint8_t WANT128[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                        0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    uint8_t got[16];
    protocore_aes_hw_acquire();
    protocore_aes_hw_setkey(K256, 32u);
    protocore_aes_hw_block(PT, got);
    protocore_aes_hw_setkey(K128, 16u);
    protocore_aes_hw_block(PT, got);
    protocore_aes_hw_release();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT128, got, 16);
}

// in and out may alias, which is what the GCTR and CBC-MAC loops above this rely on.
void test_in_and_out_may_alias(void)
{
    static const uint8_t KEY[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t WANT[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                     0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    uint8_t buf[16];
    memcpy(buf, PT, 16);
    protocore_aes_hw_acquire();
    protocore_aes_hw_setkey(KEY, 16u);
    protocore_aes_hw_block(buf, buf);
    protocore_aes_hw_release();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 16);
}

// A key length the cipher does not have must leave the bank unkeyed, and a block under an unkeyed
// bank must answer zero rather than something a caller could mistake for ciphertext.
void test_bad_key_length_leaves_the_bank_unkeyed(void)
{
    static const uint8_t KEY[20] = {0};
    uint8_t got[16];
    memset(got, 0xA5, sizeof(got));
    protocore_aes_hw_acquire();
    protocore_aes_hw_setkey(KEY, 20u);
    protocore_aes_hw_block(PT, got);
    protocore_aes_hw_release();
    for (unsigned i = 0; i < 16u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, got[i]);
    }
}
