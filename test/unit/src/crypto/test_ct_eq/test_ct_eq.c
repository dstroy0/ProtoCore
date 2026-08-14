// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the library's one secret-dependent comparator (crypto/ct_eq.h).
//
// PROVENANCE: there is no standard to quote here. A comparator has no published vector, and the
// timing property the header claims cannot be shown by a functional test at all - that needs a cycle
// counter on the die. So every case below is a PROPERTY the accumulate must hold whatever its shape,
// and the load-bearing one is test_a_difference_at_every_position_is_caught: it sweeps the differing
// octet across the whole buffer, which is what separates an XOR-accumulate that reads all n from an
// early-out compare that agrees with it on every case except where the difference sits late.

#include "crypto/ct_eq.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Buffers that agree compare equal at every length the callers actually pass: a GCM or Poly1305 tag
// (16), a SHA-256 digest (32), a SHA-512 digest or an Ed25519 signature (64).
void test_equal_buffers_match(void)
{
    static const size_t LEN[] = {0, 1, 15, 16, 17, 31, 32, 63, 64};
    uint8_t a[64], b[64];
    for (size_t i = 0; i < sizeof(a); i++)
    {
        a[i] = (uint8_t)(i * 7u + 3u);
        b[i] = a[i];
    }
    for (size_t k = 0; k < sizeof(LEN) / sizeof(LEN[0]); k++)
    {
        TEST_ASSERT_TRUE(protocore_ct_eq(a, b, LEN[k]));
    }
}

// A zero-length compare asks nothing, so it is vacuously true even over buffers that differ.
void test_zero_length_is_equal(void)
{
    uint8_t a[1] = {0xAA};
    uint8_t b[1] = {0x55};
    TEST_ASSERT_TRUE(protocore_ct_eq(a, b, 0));
}

// The same pointer twice compares equal without being special-cased into a short circuit.
void test_aliased_pointer_is_equal(void)
{
    uint8_t a[32];
    memset(a, 0x5A, sizeof(a));
    TEST_ASSERT_TRUE(protocore_ct_eq(a, a, sizeof(a)));
}

// The differing octet is walked across the whole buffer. An implementation that stopped early still
// catches a difference at octet 0; one that stopped an octet short misses only the last. Sweeping
// every position at every length catches both shapes.
void test_a_difference_at_every_position_is_caught(void)
{
    uint8_t a[64], b[64];
    for (size_t n = 1; n <= sizeof(a); n++)
    {
        for (size_t pos = 0; pos < n; pos++)
        {
            memset(a, 0x3C, sizeof(a));
            memcpy(b, a, sizeof(a));
            b[pos] ^= 0xFF;
            TEST_ASSERT_FALSE(protocore_ct_eq(a, b, n));
        }
    }
}

// One flipped bit is a difference. A check that compared loosely, or masked off a nibble, passes a
// whole-octet test and fails this one.
void test_a_single_flipped_bit_is_caught(void)
{
    uint8_t a[16], b[16];
    for (size_t pos = 0; pos < sizeof(a); pos++)
    {
        for (unsigned bit = 0; bit < 8u; bit++)
        {
            memset(a, 0x00, sizeof(a));
            memcpy(b, a, sizeof(a));
            b[pos] = (uint8_t)(1u << bit);
            TEST_ASSERT_FALSE(protocore_ct_eq(a, b, sizeof(a)));
        }
    }
}

// Exactly n octets are read: a difference at or past n does not reach the result, and the octet at
// n-1 does. This is what stops a tag check being decided by whatever the caller stored after the tag.
void test_the_walk_stops_at_n(void)
{
    uint8_t a[32], b[32];
    memset(a, 0x11, sizeof(a));
    memcpy(b, a, sizeof(a));
    b[16] = 0xFF;
    TEST_ASSERT_TRUE(protocore_ct_eq(a, b, 16));
    TEST_ASSERT_TRUE(protocore_ct_eq(a, b, 8));
    TEST_ASSERT_FALSE(protocore_ct_eq(a, b, 17));
    TEST_ASSERT_FALSE(protocore_ct_eq(a, b, 32));
}

// The per-octet XORs are OR-accumulated, so differences never cancel across positions. A sum or an
// XOR fold reports both of these pairs equal.
void test_differences_do_not_cancel(void)
{
    uint8_t a[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t b[4] = {0x0F, 0x0F, 0x00, 0x00};
    TEST_ASSERT_FALSE(protocore_ct_eq(a, b, sizeof(a)));

    uint8_t c[2] = {0xAA, 0x55};
    uint8_t d[2] = {0x55, 0xAA};
    TEST_ASSERT_FALSE(protocore_ct_eq(c, d, sizeof(c)));

    // A swap: the same multiset of octets in a different order is a different buffer.
    uint8_t e[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t f[4] = {0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_FALSE(protocore_ct_eq(e, f, sizeof(e)));
}

// The comparison is symmetric and takes const void *, so it works on any storage the callers hand
// it - a struct field, a stack array, a pool borrow - without either side being privileged.
void test_the_comparison_is_symmetric(void)
{
    uint8_t a[16], b[16];
    for (size_t i = 0; i < sizeof(a); i++)
    {
        a[i] = (uint8_t)i;
        b[i] = (uint8_t)i;
    }
    TEST_ASSERT_TRUE(protocore_ct_eq(a, b, sizeof(a)));
    TEST_ASSERT_TRUE(protocore_ct_eq(b, a, sizeof(a)));
    b[9] ^= 0x20;
    TEST_ASSERT_FALSE(protocore_ct_eq(a, b, sizeof(a)));
    TEST_ASSERT_FALSE(protocore_ct_eq(b, a, sizeof(a)));
}

// Every octet value participates: a difference of 0x80 in the high bit is a difference exactly as a
// difference of 0x01 is, so a signed-char accumulate cannot lose the top bit.
void test_the_high_bit_is_not_lost(void)
{
    uint8_t a[8], b[8];
    memset(a, 0x00, sizeof(a));
    memcpy(b, a, sizeof(a));
    for (size_t pos = 0; pos < sizeof(a); pos++)
    {
        b[pos] = 0x80;
        TEST_ASSERT_FALSE(protocore_ct_eq(a, b, sizeof(a)));
        b[pos] = 0x00;
    }

    memset(a, 0xFF, sizeof(a));
    memset(b, 0xFF, sizeof(b));
    TEST_ASSERT_TRUE(protocore_ct_eq(a, b, sizeof(a)));
    b[0] = 0x7F;
    TEST_ASSERT_FALSE(protocore_ct_eq(a, b, sizeof(a)));
}
