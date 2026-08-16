// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for ML-KEM-768 (crypto/pqc/mlkem.h), the post-quantum half of the
// mlkem768x25519-sha256 (SSH) and X25519MLKEM768 (TLS 1.3) hybrid key exchanges.
//
// The load-bearing cases are the three NIST ACVP known-answer vectors in mlkem_acvp_kat.h - KeyGen,
// Encaps and Decaps, each copied verbatim from NIST's published FIPS 203 vector set. All three
// entry points here are the derandomized "internal" forms, so each is a pure function of its inputs
// and the whole of it is pinned: KeyGen's (ek, dk) from (d, z), Encaps's (c, K) from (ek, m), and
// Decaps's K from (dk, c). A round trip alone would agree with a wrong NTT or a wrong compression
// as long as both halves were wrong the same way; these do not.

#include "crypto/pqc/mlkem.h"
#include <string.h>

#include <unity.h>

#include "mlkem_acvp_kat.h"

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t nib(char c)
{
    return (uint8_t)(c <= '9' ? c - '0' : ((c | 0x20) - 'a' + 10));
}

static size_t unhex(const char *h, uint8_t *out)
{
    size_t n = 0;
    for (; h[0] && h[1]; h += 2)
    {
        out[n++] = (uint8_t)((nib(h[0]) << 4) | nib(h[1]));
    }
    return n;
}

// The bytes the entries run out of: ML-KEM's borrow is the region its SHA-3 sponge runs in.
static uint8_t g_ws[PROTOCORE_MLKEM_BORROW] __attribute__((aligned(8)));

// The namespace, called the way the vectors below read: operands in, one call, answer out.
static void mlkem_keygen(const uint8_t *d, const uint8_t *z, uint8_t *ek, uint8_t *dk)
{
    MlKem.keygen_args.d = d;
    MlKem.keygen_args.z = z;
    MlKem.keygen_args.ek = ek;
    MlKem.keygen_args.dk = dk;
    MlKem.keygen(g_ws);
}

static proto_bool mlkem_encaps(const uint8_t *ek, const uint8_t *m, uint8_t *ct, uint8_t *ss)
{
    MlKem.encaps_args.ek = ek;
    MlKem.encaps_args.m = m;
    MlKem.encaps_args.ct = ct;
    MlKem.encaps_args.ss = ss;
    MlKem.encaps(g_ws);
    return MlKem.ok;
}

static void mlkem_decaps(const uint8_t *dk, const uint8_t *ct, uint8_t *ss)
{
    MlKem.decaps_args.dk = dk;
    MlKem.decaps_args.ct = ct;
    MlKem.decaps_args.ss = ss;
    MlKem.decaps(g_ws);
}

static uint8_t g_ek[MLKEM768_EK_BYTES];
static uint8_t g_dk[MLKEM768_DK_BYTES];
static uint8_t g_ct[MLKEM768_CT_BYTES];
static uint8_t g_ss[MLKEM768_SS_BYTES];
static uint8_t g_want_ek[MLKEM768_EK_BYTES];
static uint8_t g_want_dk[MLKEM768_DK_BYTES];
static uint8_t g_want_ct[MLKEM768_CT_BYTES];
static uint8_t g_want_ss[MLKEM768_SS_BYTES];

// FIPS 203 Table 2 fixes the ML-KEM-768 encodings: ek 384k+32 = 1184, dk 768k+96 = 2400,
// ct 32(du*k + dv) = 32(10*3 + 4) = 1088, shared secret 32, all with k = 3, du = 10, dv = 4.
void test_fips203_encoding_sizes(void)
{
    TEST_ASSERT_EQUAL_UINT(1184u, (unsigned)MLKEM768_EK_BYTES);
    TEST_ASSERT_EQUAL_UINT(2400u, (unsigned)MLKEM768_DK_BYTES);
    TEST_ASSERT_EQUAL_UINT(1088u, (unsigned)MLKEM768_CT_BYTES);
    TEST_ASSERT_EQUAL_UINT(32u, (unsigned)MLKEM768_SS_BYTES);
    TEST_ASSERT_EQUAL_UINT(32u, (unsigned)MLKEM768_MSG_BYTES);
}

// ACVP keyGen: the two 32-octet seeds produce exactly the published key pair.
void test_acvp_keygen(void)
{
    uint8_t d[32], z[32];
    unhex(ACVP_KEYGEN_D, d);
    unhex(ACVP_KEYGEN_Z, z);
    TEST_ASSERT_EQUAL_UINT((size_t)MLKEM768_EK_BYTES, unhex(ACVP_KEYGEN_EK, g_want_ek));
    TEST_ASSERT_EQUAL_UINT((size_t)MLKEM768_DK_BYTES, unhex(ACVP_KEYGEN_DK, g_want_dk));

    mlkem_keygen(d, z, g_ek, g_dk);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_want_ek, g_ek, MLKEM768_EK_BYTES);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_want_dk, g_dk, MLKEM768_DK_BYTES);
}

// FIPS 203 sec 7.1: dk carries ek and H(ek) inside it, so the key pair is self-describing and
// Decaps needs no other state. The embedded copy must be byte-identical to the published ek.
void test_decapsulation_key_embeds_the_encapsulation_key(void)
{
    uint8_t d[32], z[32];
    unhex(ACVP_KEYGEN_D, d);
    unhex(ACVP_KEYGEN_Z, z);
    mlkem_keygen(d, z, g_ek, g_dk);
    // dk = dk_PKE (384k = 1152) || ek (1184) || H(ek) (32) || z (32)
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_ek, g_dk + 1152, MLKEM768_EK_BYTES);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(z, g_dk + MLKEM768_DK_BYTES - 32, 32);
}

// ACVP encapsulation AFT: the published (ek, m) produce exactly the published (c, K).
void test_acvp_encaps(void)
{
    uint8_t m[32];
    TEST_ASSERT_EQUAL_UINT((size_t)MLKEM768_EK_BYTES, unhex(ACVP_ENCAPS_EK, g_ek));
    unhex(ACVP_ENCAPS_M, m);
    TEST_ASSERT_EQUAL_UINT((size_t)MLKEM768_CT_BYTES, unhex(ACVP_ENCAPS_C, g_want_ct));
    unhex(ACVP_ENCAPS_K, g_want_ss);

    TEST_ASSERT_TRUE(mlkem_encaps(g_ek, m, g_ct, g_ss));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_want_ct, g_ct, MLKEM768_CT_BYTES);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_want_ss, g_ss, MLKEM768_SS_BYTES);
}

// ACVP decapsulation VAL: the published (dk, c) produce exactly the published K.
void test_acvp_decaps(void)
{
    TEST_ASSERT_EQUAL_UINT((size_t)MLKEM768_DK_BYTES, unhex(ACVP_DECAPS_DK, g_dk));
    TEST_ASSERT_EQUAL_UINT((size_t)MLKEM768_CT_BYTES, unhex(ACVP_DECAPS_C, g_ct));
    unhex(ACVP_DECAPS_K, g_want_ss);

    mlkem_decaps(g_dk, g_ct, g_ss);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_want_ss, g_ss, MLKEM768_SS_BYTES);
}

// KEM correctness: with the published key pair, whatever message the responder encapsulates, the
// initiator's Decaps recovers the same secret. Run over several messages so the compression rounding
// is exercised at more than one point.
void test_encaps_decaps_agree(void)
{
    uint8_t d[32], z[32], m[32], back[MLKEM768_SS_BYTES];
    unhex(ACVP_KEYGEN_D, d);
    unhex(ACVP_KEYGEN_Z, z);
    mlkem_keygen(d, z, g_ek, g_dk);

    for (int round = 0; round < 4; round++)
    {
        for (int i = 0; i < 32; i++)
        {
            m[i] = (uint8_t)(i * 31 + round * 7 + 1);
        }
        TEST_ASSERT_TRUE(mlkem_encaps(g_ek, m, g_ct, g_ss));
        mlkem_decaps(g_dk, g_ct, back);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(g_ss, back, MLKEM768_SS_BYTES);
    }
}

// FIPS 203 sec 6.3: Decaps never reports failure. A tampered ciphertext yields the implicit-reject
// key J(z || c) instead, which must differ from the real secret, must be a deterministic function of
// the tampered ciphertext, and must move when the tampering moves.
void test_tampered_ciphertext_implicitly_rejects(void)
{
    uint8_t d[32], z[32], m[32], real[32], rej1[32], rej2[32], rej3[32];
    unhex(ACVP_KEYGEN_D, d);
    unhex(ACVP_KEYGEN_Z, z);
    mlkem_keygen(d, z, g_ek, g_dk);
    for (int i = 0; i < 32; i++)
    {
        m[i] = (uint8_t)(i + 1);
    }
    TEST_ASSERT_TRUE(mlkem_encaps(g_ek, m, g_ct, real));

    g_ct[0] ^= 0x01;
    mlkem_decaps(g_dk, g_ct, rej1);
    mlkem_decaps(g_dk, g_ct, rej2);
    TEST_ASSERT_TRUE(memcmp(real, rej1, 32) != 0); // not the real key
    TEST_ASSERT_EQUAL_HEX8_ARRAY(rej1, rej2, 32);  // deterministic
    g_ct[0] ^= 0x01;

    g_ct[MLKEM768_CT_BYTES - 1] ^= 0x80;
    mlkem_decaps(g_dk, g_ct, rej3);
    TEST_ASSERT_TRUE(memcmp(real, rej3, 32) != 0);
    TEST_ASSERT_TRUE(memcmp(rej1, rej3, 32) != 0); // a different ciphertext, a different reject key
    g_ct[MLKEM768_CT_BYTES - 1] ^= 0x80;

    mlkem_decaps(g_dk, g_ct, rej1); // restored: the real key comes back
    TEST_ASSERT_EQUAL_HEX8_ARRAY(real, rej1, 32);
}

// FIPS 203 sec 7.2 input check: every coefficient decoded from ek must be below q = 3329, so an ek
// whose 12-bit words encode 3329 or above is refused rather than used. Setting the first two
// encoded words to 0xFFF (4095) breaks that check while leaving the length right.
void test_encaps_refuses_a_malformed_encapsulation_key(void)
{
    uint8_t m[32];
    unhex(ACVP_ENCAPS_EK, g_ek);
    unhex(ACVP_ENCAPS_M, m);
    TEST_ASSERT_TRUE(mlkem_encaps(g_ek, m, g_ct, g_ss)); // sanity: the real key works

    memset(g_ct, 0xCC, sizeof(g_ct));
    memset(g_ss, 0xCC, sizeof(g_ss));
    g_ek[0] = 0xFF;
    g_ek[1] = 0xFF;
    g_ek[2] = 0xFF; // two packed 12-bit coefficients, both 0xFFF, both >= q
    TEST_ASSERT_FALSE(mlkem_encaps(g_ek, m, g_ct, g_ss));
    for (size_t i = 0; i < sizeof(g_ss); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xCC, g_ss[i]); // nothing written on refusal
    }
}

// A one-octet change to the seed changes the whole key pair: the seeds are not decoration.
void test_seeds_determine_the_key_pair(void)
{
    uint8_t d[32], z[32], ek2[MLKEM768_EK_BYTES], dk2[MLKEM768_DK_BYTES];
    unhex(ACVP_KEYGEN_D, d);
    unhex(ACVP_KEYGEN_Z, z);
    mlkem_keygen(d, z, g_ek, g_dk);

    d[0] ^= 0x01;
    mlkem_keygen(d, z, ek2, dk2);
    TEST_ASSERT_TRUE(memcmp(g_ek, ek2, MLKEM768_EK_BYTES) != 0);
    d[0] ^= 0x01;

    // z only feeds the implicit-reject value, so ek is unchanged and only dk's tail moves.
    z[0] ^= 0x01;
    mlkem_keygen(d, z, ek2, dk2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_ek, ek2, MLKEM768_EK_BYTES);
    TEST_ASSERT_TRUE(memcmp(g_dk, dk2, MLKEM768_DK_BYTES) != 0);
}
