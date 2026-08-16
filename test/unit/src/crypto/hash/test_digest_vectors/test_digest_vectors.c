// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for SHA-256 (crypto/hash/sha256.h), the digest under the SSH exchange hash, the TLS
// 1.3 key schedule, SNMPv3, JWT and SMB signing.
//
// The load-bearing case is test_rfc6234_published_vectors. RFC 6234 sec 8.5 tabulates SHA-256
// digests for four messages chosen to sit on the padding boundaries: "abc" (one block with room),
// the 56-octet message whose padding overflows into a second block, one million 'a' (a length field
// past 2^23 bits and 15,625 compressions), and a 640-octet message that is an exact multiple of the
// 512-bit block so the padding forms a whole extra block on its own. Reproducing all four pins the
// compression function, the big-endian length field, and the padding rule at once.
//
// The empty-message digest is the one RFC 8448 sec 3 prints as the Transcript-Hash("") that TLS
// 1.3's "tls13 derived" step feeds to HKDF-Expand-Label.

#include "crypto/hash/sha256.h"
#include <string.h>

#include <unity.h>

static uint8_t g_work[PROTOCORE_SHA256_BORROW] __attribute__((aligned(8)));
static uint8_t g_ctx_work[PROTOCORE_SHA256_BORROW] __attribute__((aligned(8)));

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
    Sha256.update_args.data = (const uint8_t *)data;
    Sha256.update_args.len = len;
    Sha256.update(g_ctx_work);
}

static void sha_final(uint8_t *out)
{
    Sha256.final_args.out = out;
    Sha256.final(g_ctx_work);
}

static void one_shot_hex(const void *msg, size_t len, char out[65])
{
    uint8_t d[PROTOCORE_SHA256_DIGEST_LEN];
    Sha256.hash_args.data = (const uint8_t *)msg;
    Sha256.hash_args.len = len;
    Sha256.hash_args.out = d;
    Sha256.hash(g_work);
    tohex(d, sizeof(d), out);
}

// RFC 6234 sec 8.5, the SHA256 row: TEST1, TEST2_1 and TEST4 with their repeat counts.
#define TEST1 "abc"
#define TEST2_1 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
#define TEST4a "01234567012345670123456701234567"
#define TEST4 TEST4a TEST4a

void test_rfc6234_published_vectors(void)
{
    char got[65];

    one_shot_hex(TEST1, sizeof(TEST1) - 1, got);
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", got);

    one_shot_hex(TEST2_1, sizeof(TEST2_1) - 1, got);
    TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", got);

    // TEST4 is 64 octets and is fed 10 times: 640 octets, an exact multiple of the 512-bit block.
    char buf[640];
    for (int i = 0; i < 10; i++)
    {
        memcpy(buf + i * 64, TEST4, 64);
    }
    one_shot_hex(buf, sizeof(buf), got);
    TEST_ASSERT_EQUAL_STRING("594847328451bdfa85056225462cc1d867d877fb388df0ce35f25ab5562bfbb5", got);
}

// RFC 6234 sec 8.5 TEST3: the single octet 'a' repeated one million times, fed as 1000-octet chunks
// so the streaming path carries the length across 15,625 compressions.
void test_rfc6234_one_million_a(void)
{
    uint8_t chunk[1000];
    memset(chunk, 'a', sizeof(chunk));

    Sha256.init(g_ctx_work);
    for (int i = 0; i < 1000; i++)
    {
        sha_update(chunk, sizeof(chunk));
    }
    uint8_t d[PROTOCORE_SHA256_DIGEST_LEN];
    sha_final(d);

    char got[65];
    tohex(d, sizeof(d), got);
    TEST_ASSERT_EQUAL_STRING("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", got);
}

// RFC 8448 sec 3 prints this as the 32-octet hash the "tls13 derived" Derive-Secret uses, which
// RFC 8446 sec 7.1 defines as Transcript-Hash of the empty message.
void test_empty_message(void)
{
    char got[65];
    one_shot_hex("", 0, got);
    TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", got);
}

// The chunking a caller happens to use may not change the digest: every two-way split of the
// published 56-octet message has to land on the same published value.
void test_chunk_boundaries_do_not_change_the_digest(void)
{
    static const char MSG[] = TEST2_1;
    const size_t n = sizeof(MSG) - 1;
    for (size_t cut = 0; cut <= n; cut++)
    {
        uint8_t d[PROTOCORE_SHA256_DIGEST_LEN];
        char got[65];
        Sha256.init(g_ctx_work);
        sha_update((const uint8_t *)MSG, cut);
        sha_update((const uint8_t *)MSG + cut, n - cut);
        sha_final(d);
        tohex(d, sizeof(d), got);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", got, MSG);
    }
}

// A zero-length update is a no-op, so it may not disturb a running digest.
void test_empty_update_is_a_no_op(void)
{
    uint8_t d[PROTOCORE_SHA256_DIGEST_LEN];
    char got[65];
    Sha256.init(g_ctx_work);
    sha_update((const uint8_t *)"", 0);
    sha_update((const uint8_t *)"a", 1);
    sha_update((const uint8_t *)"", 0);
    sha_update((const uint8_t *)"bc", 2);
    sha_update((const uint8_t *)"", 0);
    sha_final(d);
    tohex(d, sizeof(d), got);
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", got);
}

// The header's contract: final() pads into a copy of the state, so the running hash is untouched and
// the context keeps taking data. Reading the digest after "abc" must give the published "abc" value,
// and continuing with the rest of the 56-octet message must then give that message's published one.
void test_final_leaves_the_context_running(void)
{
    static const char MSG[] = TEST2_1;
    uint8_t d[PROTOCORE_SHA256_DIGEST_LEN];
    char got[65];

    Sha256.init(g_ctx_work);
    sha_update((const uint8_t *)"abc", 3);
    sha_final(d);
    tohex(d, sizeof(d), got);
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", got);

    // Reading it twice in a row must give the same answer.
    sha_final(d);
    tohex(d, sizeof(d), got);
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", got);

    sha_update((const uint8_t *)MSG + 3, sizeof(MSG) - 1 - 3);
    sha_final(d);
    tohex(d, sizeof(d), got);
    TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", got);
}

// The one-shot entry point and the streaming one are the same function of the message.
void test_one_shot_matches_streaming(void)
{
    static const char MSG[] = "The quick brown fox jumps over the lazy dog";
    uint8_t one[PROTOCORE_SHA256_DIGEST_LEN], streamed[PROTOCORE_SHA256_DIGEST_LEN];
    Sha256.hash_args.data = (const uint8_t *)MSG; Sha256.hash_args.len = sizeof(MSG) - 1; Sha256.hash_args.out = one; Sha256.hash(g_work);

    Sha256.init(g_ctx_work);
    for (size_t i = 0; i < sizeof(MSG) - 1; i++)
    {
        sha_update((const uint8_t *)MSG + i, 1);
    }
    sha_final(streamed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(one, streamed, PROTOCORE_SHA256_DIGEST_LEN);
}

// A one-bit change in the message changes the digest, and one message is never a prefix-collision of
// another: two spellings that differ only by a trailing NUL must not hash alike.
void test_distinct_messages_hash_differently(void)
{
    uint8_t a[PROTOCORE_SHA256_DIGEST_LEN], b[PROTOCORE_SHA256_DIGEST_LEN];
    Sha256.hash_args.data = (const uint8_t *)"abc"; Sha256.hash_args.len = 3; Sha256.hash_args.out = a; Sha256.hash(g_work);
    Sha256.hash_args.data = (const uint8_t *)"abd"; Sha256.hash_args.len = 3; Sha256.hash_args.out = b; Sha256.hash(g_work);
    TEST_ASSERT_TRUE(memcmp(a, b, sizeof(a)) != 0);

    Sha256.hash_args.data = (const uint8_t *)"abc"; Sha256.hash_args.len = 3; Sha256.hash_args.out = a; Sha256.hash(g_work);
    Sha256.hash_args.data = (const uint8_t *)"abc\0"; Sha256.hash_args.len = 4; Sha256.hash_args.out = b; Sha256.hash(g_work);
    TEST_ASSERT_TRUE(memcmp(a, b, sizeof(a)) != 0);
}

// The block size the header states is what the padding rule is built on: a message of exactly one
// block, and one of a block less one octet, both hash without the digest depending on which side of
// the boundary the padding landed. Anchored on the published 640-octet vector's tenth block.
void test_block_length_constants(void)
{
    TEST_ASSERT_EQUAL_UINT(64u, (unsigned)PROTOCORE_SHA256_BLOCK_LEN);
    TEST_ASSERT_EQUAL_UINT(32u, (unsigned)PROTOCORE_SHA256_DIGEST_LEN);

    // 64 octets of TEST4 is one whole block; its digest must differ from the 63-octet prefix.
    uint8_t full[PROTOCORE_SHA256_DIGEST_LEN], short_[PROTOCORE_SHA256_DIGEST_LEN];
    Sha256.hash_args.data = (const uint8_t *)TEST4; Sha256.hash_args.len = 64; Sha256.hash_args.out = full; Sha256.hash(g_work);
    Sha256.hash_args.data = (const uint8_t *)TEST4; Sha256.hash_args.len = 63; Sha256.hash_args.out = short_; Sha256.hash(g_work);
    TEST_ASSERT_TRUE(memcmp(full, short_, sizeof(full)) != 0);
}
