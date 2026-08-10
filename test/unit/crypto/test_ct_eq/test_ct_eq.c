// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// pc_ct_eq is the library's one comparator for every AEAD tag, MAC, digest and signature check, and
// it had no test of any kind. A functional suite cannot prove the timing property - that needs a
// cycle counter on the die, the way base64's claim is measured - but it can prove the accumulate
// covers every byte and stops at n, which is what an early-out or a short read would break.

#include "crypto/ct_eq.h"
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Two buffers that agree everywhere compare equal, at the lengths the callers actually use: a
// Poly1305/GCM tag, a SHA-256 digest, a SHA-512 digest or an Ed25519 signature.
void test_equal_buffers_match(void)
{
    static const size_t lens[] = {0, 1, 15, 16, 32, 64};
    uint8_t a[64];
    uint8_t b[64];
    for (size_t i = 0; i < sizeof(a); i++)
    {
        a[i] = (uint8_t)(i * 7u + 3u);
        b[i] = a[i];
    }
    for (size_t k = 0; k < sizeof(lens) / sizeof(lens[0]); k++)
    {
        TEST_ASSERT_TRUE_MESSAGE(pc_ct_eq(a, b, lens[k]), "identical buffers must compare equal");
    }
}

// A zero-length compare is vacuously true: nothing was asked about.
void test_zero_length_is_equal(void)
{
    uint8_t a[1] = {0xAA};
    uint8_t b[1] = {0x55};
    TEST_ASSERT_TRUE(pc_ct_eq(a, b, 0));
}

// The same pointer twice is equal, and must not be special-cased into a short circuit elsewhere.
void test_aliased_pointer_is_equal(void)
{
    uint8_t a[32];
    memset(a, 0x5A, sizeof(a));
    TEST_ASSERT_TRUE(pc_ct_eq(a, a, sizeof(a)));
}

// Every byte position is covered. An implementation that stopped early would still catch a
// difference in byte 0; one that stopped short would miss the last byte. Sweeping all 32 catches
// both shapes.
void test_difference_at_every_position_is_caught(void)
{
    uint8_t a[32];
    uint8_t b[32];
    for (size_t pos = 0; pos < sizeof(a); pos++)
    {
        memset(a, 0x3C, sizeof(a));
        memcpy(b, a, sizeof(a));
        b[pos] ^= 0xFF;
        TEST_ASSERT_FALSE_MESSAGE(pc_ct_eq(a, b, sizeof(a)), "a differing byte must not compare equal");
    }
}

// One flipped bit is a difference. A tag check that only compared whole bytes loosely, or masked,
// would pass this buffer.
void test_single_bit_difference_is_caught(void)
{
    uint8_t a[16];
    uint8_t b[16];
    for (unsigned bit = 0; bit < 8u; bit++)
    {
        memset(a, 0x00, sizeof(a));
        memcpy(b, a, sizeof(a));
        b[sizeof(b) - 1] = (uint8_t)(1u << bit);
        TEST_ASSERT_FALSE_MESSAGE(pc_ct_eq(a, b, sizeof(a)), "a single flipped bit must not compare equal");
    }
}

// The compare reads exactly n bytes: a difference past n does not reach it. This is what stops a
// tag check from being decided by whatever follows the tag in the caller's buffer.
void test_difference_past_n_is_not_read(void)
{
    uint8_t a[32];
    uint8_t b[32];
    memset(a, 0x11, sizeof(a));
    memcpy(b, a, sizeof(a));
    b[16] = 0xFF; // past the length asked about
    TEST_ASSERT_TRUE_MESSAGE(pc_ct_eq(a, b, 16), "bytes past n must not decide the result");
    TEST_ASSERT_FALSE_MESSAGE(pc_ct_eq(a, b, 17), "the byte at n-1 must decide it");
}

// Differences that XOR-cancel across positions must not cancel: the accumulate is a bitwise OR of
// per-byte XORs, so two bytes differing by the same delta still leave the result non-zero. A sum or
// an XOR fold instead of an OR would report these equal.
void test_cancelling_differences_do_not_cancel(void)
{
    uint8_t a[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t b[4] = {0x0F, 0x0F, 0x00, 0x00};
    TEST_ASSERT_FALSE(pc_ct_eq(a, b, sizeof(a)));

    uint8_t c[2] = {0xAA, 0x55};
    uint8_t d[2] = {0x55, 0xAA};
    TEST_ASSERT_FALSE(pc_ct_eq(c, d, sizeof(c)));
}

// pc_ct_is_zero is what the TLS 1.3 handshakes key their RFC 8446 sec 7.4.2 abort on: a low-order
// peer key share drives X25519 to an all-zero shared secret, and accepting it would derive traffic
// keys from a value the peer chose.
void test_ct_is_zero(void)
{
    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(pc_ct_is_zero(buf, sizeof(buf)), "an all-zero buffer is zero");

    // A single set bit anywhere must be seen, wherever it sits.
    for (size_t pos = 0; pos < sizeof(buf); pos++)
    {
        memset(buf, 0, sizeof(buf));
        buf[pos] = 0x01;
        TEST_ASSERT_FALSE_MESSAGE(pc_ct_is_zero(buf, sizeof(buf)), "one set byte is not zero");
    }

    // Zero length is vacuously zero, and the test reads exactly n.
    memset(buf, 0, sizeof(buf));
    buf[8] = 0xFF;
    TEST_ASSERT_TRUE_MESSAGE(pc_ct_is_zero(buf, 8), "bytes past n must not be read");
    TEST_ASSERT_FALSE_MESSAGE(pc_ct_is_zero(buf, 9), "the byte at n-1 must decide it");
    TEST_ASSERT_TRUE(pc_ct_is_zero(buf, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_equal_buffers_match);
    RUN_TEST(test_zero_length_is_equal);
    RUN_TEST(test_aliased_pointer_is_equal);
    RUN_TEST(test_difference_at_every_position_is_caught);
    RUN_TEST(test_single_bit_difference_is_caught);
    RUN_TEST(test_difference_past_n_is_not_read);
    RUN_TEST(test_cancelling_differences_do_not_cancel);
    RUN_TEST(test_ct_is_zero);
    return UNITY_END();
}
