// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Known-answer tests for the Keccak/SHA-3/SHAKE primitive (network_drivers/presentation/pqc/sha3),
// the symmetric core ML-KEM is built on. Digests pinned from the NIST FIPS 202 test vectors, plus a
// streaming-XOF continuity check across the sponge block boundary (the path ML-KEM's matrix sampler
// uses). Pure host tests.

#include "crypto/hash/sha3.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

// The CSPRNG seam (forward-declared in sntrup761.cpp). This suite does not exercise sntrup761, but
// the native_pqc env compiles sntrup761.cpp into every one of its suite binaries, so this one has
// to satisfy the symbol too or it does not link - the sibling suites (test_pqc_mlkem,
// test_pqc_sntrup761) each define the same deterministic source for their own round-trips.
static uint32_t s_rng = 0xA5A5F00Du;
void ssh_rng_fill(uint8_t *b, size_t n)
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
static void hx(const char *s, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        auto nyb = [](char c) -> int {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            return c - 'A' + 10;
        };
        out[i] = (uint8_t)((nyb(s[2 * i]) << 4) | nyb(s[2 * i + 1]));
    }
}

void test_sha3_256()
{
    uint8_t got[32], want[32];
    sha3_256(got, (const uint8_t *)"", 0);
    hx("a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a", want, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);

    sha3_256(got, (const uint8_t *)"abc", 3);
    hx("3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532", want, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

void test_sha3_512()
{
    uint8_t got[64], want[64];
    sha3_512(got, (const uint8_t *)"", 0);
    hx("a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6"
       "15b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26",
       want, 64);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 64);

    sha3_512(got, (const uint8_t *)"abc", 3);
    hx("b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e"
       "10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0",
       want, 64);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 64);
}

void test_shake_empty()
{
    uint8_t got[32], want[32];
    shake128(got, 32, (const uint8_t *)"", 0);
    hx("7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26", want, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);

    shake256(got, 32, (const uint8_t *)"", 0);
    hx("46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f", want, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

// The incremental XOF (pc_shake128_absorb + repeated pc_keccak_squeeze) must produce the same stream as one
// shot, including across the 168-octet block boundary that ML-KEM's rejection sampler crosses.
void test_shake_stream_continuity()
{
    const uint8_t msg[3] = {'a', 'b', 'c'};

    uint8_t oneshot[200];
    shake128(oneshot, sizeof(oneshot), msg, sizeof(msg));

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
    RUN_TEST(test_sha3_512);
    RUN_TEST(test_shake_empty);
    RUN_TEST(test_shake_stream_continuity);
    return UNITY_END();
}
