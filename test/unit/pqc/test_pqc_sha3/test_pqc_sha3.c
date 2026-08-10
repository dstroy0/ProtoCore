// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Known-answer tests for the Keccak/SHA-3/SHAKE primitive (network_drivers/presentation/pqc/sha3),
// the symmetric core ML-KEM is built on. Digests pinned from the NIST FIPS 202 test vectors, plus a
// streaming-XOF continuity check across the sponge block boundary (the path ML-KEM's matrix sampler
// uses). Pure host tests.

#include "crypto/hash/sha3.h"
#include <stdint.h>

#include <unity.h>

// The CSPRNG seam (forward-declared in sntrup761.cpp). This suite does not exercise sntrup761, but
// the native_pqc env compiles sntrup761.cpp into every one of its suite binaries, so this one has
// to satisfy the symbol too or it does not link - the sibling suites (test_pqc_mlkem,
// test_pqc_sntrup761) each define the same deterministic source for their own round-trips.
static uint32_t s_rng = 0xA5A5F00Du;
void pc_rand_fill(uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        s_rng = s_rng * 1103515245u + 12345u;
        b[i] = (uint8_t)(s_rng >> 16);
    }
}

void setUp()
{
}
void tearDown()
{
}

// Parse an ASCII hex string into bytes.
// One hex digit to its value.
static int nyb(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return c - 'A' + 10;
}

static void hx(const char *s, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        out[i] = (uint8_t)((nyb(s[2 * i]) << 4) | nyb(s[2 * i + 1]));
    }
}

// FIPS 202 absorbs whole rate-sized blocks: SHA3-256 takes 136 bytes at a time, SHA3-512 72,
// SHAKE128 168. Absorbing only "" and "abc" left the block loop unentered, and left the case where
// the domain-separation byte and the 0x80 pad land on the SAME state byte - inlen % rate == rate-1,
// where they must merge to 0x86 for SHA-3 and 0x9F for SHAKE - unreached at every rate. The digests
// come from an independent FIPS 202 (Python hashlib) over the same deterministic bytes.
static void fill_msg(uint8_t *m, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        m[i] = (uint8_t)((i * 7u + 3u) & 0xFFu);
    }
}

static void sha3_256_case(size_t n, const char *want_hex, const char *msg_label)
{
    uint8_t msg[256];
    uint8_t got[32];
    uint8_t want[32];
    fill_msg(msg, n);
    pc_sha3_256(got, msg, n);
    hx(want_hex, want, 32);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, got, 32, msg_label);
}

static void sha3_512_case(size_t n, const char *want_hex, const char *msg_label)
{
    uint8_t msg[256];
    uint8_t got[64];
    uint8_t want[64];
    fill_msg(msg, n);
    pc_sha3_512(got, msg, n);
    hx(want_hex, want, 64);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, got, 64, msg_label);
}

// 135 = rate-1: the domain byte and the pad byte merge. 136 = one whole block. 200 spans two.
void test_sha3_256_rate_boundaries()
{
    sha3_256_case(135, "d9dcf1f98e49a79b0643a9e68fef48079ff8777c5e7e7f93469ded65f192ac71", "SHA3-256 135 (rate-1)");
    sha3_256_case(136, "743bd32e775ac7387a57d4d574c89ddef5ebcb08bb5cc6b88c55a27b5035cc45", "SHA3-256 136 (one block)");
    sha3_256_case(200, "9da37ea2fb33acd563a014f50d6f7cc225f25577a81d900452b72b5de98f239d", "SHA3-256 200 (two blocks)");
}

// The same three positions at SHA3-512's narrower 72-byte rate.
void test_sha3_512_rate_boundaries()
{
    sha3_512_case(71,
                  "a02d5795bffd44cb0ac3cc3401ae89056b8017242eaf7e802033e974672ce794"
                  "5811760c3b0d9578bc51bf90c364636ac87cda9b4f3e45620ea9c030421e9d86",
                  "SHA3-512 71 (rate-1)");
    sha3_512_case(72,
                  "2ec0da5ff440c192d33033c4257eb39dcbd27edd7e41b5ac8db9daf13db501a2"
                  "ef938151aaddd82f600335654f77512cbcc926ec5abb05b79282ec716d685618",
                  "SHA3-512 72 (one block)");
    sha3_512_case(150,
                  "56024a03e8a88cf3265249a0533d7fda1c6e7d47ecb5cde2ffcc6bb92f65ab86"
                  "c946ff93059e226f8af724fd1b1c754c26c34a08d800d328c986a7e48737aee2",
                  "SHA3-512 150 (three blocks)");
}

// SHAKE128's 168-byte rate, where the merged byte is 0x9F rather than 0x86.
void test_shake128_rate_boundaries()
{
    uint8_t msg[200];
    uint8_t got[32];
    uint8_t want[32];

    fill_msg(msg, 167);
    pc_shake128(got, sizeof(got), msg, 167);
    hx("bb961bb015521037905f9baf69ce60dd3ba73f6ead09a559c8d8a85e10753bca", want, 32);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, got, 32, "SHAKE128 167 (rate-1)");

    fill_msg(msg, 168);
    pc_shake128(got, sizeof(got), msg, 168);
    hx("d4f73f3b6c4b72d05f45ed1f80b5774409f9cf336fc202bffa42ac38a6d3fbee", want, 32);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, got, 32, "SHAKE128 168 (one block)");
}

void test_sha3_256()
{
    uint8_t got[32], want[32];
    pc_sha3_256(got, (const uint8_t *)"", 0);
    hx("a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a", want, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);

    pc_sha3_256(got, (const uint8_t *)"abc", 3);
    hx("3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532", want, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

void test_sha3_512()
{
    uint8_t got[64], want[64];
    pc_sha3_512(got, (const uint8_t *)"", 0);
    hx("a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6"
       "15b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26",
       want, 64);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 64);

    pc_sha3_512(got, (const uint8_t *)"abc", 3);
    hx("b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e"
       "10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0",
       want, 64);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 64);
}

void test_shake_empty()
{
    uint8_t got[32], want[32];
    pc_shake128(got, 32, (const uint8_t *)"", 0);
    hx("7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26", want, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);

    pc_shake256(got, 32, (const uint8_t *)"", 0);
    hx("46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f", want, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

// The incremental XOF (pc_shake128_absorb + repeated pc_keccak_squeeze) must produce the same stream as one
// shot, including across the 168-octet block boundary that ML-KEM's rejection sampler crosses.
// The continuity check below compares the one-shot against the incremental path, but sha3.c routes
// the one-shot through that same squeeze - so it proves the split does not corrupt state, and not
// that the stream is right. Only the first 32 bytes were ever pinned externally. These pin the
// whole 200-byte stream, past the 168- and 136-octet rate boundaries, against an independent
// FIPS 202 (Python hashlib).
void test_shake128_stream_matches_published()
{
    uint8_t got[200];
    uint8_t want[200];
    pc_shake128(got, sizeof(got), (const uint8_t *)"abc", 3);
    hx("5881092dd818bf5cf8a3ddb793fbcba74097d5c526a6d35f97b83351940f2cc8"
       "44c50af32acd3f2cdd066568706f509bc1bdde58295dae3f891a9a0fca578378"
       "9a41f8611214ce612394df286a62d1a2252aa94db9c538956c717dc2bed4f232"
       "a0294c857c730aa16067ac1062f1201fb0d377cfb9cde4c63599b27f3462bba4"
       "a0ed296c801f9ff7f57302bb3076ee145f97a32ae68e76ab66c48d51675bd49a"
       "cc29082f5647584e6aa01b3f5af057805f973ff8ecb8b226ac32ada6f01c1fcd"
       "4818cb006aa5b4cd",
       want, sizeof(want));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, sizeof(want));
}

void test_shake256_stream_matches_published()
{
    uint8_t got[200];
    uint8_t want[200];
    pc_shake256(got, sizeof(got), (const uint8_t *)"abc", 3);
    hx("483366601360a8771c6863080cc4114d8db44530f8f1e1ee4f94ea37e78b5739"
       "d5a15bef186a5386c75744c0527e1faa9f8726e462a12a4feb06bd8801e751e4"
       "1385141204f329979fd3047a13c5657724ada64d2470157b3cdc288620944d78"
       "dbcddbd912993f0913f164fb2ce95131a2d09a3e6d51cbfc622720d7a75c6334"
       "e8a2d7ec71a7cc29cf0ea610eeff1a588290a53000faa79932becec0bd3cd0b3"
       "3a7e5d397fed1ada9442b99903f4dcfd8559ed3950faf40fe6f3b5d710ed3b67"
       "7513771af6bfe119",
       want, sizeof(want));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, sizeof(want));
}

// SHAKE256's incremental path was never exercised: only SHAKE128 has an absorb wrapper, so a
// caller reaches SHAKE256 incrementally through the generic primitive, which is what this drives.
void test_shake256_stream_continuity()
{
    const uint8_t msg[3] = {'a', 'b', 'c'};

    uint8_t oneshot[200];
    pc_shake256(oneshot, sizeof(oneshot), msg, sizeof(msg));

    KeccakCtx ctx;
    pc_keccak_absorb(&ctx, KECCAK_RATE_SHAKE256, msg, sizeof(msg), 0x1F);
    uint8_t split[200];
    pc_keccak_squeeze(&ctx, split, 100);       // inside the first 136-octet block
    pc_keccak_squeeze(&ctx, split + 100, 100); // continues past the boundary
    TEST_ASSERT_EQUAL_HEX8_ARRAY(oneshot, split, sizeof(oneshot));
}

void test_shake_stream_continuity()
{
    const uint8_t msg[3] = {'a', 'b', 'c'};

    uint8_t oneshot[200];
    pc_shake128(oneshot, sizeof(oneshot), msg, sizeof(msg));

    KeccakCtx ctx;
    pc_shake128_absorb(&ctx, msg, sizeof(msg));
    uint8_t split[200];
    pc_keccak_squeeze(&ctx, split, 120);      // first block plus into the second
    pc_keccak_squeeze(&ctx, split + 120, 80); // continues past the 168-octet boundary
    TEST_ASSERT_EQUAL_HEX8_ARRAY(oneshot, split, sizeof(oneshot));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_sha3_256);
    RUN_TEST(test_sha3_256_rate_boundaries);
    RUN_TEST(test_sha3_512_rate_boundaries);
    RUN_TEST(test_shake128_rate_boundaries);
    RUN_TEST(test_sha3_512);
    RUN_TEST(test_shake_empty);
    RUN_TEST(test_shake128_stream_matches_published);
    RUN_TEST(test_shake256_stream_matches_published);
    RUN_TEST(test_shake256_stream_continuity);
    RUN_TEST(test_shake_stream_continuity);
    return UNITY_END();
}
