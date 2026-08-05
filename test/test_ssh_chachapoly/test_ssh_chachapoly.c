// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the chacha20-poly1305@openssh.com cipher and its primitives:
//   - ChaCha20 block function vs RFC 8439 Section 2.3.2 test vector
//   - Poly1305 vs RFC 8439 Section 2.5.2 test vector
//   - the OpenSSH AEAD: length decode, encrypt/decrypt round-trip, and tag-tamper rejection.

#include "crypto/aead/chachapoly.h"
#include "crypto/cipher/chacha20.h"
#include "crypto/mac/poly1305.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// RFC 8439 Section 2.3.2: key 00..1f, counter 1, nonce 00 00 00 09 00 00 00 4a 00 00 00 00.
void test_chacha20_block_rfc8439()
{
    uint8_t key[32];
    for (int i = 0; i < 32; i++)
    {
        key[i] = (uint8_t)i;
    }
    const uint8_t nonce[12] = {0, 0, 0, 0x09, 0, 0, 0, 0x4a, 0, 0, 0, 0};
    const uint8_t expected[64] = {0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15, 0x50, 0x0f, 0xdd, 0x1f, 0xa3,
                                  0x20, 0x71, 0xc4, 0xc7, 0xd1, 0xf4, 0xc7, 0x33, 0xc0, 0x68, 0x03, 0x04, 0x22,
                                  0xaa, 0x9a, 0xc3, 0xd4, 0x6c, 0x4e, 0xd2, 0x82, 0x64, 0x46, 0x07, 0x9f, 0xaa,
                                  0x09, 0x14, 0xc2, 0xd7, 0x05, 0xd9, 0x8b, 0x02, 0xa2, 0xb5, 0x12, 0x9c, 0xd1,
                                  0xde, 0x16, 0x4e, 0xb9, 0xcb, 0xd0, 0x83, 0xe8, 0xa2, 0x50, 0x3c, 0x4e};
    uint8_t out[64];
    pc_chacha20_block_ietf(key, 1, nonce, out);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 64);
}

// RFC 8439 Section 2.5.2: the "Cryptographic Forum Research Group" vector.
void test_poly1305_rfc8439()
{
    const uint8_t key[32] = {0x85, 0xd6, 0xbe, 0x78, 0x57, 0x55, 0x6d, 0x33, 0x7f, 0x44, 0x52,
                             0xfe, 0x42, 0xd5, 0x06, 0xa8, 0x01, 0x03, 0x80, 0x8a, 0xfb, 0x0d,
                             0xb2, 0xfd, 0x4a, 0xbf, 0xf6, 0xaf, 0x41, 0x49, 0xf5, 0x1b};
    const char *msg = "Cryptographic Forum Research Group";
    const uint8_t expected[16] = {0xa8, 0x06, 0x1d, 0xc1, 0x30, 0x51, 0x36, 0xc6,
                                  0xc2, 0x2b, 0x8b, 0xaf, 0x0c, 0x01, 0x27, 0xa9};
    uint8_t tag[16];
    pc_poly1305(tag, (const uint8_t *)msg, strlen(msg), key);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, tag, 16);
}

// A short and a multi-block message round-trip through the OpenSSH AEAD.
void test_chachapoly_roundtrip()
{
    uint8_t key[64];
    for (int i = 0; i < 64; i++)
    {
        key[i] = (uint8_t)(i * 7 + 1);
    }
    const uint32_t seqnr = 3;
    const uint32_t payload_len = 100; // spans two ChaCha blocks
    uint8_t pt[4 + 100];
    pt[0] = (uint8_t)(payload_len >> 24);
    pt[1] = (uint8_t)(payload_len >> 16);
    pt[2] = (uint8_t)(payload_len >> 8);
    pt[3] = (uint8_t)payload_len;
    for (uint32_t i = 0; i < payload_len; i++)
    {
        pt[4 + i] = (uint8_t)(i ^ 0x5a);
    }

    uint8_t ct[4 + 100 + 16];
    pc_chachapoly_encrypt(key, seqnr, ct, pt, payload_len);

    // Independent cross-check: the exact ciphertext+tag OpenSSL (pyca ChaCha20 + Poly1305) produces
    // for this key/seqnr/payload via the OpenSSH construction. Byte-for-byte agreement proves the
    // whole AEAD (key split, big-endian seqnr nonce, counter placement, MAC-over-ciphertext).
    const uint8_t openssl_ref[120] = {
        0xda, 0x99, 0x4a, 0x68, 0xd3, 0xa3, 0xf9, 0x1c, 0x2a, 0x73, 0x57, 0xca, 0x5e, 0x51, 0xb2, 0xbb, 0xb7, 0x66,
        0xb2, 0xaa, 0x56, 0x52, 0x8f, 0xeb, 0x90, 0xb0, 0x0f, 0xe7, 0x9c, 0x39, 0xf9, 0x57, 0xe8, 0x81, 0x45, 0xc7,
        0x37, 0xa7, 0x4c, 0xb4, 0x0f, 0x49, 0x07, 0x35, 0x77, 0xbc, 0x7f, 0x47, 0xe2, 0x8b, 0x36, 0x0d, 0x96, 0x74,
        0xe7, 0x38, 0x03, 0x4e, 0xdb, 0x50, 0xc7, 0x26, 0x80, 0x80, 0xd1, 0x5a, 0xaf, 0xcf, 0x47, 0x7a, 0xf5, 0xad,
        0x76, 0x91, 0x56, 0x61, 0xec, 0xad, 0x33, 0x39, 0xb2, 0x66, 0x93, 0x70, 0x16, 0xce, 0x78, 0x34, 0x0d, 0xd1,
        0xfe, 0x73, 0x5a, 0x85, 0xff, 0xfe, 0x06, 0x0e, 0x8f, 0x1a, 0x20, 0xec, 0xe0, 0x6e, 0xc7, 0x17, 0xa9, 0x02,
        0x18, 0x57, 0x92, 0xed, 0xb1, 0xe1, 0x16, 0x9a, 0xf7, 0x86, 0x17, 0x6e};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(openssl_ref, ct, 120);

    // The length field is independently recoverable.
    uint8_t enc_len[4] = {ct[0], ct[1], ct[2], ct[3]};
    TEST_ASSERT_EQUAL_UINT32(payload_len, pc_chachapoly_get_length(key, seqnr, enc_len));

    // Verify + decrypt recovers the plaintext exactly.
    uint8_t rt[4 + 100];
    TEST_ASSERT_TRUE(pc_chachapoly_decrypt(key, seqnr, rt, ct, payload_len));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, rt, 4 + payload_len);

    // The ciphertext must differ from the plaintext (it is actually encrypted).
    TEST_ASSERT_TRUE(memcmp(ct + 4, pt + 4, payload_len) != 0);
}

void test_chachapoly_tamper_rejected()
{
    uint8_t key[64];
    for (int i = 0; i < 64; i++)
    {
        key[i] = (uint8_t)(i + 3);
    }
    const uint32_t payload_len = 16;
    uint8_t pt[4 + 16] = {0, 0, 0, 16, 'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd', '!', '!', '?', '?', '?', '?'};
    uint8_t ct[4 + 16 + 16];
    pc_chachapoly_encrypt(key, 0, ct, pt, payload_len);
    uint8_t rt[4 + 16];

    // Flip a payload byte -> tag mismatch -> reject.
    ct[6] ^= 0x01;
    TEST_ASSERT_FALSE(pc_chachapoly_decrypt(key, 0, rt, ct, payload_len));
    ct[6] ^= 0x01; // restore

    // Flip a tag byte -> reject.
    ct[4 + payload_len] ^= 0x80;
    TEST_ASSERT_FALSE(pc_chachapoly_decrypt(key, 0, rt, ct, payload_len));
    ct[4 + payload_len] ^= 0x80;

    // Wrong sequence number (different nonce) -> reject.
    TEST_ASSERT_FALSE(pc_chachapoly_decrypt(key, 1, rt, ct, payload_len));
    // Correct again -> accept.
    TEST_ASSERT_TRUE(pc_chachapoly_decrypt(key, 0, rt, ct, payload_len));
}

// A zero-length payload (payload_len == 0): the AEAD still authenticates the 4-byte length field. The
// encrypt/decrypt round-trip succeeds, the length decodes to 0, and a tampered tag is still rejected.
void test_chachapoly_empty_payload()
{
    uint8_t key[64];
    for (int i = 0; i < 64; i++)
    {
        key[i] = (uint8_t)(i * 5 + 9);
    }
    const uint32_t seqnr = 7;
    uint8_t pt[4] = {0, 0, 0, 0}; // length field = 0, no payload
    uint8_t ct[4 + 16];           // encrypted length (4) + tag (16), no payload bytes
    pc_chachapoly_encrypt(key, seqnr, ct, pt, 0);

    // The length field decodes to 0.
    uint8_t enc_len[4] = {ct[0], ct[1], ct[2], ct[3]};
    TEST_ASSERT_EQUAL_UINT32(0, pc_chachapoly_get_length(key, seqnr, enc_len));

    // Verify + decrypt recovers the (empty-payload) length field.
    uint8_t rt[4];
    TEST_ASSERT_TRUE(pc_chachapoly_decrypt(key, seqnr, rt, ct, 0));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, rt, 4);

    // Flip a tag byte -> reject even with an empty payload.
    ct[4] ^= 0x01;
    TEST_ASSERT_FALSE(pc_chachapoly_decrypt(key, seqnr, rt, ct, 0));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_chacha20_block_rfc8439);
    RUN_TEST(test_poly1305_rfc8439);
    RUN_TEST(test_chachapoly_roundtrip);
    RUN_TEST(test_chachapoly_tamper_rejected);
    RUN_TEST(test_chachapoly_empty_payload);
    return UNITY_END();
}
