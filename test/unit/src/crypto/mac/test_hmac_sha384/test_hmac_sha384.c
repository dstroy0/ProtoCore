// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for HMAC-SHA-384 (crypto/mac/hmac_sha384.h), the MAC the TLS 1.3 SHA-384 key schedule
// runs HKDF and the Finished verify_data on.
//
// The load-bearing case is test_rfc4231_published_vectors: all seven HMAC-SHA-384 rows of RFC 4231
// sec 4.2 through 4.8, which the RFC states were cross-verified by three independent
// implementations. They cover the three key regimes the construction branches on - a key shorter
// than the digest (case 2), a key shorter than the block (cases 1, 3, 4, 5), and a 131-octet key
// longer than the 128-octet block, which must be pre-hashed (cases 6 and 7) - plus a message longer
// than the block (case 7).
//
// The block is SHA-512's 128 octets and NOT the 48-octet digest. A build that padded to 48 would
// still produce a self-consistent MAC, and cases 6 and 7 are what catch it: a 131-octet key is over
// the real block and under no other plausible one.

#include "crypto/hash/sha384/sha384.h" // PROTOCORE_SHA384_BLOCK_LEN: the block the pads are built on
#include "crypto/mac/hmac_sha384/hmac_sha384.h"
#include "crypto/mac/hmac_sha512/hmac_sha512.h"
#include <string.h>

#include <unity.h>

static uint8_t g_work[PROTOCORE_HMAC_SHA384_BORROW] __attribute__((aligned(8)));
static uint8_t g_ctx_work[PROTOCORE_HMAC_SHA384_BORROW] __attribute__((aligned(8)));
static uint8_t g_512_work[PROTOCORE_HMAC_SHA512_BORROW] __attribute__((aligned(8)));

void setUp(void)
{
}
void tearDown(void)
{
}

// Render a MAC as lowercase hex so a mismatch prints the whole value.
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

// One-shot, the way the vectors below read: key and message in, one call, MAC out as hex.
static void mac_hex(const uint8_t *key, size_t key_len, const void *msg, size_t msg_len, size_t take, char *out)
{
    uint8_t d[PROTOCORE_HMAC_SHA384_LEN];
    HmacSha384V.mac_args.key = key;
    HmacSha384V.mac_args.key_len = key_len;
    HmacSha384V.mac_args.data = (const uint8_t *)msg;
    HmacSha384V.mac_args.len = msg_len;
    HmacSha384V.mac_args.out = d;
    HmacSha384.mac(g_work);
    tohex(d, take, out);
}

// The RFC 4231 keys: three fill patterns and two literals.
static uint8_t g_key[131];

static const uint8_t *fill_key(uint8_t b, size_t n)
{
    memset(g_key, b, n);
    return g_key;
}

static const uint8_t *counting_key(void)
{
    for (uint8_t i = 0; i < 25; i++)
    {
        g_key[i] = (uint8_t)(i + 1);
    }
    return g_key;
}

static uint8_t g_data[50];

static const uint8_t *fill_data(uint8_t b, size_t n)
{
    memset(g_data, b, n);
    return g_data;
}

#define CASE6_DATA "Test Using Larger Than Block-Size Key - Hash Key First"
#define CASE7_DATA                                                                                                     \
    "This is a test using a larger than block-size key and a larger than block-size data. The key needs to be "        \
    "hashed before being used by the HMAC algorithm."

void test_rfc4231_published_vectors(void)
{
    char got[2 * PROTOCORE_HMAC_SHA384_LEN + 1];

    // Case 1: a 20-octet key, an 8-octet message.
    mac_hex(fill_key(0x0bu, 20u), 20u, "Hi There", 8u, PROTOCORE_HMAC_SHA384_LEN, got);
    TEST_ASSERT_EQUAL_STRING("afd03944d84895626b0825f4ab46907f"
                             "15f9dadbe4101ec682aa034c7cebc59c"
                             "faea9ea9076ede7f4af152e8b2fa9cb6",
                             got);

    // Case 2: a key shorter than the digest.
    mac_hex((const uint8_t *)"Jefe", 4u, "what do ya want for nothing?", 28u, PROTOCORE_HMAC_SHA384_LEN, got);
    TEST_ASSERT_EQUAL_STRING("af45d2e376484031617f78d2b58a6b1b"
                             "9c7ef464f5a01b47e42ec3736322445e"
                             "8e2240ca5e69e2c78b3239ecfab21649",
                             got);

    // Case 3: a 20-octet key over a 50-octet message.
    mac_hex(fill_key(0xaau, 20u), 20u, fill_data(0xddu, 50u), 50u, PROTOCORE_HMAC_SHA384_LEN, got);
    TEST_ASSERT_EQUAL_STRING("88062608d3e6ad8a0aa2ace014c8a86f"
                             "0aa635d947ac9febe83ef4e55966144b"
                             "2a5ab39dc13814b94e3ab6e101a34f27",
                             got);

    // Case 4: a 25-octet counting key over a 50-octet message.
    mac_hex(counting_key(), 25u, fill_data(0xcdu, 50u), 50u, PROTOCORE_HMAC_SHA384_LEN, got);
    TEST_ASSERT_EQUAL_STRING("3e8a69b7783c25851933ab6290af6ca7"
                             "7a9981480850009cc5577c6e1f573b4e"
                             "6801dd23c4a7d679ccf8a386c674cffb",
                             got);

    // Case 5: the RFC prints only the first 128 bits of this one.
    mac_hex(fill_key(0x0cu, 20u), 20u, "Test With Truncation", 20u, 16u, got);
    TEST_ASSERT_EQUAL_STRING("3abf34c3503b2a23a46efc619baef897", got);

    // Case 6: a 131-octet key, over the 128-octet block, so it is pre-hashed.
    mac_hex(fill_key(0xaau, 131u), 131u, CASE6_DATA, sizeof(CASE6_DATA) - 1, PROTOCORE_HMAC_SHA384_LEN, got);
    TEST_ASSERT_EQUAL_STRING("4ece084485813e9088d2c63a041bc5b4"
                             "4f9ef1012a2b588f3cd11f05033ac4c6"
                             "0c2ef6ab4030fe8296248df163f44952",
                             got);

    // Case 7: the same key over a message that is itself longer than the block.
    mac_hex(fill_key(0xaau, 131u), 131u, CASE7_DATA, sizeof(CASE7_DATA) - 1, PROTOCORE_HMAC_SHA384_LEN, got);
    TEST_ASSERT_EQUAL_STRING("6617178e941f020d351e2f254e8fd32c"
                             "602420feb0b8fb9adccebb82461e99c5"
                             "a678cc31e799176d3860e6110c46523e",
                             got);
}

// The key block is 128 octets, not 48. A 129-octet key is over the real block and under the digest
// length, so a build that padded to the wrong width would agree with this one and disagree with the
// published case 6.
void test_the_block_is_the_sha512_block(void)
{
    TEST_ASSERT_EQUAL_UINT(128u, (unsigned)PROTOCORE_SHA384_BLOCK_LEN);
    TEST_ASSERT_EQUAL_UINT(48u, (unsigned)PROTOCORE_HMAC_SHA384_LEN);

    // A 128-octet key is padded; a 129-octet one is pre-hashed. Two different keys, two different
    // MACs, and neither path writes past its block.
    uint8_t at[PROTOCORE_HMAC_SHA384_LEN], over[PROTOCORE_HMAC_SHA384_LEN];
    HmacSha384V.mac_args.key = fill_key(0x5au, 128u);
    HmacSha384V.mac_args.key_len = 128u;
    HmacSha384V.mac_args.data = (const uint8_t *)"abc";
    HmacSha384V.mac_args.len = 3u;
    HmacSha384V.mac_args.out = at;
    HmacSha384.mac(g_work);

    HmacSha384V.mac_args.key = fill_key(0x5au, 129u);
    HmacSha384V.mac_args.key_len = 129u;
    HmacSha384V.mac_args.out = over;
    HmacSha384.mac(g_work);

    TEST_ASSERT_TRUE(memcmp(at, over, sizeof(at)) != 0);
}

// HMAC-SHA-384 is not HMAC-SHA-512 cut to 48 octets: the inner hash is a different digest.
void test_not_a_truncated_hmac_sha512(void)
{
    uint8_t d384[PROTOCORE_HMAC_SHA384_LEN];
    uint8_t d512[PROTOCORE_HMAC_SHA512_LEN];

    HmacSha384V.mac_args.key = fill_key(0x0bu, 20u);
    HmacSha384V.mac_args.key_len = 20u;
    HmacSha384V.mac_args.data = (const uint8_t *)"Hi There";
    HmacSha384V.mac_args.len = 8u;
    HmacSha384V.mac_args.out = d384;
    HmacSha384.mac(g_work);

    HmacSha512V.mac_args.key = fill_key(0x0bu, 20u);
    HmacSha512V.mac_args.key_len = 20u;
    HmacSha512V.mac_args.data = (const uint8_t *)"Hi There";
    HmacSha512V.mac_args.len = 8u;
    HmacSha512V.mac_args.out = d512;
    HmacSha512.mac(g_512_work);

    TEST_ASSERT_TRUE(memcmp(d384, d512, PROTOCORE_HMAC_SHA384_LEN) != 0);
}

// The streaming entries and the one-shot are the same MAC, whatever the split.
void test_streaming_matches_one_shot(void)
{
    uint8_t one[PROTOCORE_HMAC_SHA384_LEN], streamed[PROTOCORE_HMAC_SHA384_LEN];
    const char *msg = CASE7_DATA;
    const size_t len = sizeof(CASE7_DATA) - 1;

    HmacSha384V.mac_args.key = fill_key(0xaau, 131u);
    HmacSha384V.mac_args.key_len = 131u;
    HmacSha384V.mac_args.data = (const uint8_t *)msg;
    HmacSha384V.mac_args.len = len;
    HmacSha384V.mac_args.out = one;
    HmacSha384.mac(g_work);

    for (size_t cut = 1; cut < len; cut += 23)
    {
        HmacSha384V.key_args.key = fill_key(0xaau, 131u);
        HmacSha384V.key_args.key_len = 131u;
        HmacSha384.init(g_ctx_work);
        HmacSha384V.update_args.data = (const uint8_t *)msg;
        HmacSha384V.update_args.len = cut;
        HmacSha384.update(g_ctx_work);
        HmacSha384V.update_args.data = (const uint8_t *)msg + cut;
        HmacSha384V.update_args.len = len - cut;
        HmacSha384.update(g_ctx_work);
        HmacSha384V.final_args.out = streamed;
        HmacSha384.final(g_ctx_work);
        TEST_ASSERT_EQUAL_MEMORY(one, streamed, sizeof(one));
    }
}

// A one-bit change in the key or the message changes the MAC.
void test_a_changed_key_or_message_changes_the_mac(void)
{
    uint8_t base[PROTOCORE_HMAC_SHA384_LEN], other[PROTOCORE_HMAC_SHA384_LEN];

    HmacSha384V.mac_args.key = fill_key(0x0bu, 20u);
    HmacSha384V.mac_args.key_len = 20u;
    HmacSha384V.mac_args.data = (const uint8_t *)"Hi There";
    HmacSha384V.mac_args.len = 8u;
    HmacSha384V.mac_args.out = base;
    HmacSha384.mac(g_work);

    HmacSha384V.mac_args.key = fill_key(0x0au, 20u);
    HmacSha384V.mac_args.key_len = 20u;
    HmacSha384V.mac_args.out = other;
    HmacSha384.mac(g_work);
    TEST_ASSERT_TRUE(memcmp(base, other, sizeof(base)) != 0);

    HmacSha384V.mac_args.key = fill_key(0x0bu, 20u);
    HmacSha384V.mac_args.key_len = 20u;
    HmacSha384V.mac_args.data = (const uint8_t *)"Hi there";
    HmacSha384V.mac_args.len = 8u;
    HmacSha384V.mac_args.out = other;
    HmacSha384.mac(g_work);
    TEST_ASSERT_TRUE(memcmp(base, other, sizeof(base)) != 0);
}
