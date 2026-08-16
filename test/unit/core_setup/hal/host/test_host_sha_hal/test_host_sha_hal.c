// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SHA accelerator's host arm (core_setup/hal/host/host_sha_hal.h).
//
// This arm stands in for a peripheral, so the only thing worth asserting is that it answers with the
// bytes the peripheral would: the FIPS 180-4 one-block digests. Padding and block buffering belong to
// the hash modules and are not exercised here - each case pads its own single block, which is what
// isolates the compression itself.
//
// The `first` flag is the peripheral's IV supply. A case that passes first=true and never seeds the
// state is asserting exactly that, so a stand-in that ignored the flag would fail here rather than
// silently produce a wrong digest inside a module.

#include "core_setup/hal/host/host_sha_hal.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}

void tearDown(void)
{
}

// One 64-byte block holding "abc" padded per FIPS 180-4 sec 5.1.1: the message, 0x80, zeros, and the
// 64-bit big-endian bit length in the last eight octets.
static void block_abc_512(uint8_t blk[64])
{
    memset(blk, 0, 64);
    blk[0] = 'a';
    blk[1] = 'b';
    blk[2] = 'c';
    blk[3] = 0x80;
    blk[63] = 24; // 3 octets = 24 bits
}

// The same for the 1024-bit block SHA-512 takes, with a 128-bit length field.
static void block_abc_1024(uint8_t blk[128])
{
    memset(blk, 0, 128);
    blk[0] = 'a';
    blk[1] = 'b';
    blk[2] = 'c';
    blk[3] = 0x80;
    blk[127] = 24;
}

// The state comes back as words; the digest is those words big-endian.
static void words_to_be(const uint32_t *h, unsigned n, uint8_t *out)
{
    for (unsigned i = 0; i < n; i++)
    {
        out[4 * i + 0] = (uint8_t)(h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8);
        out[4 * i + 3] = (uint8_t)(h[i]);
    }
}

// A 64-bit family carries its state as lanes, which is the storage sha512.c hands in as words. The
// digest is those lanes big-endian.
static void lanes_to_be(const uint64_t *h, unsigned n, uint8_t *out)
{
    for (unsigned i = 0; i < n; i++)
    {
        for (unsigned b = 0; b < 8u; b++)
        {
            out[8 * i + b] = (uint8_t)(h[i] >> (56u - 8u * b));
        }
    }
}

void test_sha1_abc_one_block(void)
{
    static const uint8_t WANT[20] = {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                                     0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
    uint8_t blk[64];
    uint32_t h[5] = {0};
    uint8_t got[20];
    block_abc_512(blk);
    protocore_sha_hw_acquire();
    protocore_sha_hw_block(PROTOCORE_SHA_MODE_1, h, 5u, (const uint32_t *)(const void *)blk, 16u, PROTO_TRUE);
    protocore_sha_hw_release();
    words_to_be(h, 5u, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, got, 20);
}

void test_sha256_abc_one_block(void)
{
    static const uint8_t WANT[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                     0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                     0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    uint8_t blk[64];
    uint32_t h[8] = {0};
    uint8_t got[32];
    block_abc_512(blk);
    protocore_sha_hw_acquire();
    protocore_sha_hw_block(PROTOCORE_SHA_MODE_256, h, 8u, (const uint32_t *)(const void *)blk, 16u, PROTO_TRUE);
    protocore_sha_hw_release();
    words_to_be(h, 8u, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, got, 32);
}

void test_sha224_abc_one_block(void)
{
    // SHA-224 is SHA-256 under a different IV, truncated to 28 octets.
    static const uint8_t WANT[28] = {0x23, 0x09, 0x7d, 0x22, 0x34, 0x05, 0xd8, 0x22, 0x86, 0x42,
                                     0xa4, 0x77, 0xbd, 0xa2, 0x55, 0xb3, 0x2a, 0xad, 0xbc, 0xe4,
                                     0xbd, 0xa0, 0xb3, 0xf7, 0xe3, 0x6c, 0x9d, 0xa7};
    uint8_t blk[64];
    uint32_t h[8] = {0};
    uint8_t got[32];
    block_abc_512(blk);
    protocore_sha_hw_acquire();
    protocore_sha_hw_block(PROTOCORE_SHA_MODE_224, h, 8u, (const uint32_t *)(const void *)blk, 16u, PROTO_TRUE);
    protocore_sha_hw_release();
    words_to_be(h, 8u, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, got, 28);
}

void test_sha512_abc_one_block(void)
{
    static const uint8_t WANT[64] = {0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba, 0xcc, 0x41, 0x73, 0x49, 0xae,
                                     0x20, 0x41, 0x31, 0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2, 0x0a, 0x9e,
                                     0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a, 0x21, 0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1,
                                     0xa8, 0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd, 0x45, 0x4d, 0x44, 0x23,
                                     0x64, 0x3c, 0xe8, 0x0e, 0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f};
    uint8_t blk[128];
    uint64_t h[8] = {0};
    uint8_t got[64];
    block_abc_1024(blk);
    protocore_sha_hw_acquire();
    protocore_sha_hw_block(PROTOCORE_SHA_MODE_512, (uint32_t *)(void *)h, 16u, (const uint32_t *)(const void *)blk, 32u,
                           PROTO_TRUE);
    protocore_sha_hw_release();
    lanes_to_be(h, 8u, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, got, 64);
}

void test_sha384_abc_one_block(void)
{
    // SHA-384 is SHA-512 under a different IV, truncated to 48 octets.
    static const uint8_t WANT[48] = {0xcb, 0x00, 0x75, 0x3f, 0x45, 0xa3, 0x5e, 0x8b, 0xb5, 0xa0, 0x3d, 0x69,
                                     0x9a, 0xc6, 0x50, 0x07, 0x27, 0x2c, 0x32, 0xab, 0x0e, 0xde, 0xd1, 0x63,
                                     0x1a, 0x8b, 0x60, 0x5a, 0x43, 0xff, 0x5b, 0xed, 0x80, 0x86, 0x07, 0x2b,
                                     0xa1, 0xe7, 0xcc, 0x23, 0x58, 0xba, 0xec, 0xa1, 0x34, 0xc8, 0x25, 0xa7};
    uint8_t blk[128];
    uint64_t h[8] = {0};
    uint8_t got[64];
    block_abc_1024(blk);
    protocore_sha_hw_acquire();
    protocore_sha_hw_block(PROTOCORE_SHA_MODE_384, (uint32_t *)(void *)h, 16u, (const uint32_t *)(const void *)blk, 32u,
                           PROTO_TRUE);
    protocore_sha_hw_release();
    lanes_to_be(h, 8u, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, got, 48);
}

// first=false compresses onto the state the caller carries, which is what lets a module keep a
// running digest across blocks. Seeding by hand with the standard IV and asking for a continue must
// reach the same answer as asking the arm to supply that IV itself.
void test_continue_matches_supplied_iv(void)
{
    static const uint32_t IV256[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint8_t blk[64];
    uint32_t a[8] = {0};
    uint32_t b[8];
    block_abc_512(blk);
    protocore_sha_hw_acquire();
    protocore_sha_hw_block(PROTOCORE_SHA_MODE_256, a, 8u, (const uint32_t *)(const void *)blk, 16u, PROTO_TRUE);
    memcpy(b, IV256, sizeof(IV256));
    protocore_sha_hw_block(PROTOCORE_SHA_MODE_256, b, 8u, (const uint32_t *)(const void *)blk, 16u, PROTO_FALSE);
    protocore_sha_hw_release();
    TEST_ASSERT_EQUAL_HEX32_ARRAY(a, b, 8);
}

// A null state or block must leave the caller's buffer alone rather than write through the pointer.
void test_null_operands_are_refused(void)
{
    uint8_t blk[64];
    uint32_t h[8];
    block_abc_512(blk);
    memset(h, 0x5A, sizeof(h));
    protocore_sha_hw_block(PROTOCORE_SHA_MODE_256, NULL, 8u, (const uint32_t *)(const void *)blk, 16u, PROTO_TRUE);
    protocore_sha_hw_block(PROTOCORE_SHA_MODE_256, h, 8u, NULL, 16u, PROTO_TRUE);
    for (unsigned i = 0; i < 8u; i++)
    {
        TEST_ASSERT_EQUAL_HEX32(0x5A5A5A5Au, h[i]);
    }
}
