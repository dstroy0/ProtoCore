// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The 2048-bit big-integer layer (crypto/asymmetric/bignum.h): the RFC 3526 group-14 constants, the
// big-endian conversions, the ordering entries, and the modular exponentiation the linked backend
// answers.
//
// The prime and the generator are checked against RFC 3526 section 3 as printed, transcribed here and
// nowhere else in this file. The modexp residues come from bignum_kat_data.inc, compiled from
// test/vectors/rfc3526_group14_modexp.json - the RFC publishes no residues, so those were computed by
// CPython's pow() over the same transcribed prime, outside this tree.
//
// The same suite runs on both arms of the backend seam: native_bignum_group14 links the portable
// software Montgomery backend, native_bignum_group14_hw links the accelerated one against the HAL's
// host arm of the RSA/MPI MODMULT. bn_expmod_group14 has exactly one definition in a build and the
// vectors are the same either way, which is what makes PROTOCORE_HAS_HW_BIGNUM a capability that runs
// natively rather than one only silicon can answer.

#include "crypto/asymmetric/bignum/bignum.h"
#include "mmgr/secure/secure.h"
#include <string.h>

#include <unity.h>

typedef struct
{
    int tc;
    const char *base;
    const char *exp;
    const char *out;
    const char *comment;
} KatGroup14;

#include "bignum_kat_data.inc"

#define ROWS(a) (sizeof(a) / sizeof((a)[0]))
#define BN_BYTES 256

// RFC 3526 section 3, as printed. The generator is 2.
static const char RFC3526_GROUP14_P[] = "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74"
                                        "020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F1437"
                                        "4FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
                                        "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF05"
                                        "98DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB"
                                        "9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
                                        "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF695581718"
                                        "3995497CEA956AE515D2261898FA051015728E5A8AACAA68FFFFFFFFFFFFFFFF";

static uint8_t g_work[PROTOCORE_BIGNUM_BORROW] __attribute__((aligned(8)));

void setUp(void)
{
    protocore_secure_reset();
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

static void read_bn(protocore_bignum *out, const char *hex)
{
    uint8_t raw[BN_BYTES];
    size_t n = unhex(hex, raw);
    TEST_ASSERT_EQUAL_UINT(BN_BYTES, (unsigned)n);
    Bignum.from_bytes_args.out = out;
    Bignum.from_bytes_args.bytes = raw;
    Bignum.from_bytes_args.len = n;
    Bignum.from_bytes(g_work);
    TEST_ASSERT_TRUE(Bignum.ok);
}

static void write_bn(uint8_t out[BN_BYTES], const protocore_bignum *v)
{
    Bignum.to_bytes_args.bytes = out;
    Bignum.to_bytes_args.in = v;
    Bignum.to_bytes(g_work);
    TEST_ASSERT_TRUE(Bignum.ok);
}

// ---- the group constants ---------------------------------------------------

void test_group14_prime_and_generator_match_rfc3526(void)
{
    uint8_t want[BN_BYTES];
    uint8_t got[BN_BYTES];
    TEST_ASSERT_EQUAL_UINT(BN_BYTES, (unsigned)unhex(RFC3526_GROUP14_P, want));
    write_bn(got, &group14_p);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, BN_BYTES);

    write_bn(got, &group14_g);
    for (size_t i = 0; i < BN_BYTES - 1u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, got[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0x02, got[BN_BYTES - 1u]);
}

// ---- the conversions -------------------------------------------------------

void test_from_bytes_and_to_bytes_are_inverses(void)
{
    for (size_t i = 0; i < ROWS(KAT_GROUP14); i++)
    {
        uint8_t want[BN_BYTES];
        uint8_t got[BN_BYTES];
        protocore_bignum v;
        unhex(KAT_GROUP14[i].exp, want);
        read_bn(&v, KAT_GROUP14[i].exp);
        write_bn(got, &v);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, BN_BYTES, KAT_GROUP14[i].comment);
    }
}

// A source shorter than the full width lands in the low limbs and zeroes the rest; a source longer
// than the full width keeps its low 256 octets.
void test_from_bytes_handles_a_short_and_a_long_source(void)
{
    protocore_bignum v;
    uint8_t got[BN_BYTES];
    const uint8_t four[4] = {0x01, 0x02, 0x03, 0x04};

    Bignum.from_bytes_args.out = &v;
    Bignum.from_bytes_args.bytes = four;
    Bignum.from_bytes_args.len = sizeof(four);
    Bignum.from_bytes(g_work);
    TEST_ASSERT_TRUE(Bignum.ok);
    write_bn(got, &v);
    for (size_t i = 0; i < BN_BYTES - 4u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, got[i]);
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(four, got + BN_BYTES - 4u, 4);

    uint8_t wide[BN_BYTES + 8];
    memset(wide, 0xAA, sizeof(wide));
    memset(wide + 8, 0x5A, BN_BYTES);
    Bignum.from_bytes_args.out = &v;
    Bignum.from_bytes_args.bytes = wide;
    Bignum.from_bytes_args.len = sizeof(wide);
    Bignum.from_bytes(g_work);
    TEST_ASSERT_TRUE(Bignum.ok);
    write_bn(got, &v);
    for (size_t i = 0; i < BN_BYTES; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x5A, got[i]);
    }
}

// ---- ordering and zero -----------------------------------------------------

void test_cmp_orders_two_values_and_cmp_raw_spans_the_stated_limbs(void)
{
    protocore_bignum a;
    protocore_bignum b;
    read_bn(&a, KAT_GROUP14[0].base); // 2
    read_bn(&b, KAT_GROUP14[2].exp);  // p-1

    Bignum.cmp_args.a = &a;
    Bignum.cmp_args.b = &b;
    Bignum.cmp(g_work);
    TEST_ASSERT_TRUE(Bignum.ok);
    TEST_ASSERT_EQUAL_INT(-1, Bignum.sign);

    Bignum.cmp_args.a = &b;
    Bignum.cmp_args.b = &a;
    Bignum.cmp(g_work);
    TEST_ASSERT_EQUAL_INT(1, Bignum.sign);

    Bignum.cmp_args.a = &a;
    Bignum.cmp_args.b = &a;
    Bignum.cmp(g_work);
    TEST_ASSERT_EQUAL_INT(0, Bignum.sign);

    // over one limb the two agree, because they differ only above it
    Bignum.cmp_raw_args.a = a.d;
    Bignum.cmp_raw_args.b = b.d;
    Bignum.cmp_raw_args.n = PROTOCORE_BN_LIMBS;
    Bignum.cmp_raw(g_work);
    TEST_ASSERT_EQUAL_INT(-1, Bignum.sign);
}

void test_is_zero_finds_every_limb_zero(void)
{
    protocore_bignum v;
    memset(v.d, 0, sizeof(v.d));
    Bignum.is_zero_args.a = &v;
    Bignum.is_zero(g_work);
    TEST_ASSERT_TRUE(Bignum.ok);
    TEST_ASSERT_TRUE(Bignum.zero);

    // one bit anywhere in the width is enough to make it non-zero
    for (int i = 0; i < PROTOCORE_BN_LIMBS; i++)
    {
        memset(v.d, 0, sizeof(v.d));
        v.d[i] = 1u;
        Bignum.is_zero(g_work);
        TEST_ASSERT_TRUE(Bignum.ok);
        TEST_ASSERT_FALSE(Bignum.zero);
    }
}

// ---- RFC 4253 section 8 ----------------------------------------------------

void test_dh_validate_accepts_only_values_strictly_between_one_and_p_minus_one(void)
{
    protocore_bignum v;
    memset(v.d, 0, sizeof(v.d));
    Bignum.validate_args.v = &v;

    v.d[0] = 0u; // 0
    Bignum.dh_validate(g_work);
    TEST_ASSERT_FALSE(Bignum.ok);

    v.d[0] = 1u; // 1
    Bignum.dh_validate(g_work);
    TEST_ASSERT_FALSE(Bignum.ok);

    v.d[0] = 2u; // the generator
    Bignum.dh_validate(g_work);
    TEST_ASSERT_TRUE(Bignum.ok);

    v = group14_p; // p
    Bignum.dh_validate(g_work);
    TEST_ASSERT_FALSE(Bignum.ok);

    v = group14_p;
    v.d[0]--; // p-1
    Bignum.dh_validate(g_work);
    TEST_ASSERT_FALSE(Bignum.ok);

    v = group14_p;
    v.d[0] -= 2u; // p-2, the largest accepted value
    Bignum.dh_validate(g_work);
    TEST_ASSERT_TRUE(Bignum.ok);
}

// ---- the modexp, on whichever backend the build linked ---------------------

void test_expmod_group14_matches_the_vectors(void)
{
    for (size_t i = 0; i < ROWS(KAT_GROUP14); i++)
    {
        const KatGroup14 *v = &KAT_GROUP14[i];
        protocore_bignum base;
        protocore_bignum exp;
        protocore_bignum out;
        uint8_t want[BN_BYTES];
        uint8_t got[BN_BYTES];
        read_bn(&base, v->base);
        read_bn(&exp, v->exp);
        unhex(v->out, want);

        Bignum.expmod_args.out = &out;
        Bignum.expmod_args.base = &base;
        Bignum.expmod_args.exp = &exp;
        Bignum.expmod_group14(g_work);
        TEST_ASSERT_TRUE_MESSAGE(Bignum.ok, v->comment);

        write_bn(got, &out);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, BN_BYTES, v->comment);
    }
}

// The last two rows are the two halves of one exchange, so their results must be the same secret.
// A backend that is merely self-consistent still fails this if its ladder drops an exponent bit.
void test_the_two_diffie_hellman_halves_agree(void)
{
    const size_t n = ROWS(KAT_GROUP14);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(2u, (unsigned)n);
    uint8_t a[BN_BYTES];
    uint8_t b[BN_BYTES];
    unhex(KAT_GROUP14[n - 2].out, a);
    unhex(KAT_GROUP14[n - 1].out, b);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, b, BN_BYTES);
}

void test_every_entry_refuses_a_null_operand(void)
{
    protocore_bignum v;
    memset(v.d, 0, sizeof(v.d));
    uint8_t bytes[BN_BYTES];

    Bignum.from_bytes_args.out = NULL;
    Bignum.from_bytes_args.bytes = bytes;
    Bignum.from_bytes_args.len = BN_BYTES;
    Bignum.from_bytes(g_work);
    TEST_ASSERT_FALSE(Bignum.ok);

    Bignum.to_bytes_args.bytes = NULL;
    Bignum.to_bytes_args.in = &v;
    Bignum.to_bytes(g_work);
    TEST_ASSERT_FALSE(Bignum.ok);

    Bignum.cmp_args.a = NULL;
    Bignum.cmp_args.b = &v;
    Bignum.cmp(g_work);
    TEST_ASSERT_FALSE(Bignum.ok);

    Bignum.cmp_raw_args.a = NULL;
    Bignum.cmp_raw_args.b = v.d;
    Bignum.cmp_raw_args.n = PROTOCORE_BN_LIMBS;
    Bignum.cmp_raw(g_work);
    TEST_ASSERT_FALSE(Bignum.ok);

    Bignum.is_zero_args.a = NULL;
    Bignum.is_zero(g_work);
    TEST_ASSERT_FALSE(Bignum.ok);

    Bignum.expmod_args.out = NULL;
    Bignum.expmod_args.base = &v;
    Bignum.expmod_args.exp = &v;
    Bignum.expmod_group14(g_work);
    TEST_ASSERT_FALSE(Bignum.ok);

    Bignum.validate_args.v = NULL;
    Bignum.dh_validate(g_work);
    TEST_ASSERT_FALSE(Bignum.ok);
}

void test_vector_table_is_populated(void)
{
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_GROUP14));
}
