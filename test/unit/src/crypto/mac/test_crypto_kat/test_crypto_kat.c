// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Data-driven external known-answer tests for the library's shared crypto primitives: HMAC-SHA256,
// HMAC-SHA512, AEAD_AES_128_GCM (crypto/aead/aes128gcm.h), X25519, Ed25519, HKDF-SHA256, the
// ChaCha20 block and Poly1305.
//
// Every expected value comes from kat_data.inc, which is compiled from the auditable JSON under
// test/vectors by tools/gen_crypto_vectors.py. Those vectors are Project Wycheproof at a pinned
// commit plus the RFC appendix vectors (RFC 5869 Appendix A for HKDF, RFC 8439 sections 2.4.2 and
// 2.5.2 for ChaCha20 and Poly1305), so nothing here was produced by this tree.
//
// The load-bearing part is Wycheproof's adversarial half: rows flagged invalid carry the ciphertext,
// tag, signature or IV a hostile peer would actually send - a flipped tag bit, a truncated tag, a
// low-order X25519 point, a malleable Ed25519 S. A primitive that only ever sees well-formed input
// passes a round trip and still accepts all of them.

#include "crypto/aead/aes128gcm.h"
#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ed25519.h"
#include "crypto/cipher/chacha20.h"
#include "crypto/kdf/hkdf.h"
#include "crypto/mac/hmac_sha256.h"
#include "crypto/mac/hmac_sha512.h"
#include "crypto/mac/poly1305.h"
#include <string.h>

#include <unity.h>

// --- Vector table row layouts (kat_data.inc initializes these) --------------
typedef struct
{
    int tc;
    const char *key;
    const char *msg;
    const char *tag;
    int tag_bits;
    int valid;
} KatMac;
typedef struct
{
    int tc;
    const char *key;
    const char *iv;
    const char *aad;
    const char *msg;
    const char *ct;
    const char *tag;
    int valid;
} KatAead;
typedef struct
{
    int tc;
    const char *pub;
    const char *priv;
    const char *shared;
} KatX25519;
typedef struct
{
    int tc;
    const char *pub;
    const char *msg;
    const char *sig;
    int valid;
} KatEd25519;
typedef struct
{
    int tc;
    const char *seed;
    const char *pub;
    const char *msg;
    const char *sig;
} KatEd25519Sign;
typedef struct
{
    int tc;
    const char *salt;
    const char *ikm;
    const char *prk;
    const char *info;
    uint32_t l;
    const char *okm;
} KatHkdf;
typedef struct
{
    int tc;
    const char *key;
    uint32_t counter;
    const char *nonce;
    const char *keystream;
} KatChacha;
typedef struct
{
    int tc;
    const char *key;
    const char *msg;
    const char *tag;
} KatPoly;

#include "kat_data.inc"

#define ROWS(a) (sizeof(a) / sizeof((a)[0]))
#define MAXB 2048 // the widest field in the tables: an Ed25519 message runs to 1023 octets

static uint8_t g_work[4096] __attribute__((aligned(8)));

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

// A keyed AES-128-GCM context over one reusable work region. Some backends attach vendor resources
// to a context, so the previous one is released before the next is built.
static uint8_t g_gcm_ws[PROTOCORE_WORK_AES128GCM] __attribute__((aligned(8)));
static proto_bool g_gcm_live = PROTO_FALSE;
static struct protocore_aes128gcm_key *gcm(const uint8_t *key)
{
    if (g_gcm_live)
    {
        protocore_aes128gcm_key_wipe((struct protocore_aes128gcm_key *)g_gcm_ws);
    }
    g_gcm_live = PROTO_TRUE;
    return protocore_aes128gcm_key_init(g_gcm_ws, key);
}

// ---- HMAC (RFC 4231 / Wycheproof) -----------------------------------------
// Compute the whole MAC and compare the leading tag_bits/8 octets to the vector. A valid row must
// match; an invalid row must not, which is what catches an implementation that ignores a truncated
// or bit-flipped tag.
static void run_hmac(const KatMac *rows, size_t n, proto_bool is512)
{
    for (size_t i = 0; i < n; i++)
    {
        const KatMac *v = &rows[i];
        uint8_t key[MAXB], msg[MAXB], want[64], got[64];
        size_t klen = unhex(v->key, key);
        size_t mlen = unhex(v->msg, msg);
        size_t wlen = unhex(v->tag, want);
        if (is512)
        {
            protocore_hmac_sha512(g_work, key, klen, msg, mlen, got);
        }
        else
        {
            protocore_hmac_sha256(g_work, key, klen, msg, mlen, got);
        }
        size_t cmp = (size_t)v->tag_bits / 8;
        proto_bool match = (wlen == cmp) && memcmp(got, want, cmp) == 0;
        if (v->valid)
        {
            TEST_ASSERT_TRUE_MESSAGE(match, v->tag);
        }
        else
        {
            TEST_ASSERT_FALSE_MESSAGE(match, v->tag);
        }
    }
}

void test_hmac_sha256(void)
{
    run_hmac(KAT_HMAC_SHA256, ROWS(KAT_HMAC_SHA256), PROTO_FALSE);
}

void test_hmac_sha512(void)
{
    run_hmac(KAT_HMAC_SHA512, ROWS(KAT_HMAC_SHA512), PROTO_TRUE);
}

// ---- AEAD_AES_128_GCM -----------------------------------------------------
// A valid row: seal must reproduce ciphertext and tag, open must recover the plaintext, and a
// flipped tag octet must then be refused. An invalid row carries the pair a peer would send, so it
// is handed to open exactly as received - sealing its plaintext would produce the correct tag,
// which is not what the row is about.
void test_aes128gcm(void)
{
    for (size_t i = 0; i < ROWS(KAT_AES128GCM); i++)
    {
        const KatAead *v = &KAT_AES128GCM[i];
        uint8_t key[16], iv[12], aad[MAXB], pt[MAXB], ct[MAXB], tag[16];
        uint8_t sealed[MAXB + 16], opened[MAXB];
        unhex(v->key, key);
        unhex(v->iv, iv);
        size_t alen = unhex(v->aad, aad);
        size_t plen = unhex(v->msg, pt);
        size_t clen = unhex(v->ct, ct);
        unhex(v->tag, tag);

        if (!v->valid)
        {
            TEST_ASSERT_FALSE_MESSAGE(
                protocore_aes128gcm_open(gcm(key), iv, alen ? aad : NULL, alen, clen ? ct : NULL, clen, tag, opened),
                v->tag);
            continue;
        }

        protocore_aes128gcm_seal(gcm(key), iv, alen ? aad : NULL, alen, plen ? pt : NULL, plen, sealed, sealed + plen);
        if (clen)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(ct, sealed, clen, v->ct);
        }
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(tag, sealed + plen, 16, v->tag);

        TEST_ASSERT_TRUE_MESSAGE(
            protocore_aes128gcm_open(gcm(key), iv, alen ? aad : NULL, alen, sealed, plen, sealed + plen, opened),
            v->tag);
        if (plen)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(pt, opened, plen, v->msg);
        }

        sealed[plen + 15] ^= 0x80;
        TEST_ASSERT_FALSE_MESSAGE(
            protocore_aes128gcm_open(gcm(key), iv, alen ? aad : NULL, alen, sealed, plen, sealed + plen, opened),
            v->tag);
    }
}

// NIST SP 800-38D sec 6.5: GCTR steps the low 32 bits of the counter block once per 16-octet block.
// None of the table rows reach 256 blocks, so the low octet never rolls 0xff -> 0x00 into the one
// above it. 4200 octets is 263 blocks, which forces that carry inside a single seal.
void test_aes128gcm_counter_carry(void)
{
    static const uint8_t KEY[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t IV[12] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b};
    static uint8_t pt[4200];
    static uint8_t sealed[4200 + 16];
    static uint8_t opened[4200];
    for (size_t i = 0; i < sizeof(pt); i++)
    {
        pt[i] = (uint8_t)(i * 131u + 7u);
    }

    protocore_aes128gcm_seal(gcm(KEY), IV, NULL, 0, pt, sizeof(pt), sealed, sealed + sizeof(pt));
    TEST_ASSERT_TRUE(protocore_aes128gcm_open(gcm(KEY), IV, NULL, 0, sealed, sizeof(pt), sealed + sizeof(pt), opened));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pt, opened, sizeof(pt));

    sealed[sizeof(pt) + 15] ^= 0x80;
    TEST_ASSERT_FALSE(protocore_aes128gcm_open(gcm(KEY), IV, NULL, 0, sealed, sizeof(pt), sealed + sizeof(pt), opened));
}

// ---- X25519 ---------------------------------------------------------------
// Scalar multiplication is a total function, so every row - including the low-order and
// non-canonical u-coordinates Wycheproof flags - has one right answer.
void test_x25519(void)
{
    for (size_t i = 0; i < ROWS(KAT_X25519); i++)
    {
        const KatX25519 *v = &KAT_X25519[i];
        uint8_t pub[32], priv[32], want[32], got[32];
        unhex(v->pub, pub);
        unhex(v->priv, priv);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(32u, (unsigned)unhex(v->shared, want), v->shared);
        protocore_x25519(got, priv, pub);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, 32, v->shared);
    }
}

// ---- Ed25519 --------------------------------------------------------------
// The length gate every caller applies before the crypto (len == 64) is applied here too, so a row
// whose signature is the wrong length reaches the same assertion as the rest instead of asserting
// the corpus.
void test_ed25519_verify(void)
{
    for (size_t i = 0; i < ROWS(KAT_ED25519); i++)
    {
        const KatEd25519 *v = &KAT_ED25519[i];
        uint8_t pub[32], msg[MAXB], sig[64];
        unhex(v->pub, pub);
        size_t mlen = unhex(v->msg, msg);
        size_t slen = unhex(v->sig, sig);
        proto_bool ok = (slen == PROTOCORE_ED25519_SIG_LEN) && protocore_ed25519_verify(g_work, pub, msg, mlen, sig);
        TEST_ASSERT_EQUAL_MESSAGE(v->valid ? PROTO_TRUE : PROTO_FALSE, ok, v->sig);
    }
}

// Signing is deterministic (RFC 8032 sec 5.1.6), so the derived public key and the signature are
// both fixed functions of the seed and the message - the SSH host-key signing path.
void test_ed25519_sign(void)
{
    for (size_t i = 0; i < ROWS(KAT_ED25519_SIGN); i++)
    {
        const KatEd25519Sign *v = &KAT_ED25519_SIGN[i];
        uint8_t seed[32], want_pub[32], msg[MAXB], want_sig[64], got_pub[32], got_sig[64];
        unhex(v->seed, seed);
        unhex(v->pub, want_pub);
        unhex(v->sig, want_sig);
        size_t mlen = unhex(v->msg, msg);
        protocore_ed25519_pubkey(g_work, got_pub, seed);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want_pub, got_pub, 32, v->pub);
        protocore_ed25519_sign(g_work, got_sig, msg, mlen, seed);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want_sig, got_sig, 64, v->sig);
    }
}

// ---- HKDF-SHA256 (RFC 5869 Appendix A) ------------------------------------
// Extract: PRK = HMAC-SHA256(salt, IKM).
void test_hkdf_extract(void)
{
    for (size_t i = 0; i < ROWS(KAT_HKDF); i++)
    {
        const KatHkdf *v = &KAT_HKDF[i];
        uint8_t salt[MAXB], ikm[MAXB], want[32], got[32];
        size_t slen = unhex(v->salt, salt);
        size_t ilen = unhex(v->ikm, ikm);
        unhex(v->prk, want);
        protocore_hkdf_extract(g_work, slen ? salt : NULL, slen, ikm, ilen, got);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, 32, v->prk);
    }
}

// Expand: OKM = T(1) | T(2) | ..., T(i) = HMAC(PRK, T(i-1) | info | i). Appendix A.2 asks for 82
// octets, three SHA-256 blocks, which is the one published vector that runs the T(i) chain.
void test_hkdf_expand(void)
{
    for (size_t i = 0; i < ROWS(KAT_HKDF); i++)
    {
        const KatHkdf *v = &KAT_HKDF[i];
        uint8_t prk[32], info[MAXB], want[MAXB], got[MAXB];
        unhex(v->prk, prk);
        size_t ilen = unhex(v->info, info);
        size_t wlen = unhex(v->okm, want);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->l, (uint32_t)wlen, v->okm);
        protocore_hkdf_expand(g_work, prk, ilen ? info : NULL, ilen, got, wlen);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, wlen, v->okm);
    }
}

// RFC 5869 sec 2.3 caps L at 255*HashLen: the block counter is one octet and has no encoding past
// that, so a larger request must produce no key material rather than blocks that repeat.
void test_hkdf_expand_length_bound(void)
{
    static uint8_t out[256 * 32];
    uint8_t prk[32];
    unhex(KAT_HKDF[0].prk, prk);

    memset(out, 0xAA, sizeof(out));
    protocore_hkdf_expand(g_work, prk, NULL, 0, out, (size_t)255 * 32);
    TEST_ASSERT_NOT_EQUAL(0xAA, out[0]);

    memset(out, 0xAA, sizeof(out));
    protocore_hkdf_expand(g_work, prk, NULL, 0, out, (size_t)255 * 32 + 1);
    for (size_t i = 0; i < (size_t)255 * 32 + 1; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
    }
}

// ---- ChaCha20 (RFC 8439 sec 2.4.2) and Poly1305 (sec 2.5.2) ---------------
void test_chacha20_block(void)
{
    for (size_t i = 0; i < ROWS(KAT_CHACHA20); i++)
    {
        const KatChacha *v = &KAT_CHACHA20[i];
        uint8_t key[32], nonce[12], want[64], got[64];
        unhex(v->key, key);
        unhex(v->nonce, nonce);
        unhex(v->keystream, want);
        protocore_chacha20_block_ietf(key, v->counter, nonce, got);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, 64, v->keystream);
    }
}

void test_poly1305(void)
{
    for (size_t i = 0; i < ROWS(KAT_POLY1305); i++)
    {
        const KatPoly *v = &KAT_POLY1305[i];
        uint8_t key[32], msg[MAXB], want[16], got[16];
        unhex(v->key, key);
        size_t mlen = unhex(v->msg, msg);
        unhex(v->tag, want);
        protocore_poly1305(got, msg, mlen, key);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, 16, v->tag);
    }
}

// The tables are the point of this suite: an empty one would make every case above pass while
// asserting nothing, so the row counts are checked before anything else runs on them.
void test_vector_tables_are_populated(void)
{
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_HMAC_SHA256));
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_HMAC_SHA512));
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_AES128GCM));
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_X25519));
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_ED25519));
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_ED25519_SIGN));
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_HKDF));
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_CHACHA20));
    TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)ROWS(KAT_POLY1305));
}
