// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for chacha20-poly1305@openssh.com (crypto/aead/chachapoly.h).
//
// OpenSSH's PROTOCOL.chacha20poly1305 publishes no test vectors, so the load-bearing case is built
// from ones that are. RFC 8439 Appendix A.1 prints the ChaCha20 keystream for an all-zero key and
// an all-zero nonce at block counter 0 and at block counter 1. The OpenSSH construction is exactly
// those two blocks: the packet-length field is XORed with the header key's counter-0 block and the
// payload with the main key's counter-1 block, with the main key's counter-0 block reserved for the
// one-time Poly1305 key. test_rfc8439_keystream_is_applied_as_openssh_splits_it drives the module
// with an all-zero 64-octet key and sequence number 0, which makes both ChaCha keys all-zero and
// both nonces all-zero, and asserts the exact octets those published keystreams produce - each
// expected byte written as the RFC's keystream byte XOR the plaintext byte.
//
// test_the_two_keys_are_not_interchangeable then gives the two halves different keys, so that the
// length field and the payload draw from two DIFFERENT published keystreams and a swapped split
// cannot pass. The Poly1305 tag itself has no published value reachable this way, so it is held to
// properties instead: it verifies what it covers and refuses every change to it.

#include "crypto/aead/chachapoly.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 8439 A.1 Test Vector #1: key 0^32, nonce 0^12, block counter 0.
static const uint8_t KS_ZERO_CTR0[16] = {0x76, 0xb8, 0xe0, 0xad, 0xa0, 0xf1, 0x3d, 0x90,
                                         0x40, 0x5d, 0x6a, 0xe5, 0x53, 0x86, 0xbd, 0x28};
// RFC 8439 A.1 Test Vector #2: key 0^32, nonce 0^12, block counter 1.
static const uint8_t KS_ZERO_CTR1[16] = {0x9f, 0x07, 0xe7, 0xbe, 0x55, 0x51, 0x38, 0x7a,
                                         0x98, 0xba, 0x97, 0x7c, 0x73, 0x2d, 0x08, 0x0d};
// RFC 8439 A.1 Test Vector #3: key 0^31 || 01, nonce 0^12, block counter 1.
static const uint8_t KS_ONE_CTR1[16] = {0x3a, 0xeb, 0x52, 0x24, 0xec, 0xf8, 0x49, 0x92,
                                        0x9b, 0x9d, 0x82, 0x8d, 0xb1, 0xce, 0xd4, 0xdd};

static const uint8_t HELLO[5] = {'h', 'e', 'l', 'l', 'o'};

// The 4-octet big-endian packet_length followed by the payload, as the module's src wants it.
static void make_plaintext(uint8_t *dst, uint32_t len, const uint8_t *payload)
{
    dst[0] = (uint8_t)(len >> 24);
    dst[1] = (uint8_t)(len >> 16);
    dst[2] = (uint8_t)(len >> 8);
    dst[3] = (uint8_t)len;
    memcpy(dst + 4, payload, len);
}

// key = 0^64 and seqnr = 0 put both ChaCha20 keys and both nonces at all-zero, so the module must
// emit precisely the RFC's two published blocks XORed with the plaintext:
//   length field = KS_ZERO_CTR0 ^ 00 00 00 05   (header key, counter 0)
//   payload      = KS_ZERO_CTR1 ^ "hello"       (main key, counter 1 - counter 0 is the Poly key)
void test_rfc8439_keystream_is_applied_as_openssh_splits_it(void)
{
    uint8_t key[PROTOCORE_CHACHAPOLY_KEY_LEN];
    memset(key, 0, sizeof(key));

    uint8_t pt[4 + 5], ct[4 + 5 + PROTOCORE_CHACHAPOLY_TAG_LEN];
    make_plaintext(pt, 5, HELLO);
    protocore_chachapoly_encrypt(key, 0, ct, pt, 5);

    uint8_t want[9];
    for (int i = 0; i < 4; i++)
    {
        want[i] = (uint8_t)(KS_ZERO_CTR0[i] ^ pt[i]);
    }
    for (int i = 0; i < 5; i++)
    {
        want[4 + i] = (uint8_t)(KS_ZERO_CTR1[i] ^ HELLO[i]);
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, ct, 9);

    // The same keystream read back the other way: the length field decodes on its own.
    TEST_ASSERT_EQUAL_UINT32(5u, protocore_chachapoly_get_length(key, 0, ct));

    uint8_t rt[4 + 5];
    TEST_ASSERT_TRUE(protocore_chachapoly_decrypt(key, 0, rt, ct, 5));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, rt, sizeof(pt));
}

// K_header = key[32..64] encrypts the length and K_main = key[0..32] the payload. Giving them the
// two different RFC 8439 A.1 keys makes the two halves draw on two different published keystreams,
// so a build that swapped the halves produces neither expected run.
void test_the_two_keys_are_not_interchangeable(void)
{
    uint8_t key[PROTOCORE_CHACHAPOLY_KEY_LEN];
    memset(key, 0, sizeof(key));
    key[31] = 0x01; // K_main = the Test Vector #3 key; K_header stays all-zero

    uint8_t pt[4 + 5], ct[4 + 5 + PROTOCORE_CHACHAPOLY_TAG_LEN];
    make_plaintext(pt, 5, HELLO);
    protocore_chachapoly_encrypt(key, 0, ct, pt, 5);

    uint8_t want[9];
    for (int i = 0; i < 4; i++)
    {
        want[i] = (uint8_t)(KS_ZERO_CTR0[i] ^ pt[i]); // header key still all-zero
    }
    for (int i = 0; i < 5; i++)
    {
        want[4 + i] = (uint8_t)(KS_ONE_CTR1[i] ^ HELLO[i]); // main key is now 0^31 || 01
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, ct, 9);
    TEST_ASSERT_EQUAL_UINT32(5u, protocore_chachapoly_get_length(key, 0, ct));
}

// The nonce is the sequence number, so the same packet under a different seqnr is a different
// ciphertext, and get_length reads the length only under the seqnr it was written with.
void test_sequence_number_is_the_nonce(void)
{
    uint8_t key[PROTOCORE_CHACHAPOLY_KEY_LEN];
    for (int i = 0; i < PROTOCORE_CHACHAPOLY_KEY_LEN; i++)
    {
        key[i] = (uint8_t)(i * 7 + 1);
    }
    uint8_t pt[4 + 5], c0[4 + 5 + 16], c1[4 + 5 + 16];
    make_plaintext(pt, 5, HELLO);
    protocore_chachapoly_encrypt(key, 0, c0, pt, 5);
    protocore_chachapoly_encrypt(key, 1, c1, pt, 5);
    TEST_ASSERT_TRUE(memcmp(c0, c1, sizeof(c0)) != 0);

    TEST_ASSERT_EQUAL_UINT32(5u, protocore_chachapoly_get_length(key, 0, c0));
    TEST_ASSERT_EQUAL_UINT32(5u, protocore_chachapoly_get_length(key, 1, c1));
    TEST_ASSERT_TRUE(protocore_chachapoly_get_length(key, 1, c0) != 5u);
}

// A payload spanning several ChaCha blocks round-trips, and the ciphertext is not the plaintext.
void test_multi_block_payload_round_trip(void)
{
    uint8_t key[PROTOCORE_CHACHAPOLY_KEY_LEN];
    for (int i = 0; i < PROTOCORE_CHACHAPOLY_KEY_LEN; i++)
    {
        key[i] = (uint8_t)(i * 5 + 9);
    }
    const uint32_t n = 200; // four ChaCha blocks and a partial one
    uint8_t payload[200];
    for (uint32_t i = 0; i < n; i++)
    {
        payload[i] = (uint8_t)(i ^ 0x5a);
    }
    uint8_t pt[4 + 200], ct[4 + 200 + 16], rt[4 + 200];
    make_plaintext(pt, n, payload);

    protocore_chachapoly_encrypt(key, 42, ct, pt, n);
    TEST_ASSERT_TRUE(memcmp(ct + 4, pt + 4, n) != 0);
    TEST_ASSERT_EQUAL_UINT32(n, protocore_chachapoly_get_length(key, 42, ct));
    TEST_ASSERT_TRUE(protocore_chachapoly_decrypt(key, 42, rt, ct, n));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, rt, 4 + n);
}

// The tag covers the encrypted length field and the encrypted payload, so a change to either, to
// the tag itself, or to the sequence number the tag key is derived from must fail the verification
// and produce no plaintext.
void test_decrypt_refuses_every_tampered_field(void)
{
    uint8_t key[PROTOCORE_CHACHAPOLY_KEY_LEN];
    for (int i = 0; i < PROTOCORE_CHACHAPOLY_KEY_LEN; i++)
    {
        key[i] = (uint8_t)(i + 3);
    }
    const uint32_t n = 16;
    uint8_t payload[16];
    for (uint32_t i = 0; i < n; i++)
    {
        payload[i] = (uint8_t)(0xA0 + i);
    }
    uint8_t pt[4 + 16], ct[4 + 16 + 16], rt[4 + 16];
    make_plaintext(pt, n, payload);
    protocore_chachapoly_encrypt(key, 0, ct, pt, n);

    memset(rt, 0xCC, sizeof(rt));
    ct[1] ^= 0x01; // the encrypted length field
    TEST_ASSERT_FALSE(protocore_chachapoly_decrypt(key, 0, rt, ct, n));
    ct[1] ^= 0x01;

    ct[6] ^= 0x01; // the encrypted payload
    TEST_ASSERT_FALSE(protocore_chachapoly_decrypt(key, 0, rt, ct, n));
    ct[6] ^= 0x01;

    ct[4 + n] ^= 0x80; // the tag
    TEST_ASSERT_FALSE(protocore_chachapoly_decrypt(key, 0, rt, ct, n));
    ct[4 + n] ^= 0x80;

    TEST_ASSERT_FALSE(protocore_chachapoly_decrypt(key, 1, rt, ct, n)); // wrong sequence number

    for (size_t i = 0; i < sizeof(rt); i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0xCC, rt[i]); // no refusal wrote plaintext
    }
    TEST_ASSERT_TRUE(protocore_chachapoly_decrypt(key, 0, rt, ct, n));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, rt, sizeof(pt));
}

// payload_len 0 still authenticates the 4-octet length field, which is what an SSH packet with no
// payload after the length reduces to.
void test_empty_payload(void)
{
    uint8_t key[PROTOCORE_CHACHAPOLY_KEY_LEN];
    for (int i = 0; i < PROTOCORE_CHACHAPOLY_KEY_LEN; i++)
    {
        key[i] = (uint8_t)(i * 5 + 9);
    }
    uint8_t pt[4] = {0, 0, 0, 0};
    uint8_t ct[4 + 16], rt[4];
    protocore_chachapoly_encrypt(key, 7, ct, pt, 0);

    TEST_ASSERT_EQUAL_UINT32(0u, protocore_chachapoly_get_length(key, 7, ct));
    TEST_ASSERT_TRUE(protocore_chachapoly_decrypt(key, 7, rt, ct, 0));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, rt, 4);

    ct[4] ^= 0x01;
    TEST_ASSERT_FALSE(protocore_chachapoly_decrypt(key, 7, rt, ct, 0));
}

// dest may alias src, which is how the SSH transport encrypts a packet in its own buffer.
void test_in_place_encrypt_and_decrypt(void)
{
    uint8_t key[PROTOCORE_CHACHAPOLY_KEY_LEN];
    for (int i = 0; i < PROTOCORE_CHACHAPOLY_KEY_LEN; i++)
    {
        key[i] = (uint8_t)(i * 3 + 11);
    }
    const uint32_t n = 64;
    uint8_t payload[64];
    for (uint32_t i = 0; i < n; i++)
    {
        payload[i] = (uint8_t)(i * 13 + 2);
    }
    uint8_t pt[4 + 64], sep[4 + 64 + 16], buf[4 + 64 + 16];
    make_plaintext(pt, n, payload);

    protocore_chachapoly_encrypt(key, 9, sep, pt, n);
    memcpy(buf, pt, 4 + n);
    protocore_chachapoly_encrypt(key, 9, buf, buf, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(sep, buf, 4 + n + 16);

    TEST_ASSERT_TRUE(protocore_chachapoly_decrypt(key, 9, buf, buf, n));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, buf, 4 + n);
}

// The length field is recoverable before the body has arrived, which is the whole reason it has its
// own key: a receiver reads four octets, sizes the packet, and only then reads the rest.
void test_length_is_readable_before_the_body(void)
{
    uint8_t key[PROTOCORE_CHACHAPOLY_KEY_LEN];
    for (int i = 0; i < PROTOCORE_CHACHAPOLY_KEY_LEN; i++)
    {
        key[i] = (uint8_t)(255 - i);
    }
    static const uint32_t LENGTHS[] = {0u, 1u, 16u, 255u, 65535u, 0x00FFFFFFu};
    for (size_t i = 0; i < sizeof(LENGTHS) / sizeof(LENGTHS[0]); i++)
    {
        uint8_t pt[4], ct[4 + 16], enc_len[4];
        pt[0] = (uint8_t)(LENGTHS[i] >> 24);
        pt[1] = (uint8_t)(LENGTHS[i] >> 16);
        pt[2] = (uint8_t)(LENGTHS[i] >> 8);
        pt[3] = (uint8_t)LENGTHS[i];
        protocore_chachapoly_encrypt(key, 5, ct, pt, 0);
        memcpy(enc_len, ct, 4);
        TEST_ASSERT_EQUAL_UINT32(LENGTHS[i], protocore_chachapoly_get_length(key, 5, enc_len));
    }
}
