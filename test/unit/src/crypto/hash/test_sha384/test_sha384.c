// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for SHA-384 (crypto/hash/sha384.h), the digest the TLS 1.3 SHA-384 cipher suites hash
// their transcript and run their key schedule on.
//
// The load-bearing case is test_rfc6234_published_vectors. RFC 6234 sec 8.5 tabulates SHA-384
// digests for messages chosen to sit on the padding boundaries: "abc" (one block with room), the
// 112-octet message whose padding overflows into a second block, one million 'a' (a length field
// past 2^23 bits), and a 640-octet message that is an exact multiple of the 1024-bit block so the
// padding forms a whole extra block on its own. The 1-octet and 195-octet rows come from the same
// table. Reproducing them pins the compression function, the 128-bit big-endian length field, and
// the padding rule at once.
//
// SHA-384 shares every round and constant with SHA-512 and differs only in the seed and in cutting
// the digest to six state words (RFC 6234 sec 6.3 and 6.4), so test_not_a_truncated_sha512 is the
// case that catches a wrong seed: a digest that is the first 48 octets of SHA-512 would pass every
// self-consistency check here and fail on the wire.

#include "crypto/hash/sha384.h"
#include "crypto/hash/sha512.h"
#include <string.h>

#include <unity.h>

static uint8_t g_work[PROTOCORE_SHA384_BORROW] __attribute__((aligned(8)));
static uint8_t g_ctx_work[PROTOCORE_SHA384_BORROW] __attribute__((aligned(8)));
static uint8_t g_512_work[PROTOCORE_SHA512_BORROW] __attribute__((aligned(8)));

void setUp(void)
{
}
void tearDown(void)
{
}

// Render a digest as lowercase hex so a mismatch prints the whole value.
static void tohex(const uint8_t *d, size_t n, char *out)
{
    static const char *const H = "0123456789abcdef";
    for (size_t i = 0; i < n; i++)
    {
        out[2 * i] = H[d[i] >> 4];
        out[2 * i + 1] = H[d[i] & 0x0F];
    }
    out[2 * n] = '\0';
}

// The namespace, called the way the vectors below read: operands in, one call, digest out. A running
// digest is its borrow, so a streaming case feeds g_ctx_work and never carries a handle of its own.
static void sha_update(const void *data, size_t len)
{
    Sha384.update_args.data = (const uint8_t *)data;
    Sha384.update_args.len = len;
    Sha384.update(g_ctx_work);
}

static void sha_final(uint8_t *out)
{
    Sha384.final_args.out = out;
    Sha384.final(g_ctx_work);
}

static void one_shot_hex(const void *msg, size_t len, char out[97])
{
    uint8_t d[PROTOCORE_SHA384_DIGEST_LEN];
    Sha384.hash_args.data = (const uint8_t *)msg;
    Sha384.hash_args.len = len;
    Sha384.hash_args.out = d;
    Sha384.hash(g_work);
    tohex(d, sizeof(d), out);
}

// RFC 6234 sec 8.5, the SHA384 row and the message constants above it. Only the whole-octet rows are
// here: rows 5, 7 and 9 end on a partial octet, which the byte-oriented entries cannot express.
#define TEST1 "abc"
#define TEST2_2a "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
#define TEST2_2b "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"
#define TEST2_2 TEST2_2a TEST2_2b
#define TEST4a "01234567012345670123456701234567"
#define TEST4 TEST4a TEST4a
#define TEST6 "\xb9"
#define TEST8_384 "\xa4\x1c\x49\x77\x79\xc0\x37\x5f\xf1\x0a\x7f\x4e\x08\x59\x17\x39"
#define TEST10_384                                                                                                     \
    "\x39\x96\x69\xe2\x8f\x6b\x9c\x6d\xbc\xbb\x69\x12\xec\x10\xff\xcf"                                                 \
    "\x74\x79\x03\x49\xb7\xdc\x8f\xbe\x4a\x8e\x7b\x3b\x56\x21\xdb\x0f"                                                 \
    "\x3e\x7d\xc8\x7f\x82\x32\x64\xbb\xe4\x0d\x18\x11\xc9\xea\x20\x61"                                                 \
    "\xe1\xc8\x4a\xd1\x0a\x23\xfa\xc1\x72\x7e\x72\x02\xfc\x3f\x50\x42"                                                 \
    "\xe6\xbf\x58\xcb\xa8\xa2\x74\x6e\x1f\x64\xf9\xb9\xea\x35\x2c\x71"                                                 \
    "\x15\x07\x05\x3c\xf4\xe5\x33\x9d\x52\x86\x5f\x25\xcc\x22\xb5\xe8"                                                 \
    "\x77\x84\xa1\x2f\xc9\x61\xd6\x6c\xb6\xe8\x95\x73\x19\x9a\x2c\xe6"                                                 \
    "\x56\x5c\xbd\xf1\x3d\xca\x40\x38\x32\xcf\xcb\x0e\x8b\x72\x11\xe8"                                                 \
    "\x3a\xf3\x2a\x11\xac\x17\x92\x9f\xf1\xc0\x73\xa5\x1c\xc0\x27\xaa"                                                 \
    "\xed\xef\xf8\x5a\xad\x7c\x2b\x7c\x5a\x80\x3e\x24\x04\xd9\x6d\x2a"                                                 \
    "\x77\x35\x7b\xda\x1a\x6d\xae\xed\x17\x15\x1c\xb9\xbc\x51\x25\xa4"                                                 \
    "\x22\xe9\x41\xde\x0c\xa0\xfc\x50\x11\xc2\x3e\xcf\xfe\xfd\xd0\x96"                                                 \
    "\x76\x71\x1c\xf3\xdb\x0a\x34\x40\x72\x0e\x16\x15\xc1\xf2\x2f\xbc"                                                 \
    "\x3c\x72\x1d\xe5\x21\xe1\xb9\x9b\xa1\xbd\x55\x77\x40\x86\x42\x14"                                                 \
    "\x7e\xd0\x96"

void test_rfc6234_published_vectors(void)
{
    char got[97];

    // Row 1: one block with room for the mark and the length.
    one_shot_hex(TEST1, sizeof(TEST1) - 1, got);
    TEST_ASSERT_EQUAL_STRING("cb00753f45a35e8bb5a03d699ac65007272c32ab0eded163"
                             "1a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7",
                             got);

    // Row 2: 112 octets, so the mark sits inside the 16-octet length field and padding takes a
    // second block.
    one_shot_hex(TEST2_2, sizeof(TEST2_2) - 1, got);
    TEST_ASSERT_EQUAL_STRING("09330c33f71147e83d192fc782cd1b4753111b173b3b05d2"
                             "2fa08086e3b0f712fcc7c71a557e2db966c3e9fa91746039",
                             got);

    // Row 6: a single octet.
    one_shot_hex(TEST6, 1u, got);
    TEST_ASSERT_EQUAL_STRING("bc8089a19007c0b14195f4ecc74094fec64f01f90929282c"
                             "2fb392881578208ad466828b1c6c283d2722cf0ad1ab6938",
                             got);

    // Row 8: 16 octets that are not text.
    one_shot_hex(TEST8_384, sizeof(TEST8_384) - 1, got);
    TEST_ASSERT_EQUAL_STRING("c9a68443a005812256b8ec76b00516f0dbb74fab26d66591"
                             "3f194b6ffb0e91ea9967566b58109cbc675cc208e4c823f7",
                             got);

    // Row 10: 195 octets, two blocks and a partial third.
    one_shot_hex(TEST10_384, sizeof(TEST10_384) - 1, got);
    TEST_ASSERT_EQUAL_STRING("4f440db1e6edd2899fa335f09515aa025ee177a79f4b4aaf"
                             "38e42b5c4de660f5de8fb2a5b2fbd2a3cbffd20cff1288c0",
                             got);
}

// Row 4: TEST4 ten times is 640 octets, an exact multiple of the 128-octet block, so the padding
// forms a whole extra block on its own. Fed as ten updates, which also exercises the chunking.
void test_rfc6234_exact_block_multiple(void)
{
    char got[97];
    uint8_t d[PROTOCORE_SHA384_DIGEST_LEN];

    Sha384.init(g_ctx_work);
    for (int i = 0; i < 10; i++)
    {
        sha_update(TEST4, sizeof(TEST4) - 1);
    }
    sha_final(d);
    tohex(d, sizeof(d), got);
    TEST_ASSERT_EQUAL_STRING("2fc64a4f500ddb6828f6a3430b8dd72a368eb7f3a8322a70"
                             "bc84275b9c0b3ab00d27a5cc3c2d224aa6b61a0d79fb4596",
                             got);
}

// Row 3: one million 'a'. 7,813 compressions and a length field past 2^23 bits.
void test_rfc6234_one_million_a(void)
{
    char got[97];
    uint8_t d[PROTOCORE_SHA384_DIGEST_LEN];

    Sha384.init(g_ctx_work);
    for (int i = 0; i < 1000000; i++)
    {
        sha_update("a", 1u);
    }
    sha_final(d);
    tohex(d, sizeof(d), got);
    TEST_ASSERT_EQUAL_STRING("9d0e1809716474cb086e834e310a4a1ced149e9c00f24852"
                             "7972cec5704c2a5b07b8b3dc38ecc4ebae97ddd87f3d8985",
                             got);
}

// The seed is the whole difference from SHA-512. A digest cut from SHA-512 would satisfy every other
// case in this suite, so the published value is checked against the other digest directly.
void test_not_a_truncated_sha512(void)
{
    uint8_t d384[PROTOCORE_SHA384_DIGEST_LEN];
    uint8_t d512[PROTOCORE_SHA512_DIGEST_LEN];

    Sha384.hash_args.data = (const uint8_t *)TEST1;
    Sha384.hash_args.len = sizeof(TEST1) - 1;
    Sha384.hash_args.out = d384;
    Sha384.hash(g_work);

    Sha512.hash_args.data = (const uint8_t *)TEST1;
    Sha512.hash_args.len = sizeof(TEST1) - 1;
    Sha512.hash_args.out = d512;
    Sha512.hash(g_512_work);

    TEST_ASSERT_TRUE(memcmp(d384, d512, PROTOCORE_SHA384_DIGEST_LEN) != 0);
}

// Where a message is split across updates cannot change the digest.
void test_chunk_split_invariance(void)
{
    uint8_t whole[PROTOCORE_SHA384_DIGEST_LEN], split[PROTOCORE_SHA384_DIGEST_LEN];
    const char *msg = TEST10_384;
    const size_t len = sizeof(TEST10_384) - 1;

    Sha384.hash_args.data = (const uint8_t *)msg;
    Sha384.hash_args.len = len;
    Sha384.hash_args.out = whole;
    Sha384.hash(g_work);

    for (size_t cut = 1; cut < len; cut += 17)
    {
        Sha384.init(g_ctx_work);
        sha_update(msg, cut);
        sha_update(msg + cut, len - cut);
        sha_final(split);
        TEST_ASSERT_EQUAL_MEMORY(whole, split, sizeof(whole));
    }
}

// The header's contract: final() compresses the padded blocks into a copy, so the running digest is
// untouched and keeps taking data.
void test_final_leaves_the_context_running(void)
{
    uint8_t first[PROTOCORE_SHA384_DIGEST_LEN], second[PROTOCORE_SHA384_DIGEST_LEN];
    uint8_t direct[PROTOCORE_SHA384_DIGEST_LEN];

    Sha384.init(g_ctx_work);
    sha_update("abc", 3u);
    sha_final(first);
    sha_update("def", 3u);
    sha_final(second);

    Sha384.hash_args.data = (const uint8_t *)"abcdef";
    Sha384.hash_args.len = 6u;
    Sha384.hash_args.out = direct;
    Sha384.hash(g_work);

    TEST_ASSERT_EQUAL_MEMORY(direct, second, sizeof(direct));
    TEST_ASSERT_TRUE(memcmp(first, second, sizeof(first)) != 0);
}

// The one-shot and the streaming entries are the same digest.
void test_one_shot_matches_streaming(void)
{
    uint8_t a[PROTOCORE_SHA384_DIGEST_LEN], b[PROTOCORE_SHA384_DIGEST_LEN];

    Sha384.hash_args.data = (const uint8_t *)TEST2_2;
    Sha384.hash_args.len = sizeof(TEST2_2) - 1;
    Sha384.hash_args.out = a;
    Sha384.hash(g_work);

    Sha384.init(g_ctx_work);
    sha_update(TEST2_2, sizeof(TEST2_2) - 1);
    sha_final(b);

    TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(a));
}

// The empty message, and two messages a byte apart, all land on distinct digests.
void test_distinct_messages_distinct_digests(void)
{
    uint8_t e[PROTOCORE_SHA384_DIGEST_LEN], a[PROTOCORE_SHA384_DIGEST_LEN], b[PROTOCORE_SHA384_DIGEST_LEN];

    Sha384.hash_args.data = NULL;
    Sha384.hash_args.len = 0;
    Sha384.hash_args.out = e;
    Sha384.hash(g_work);

    Sha384.hash_args.data = (const uint8_t *)"abc";
    Sha384.hash_args.len = 3u;
    Sha384.hash_args.out = a;
    Sha384.hash(g_work);

    Sha384.hash_args.data = (const uint8_t *)"abd";
    Sha384.hash_args.len = 3u;
    Sha384.hash_args.out = b;
    Sha384.hash(g_work);

    TEST_ASSERT_TRUE(memcmp(a, b, sizeof(a)) != 0);
    TEST_ASSERT_TRUE(memcmp(e, a, sizeof(a)) != 0);
}

// The widths the header states, and the padding rule they build: a message of exactly one block and
// one of a block less an octet hash to different digests.
void test_block_length_constants(void)
{
    TEST_ASSERT_EQUAL_UINT(128u, (unsigned)PROTOCORE_SHA384_BLOCK_LEN);
    TEST_ASSERT_EQUAL_UINT(48u, (unsigned)PROTOCORE_SHA384_DIGEST_LEN);

    uint8_t full[PROTOCORE_SHA384_DIGEST_LEN], short_[PROTOCORE_SHA384_DIGEST_LEN];
    Sha384.hash_args.data = (const uint8_t *)TEST10_384;
    Sha384.hash_args.len = 128u;
    Sha384.hash_args.out = full;
    Sha384.hash(g_work);
    Sha384.hash_args.data = (const uint8_t *)TEST10_384;
    Sha384.hash_args.len = 127u;
    Sha384.hash_args.out = short_;
    Sha384.hash(g_work);
    TEST_ASSERT_TRUE(memcmp(full, short_, sizeof(full)) != 0);
}

// A null borrow leaves ok false and writes nothing.
void test_a_null_borrow_is_refused(void)
{
    uint8_t d[PROTOCORE_SHA384_DIGEST_LEN];
    memset(d, 0xA5, sizeof(d));

    Sha384.hash_args.data = (const uint8_t *)"abc";
    Sha384.hash_args.len = 3u;
    Sha384.hash_args.out = d;
    Sha384.hash(NULL);
    TEST_ASSERT_FALSE(Sha384.ok);
    for (size_t i = 0; i < sizeof(d); i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0xA5, d[i]);
    }

    Sha384.init(NULL);
    TEST_ASSERT_FALSE(Sha384.ok);
}
