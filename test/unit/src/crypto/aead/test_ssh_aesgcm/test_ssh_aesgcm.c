// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for AES-256-GCM (crypto/aead/aesgcm.h), the AEAD behind aes256-gcm@openssh.com.
//
// The load-bearing cases are the four NIST CAVP known-answer vectors below, taken verbatim from
// gcmEncryptExtIV256.rsp of the SP 800-38D GCM validation set (Keylen 256, IVlen 96, Taglen 128).
// They pin the ciphertext AND the tag, so a wrong GHASH subkey, a wrong J0, a wrong length block,
// or a counter that starts at J0 instead of inc32(J0) each fail on a published number rather than
// on a round trip that would agree with itself.
//
// The nonce advance is RFC 5647 sec 7.1: the 12-octet IV is a 4-octet fixed field followed by an
// 8-octet invocation counter incremented once per binary packet.

#include "crypto/aead/aesgcm.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// One scratch context, rebuilt per vector and wiped first so a backend that attaches vendor
// resources to a context does not leak one per call.
static uint8_t g_ws[PROTOCORE_AESGCM_BORROW] __attribute__((aligned(8)));
static proto_bool g_live = PROTO_FALSE;

static uint8_t *keyed(const uint8_t *key)
{
    if (g_live)
    {
        AesGcm.key_wipe(g_ws);
    }
    g_live = PROTO_TRUE;
    AesGcm.key_args.key = key;
    AesGcm.key_init(g_ws);
    return g_ws;
}

// One sealed record through the namespace, so the vectors below read as one call each.
static void gcm_seal(const uint8_t *key, const uint8_t *iv, const uint8_t *aad, size_t aadn, const uint8_t *pt,
                     size_t ptn, uint8_t *ct_out, uint8_t *tag_out)
{
    uint8_t *w = keyed(key);
    AesGcm.seal_args.nonce = iv;
    AesGcm.seal_args.aad = aad;
    AesGcm.seal_args.aad_len = aadn;
    AesGcm.seal_args.pt = pt;
    AesGcm.seal_args.pt_len = ptn;
    AesGcm.seal_args.ct_out = ct_out;
    AesGcm.seal_args.tag_out = tag_out;
    AesGcm.seal(w);
}

// One opened record; the outcome is the return, as the flat call's was.
static proto_bool gcm_open(const uint8_t *key, const uint8_t *iv, const uint8_t *aad, size_t aadn, const uint8_t *ct,
                           size_t ctn, const uint8_t *tag, uint8_t *out)
{
    uint8_t *w = keyed(key);
    AesGcm.open_args.nonce = iv;
    AesGcm.open_args.aad = aad;
    AesGcm.open_args.aad_len = aadn;
    AesGcm.open_args.ct = ct;
    AesGcm.open_args.ct_len = ctn;
    AesGcm.open_args.tag = tag;
    AesGcm.open_args.out = out;
    AesGcm.open(w);
    return AesGcm.ok;
}

// The nonce is the caller's own, so the counter advance reads nothing out of the borrow.
static void gcm_iv_increment(uint8_t *iv)
{
    AesGcm.iv_args.iv = iv;
    AesGcm.iv_increment(g_ws);
}

static uint8_t nib(char c)
{
    return (uint8_t)(c <= '9' ? c - '0' : ((c | 0x20) - 'a' + 10));
}

// Decode @p h into @p out and return the byte count.
static size_t unhex(const char *h, uint8_t *out)
{
    size_t n = 0;
    for (; h[0] && h[1]; h += 2)
    {
        out[n++] = (uint8_t)((nib(h[0]) << 4) | nib(h[1]));
    }
    return n;
}

// NIST CAVP gcmEncryptExtIV256.rsp, [Keylen=256][IVlen=96][Taglen=128], Count = 0 of four sections.
struct kat
{
    const char *key;
    const char *iv;
    const char *pt;
    const char *aad;
    const char *ct;
    const char *tag;
};

// [PTlen=0][AADlen=0]
static const struct kat KAT_EMPTY = {"b52c505a37d78eda5dd34f20c22540ea1b58963cf8e5bf8ffa85f9f2492505b4",
                                     "516c33929df5a3284ff463d7",
                                     "",
                                     "",
                                     "",
                                     "bdc1ac884d332457a1d2664f168c76f0"};

// [PTlen=0][AADlen=160] - GMAC: 20 octets of AAD, no plaintext at all.
static const struct kat KAT_GMAC = {"886cff5f3e6b8d0e1ad0a38fcdb26de97e8acbe79f6bed66959a598fa5047d65",
                                    "3a8efa1cd74bbab5448f9945",
                                    "",
                                    "519fee519d25c7a304d6c6aa1897ee1eb8c59655",
                                    "",
                                    "f6d47505ec96c98a42dc3ae719877b87"};

// [PTlen=128][AADlen=160] - exactly one cipher block, AAD not a block multiple.
static const struct kat KAT_ONE_BLOCK = {"83688deb4af8007f9b713b47cfa6c73e35ea7a3aa4ecdb414dded03bf7a0fd3a",
                                         "0b459724904e010a46901cf3",
                                         "33d893a2114ce06fc15d55e454cf90c3",
                                         "794a14ccd178c8ebfd1379dc704c5e208f9d8424",
                                         "cc66bee423e3fcd4c0865715e9586696",
                                         "0fb291bd3dba94a1dfd8b286cfb97ac5"};

// [PTlen=408][AADlen=160] - 51 octets, so the last GCTR block and the last GHASH block are partial.
static const struct kat KAT_PARTIAL = {
    "24501ad384e473963d476edcfe08205237acfd49b5b8f33857f8114e863fec7f",
    "9ff18563b978ec281b3f2794",
    "27f348f9cdc0c5bd5e66b1ccb63ad920ff2219d14e8d631b3872265cf117ee86757accb158bd9abb3868fdc0d0b074b5f01b2c",
    "adb5ec720ccf9898500028bf34afccbcaca126ef",
    "eb7cb754c824e8d96f7c6d9b76c7d26fb874ffbf1d65c6f64a698d839b0b06145dae82057ad55994cf59ad7f67c0fa5e85fab8",
    "bc95c532fecc594c36d1550286a7a3f0"};

// Seal the vector's plaintext and compare both halves of the AEAD output to the published bytes.
static void seal_matches(const struct kat *v)
{
    uint8_t key[32], iv[12], pt[64], aad[64], ct[64], tag[16];
    uint8_t got_ct[64], got_tag[16];
    unhex(v->key, key);
    unhex(v->iv, iv);
    size_t ptn = unhex(v->pt, pt);
    size_t aadn = unhex(v->aad, aad);
    unhex(v->ct, ct);
    unhex(v->tag, tag);

    gcm_seal(key, iv, aad, aadn, pt, ptn, got_ct, got_tag);
    TEST_ASSERT_TRUE(AesGcm.ok);
    if (ptn)
    {
        TEST_ASSERT_EQUAL_UINT8_ARRAY(ct, got_ct, ptn);
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tag, got_tag, 16);
}

// Open the vector's ciphertext and compare the recovered plaintext to the published bytes.
static void open_matches(const struct kat *v)
{
    uint8_t key[32], iv[12], pt[64], aad[64], ct[64], tag[16], got[64];
    unhex(v->key, key);
    unhex(v->iv, iv);
    size_t ptn = unhex(v->pt, pt);
    size_t aadn = unhex(v->aad, aad);
    unhex(v->ct, ct);
    unhex(v->tag, tag);

    TEST_ASSERT_TRUE(gcm_open(key, iv, aad, aadn, ct, ptn, tag, got));
    if (ptn)
    {
        TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, got, ptn);
    }
}

void test_cavp_empty_plaintext_and_aad(void)
{
    seal_matches(&KAT_EMPTY);
    open_matches(&KAT_EMPTY);
}

void test_cavp_aad_only_gmac(void)
{
    seal_matches(&KAT_GMAC);
    open_matches(&KAT_GMAC);
}

void test_cavp_one_block(void)
{
    seal_matches(&KAT_ONE_BLOCK);
    open_matches(&KAT_ONE_BLOCK);
}

void test_cavp_partial_final_block(void)
{
    seal_matches(&KAT_PARTIAL);
    open_matches(&KAT_PARTIAL);
}

// A one-bit change anywhere the tag covers (tag, ciphertext, AAD, nonce) must fail the open, and the
// unmodified inputs must still open afterwards: the refusal is stateless.
void test_open_refuses_every_tampered_input(void)
{
    uint8_t key[32], iv[12], pt[64], aad[64], ct[64], tag[16], got[64];
    unhex(KAT_PARTIAL.key, key);
    unhex(KAT_PARTIAL.iv, iv);
    size_t ptn = unhex(KAT_PARTIAL.pt, pt);
    size_t aadn = unhex(KAT_PARTIAL.aad, aad);
    unhex(KAT_PARTIAL.ct, ct);
    unhex(KAT_PARTIAL.tag, tag);

    uint8_t bad_tag[16];
    memcpy(bad_tag, tag, 16);
    bad_tag[15] ^= 0x01;
    TEST_ASSERT_FALSE(gcm_open(key, iv, aad, aadn, ct, ptn, bad_tag, got));

    uint8_t bad_ct[64];
    memcpy(bad_ct, ct, ptn);
    bad_ct[0] ^= 0x80;
    TEST_ASSERT_FALSE(gcm_open(key, iv, aad, aadn, bad_ct, ptn, tag, got));

    uint8_t bad_aad[64];
    memcpy(bad_aad, aad, aadn);
    bad_aad[aadn - 1] ^= 0x01;
    TEST_ASSERT_FALSE(gcm_open(key, iv, bad_aad, aadn, ct, ptn, tag, got));

    uint8_t bad_iv[12];
    memcpy(bad_iv, iv, 12);
    bad_iv[11] ^= 0x01;
    TEST_ASSERT_FALSE(gcm_open(key, bad_iv, aad, aadn, ct, ptn, tag, got));

    TEST_ASSERT_TRUE(gcm_open(key, iv, aad, aadn, ct, ptn, tag, got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, got, ptn);
}

// AAD and plaintext are separate GHASH inputs with their own length fields, so moving a byte from one
// to the other is a different message. The CAVP one-block vector's AAD is 20 octets; hashing 19 of
// them with the 20th prepended to the plaintext must not reproduce the published tag.
void test_aad_and_plaintext_are_not_interchangeable(void)
{
    uint8_t key[32], iv[12], aad[64], tag[16];
    uint8_t moved[80], ct[80], got_tag[16];
    unhex(KAT_ONE_BLOCK.key, key);
    unhex(KAT_ONE_BLOCK.iv, iv);
    size_t aadn = unhex(KAT_ONE_BLOCK.aad, aad);
    unhex(KAT_ONE_BLOCK.tag, tag);

    moved[0] = aad[aadn - 1];
    unhex(KAT_ONE_BLOCK.pt, moved + 1);
    gcm_seal(key, iv, aad, aadn - 1, moved, 17, ct, got_tag);
    TEST_ASSERT_TRUE(memcmp(tag, got_tag, 16) != 0);
}

// RFC 5647 sec 7.1: the low 8 octets are one big-endian invocation counter and the 4-octet fixed
// field never changes. Pre-loaded to all-ones, a single increment must carry through all eight.
void test_rfc5647_invocation_counter_carries(void)
{
    uint8_t iv[12] = {0x01, 0x02, 0x03, 0x04, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const uint8_t want[12] = {0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0, 0, 0, 0, 0};
    gcm_iv_increment(iv);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, iv, 12);
}

// ...and the ordinary step is +1 on the last octet, with a carry only where one is due.
void test_rfc5647_invocation_counter_steps(void)
{
    uint8_t iv[12] = {0xaa, 0xbb, 0xcc, 0xdd, 0, 0, 0, 0, 0, 0, 0x00, 0xff};
    static const uint8_t want[12] = {0xaa, 0xbb, 0xcc, 0xdd, 0, 0, 0, 0, 0, 0, 0x01, 0x00};
    gcm_iv_increment(iv);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, iv, 12);
}

// The same plaintext under a stepped counter must seal differently, and a receiver stepping in
// lockstep opens both while the initial nonce opens only the first.
void test_stepped_nonce_changes_the_record(void)
{
    uint8_t key[32], iv[12];
    for (int i = 0; i < 32; i++)
    {
        key[i] = (uint8_t)(i * 7 + 1);
    }
    for (int i = 0; i < 12; i++)
    {
        iv[i] = (uint8_t)(0x10 + i);
    }
    const uint8_t aad[4] = {0, 0, 0, 16};
    uint8_t msg[16];
    for (int i = 0; i < 16; i++)
    {
        msg[i] = (uint8_t)(0xa0 + i);
    }

    uint8_t enc_iv[12];
    memcpy(enc_iv, iv, 12);
    uint8_t p0[32], p1[32];
    gcm_seal(key, enc_iv, aad, 4, msg, 16, p0, p0 + 16);
    gcm_iv_increment(enc_iv);
    gcm_seal(key, enc_iv, aad, 4, msg, 16, p1, p1 + 16);
    TEST_ASSERT_TRUE(memcmp(p0, p1, 32) != 0);

    uint8_t dec_iv[12], r[16];
    memcpy(dec_iv, iv, 12);
    TEST_ASSERT_TRUE(gcm_open(key, dec_iv, aad, 4, p0, 16, p0 + 16, r));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(msg, r, 16);
    gcm_iv_increment(dec_iv);
    TEST_ASSERT_TRUE(gcm_open(key, dec_iv, aad, 4, p1, 16, p1 + 16, r));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(msg, r, 16);
    TEST_ASSERT_FALSE(gcm_open(key, iv, aad, 4, p1, 16, p1 + 16, r));
}

// NIST SP 800-38D sec 6.5: GCTR steps the low 32 bits of the counter block once per 16-octet block,
// starting at inc32(J0) = ..00 00 00 02. Byte 15 rolls 0xff -> 0x00 into byte 14 after 254 blocks,
// so a record of 255 blocks crosses that carry inside one seal; the round trip proves the wrapped
// keystream is the same one the open side reproduces.
void test_gctr_counter_byte_carry(void)
{
    static uint8_t pt[255 * 16];
    static uint8_t out[255 * 16 + 16];
    static uint8_t rt[255 * 16];
    uint8_t key[32], iv[12];
    for (int i = 0; i < 32; i++)
    {
        key[i] = (uint8_t)(i * 3 + 5);
    }
    for (int i = 0; i < 12; i++)
    {
        iv[i] = (uint8_t)(0x20 + i);
    }
    for (size_t i = 0; i < sizeof(pt); i++)
    {
        pt[i] = (uint8_t)(i * 31 + 7);
    }

    gcm_seal(key, iv, NULL, 0, pt, sizeof(pt), out, out + sizeof(pt));
    TEST_ASSERT_TRUE(gcm_open(key, iv, NULL, 0, out, sizeof(pt), out + sizeof(pt), rt));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, rt, sizeof(pt));
}

// Sealing in place (ct_out == pt) must produce the same record as sealing into a separate buffer.
void test_seal_in_place(void)
{
    uint8_t key[32], iv[12], pt[64], aad[64], ct[64], tag[16];
    uint8_t inplace[64], inplace_tag[16];
    unhex(KAT_PARTIAL.key, key);
    unhex(KAT_PARTIAL.iv, iv);
    size_t ptn = unhex(KAT_PARTIAL.pt, pt);
    size_t aadn = unhex(KAT_PARTIAL.aad, aad);
    unhex(KAT_PARTIAL.ct, ct);
    unhex(KAT_PARTIAL.tag, tag);

    memcpy(inplace, pt, ptn);
    gcm_seal(key, iv, aad, aadn, inplace, ptn, inplace, inplace_tag);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ct, inplace, ptn);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tag, inplace_tag, 16);

    TEST_ASSERT_TRUE(gcm_open(key, iv, aad, aadn, inplace, ptn, inplace_tag, inplace));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, inplace, ptn);
}
