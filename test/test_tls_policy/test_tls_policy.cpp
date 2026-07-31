// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/tls_policy: version negotiation, cipher selection, AEAD classification.

#include "services/security/tls_policy/tls_policy.h"
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_negotiate_version(void)
{
    // Server supports 1.2..1.3.
    // A 1.3 client -> 1.3.
    TEST_ASSERT_EQUAL_HEX16(TLS_VERSION_1_3,
                            pc_tls_negotiate_version(TLS_VERSION_1_3, TLS_VERSION_1_2, TLS_VERSION_1_3));
    // A 1.2-only client -> 1.2.
    TEST_ASSERT_EQUAL_HEX16(TLS_VERSION_1_2,
                            pc_tls_negotiate_version(TLS_VERSION_1_2, TLS_VERSION_1_2, TLS_VERSION_1_3));
    // Server pinned to 1.3-only, a 1.2 client -> no overlap (0).
    TEST_ASSERT_EQUAL_HEX16(0, pc_tls_negotiate_version(TLS_VERSION_1_2, TLS_VERSION_1_3, TLS_VERSION_1_3));
    // Server pinned to 1.2-only, a 1.3 client -> 1.2 (server caps down).
    TEST_ASSERT_EQUAL_HEX16(TLS_VERSION_1_2,
                            pc_tls_negotiate_version(TLS_VERSION_1_3, TLS_VERSION_1_2, TLS_VERSION_1_2));
    // Inverted server range -> 0.
    TEST_ASSERT_EQUAL_HEX16(0, pc_tls_negotiate_version(TLS_VERSION_1_3, TLS_VERSION_1_3, TLS_VERSION_1_2));
}

void test_version_name(void)
{
    TEST_ASSERT_EQUAL_STRING("TLS 1.2", pc_tls_version_name(TLS_VERSION_1_2));
    TEST_ASSERT_EQUAL_STRING("TLS 1.3", pc_tls_version_name(TLS_VERSION_1_3));
    TEST_ASSERT_EQUAL_STRING("unknown", pc_tls_version_name(0x0301)); // TLS 1.0
}

void test_select_cipher(void)
{
    // Server prefers ECDHE_RSA_AES_128_GCM then CHACHA20; client offers CHACHA20 + a legacy suite.
    const uint16_t pinned[] = {0xC02F, 0xCCA8};
    const uint16_t offered[] = {0x002F, 0xCCA8}; // legacy AES_CBC + CHACHA20
    TEST_ASSERT_EQUAL_HEX16(0xCCA8, pc_tls_select_cipher(offered, 2, pinned, 2));
    // Client offers the top pinned suite -> that one wins (server preference order).
    const uint16_t offered2[] = {0xC02F, 0xCCA8};
    TEST_ASSERT_EQUAL_HEX16(0xC02F, pc_tls_select_cipher(offered2, 2, pinned, 2));
    // No overlap -> 0.
    const uint16_t offered3[] = {0x002F, 0x0035};
    TEST_ASSERT_EQUAL_HEX16(0, pc_tls_select_cipher(offered3, 2, pinned, 2));
}

void test_select_cipher_null_args(void)
{
    const uint16_t pinned[] = {0xC02F, 0xCCA8};
    const uint16_t offered[] = {0xC02F, 0xCCA8};
    // Null client_offered -> 0, defensive early-out.
    TEST_ASSERT_EQUAL_HEX16(0, pc_tls_select_cipher(NULL, 2, pinned, 2));
    // Null server_pinned -> 0, defensive early-out.
    TEST_ASSERT_EQUAL_HEX16(0, pc_tls_select_cipher(offered, 2, NULL, 2));
    // Both null -> 0.
    TEST_ASSERT_EQUAL_HEX16(0, pc_tls_select_cipher(NULL, 0, NULL, 0));
}

void test_is_aead(void)
{
    TEST_ASSERT_TRUE(pc_tls_is_aead(0x1301));  // TLS_AES_128_GCM_SHA256
    TEST_ASSERT_TRUE(pc_tls_is_aead(0xCCA8));  // ECDHE_RSA_CHACHA20_POLY1305
    TEST_ASSERT_TRUE(pc_tls_is_aead(0xC030));  // ECDHE_RSA_AES_256_GCM
    TEST_ASSERT_FALSE(pc_tls_is_aead(0x002F)); // AES_128_CBC_SHA (not AEAD)
    TEST_ASSERT_FALSE(pc_tls_is_aead(0x0000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_negotiate_version);
    RUN_TEST(test_version_name);
    RUN_TEST(test_select_cipher);
    RUN_TEST(test_select_cipher_null_args);
    RUN_TEST(test_is_aead);
    return UNITY_END();
}
