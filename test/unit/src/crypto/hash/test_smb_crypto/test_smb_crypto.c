// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the MD-family digests NTLM needs (crypto/hash/md.h): MD4, MD5 and HMAC-MD5.
//
// The load-bearing cases are the three published test suites, reproduced whole rather than sampled:
// RFC 1321 Appendix A.5 for MD5, RFC 1320 Appendix A.5 for MD4, and RFC 2202 section 2 for
// HMAC-MD5. Each RFC suite runs from the empty message up past the 64-octet block, so the padding
// and the length field are covered at both sides of the boundary, and RFC 2202's cases 6 and 7 are
// the ones with an 80-octet key, where RFC 2104 requires the key be hashed down before it is
// padded - the branch a short key never reaches.

#include "crypto/hash/md.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static void tohex(const uint8_t d[16], char out[33])
{
    static const char *const H = "0123456789abcdef";
    for (int i = 0; i < 16; i++)
    {
        out[2 * i] = H[d[i] >> 4];
        out[2 * i + 1] = H[d[i] & 0x0F];
    }
    out[32] = '\0';
}

// The seven messages RFC 1321 A.5 and RFC 1320 A.5 both use, in the order they are printed.
static const char *const SUITE[7] = {"",
                                     "a",
                                     "abc",
                                     "message digest",
                                     "abcdefghijklmnopqrstuvwxyz",
                                     "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
                                     "123456789012345678901234567890123456789012345678901234567890"
                                     "12345678901234567890"};

// RFC 1321 Appendix A.5, "MD5 test suite".
void test_rfc1321_md5_suite(void)
{
    static const char *const WANT[7] = {
        "d41d8cd98f00b204e9800998ecf8427e", "0cc175b9c0f1b6a831c399e269772661", "900150983cd24fb0d6963f7d28e17f72",
        "f96b697d7cb7938d525a2f31aaf161d0", "c3fcd3d76192e4007dfb496cca67e13b", "d174ab98d277d9f5a5611c2c9f419d9f",
        "57edf4a22be3c955ac49da2e2107b67a"};
    for (int i = 0; i < 7; i++)
    {
        uint8_t d[16];
        char got[33];
        protocore_md5((const uint8_t *)SUITE[i], strlen(SUITE[i]), d);
        tohex(d, got);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(WANT[i], got, SUITE[i]);
    }
}

// RFC 1320 Appendix A.5, "MD4 test suite". MD4 is the NT-hash primitive (MS-NLMP NTOWFv1).
void test_rfc1320_md4_suite(void)
{
    static const char *const WANT[7] = {
        "31d6cfe0d16ae931b73c59d7e0c089c0", "bde52cb31de33e46245e05fbdbd6fb24", "a448017aaf21d8525fc10ae87aa6729d",
        "d9130a8164549fe818874806e1c7014b", "d79e1c308aa5bbcdeea8ed63df412da9", "043f8582f241db351ce627e153e7f0e4",
        "e33b4ddc9c38f2199c3e7b164fcc0536"};
    for (int i = 0; i < 7; i++)
    {
        uint8_t d[16];
        char got[33];
        protocore_md4((const uint8_t *)SUITE[i], strlen(SUITE[i]), d);
        tohex(d, got);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(WANT[i], got, SUITE[i]);
    }
}

// RFC 2202 section 2, all seven HMAC-MD5 cases. Cases 6 and 7 carry an 80-octet key, longer than
// MD5's 64-octet block, so RFC 2104 requires the key be replaced by its own digest first.
void test_rfc2202_hmac_md5_cases(void)
{
    uint8_t key[80], data[80], d[16];
    char got[33];

    // case 1: key 0x0b x16, data "Hi There"
    memset(key, 0x0b, 16);
    protocore_hmac_md5(key, 16, (const uint8_t *)"Hi There", 8, d);
    tohex(d, got);
    TEST_ASSERT_EQUAL_STRING("9294727a3638bb1c13f48ef8158bfc9d", got);

    // case 2: key "Jefe"
    protocore_hmac_md5((const uint8_t *)"Jefe", 4, (const uint8_t *)"what do ya want for nothing?", 28, d);
    tohex(d, got);
    TEST_ASSERT_EQUAL_STRING("750c783e6ab0b503eaa86e310a5db738", got);

    // case 3: key 0xaa x16, data 0xdd x50
    memset(key, 0xaa, 16);
    memset(data, 0xdd, 50);
    protocore_hmac_md5(key, 16, data, 50, d);
    tohex(d, got);
    TEST_ASSERT_EQUAL_STRING("56be34521d144c88dbb8c733f0e8b3f6", got);

    // case 4: key 0x01..0x19 (25 octets), data 0xcd x50
    for (int i = 0; i < 25; i++)
    {
        key[i] = (uint8_t)(i + 1);
    }
    memset(data, 0xcd, 50);
    protocore_hmac_md5(key, 25, data, 50, d);
    tohex(d, got);
    TEST_ASSERT_EQUAL_STRING("697eaf0aca3a3aea3a75164746ffaa79", got);

    // case 5: key 0x0c x16, data "Test With Truncation"
    memset(key, 0x0c, 16);
    protocore_hmac_md5(key, 16, (const uint8_t *)"Test With Truncation", 20, d);
    tohex(d, got);
    TEST_ASSERT_EQUAL_STRING("56461ef2342edc00f9bab995690efd4c", got);

    // case 6: key 0xaa x80 (longer than the block)
    memset(key, 0xaa, 80);
    protocore_hmac_md5(key, 80, (const uint8_t *)"Test Using Larger Than Block-Size Key - Hash Key First", 54, d);
    tohex(d, got);
    TEST_ASSERT_EQUAL_STRING("6b1ab7fe4bd7bf8f0b62e6ce61b9d0cd", got);

    // case 7: the same 80-octet key with a message longer than one block
    static const char MSG7[] = "Test Using Larger Than Block-Size Key and Larger Than One Block-Size Data";
    TEST_ASSERT_EQUAL_UINT(73u, (unsigned)(sizeof(MSG7) - 1));
    protocore_hmac_md5(key, 80, (const uint8_t *)MSG7, sizeof(MSG7) - 1, d);
    tohex(d, got);
    TEST_ASSERT_EQUAL_STRING("6f630fad67cda0ee1fb1f562db3aa53e", got);
}

// RFC 2104: a key longer than the block is hashed to its digest and used as that. Handing the
// digest of the long key in directly therefore has to give the same MAC as the long key itself.
void test_long_key_is_its_own_digest(void)
{
    uint8_t key[80], hashed[16], a[16], b[16];
    memset(key, 0xaa, sizeof(key));
    protocore_md5(key, sizeof(key), hashed);

    static const char MSG[] = "Test Using Larger Than Block-Size Key - Hash Key First";
    protocore_hmac_md5(key, sizeof(key), (const uint8_t *)MSG, sizeof(MSG) - 1, a);
    protocore_hmac_md5(hashed, sizeof(hashed), (const uint8_t *)MSG, sizeof(MSG) - 1, b);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, b, 16);
}

// The streaming contexts must agree with the one-shots at every split of a message that spans the
// 64-octet block, so a caller that feeds a socket a chunk at a time gets the published digest.
void test_streaming_matches_one_shot(void)
{
    static const char MSG[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
                              "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    const size_t n = sizeof(MSG) - 1;
    uint8_t want5[16], want4[16], got[16];
    protocore_md5((const uint8_t *)MSG, n, want5);
    protocore_md4((const uint8_t *)MSG, n, want4);

    struct MdCtx *c = protocore_md_wants();
    TEST_ASSERT_NOT_NULL(c);
    for (size_t cut = 0; cut <= n; cut += 7)
    {
        protocore_md5_init(c);
        protocore_md5_update(c, (const uint8_t *)MSG, cut);
        protocore_md5_update(c, (const uint8_t *)MSG + cut, n - cut);
        protocore_md5_final(c, got);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want5, got, 16);

        protocore_md4_init(c);
        protocore_md4_update(c, (const uint8_t *)MSG, cut);
        protocore_md4_update(c, (const uint8_t *)MSG + cut, n - cut);
        protocore_md4_final(c, got);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want4, got, 16);
    }
}

// MD4 and MD5 are different functions with the same output width: no message may digest alike under
// both, and re-initializing a context must not leave the previous algorithm's state behind.
void test_md4_and_md5_are_distinct(void)
{
    for (int i = 0; i < 7; i++)
    {
        uint8_t a[16], b[16];
        protocore_md5((const uint8_t *)SUITE[i], strlen(SUITE[i]), a);
        protocore_md4((const uint8_t *)SUITE[i], strlen(SUITE[i]), b);
        TEST_ASSERT_TRUE_MESSAGE(memcmp(a, b, 16) != 0, SUITE[i]);
    }

    struct MdCtx *c = protocore_md_wants();
    TEST_ASSERT_NOT_NULL(c);
    uint8_t got[16];
    protocore_md4_init(c);
    protocore_md4_update(c, (const uint8_t *)"abc", 3);
    protocore_md4_final(c, got);
    protocore_md5_init(c); // the same storage, now an MD5
    protocore_md5_update(c, (const uint8_t *)"abc", 3);
    protocore_md5_final(c, got);
    char hex[33];
    tohex(got, hex);
    TEST_ASSERT_EQUAL_STRING("900150983cd24fb0d6963f7d28e17f72", hex);
}

// The 56-octet and 64-octet lengths are where the length field either fits after the 0x80 pad or
// forces a second block. RFC 1321 A.5's 80-octet vector already crosses one block; these two
// lengths sit exactly on the boundary, and each must differ from its neighbour.
void test_block_boundary_lengths(void)
{
    uint8_t msg[65], d55[16], d56[16], d63[16], d64[16];
    for (int i = 0; i < 65; i++)
    {
        msg[i] = (uint8_t)('a' + (i % 26));
    }
    protocore_md5(msg, 55, d55);
    protocore_md5(msg, 56, d56);
    protocore_md5(msg, 63, d63);
    protocore_md5(msg, 64, d64);
    TEST_ASSERT_TRUE(memcmp(d55, d56, 16) != 0);
    TEST_ASSERT_TRUE(memcmp(d63, d64, 16) != 0);
    TEST_ASSERT_TRUE(memcmp(d56, d64, 16) != 0);
}
