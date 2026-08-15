// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for Streamlined NTRU Prime sntrup761 (crypto/pqc/sntrup761.h), the KEM behind
// sntrup761x25519-sha512@openssh.com.
//
// PROVENANCE: sntrup761 is not standardized by any body, so there is no published test vector to
// anchor on. The one conformance value here, sntrup761_kat.h, is an INTEROP vector captured from
// OpenSSH's embedded sntrup761 reference - the implementation OpenSSH's PROTOCOL.sntrup761x25519
// defines the wire against - and it pins Decaps: dec(SK, CT) must return exactly the secret that
// peer derived. Everything else in this file is a PROPERTY that must hold whatever the
// implementation, stated as such rather than as a standard's number: the KEM round trip, implicit
// rejection on a tampered ciphertext, the public key embedded in the secret key at the offset the
// header names, and the KeyGen retry when g is not invertible.

#include "crypto/pqc/sntrup761.h"
#include <string.h>

#include <unity.h>

#include "sntrup761_kat.h"

static uint8_t g_work[4096] __attribute__((aligned(8)));

// The CSPRNG seam sntrup761.c declares: a deterministic source so every draw here is reproducible.
// sntrup761 interop does not depend on how r and the keys are drawn, only that they are valid.
static uint32_t s_rng = 0xA5A5F00Du;

// KeyGen's g retry hook. While positive, protocore_rand_fill hands back bytes that drive
// Small_random's ternary map to coefficient 0 and counts down one per call; 0 everywhere else, so
// it is inert for the other cases.
static int s_force_zero_calls = 0;

void protocore_rand_fill(uint8_t *b, size_t n)
{
    if (s_force_zero_calls > 0)
    {
        --s_force_zero_calls;
        for (size_t i = 0; i < n; i++)
        {
            b[i] = 0;
        }
        if (n >= 4)
        {
            b[3] = 0x20; // u & 0x3fffffff == 0x20000000 -> (u30*3)>>30 == 1 -> ternary coefficient 0
        }
        return;
    }
    for (size_t i = 0; i < n; i++)
    {
        s_rng = s_rng * 1103515245u + 12345u;
        b[i] = (uint8_t)(s_rng >> 16);
    }
}

void setUp(void)
{
}
void tearDown(void)
{
}

// The interop vector: the shared secret an OpenSSH peer derived from this (SK, CT) pair. A wrong
// Rq/Rounded decode, a wrong reciprocal, or a wrong hash chain all land somewhere else.
void test_openssh_interop_decaps_vector(void)
{
    uint8_t ss[PROTOCORE_SNTRUP761_SS_BYTES];
    protocore_sntrup761_dec(g_work, KAT_SK, KAT_CT, ss);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(KAT_SS, ss, PROTOCORE_SNTRUP761_SS_BYTES);
}

// The sizes the header states are the encodings sntrup761 (p=761, q=4591, w=286) fixes, and the KAT
// arrays are exactly those widths - so a build that changed a parameter would fail to compile this
// rather than silently exchange a different KEM.
void test_encoding_sizes_match_the_vector(void)
{
    TEST_ASSERT_EQUAL_UINT((unsigned)PROTOCORE_SNTRUP761_SK_BYTES, (unsigned)sizeof(KAT_SK));
    TEST_ASSERT_EQUAL_UINT((unsigned)PROTOCORE_SNTRUP761_CT_BYTES, (unsigned)sizeof(KAT_CT));
    TEST_ASSERT_EQUAL_UINT((unsigned)PROTOCORE_SNTRUP761_SS_BYTES, (unsigned)sizeof(KAT_SS));
}

// KEM correctness: the responder's Encaps and the initiator's KeyGen + Decaps reach the same secret.
// Run over many keypairs so the g / f draw retries and the rounding both get more than one shape.
void test_round_trip_agrees_over_many_keypairs(void)
{
    for (int t = 0; t < 20; t++)
    {
        uint8_t pk[PROTOCORE_SNTRUP761_PK_BYTES], sk[PROTOCORE_SNTRUP761_SK_BYTES];
        uint8_t ct[PROTOCORE_SNTRUP761_CT_BYTES];
        uint8_t ss_enc[PROTOCORE_SNTRUP761_SS_BYTES], ss_dec[PROTOCORE_SNTRUP761_SS_BYTES];
        protocore_sntrup761_keypair(g_work, pk, sk);
        protocore_sntrup761_enc(g_work, pk, ct, ss_enc);
        protocore_sntrup761_dec(g_work, sk, ct, ss_dec);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(ss_enc, ss_dec, PROTOCORE_SNTRUP761_SS_BYTES);
    }
}

// Two Encaps against the same public key draw different r, so they must not produce the same
// ciphertext or the same secret - a KEM whose randomness did not reach the ciphertext would.
void test_encaps_is_randomized(void)
{
    uint8_t pk[PROTOCORE_SNTRUP761_PK_BYTES], sk[PROTOCORE_SNTRUP761_SK_BYTES];
    uint8_t c1[PROTOCORE_SNTRUP761_CT_BYTES], c2[PROTOCORE_SNTRUP761_CT_BYTES];
    uint8_t s1[PROTOCORE_SNTRUP761_SS_BYTES], s2[PROTOCORE_SNTRUP761_SS_BYTES];
    protocore_sntrup761_keypair(g_work, pk, sk);
    protocore_sntrup761_enc(g_work, pk, c1, s1);
    protocore_sntrup761_enc(g_work, pk, c2, s2);
    TEST_ASSERT_TRUE(memcmp(c1, c2, sizeof(c1)) != 0);
    TEST_ASSERT_TRUE(memcmp(s1, s2, sizeof(s1)) != 0);
}

// The header states the public key is embedded in sk at PROTOCORE_SNTRUP761_SK_PK_OFFSET so the
// initiator can rebuild its side of the exchange hash without storing pk twice. That copy has to be
// the pk KeyGen returned, byte for byte.
void test_secret_key_embeds_the_public_key(void)
{
    uint8_t pk[PROTOCORE_SNTRUP761_PK_BYTES], sk[PROTOCORE_SNTRUP761_SK_BYTES];
    protocore_sntrup761_keypair(g_work, pk, sk);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pk, sk + PROTOCORE_SNTRUP761_SK_PK_OFFSET, PROTOCORE_SNTRUP761_PK_BYTES);

    // ...and encapsulating against the embedded copy is the same operation as against pk itself.
    uint8_t c1[PROTOCORE_SNTRUP761_CT_BYTES], c2[PROTOCORE_SNTRUP761_CT_BYTES];
    uint8_t s1[PROTOCORE_SNTRUP761_SS_BYTES], s2[PROTOCORE_SNTRUP761_SS_BYTES];
    s_rng = 0x1234ABCDu;
    protocore_sntrup761_enc(g_work, pk, c1, s1);
    s_rng = 0x1234ABCDu;
    protocore_sntrup761_enc(g_work, sk + PROTOCORE_SNTRUP761_SK_PK_OFFSET, c2, s2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(c1, c2, sizeof(c1));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(s1, s2, sizeof(s1));
}

// Implicit rejection: a tampered ciphertext must yield a deterministic secret that is not the real
// one, at either end of the ciphertext - the rounded polynomial and the 32-octet confirmation hash
// are separate fields and both are covered.
void test_tampered_ciphertext_implicitly_rejects(void)
{
    uint8_t pk[PROTOCORE_SNTRUP761_PK_BYTES], sk[PROTOCORE_SNTRUP761_SK_BYTES];
    uint8_t ct[PROTOCORE_SNTRUP761_CT_BYTES];
    uint8_t good[PROTOCORE_SNTRUP761_SS_BYTES], r1[PROTOCORE_SNTRUP761_SS_BYTES], r2[PROTOCORE_SNTRUP761_SS_BYTES];
    protocore_sntrup761_keypair(g_work, pk, sk);
    protocore_sntrup761_enc(g_work, pk, ct, good);

    ct[0] ^= 0xFF; // the Rounded-encoded polynomial
    protocore_sntrup761_dec(g_work, sk, ct, r1);
    protocore_sntrup761_dec(g_work, sk, ct, r2);
    TEST_ASSERT_TRUE(memcmp(good, r1, sizeof(good)) != 0);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(r1, r2, sizeof(r1)); // deterministic, not random
    ct[0] ^= 0xFF;

    ct[PROTOCORE_SNTRUP761_CT_BYTES - 1] ^= 0x01; // the trailing Confirm hash
    protocore_sntrup761_dec(g_work, sk, ct, r2);
    TEST_ASSERT_TRUE(memcmp(good, r2, sizeof(good)) != 0);
    TEST_ASSERT_TRUE(memcmp(r1, r2, sizeof(r1)) != 0);
    ct[PROTOCORE_SNTRUP761_CT_BYTES - 1] ^= 0x01;

    protocore_sntrup761_dec(g_work, sk, ct, r1); // restored: the real secret comes back
    TEST_ASSERT_EQUAL_HEX8_ARRAY(good, r1, sizeof(good));
}

// KeyGen redraws g while it is not invertible mod 3. The all-zero polynomial never is, so forcing
// the first p=761 draws to zero takes that retry arm deterministically; KeyGen then draws from the
// normal stream and the keypair it produces still round-trips.
void test_keygen_retries_a_noninvertible_g(void)
{
    s_force_zero_calls = 761; // p = 761: every coefficient of the first g is forced to 0
    uint8_t pk[PROTOCORE_SNTRUP761_PK_BYTES], sk[PROTOCORE_SNTRUP761_SK_BYTES];
    uint8_t ct[PROTOCORE_SNTRUP761_CT_BYTES];
    uint8_t ss_enc[PROTOCORE_SNTRUP761_SS_BYTES], ss_dec[PROTOCORE_SNTRUP761_SS_BYTES];
    protocore_sntrup761_keypair(g_work, pk, sk);
    TEST_ASSERT_EQUAL_INT(0, s_force_zero_calls); // the forced draw really was consumed
    protocore_sntrup761_enc(g_work, pk, ct, ss_enc);
    protocore_sntrup761_dec(g_work, sk, ct, ss_dec);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ss_enc, ss_dec, PROTOCORE_SNTRUP761_SS_BYTES);
}
