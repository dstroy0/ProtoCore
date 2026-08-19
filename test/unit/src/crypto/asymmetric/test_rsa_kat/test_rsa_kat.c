// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Data-driven external known-answer tests for RSASSA-PKCS1-v1.5 over RSA-2048 (crypto/asymmetric/rsa.h).
//
// Every expected value comes from rsa_kat_data.inc, which is compiled from the auditable JSON under
// test/vectors by tools/crypto/gen_crypto_vectors.py. Verify is driven onto Project Wycheproof's
// rsa_signature_2048_sha256 / _sha512 files at a pinned commit; sign is driven onto signatures
// openssl produced over a throwaway key whose primes the vector file publishes. Nothing here was
// produced by this tree.
//
// The load-bearing part is Wycheproof's adversarial half. PKCS#1 v1.5 verification is a byte-exact
// comparison against a canonical block, and the invalid rows are exactly the encodings that survive a
// lax parser: BER-encoded padding lengths, extra or altered ASN.1 in the DigestInfo, a missing NULL
// parameter, short padding, a signature raised past the modulus. An implementation that parses the
// recovered block instead of rebuilding it passes a round trip and accepts all of them.
//
// The same suite runs on both arms of the modular multiply: native_rsa_kat compiles the software
// schoolbook product, native_rsa_kat_hw compiles the accelerator path against the HAL's host arm, and
// the vectors are the same either way because the arms answer the same contract.

#include "crypto/asymmetric/rsa/rsa.h"
#include <string.h>

#include <unity.h>

// --- Vector table row layouts (rsa_kat_data.inc initializes these) ----------
typedef struct
{
    int tc;
    const char *n;
    const char *e;
    const char *msg;
    const char *sig;
    int valid;
} KatRsaVerify;
typedef struct
{
    int tc;
    const char *hash;
    const char *n;
    const char *e;
    const char *d;
    const char *msg;
    const char *sig;
} KatRsaSign;

#include "rsa_kat_data.inc"

#define ROWS(a) (sizeof(a) / sizeof((a)[0]))
#define MAXMSG 512 // the widest message in the tables: a 256-octet sign vector

static uint8_t g_work[PROTOCORE_RSA_BORROW] __attribute__((aligned(8)));

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

// Wycheproof prints the modulus as a DER INTEGER, so a 2048-bit one arrives with the sign byte in
// front. The primitive takes exactly PROTOCORE_RSA_KEY_BYTES, so the leading zeroes come off.
static void unhex_modulus(const char *h, uint8_t out[PROTOCORE_RSA_KEY_BYTES])
{
    uint8_t raw[PROTOCORE_RSA_KEY_BYTES + 8];
    size_t n = unhex(h, raw);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(PROTOCORE_RSA_KEY_BYTES, (unsigned)n);
    memcpy(out, raw + (n - PROTOCORE_RSA_KEY_BYTES), PROTOCORE_RSA_KEY_BYTES);
}

// The public exponent is four big-endian octets here and one to three hex octets in the tables.
static void unhex_exponent(const char *h, uint8_t out[4])
{
    uint8_t raw[8];
    size_t n = unhex(h, raw);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(4u, (unsigned)n);
    memset(out, 0, 4);
    memcpy(out + (4u - n), raw, n);
}

static proto_bool verify_row(const KatRsaVerify *v, protocore_rsa_hash alg, const uint8_t *msg, size_t msg_len)
{
    uint8_t n[PROTOCORE_RSA_KEY_BYTES];
    uint8_t e[4];
    uint8_t sig[PROTOCORE_RSA_KEY_BYTES + 8];
    unhex_modulus(v->n, n);
    unhex_exponent(v->e, e);
    size_t sig_len = unhex(v->sig, sig);

    Rsa.verify_args.n = n;
    Rsa.verify_args.e = e;
    Rsa.verify_args.msg = msg;
    Rsa.verify_args.msg_len = msg_len;
    Rsa.verify_args.sig = sig;
    Rsa.verify_args.sig_len = sig_len;
    Rsa.verify_args.hash = alg;
    Rsa.verify(g_work);
    return Rsa.ok;
}

// ---- verify (Project Wycheproof) ------------------------------------------
// A valid row must verify; every other row must not. The invalid half is the whole point: those are
// the blocks a hostile peer sends, and each one differs from the canonical encoding by construction.
static void run_verify(const KatRsaVerify *rows, size_t count, protocore_rsa_hash alg)
{
    for (size_t i = 0; i < count; i++)
    {
        const KatRsaVerify *v = &rows[i];
        uint8_t msg[MAXMSG];
        size_t msg_len = unhex(v->msg, msg);
        const proto_bool got = verify_row(v, alg, msg, msg_len);
        if (v->valid)
        {
            TEST_ASSERT_TRUE_MESSAGE(got, v->sig);
        }
        else
        {
            TEST_ASSERT_FALSE_MESSAGE(got, v->sig);
        }
    }
}

void test_rsa_verify_sha256_wycheproof(void)
{
    run_verify(KAT_RSA_SHA256, ROWS(KAT_RSA_SHA256), PROTOCORE_RSA_HASH_SHA256);
}

void test_rsa_verify_sha512_wycheproof(void)
{
    run_verify(KAT_RSA_SHA512, ROWS(KAT_RSA_SHA512), PROTOCORE_RSA_HASH_SHA512);
}

// RSASSA-PSS (RFC 8017 sec 8.1.2): openssl signed these, and PSS draws a random salt, so nothing
// here could have been produced by recomputing the encoding - only a verifier can check them.
void test_rsa_verify_pss_sha256_openssl(void)
{
    run_verify(KAT_RSA_PSS, ROWS(KAT_RSA_PSS), PROTOCORE_RSA_HASH_PSS_SHA256);
}

// The padding mode is not a label on the block: a PSS signature must not verify as PKCS#1 v1.5, and
// a v1.5 signature must not verify as PSS.
void test_the_two_padding_modes_do_not_accept_each_other(void)
{
    for (size_t i = 0; i < ROWS(KAT_RSA_PSS); i++)
    {
        const KatRsaVerify *v = &KAT_RSA_PSS[i];
        if (!v->valid)
        {
            continue;
        }
        uint8_t msg[MAXMSG];
        const size_t msg_len = unhex(v->msg, msg);
        TEST_ASSERT_FALSE_MESSAGE(verify_row(v, PROTOCORE_RSA_HASH_SHA256, msg, msg_len),
                                  "a PSS signature verified as PKCS#1 v1.5");
    }
    for (size_t i = 0; i < ROWS(KAT_RSA_SHA256); i++)
    {
        const KatRsaVerify *v = &KAT_RSA_SHA256[i];
        if (!v->valid)
        {
            continue;
        }
        uint8_t msg[MAXMSG];
        const size_t msg_len = unhex(v->msg, msg);
        TEST_ASSERT_FALSE_MESSAGE(verify_row(v, PROTOCORE_RSA_HASH_PSS_SHA256, msg, msg_len),
                                  "a PKCS#1 v1.5 signature verified as PSS");
    }
}

// A SHA-256 row presented as SHA-512 recovers the same block against a different expected one, so
// every valid vector must stop verifying when the digest is swapped.
void test_rsa_verify_rejects_the_wrong_digest(void)
{
    size_t checked = 0;
    for (size_t i = 0; i < ROWS(KAT_RSA_SHA256); i++)
    {
        const KatRsaVerify *v = &KAT_RSA_SHA256[i];
        if (!v->valid)
        {
            continue;
        }
        uint8_t msg[MAXMSG];
        size_t msg_len = unhex(v->msg, msg);
        TEST_ASSERT_TRUE(verify_row(v, PROTOCORE_RSA_HASH_SHA256, msg, msg_len));
        TEST_ASSERT_FALSE(verify_row(v, PROTOCORE_RSA_HASH_SHA512, msg, msg_len));
        checked++;
    }
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)checked);
}

// One flipped message octet must break every valid signature.
void test_rsa_verify_rejects_a_tampered_message(void)
{
    size_t checked = 0;
    for (size_t i = 0; i < ROWS(KAT_RSA_SHA256); i++)
    {
        const KatRsaVerify *v = &KAT_RSA_SHA256[i];
        uint8_t msg[MAXMSG];
        size_t msg_len = unhex(v->msg, msg);
        if (!v->valid || msg_len == 0)
        {
            continue;
        }
        msg[msg_len - 1] ^= 0x01;
        TEST_ASSERT_FALSE_MESSAGE(verify_row(v, PROTOCORE_RSA_HASH_SHA256, msg, msg_len), v->sig);
        checked++;
    }
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)checked);
}

// RFC 8017 sec 5.2.2: a signature representative out of range is an error, not a value to reduce.
// s + n is congruent to s mod n, so a lax implementation recovers the same block and accepts it.
void test_rsa_verify_rejects_a_signature_at_or_above_the_modulus(void)
{
    size_t checked = 0;
    for (size_t i = 0; i < ROWS(KAT_RSA_SHA256); i++)
    {
        const KatRsaVerify *v = &KAT_RSA_SHA256[i];
        if (!v->valid)
        {
            continue;
        }
        uint8_t n[PROTOCORE_RSA_KEY_BYTES];
        uint8_t e[4];
        uint8_t sig[PROTOCORE_RSA_KEY_BYTES];
        uint8_t msg[MAXMSG];
        unhex_modulus(v->n, n);
        unhex_exponent(v->e, e);
        unhex(v->sig, sig);
        size_t msg_len = unhex(v->msg, msg);

        // sig += n, big-endian. A sum that carries out of the buffer is no longer congruent to sig
        // mod n, so those rows carry no claim and are skipped.
        unsigned carry = 0;
        for (int k = PROTOCORE_RSA_KEY_BYTES - 1; k >= 0; k--)
        {
            const unsigned t = (unsigned)sig[k] + n[k] + carry;
            sig[k] = (uint8_t)t;
            carry = t >> 8;
        }
        if (carry)
        {
            continue;
        }

        Rsa.verify_args.n = n;
        Rsa.verify_args.e = e;
        Rsa.verify_args.msg = msg;
        Rsa.verify_args.msg_len = msg_len;
        Rsa.verify_args.sig = sig;
        Rsa.verify_args.sig_len = PROTOCORE_RSA_KEY_BYTES;
        Rsa.verify_args.hash = PROTOCORE_RSA_HASH_SHA256;
        Rsa.verify(g_work);
        TEST_ASSERT_FALSE_MESSAGE(Rsa.ok, v->sig);
        checked++;
    }
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)checked);
}

// A signature that is not exactly PROTOCORE_RSA_KEY_BYTES long, and a null operand, are refused
// before any arithmetic runs.
void test_rsa_verify_refuses_a_short_signature_and_null_operands(void)
{
    const KatRsaVerify *v = &KAT_RSA_SHA256[0];
    uint8_t n[PROTOCORE_RSA_KEY_BYTES];
    uint8_t e[4];
    uint8_t sig[PROTOCORE_RSA_KEY_BYTES];
    unhex_modulus(v->n, n);
    unhex_exponent(v->e, e);
    unhex(v->sig, sig);

    Rsa.verify_args.n = n;
    Rsa.verify_args.e = e;
    Rsa.verify_args.msg = (const uint8_t *)"x";
    Rsa.verify_args.msg_len = 1;
    Rsa.verify_args.sig = sig;
    Rsa.verify_args.hash = PROTOCORE_RSA_HASH_SHA256;

    Rsa.verify_args.sig_len = PROTOCORE_RSA_KEY_BYTES - 1;
    Rsa.verify(g_work);
    TEST_ASSERT_FALSE(Rsa.ok);

    Rsa.verify_args.sig_len = PROTOCORE_RSA_KEY_BYTES + 1;
    Rsa.verify(g_work);
    TEST_ASSERT_FALSE(Rsa.ok);

    Rsa.verify_args.sig_len = PROTOCORE_RSA_KEY_BYTES;
    Rsa.verify(NULL);
    TEST_ASSERT_FALSE(Rsa.ok);

    Rsa.verify_args.n = NULL;
    Rsa.verify(g_work);
    TEST_ASSERT_FALSE(Rsa.ok);
    Rsa.verify_args.n = n;

    Rsa.verify_args.e = NULL;
    Rsa.verify(g_work);
    TEST_ASSERT_FALSE(Rsa.ok);
    Rsa.verify_args.e = e;

    Rsa.verify_args.sig = NULL;
    Rsa.verify(g_work);
    TEST_ASSERT_FALSE(Rsa.ok);
}

// ---- sign (openssl) --------------------------------------------------------
// PKCS#1 v1.5 signing is deterministic in the key, the message and the digest, so the signature must
// be the one openssl produced, octet for octet.
void test_rsa_sign_matches_openssl(void)
{
    for (size_t i = 0; i < ROWS(KAT_RSA_SIGN); i++)
    {
        const KatRsaSign *v = &KAT_RSA_SIGN[i];
        uint8_t n[PROTOCORE_RSA_KEY_BYTES];
        uint8_t d[PROTOCORE_RSA_KEY_BYTES];
        uint8_t want[PROTOCORE_RSA_KEY_BYTES];
        uint8_t got[PROTOCORE_RSA_KEY_BYTES];
        uint8_t msg[MAXMSG];
        unhex_modulus(v->n, n);
        unhex(v->d, d);
        unhex(v->sig, want);
        size_t msg_len = unhex(v->msg, msg);

        Rsa.sign_args.n = n;
        Rsa.sign_args.d = d;
        Rsa.sign_args.msg = msg;
        Rsa.sign_args.msg_len = msg_len;
        Rsa.sign_args.hash = (strcmp(v->hash, "SHA-512") == 0) ? PROTOCORE_RSA_HASH_SHA512 : PROTOCORE_RSA_HASH_SHA256;
        Rsa.sign_args.sig = got;
        Rsa.sign(g_work);
        TEST_ASSERT_TRUE_MESSAGE(Rsa.ok, v->hash);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, PROTOCORE_RSA_KEY_BYTES, v->sig);
    }
}

// The two entries are inverses over the same key: what sign produces, verify accepts.
void test_rsa_sign_then_verify_round_trips(void)
{
    for (size_t i = 0; i < ROWS(KAT_RSA_SIGN); i++)
    {
        const KatRsaSign *v = &KAT_RSA_SIGN[i];
        uint8_t n[PROTOCORE_RSA_KEY_BYTES];
        uint8_t d[PROTOCORE_RSA_KEY_BYTES];
        uint8_t e[4];
        uint8_t sig[PROTOCORE_RSA_KEY_BYTES];
        uint8_t msg[MAXMSG];
        unhex_modulus(v->n, n);
        unhex(v->d, d);
        unhex_exponent(v->e, e);
        size_t msg_len = unhex(v->msg, msg);
        const protocore_rsa_hash alg =
            (strcmp(v->hash, "SHA-512") == 0) ? PROTOCORE_RSA_HASH_SHA512 : PROTOCORE_RSA_HASH_SHA256;

        Rsa.sign_args.n = n;
        Rsa.sign_args.d = d;
        Rsa.sign_args.msg = msg;
        Rsa.sign_args.msg_len = msg_len;
        Rsa.sign_args.hash = alg;
        Rsa.sign_args.sig = sig;
        Rsa.sign(g_work);
        TEST_ASSERT_TRUE(Rsa.ok);

        Rsa.verify_args.n = n;
        Rsa.verify_args.e = e;
        Rsa.verify_args.msg = msg;
        Rsa.verify_args.msg_len = msg_len;
        Rsa.verify_args.sig = sig;
        Rsa.verify_args.sig_len = PROTOCORE_RSA_KEY_BYTES;
        Rsa.verify_args.hash = alg;
        Rsa.verify(g_work);
        TEST_ASSERT_TRUE(Rsa.ok);

        // and one flipped signature octet must break it
        sig[PROTOCORE_RSA_KEY_BYTES - 1] ^= 0x01;
        Rsa.verify(g_work);
        TEST_ASSERT_FALSE(Rsa.ok);
    }
}

void test_rsa_sign_refuses_null_operands(void)
{
    const KatRsaSign *v = &KAT_RSA_SIGN[0];
    uint8_t n[PROTOCORE_RSA_KEY_BYTES];
    uint8_t d[PROTOCORE_RSA_KEY_BYTES];
    uint8_t sig[PROTOCORE_RSA_KEY_BYTES];
    unhex_modulus(v->n, n);
    unhex(v->d, d);

    Rsa.sign_args.n = n;
    Rsa.sign_args.d = d;
    Rsa.sign_args.msg = (const uint8_t *)"x";
    Rsa.sign_args.msg_len = 1;
    Rsa.sign_args.hash = PROTOCORE_RSA_HASH_SHA256;
    Rsa.sign_args.sig = sig;

    Rsa.sign(NULL);
    TEST_ASSERT_FALSE(Rsa.ok);

    Rsa.sign_args.n = NULL;
    Rsa.sign(g_work);
    TEST_ASSERT_FALSE(Rsa.ok);
    Rsa.sign_args.n = n;

    Rsa.sign_args.d = NULL;
    Rsa.sign(g_work);
    TEST_ASSERT_FALSE(Rsa.ok);
    Rsa.sign_args.d = d;

    Rsa.sign_args.sig = NULL;
    Rsa.sign(g_work);
    TEST_ASSERT_FALSE(Rsa.ok);
}

// The tables are the point of this suite: an empty one would make every case above pass while
// asserting nothing, so the row counts are checked before anything else runs on them.
void test_vector_tables_are_populated(void)
{
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_RSA_SHA256));
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_RSA_SHA512));
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_RSA_SIGN));

    // and both outcomes are represented, so neither branch of run_verify is dead
    size_t valid = 0;
    size_t invalid = 0;
    for (size_t i = 0; i < ROWS(KAT_RSA_SHA256); i++)
    {
        if (KAT_RSA_SHA256[i].valid)
        {
            valid++;
        }
        else
        {
            invalid++;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)valid);
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)invalid);
}
