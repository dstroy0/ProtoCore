// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for TOTP (services/security/totp): the RFC 6238 Appendix B test vectors
// (HMAC-SHA1, 8-digit), the verifier window, and base32 decode.

#include "services/security/totp/totp.h"
#include <string.h>
#include <unity.h>

// RFC 6238 SHA-1 secret: the ASCII "12345678901234567890" (20 bytes).
static const uint8_t SECRET[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
                                 '1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
static const size_t SECRET_LEN = sizeof(SECRET);

void setUp()
{
}
void tearDown()
{
}

void test_rfc6238_vectors()
{
    // RFC 6238 Appendix B (SHA-1, T0=0, step=30, digits=8).
    TEST_ASSERT_EQUAL_UINT32(94287082u, pc_totp(SECRET, SECRET_LEN, 59ull, 30, 8));
    TEST_ASSERT_EQUAL_UINT32(7081804u, pc_totp(SECRET, SECRET_LEN, 1111111109ull, 30, 8));
    TEST_ASSERT_EQUAL_UINT32(14050471u, pc_totp(SECRET, SECRET_LEN, 1111111111ull, 30, 8));
    TEST_ASSERT_EQUAL_UINT32(89005924u, pc_totp(SECRET, SECRET_LEN, 1234567890ull, 30, 8));
    TEST_ASSERT_EQUAL_UINT32(69279037u, pc_totp(SECRET, SECRET_LEN, 2000000000ull, 30, 8));
}

void test_verify_window()
{
    uint64_t t = 1111111111ull;
    uint32_t code = pc_totp(SECRET, SECRET_LEN, t, 30, 8); // 14050471
    TEST_ASSERT_TRUE(pc_totp_verify(SECRET, SECRET_LEN, t, code, 30, 8, 1));
    TEST_ASSERT_FALSE(pc_totp_verify(SECRET, SECRET_LEN, t, code + 1, 30, 8, 1));
    // Code from the previous step is accepted within a +/-1 window (clock skew).
    uint32_t prev = pc_totp(SECRET, SECRET_LEN, t - 30, 30, 8);
    TEST_ASSERT_TRUE(pc_totp_verify(SECRET, SECRET_LEN, t, prev, 30, 8, 1));
    TEST_ASSERT_FALSE(pc_totp_verify(SECRET, SECRET_LEN, t, prev, 30, 8, 0)); // no window -> rejected
}

void test_base32_decode()
{
    uint8_t out[16];
    int n = pc_base32_decode("MZXW6===", out, sizeof(out)); // -> "foo"
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"foo", out, 3);

    // Case-insensitive, spaces/dashes ignored (how apps display a secret).
    int n2 = pc_base32_decode("mz xw-6", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(3, n2);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"foo", out, 3);
}

void test_base32_rejects_invalid()
{
    uint8_t out[16];
    TEST_ASSERT_EQUAL_INT(-1, pc_base32_decode("MZXW6!!!", out, sizeof(out))); // '!' invalid
    TEST_ASSERT_EQUAL_INT(-1, pc_base32_decode("MZXW6MZXW6", out, 1));         // overflow
}

void test_long_key_default_period_and_base32()
{
    uint8_t longkey[80];
    for (int i = 0; i < 80; i++)
    {
        longkey[i] = (uint8_t)i;
    }
    (void)pc_totp(longkey, sizeof(longkey), 59, 0, 6); // period 0 -> defaults to 30; long key pre-hashed
    (void)pc_hotp(longkey, sizeof(longkey), 1, 6);
    uint8_t out[16];
    TEST_ASSERT_TRUE(pc_base32_decode("MFRGG===", out, sizeof(out)) >= 0);  // '=' padding skipped
    TEST_ASSERT_EQUAL_INT(-1, pc_base32_decode("MFRG!", out, sizeof(out))); // invalid char
}

void test_verify_period_zero_default()
{
    // pc_totp_verify's period == 0 branch defaults to 30, same as pc_totp's.
    uint64_t t = 1111111111ull;
    uint32_t code30 = pc_totp(SECRET, SECRET_LEN, t, 30, 8);
    TEST_ASSERT_TRUE(pc_totp_verify(SECRET, SECRET_LEN, t, code30, 0, 8, 0));
}

void test_verify_window_skips_negative_step()
{
    // At unix_time 0 (step 0) with window 1, the w=-1 candidate step is negative
    // and must be skipped (loop `continue`), not treated as a valid step.
    uint32_t code0 = pc_totp(SECRET, SECRET_LEN, 0ull, 30, 8);
    TEST_ASSERT_TRUE(pc_totp_verify(SECRET, SECRET_LEN, 0ull, code0, 30, 8, 1));
}

void test_base32_decode_null_args()
{
    uint8_t out[16];
    TEST_ASSERT_EQUAL_INT(-1, pc_base32_decode(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, pc_base32_decode("ABC", NULL, sizeof(out)));
}

void test_base32_decode_rejects_char_above_z()
{
    uint8_t out[16];
    // '~' (0x7E) is >= 'a' but > 'z', exercising the else-if's upper-bound branch.
    TEST_ASSERT_EQUAL_INT(-1, pc_base32_decode("A~", out, sizeof(out)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_rfc6238_vectors);
    RUN_TEST(test_verify_window);
    RUN_TEST(test_base32_decode);
    RUN_TEST(test_base32_rejects_invalid);
    RUN_TEST(test_long_key_default_period_and_base32);
    RUN_TEST(test_verify_period_zero_default);
    RUN_TEST(test_verify_window_skips_negative_step);
    RUN_TEST(test_base32_decode_null_args);
    RUN_TEST(test_base32_decode_rejects_char_above_z);
    return UNITY_END();
}
