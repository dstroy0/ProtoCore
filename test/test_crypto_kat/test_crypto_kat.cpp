// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Data-driven external known-answer tests (KAT) for the library's crypto
// primitives. Every vector here was produced OUTSIDE this codebase - Project
// Wycheproof (adversarial edge cases: wrong tags, modified IVs, low-order
// points, signature malleability) and the RFC appendix vectors - so these fail
// if a primitive drifts from the standard even when the self-referential
// protocol tests still pass.
//
// The vectors live as auditable JSON under test/vectors/ (curated by
// tools/curate_crypto_vectors.py) and are compiled to the tables below by
// tools/gen_crypto_vectors.py -> kat_data.inc. To refresh: re-run those tools.
//
// Note: the "private" scalars in the X25519 vectors are published test inputs,
// not secrets - a known-answer test is meaningless if its inputs are not fixed.
// Ephemeral, per-run keys belong in the handshake/round-trip tests, not here.

#include "crypto/aead/aes128gcm.h"
#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ed25519.h"
#include "crypto/cipher/chacha20.h"
#include "crypto/kdf/hkdf.h"
#include "crypto/mac/hmac_sha256.h"
#include "crypto/mac/hmac_sha512.h"
#include "crypto/mac/poly1305.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

// --- Vector table row layouts (kat_data.inc initializes these) --------------
struct KatMac
{
    int tc;
    const char *key;
    const char *msg;
    const char *tag;
    int tag_bits;
    int valid;
};
struct KatAead
{
    int tc;
    const char *key;
    const char *iv;
    const char *aad;
    const char *msg;
    const char *ct;
    const char *tag;
    int valid;
};
struct KatX25519
{
    int tc;
    const char *pub;
    const char *priv;
    const char *shared;
};
struct KatEd25519
{
    int tc;
    const char *pub;
    const char *msg;
    const char *sig;
    int valid;
};
struct KatEd25519Sign
{
    int tc;
    const char *seed;
    const char *pub;
    const char *msg;
    const char *sig;
};
struct KatHkdf
{
    int tc;
    const char *salt;
    const char *ikm;
    const char *prk;
};
struct KatChacha
{
    int tc;
    const char *key;
    uint32_t counter;
    const char *nonce;
    const char *keystream;
};
struct KatPoly
{
    int tc;
    const char *key;
    const char *msg;
    const char *tag;
};

#include "kat_data.inc"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MAXB 2048 // largest vector field (ed25519 msg is 1023 bytes)

// Decode a lowercase-hex C-string into @p out; returns the byte length.
static size_t hexdec(const char *h, uint8_t *out)
{
    size_t n = 0;
    for (const char *p = h; p[0] && p[1]; p += 2)
    {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            return c - 'A' + 10;
        };
        out[n++] = (uint8_t)((nib(p[0]) << 4) | nib(p[1]));
    }
    return n;
}

void setUp()
{
}
void tearDown()
{
}

// ====================================================================
// HMAC (RFC 4231 / Wycheproof): compute the full MAC, compare the first
// tag_bits/8 bytes to the vector; valid must match, invalid must not.
// ====================================================================
static void run_hmac(const KatMac *arr, size_t n, bool is512)
{
    for (size_t i = 0; i < n; i++)
    {
        const KatMac &v = arr[i];
        uint8_t key[MAXB], msg[MAXB], want[64], got[64];
        size_t klen = hexdec(v.key, key), mlen = hexdec(v.msg, msg), wlen = hexdec(v.tag, want);
        if (is512)
        {
            pc_hmac_sha512(key, klen, msg, mlen, got);
        }
        else
        {
            pc_hmac_sha256(key, klen, msg, mlen, got);
        }
        size_t cmp = (size_t)v.tag_bits / 8; // truncated-tag length the vector pins
        char m[64];
        snprintf(m, sizeof(m), "HMAC%s tcId=%d", is512 ? "512" : "256", v.tc);
        bool match = (wlen == cmp) && memcmp(got, want, cmp) == 0;
        if (v.valid)
        {
            TEST_ASSERT_TRUE_MESSAGE(match, m);
        }
        else
        {
            TEST_ASSERT_FALSE_MESSAGE(match, m);
        }
    }
}
static void test_hmac_sha256(void)
{
    run_hmac(KAT_HMAC_SHA256, ARRAY_LEN(KAT_HMAC_SHA256), false);
}
static void test_hmac_sha512(void)
{
    run_hmac(KAT_HMAC_SHA512, ARRAY_LEN(KAT_HMAC_SHA512), true);
}

// ====================================================================
// AEAD_AES_128_GCM (Wycheproof / RFC 9001): seal must reproduce ct||tag and
// open must recover the plaintext; a one-bit tag flip must be rejected.
// ====================================================================
static void test_aes128gcm(void)
{
    for (size_t i = 0; i < ARRAY_LEN(KAT_AES128GCM); i++)
    {
        const KatAead &v = KAT_AES128GCM[i];
        uint8_t key[16], iv[12], aad[MAXB], pt[MAXB], ct[MAXB], tag[16];
        uint8_t sealed[MAXB + 16], opened[MAXB];
        hexdec(v.key, key);
        hexdec(v.iv, iv);
        size_t alen = hexdec(v.aad, aad), plen = hexdec(v.msg, pt);
        size_t clen = hexdec(v.ct, ct);
        hexdec(v.tag, tag);
        char m[48];
        snprintf(m, sizeof(m), "AES128GCM tcId=%d", v.tc);

        // seal: out == ciphertext || tag (ciphertext is empty when plaintext is)
        pc_aes128gcm_seal(key, iv, alen ? aad : nullptr, alen, plen ? pt : nullptr, plen, sealed);
        if (clen)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(ct, sealed, clen, m);
        }
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(tag, sealed + plen, 16, m);

        // open: recovers the plaintext and authenticates
        bool ok = pc_aes128gcm_open(key, iv, alen ? aad : nullptr, alen, sealed, plen + 16, NULL, opened);
        TEST_ASSERT_TRUE_MESSAGE(ok, m);
        if (plen)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(pt, opened, plen, m);
        }

        // negative: a flipped tag byte must fail authentication
        sealed[plen + 15] ^= 0x80;
        TEST_ASSERT_FALSE_MESSAGE(
            pc_aes128gcm_open(key, iv, alen ? aad : nullptr, alen, sealed, plen + 16, NULL, opened), m);
    }
}

// ====================================================================
// AEAD_AES_128_GCM counter carry: none of the vectors above exceed 256
// GCTR blocks, so the GCM counter's low byte (pc_quic_aead.cpp inc32())
// never rolls 0xff -> 0x00 and carries into the next byte. A plaintext
// past 256*16 = 4096 bytes forces that single-byte carry through the
// public seal()/open() API (no KAT oracle needed - this is an internal
// round-trip check, not an externally-sourced vector).
// ====================================================================
#define CTR_CARRY_PT_LEN 4200 // > 256*16: guarantees >=1 low-byte carry in the GCM counter
static void test_aes128gcm_ctr_carry(void)
{
    static const uint8_t key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t iv[12] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b};
    static uint8_t pt[CTR_CARRY_PT_LEN];
    static uint8_t sealed[CTR_CARRY_PT_LEN + 16];
    static uint8_t opened[CTR_CARRY_PT_LEN];
    for (size_t i = 0; i < CTR_CARRY_PT_LEN; i++)
    {
        pt[i] = (uint8_t)(i * 131u + 7u);
    }

    pc_aes128gcm_seal(key, iv, nullptr, 0, pt, CTR_CARRY_PT_LEN, sealed);
    bool ok = pc_aes128gcm_open(key, iv, nullptr, 0, sealed, CTR_CARRY_PT_LEN + 16, NULL, opened);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, opened, CTR_CARRY_PT_LEN);

    // negative: a flipped tag byte must still fail authentication past the carry boundary
    sealed[CTR_CARRY_PT_LEN + 15] ^= 0x80;
    TEST_ASSERT_FALSE(pc_aes128gcm_open(key, iv, nullptr, 0, sealed, CTR_CARRY_PT_LEN + 16, NULL, opened));
}

// ====================================================================
// X25519 (RFC 7748 / Wycheproof): scalar*point is deterministic, so the
// computed shared secret must equal the vector for valid and acceptable alike.
// ====================================================================
static void test_x25519(void)
{
    for (size_t i = 0; i < ARRAY_LEN(KAT_X25519); i++)
    {
        const KatX25519 &v = KAT_X25519[i];
        uint8_t pub[32], priv[32], want[32], got[32];
        hexdec(v.pub, pub);
        hexdec(v.priv, priv);
        size_t wlen = hexdec(v.shared, want);
        pc_x25519(got, priv, pub);
        char m[48];
        snprintf(m, sizeof(m), "X25519 tcId=%d", v.tc);
        TEST_ASSERT_EQUAL_MESSAGE(32, wlen, m);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, 32, m);
    }
}

// ====================================================================
// Ed25519 verify (RFC 8032 / Wycheproof): valid signatures verify, invalid
// ones (including wrong-length encodings, which a caller rejects up front) fail.
// ====================================================================
static void test_ed25519_verify(void)
{
    for (size_t i = 0; i < ARRAY_LEN(KAT_ED25519); i++)
    {
        const KatEd25519 &v = KAT_ED25519[i];
        uint8_t pub[32], msg[MAXB], sig[64];
        hexdec(v.pub, pub);
        size_t mlen = hexdec(v.msg, msg), slen = hexdec(v.sig, sig);
        char m[48];
        snprintf(m, sizeof(m), "Ed25519 tcId=%d", v.tc);
        if (slen != 64)
        {
            // A non-64-byte signature is malformed; the framing rejects it before
            // the crypto ever runs. Such vectors are all "invalid".
            TEST_ASSERT_FALSE_MESSAGE(v.valid, m);
            continue;
        }
        bool ok = pc_ed25519_verify(pub, msg, mlen, sig);
        TEST_ASSERT_EQUAL_MESSAGE(v.valid ? true : false, ok, m);
    }
}

// ====================================================================
// Ed25519 sign (RFC 8032 sec 7.1): signing is deterministic, so the derived
// public key and the signature over the message must both match the vector -
// this covers the SSH host-key signing path (verify above covers verification).
// ====================================================================
static void test_ed25519_sign(void)
{
    for (size_t i = 0; i < ARRAY_LEN(KAT_ED25519_SIGN); i++)
    {
        const KatEd25519Sign &v = KAT_ED25519_SIGN[i];
        uint8_t seed[32], want_pub[32], msg[MAXB], want_sig[64], got_pub[32], got_sig[64];
        hexdec(v.seed, seed);
        hexdec(v.pub, want_pub);
        hexdec(v.sig, want_sig);
        size_t mlen = hexdec(v.msg, msg);
        char m[48];
        snprintf(m, sizeof(m), "Ed25519-sign tcId=%d", v.tc);
        pc_ed25519_pubkey(got_pub, seed);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want_pub, got_pub, 32, m);
        pc_ed25519_sign(got_sig, msg, mlen, seed);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want_sig, got_sig, 64, m);
    }
}

// ====================================================================
// HKDF-SHA256 Extract (RFC 5869 Appendix A): PRK = HMAC-SHA256(salt, IKM).
// ====================================================================
static void test_hkdf_extract(void)
{
    for (size_t i = 0; i < ARRAY_LEN(KAT_HKDF_EXTRACT); i++)
    {
        const KatHkdf &v = KAT_HKDF_EXTRACT[i];
        uint8_t salt[MAXB], ikm[MAXB], want[32], got[32];
        size_t slen = hexdec(v.salt, salt), ilen = hexdec(v.ikm, ikm);
        hexdec(v.prk, want);
        pc_hkdf_extract(slen ? salt : nullptr, slen, ikm, ilen, got);
        char m[48];
        snprintf(m, sizeof(m), "HKDF-Extract tcId=%d", v.tc);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, 32, m);
    }
}

// ====================================================================
// ChaCha20 block (RFC 8439 sec 2.4.2) and Poly1305 (sec 2.5.2).
// ====================================================================
static void test_chacha20_block(void)
{
    for (size_t i = 0; i < ARRAY_LEN(KAT_CHACHA20); i++)
    {
        const KatChacha &v = KAT_CHACHA20[i];
        uint8_t key[32], nonce[12], want[64], got[64];
        hexdec(v.key, key);
        hexdec(v.nonce, nonce);
        hexdec(v.keystream, want);
        pc_chacha20_block_ietf(key, v.counter, nonce, got);
        char m[48];
        snprintf(m, sizeof(m), "ChaCha20 tcId=%d", v.tc);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, 64, m);
    }
}
static void test_poly1305(void)
{
    for (size_t i = 0; i < ARRAY_LEN(KAT_POLY1305); i++)
    {
        const KatPoly &v = KAT_POLY1305[i];
        uint8_t key[32], msg[MAXB], want[16], got[16];
        hexdec(v.key, key);
        size_t mlen = hexdec(v.msg, msg);
        hexdec(v.tag, want);
        pc_poly1305(got, msg, mlen, key);
        char m[48];
        snprintf(m, sizeof(m), "Poly1305 tcId=%d", v.tc);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, 16, m);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_hmac_sha256);
    RUN_TEST(test_hmac_sha512);
    RUN_TEST(test_aes128gcm);
    RUN_TEST(test_aes128gcm_ctr_carry);
    RUN_TEST(test_x25519);
    RUN_TEST(test_ed25519_verify);
    RUN_TEST(test_ed25519_sign);
    RUN_TEST(test_hkdf_extract);
    RUN_TEST(test_chacha20_block);
    RUN_TEST(test_poly1305);
    return UNITY_END();
}
