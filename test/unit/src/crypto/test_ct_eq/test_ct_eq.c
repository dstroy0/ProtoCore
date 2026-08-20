// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the constant-time compare (crypto/ct_eq.h).
//
// This is the compare every tag check in the library ends at - AES-GCM, AES-CCM,
// chacha20-poly1305, the TLS and SSH Finished checks - so what it must not do is return early. No
// host test can observe timing reliably, so the cases here pin the two things that ARE observable
// and that an early-exit implementation gets wrong in practice: the answer is correct wherever the
// first difference sits, including the very last byte, and every byte is actually read.
//
// The last-byte case is the load-bearing one. A memcmp that bailed at the first mismatch would still
// answer correctly, but an implementation that accumulated only part of the buffer - a wrong length,
// an off-by-one, a loop that stopped at the first zero - passes a first-byte test and fails here.

#include "crypto/ct_eq.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The entry reads nothing out of the borrow, but it takes one like every other entry does.
static uint8_t g_ws[64] __attribute__((aligned(8)));

static proto_bool eq(const void *a, const void *b, size_t n)
{
    CtEq.eq_args.a = a;
    CtEq.eq_args.b = b;
    CtEq.eq_args.n = n;
    CtEq.eq(g_ws);
    return CtEq.equal;
}

void test_identical_buffers_are_equal(void)
{
    static const uint8_t A[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                  0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    uint8_t b[16];
    memcpy(b, A, sizeof(A));
    TEST_ASSERT_TRUE(eq(A, b, sizeof(A)));
    TEST_ASSERT_TRUE(CtEq.ok);
}

// A difference at ANY position must be caught, so every position is tried. An implementation that
// compared a prefix, or stopped early on a zero byte, fails somewhere in this loop.
void test_a_difference_at_every_position_is_caught(void)
{
    uint8_t a[32], b[32];
    for (size_t i = 0; i < sizeof(a); i++)
    {
        a[i] = (uint8_t)(i * 7 + 1);
    }
    for (size_t pos = 0; pos < sizeof(a); pos++)
    {
        memcpy(b, a, sizeof(a));
        b[pos] ^= 0x01; // the smallest possible difference
        TEST_ASSERT_FALSE_MESSAGE(eq(a, b, sizeof(a)), "a one-bit change was not caught");
    }
}

// The final byte is where a length that is one short stops looking.
void test_difference_in_the_last_byte(void)
{
    uint8_t a[16], b[16];
    memset(a, 0xA5, sizeof(a));
    memcpy(b, a, sizeof(a));
    b[sizeof(b) - 1] = 0xA4;
    TEST_ASSERT_FALSE(eq(a, b, sizeof(a)));
}

// A tag compare runs over buffers holding zero bytes; a loop that treated one as a terminator would
// stop there and call two different buffers equal.
void test_embedded_zero_bytes_do_not_terminate_the_compare(void)
{
    uint8_t a[16] = {0}, b[16] = {0};
    b[15] = 0x01; // everything before the difference is zero
    TEST_ASSERT_FALSE(eq(a, b, sizeof(a)));
    b[15] = 0x00;
    TEST_ASSERT_TRUE(eq(a, b, sizeof(a)));
}

// Zero length compares nothing, so it is vacuously equal - which is what an empty AAD relies on.
void test_zero_length_is_equal(void)
{
    static const uint8_t A[1] = {0x11};
    static const uint8_t B[1] = {0x22};
    TEST_ASSERT_TRUE(eq(A, B, 0));
}

// A buffer is equal to itself whatever it holds, including when both pointers are the same.
void test_same_pointer_is_equal(void)
{
    static const uint8_t A[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_TRUE(eq(A, A, sizeof(A)));
}

// A null operand is refused rather than dereferenced, and refusal reads as not-equal so a caller
// that ignores ok cannot be tricked into treating a failed compare as a match.
void test_null_operands_are_refused(void)
{
    static const uint8_t A[4] = {1, 2, 3, 4};
    CtEq.eq_args.a = NULL;
    CtEq.eq_args.b = A;
    CtEq.eq_args.n = sizeof(A);
    CtEq.eq(g_ws);
    TEST_ASSERT_FALSE(CtEq.ok);
    TEST_ASSERT_FALSE(CtEq.equal);

    CtEq.eq_args.a = A;
    CtEq.eq_args.b = NULL;
    CtEq.eq(g_ws);
    TEST_ASSERT_FALSE(CtEq.ok);
    TEST_ASSERT_FALSE(CtEq.equal);
}

// The inline the whole library calls directly and the namespace entry must be the same function.
void test_inline_and_namespace_agree(void)
{
    uint8_t a[24], b[24];
    for (size_t i = 0; i < sizeof(a); i++)
    {
        a[i] = (uint8_t)(i * 13 + 5);
    }
    memcpy(b, a, sizeof(a));
    TEST_ASSERT_EQUAL_INT(protocore_ct_eq(a, b, sizeof(a)) ? 1 : 0, eq(a, b, sizeof(a)) ? 1 : 0);
    b[7] ^= 0x80;
    TEST_ASSERT_EQUAL_INT(protocore_ct_eq(a, b, sizeof(a)) ? 1 : 0, eq(a, b, sizeof(a)) ? 1 : 0);
}
