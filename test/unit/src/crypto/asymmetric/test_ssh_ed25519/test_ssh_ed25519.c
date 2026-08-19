// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for Curve25519 (crypto/asymmetric/curve25519.h) and the Ed25519 signatures built on
// its field arithmetic (crypto/asymmetric/ed25519.h) - the curve25519-sha256 KEX and the
// ssh-ed25519 host key.
//
// Two load-bearing cases. test_rfc7748_scalar_multiplication reproduces both X25519 vectors from
// RFC 7748 sec 5.2 and the iterated results the same section publishes after 1 and 1,000 rounds;
// the iteration feeds each output back in as the next u-coordinate, so a single wrong limb carry
// anywhere in the ladder diverges long before round 1,000. test_rfc8032_signature_vectors
// reproduces the four RFC 8032 sec 7.1 vectors byte for byte, public key and signature both, which
// pins the clamping, the deterministic nonce, the scalar reduction mod L, and the encoding.

#include "crypto/asymmetric/curve25519/curve25519.h"
#include "crypto/asymmetric/ed25519/ed25519.h"
#include <string.h>

#include <unity.h>

// Each module states the bytes its entries run out of, and they are not the same: Ed25519 carries a
// signature working set on top of the SHA-512 region it drives, and X25519 carries a ladder.
static uint8_t g_work[PROTOCORE_ED25519_BORROW] __attribute__((aligned(8)));
static uint8_t g_c25519[PROTOCORE_CURVE25519_BORROW] __attribute__((aligned(8)));

void setUp(void)
{
}
void tearDown(void)
{
}

// The namespaces, called the way the vectors below read: operands in, one call, answer out.
static void x25519(uint8_t *out, const uint8_t *scalar, const uint8_t *point)
{
    Curve25519.x25519_args.scalar = scalar;
    Curve25519.x25519_args.point = point;
    Curve25519.x25519_args.out = out;
    Curve25519.x25519(g_c25519);
}

static void x25519_base(uint8_t *out, const uint8_t *scalar)
{
    Curve25519.x25519_base_args.scalar = scalar;
    Curve25519.x25519_base_args.out = out;
    Curve25519.x25519_base(g_c25519);
}

static void ed_pubkey(uint8_t *w, uint8_t *pub, const uint8_t *seed)
{
    Ed25519.pubkey_args.seed = seed;
    Ed25519.pubkey_args.pub = pub;
    Ed25519.pubkey(w);
}

static void ed_sign(uint8_t *w, uint8_t *sig, const uint8_t *msg, size_t mlen, const uint8_t *seed)
{
    Ed25519.sign_args.seed = seed;
    Ed25519.sign_args.msg = msg;
    Ed25519.sign_args.msg_len = mlen;
    Ed25519.sign_args.sig = sig;
    Ed25519.sign(w);
}

static proto_bool ed_verify(uint8_t *w, const uint8_t *pub, const uint8_t *msg, size_t mlen, const uint8_t *sig)
{
    Ed25519.verify_args.pub = pub;
    Ed25519.verify_args.msg = msg;
    Ed25519.verify_args.msg_len = mlen;
    Ed25519.verify_args.sig = sig;
    Ed25519.verify(w);
    return Ed25519.ok;
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

// ---- X25519 (RFC 7748) ----------------------------------------------------

static void x25519_case(const char *scalar_hex, const char *u_hex, const char *want_hex)
{
    uint8_t scalar[32], u[32], want[32], got[32];
    unhex(scalar_hex, scalar);
    unhex(u_hex, u);
    unhex(want_hex, want);
    x25519(got, scalar, u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

// RFC 7748 sec 5.2, both X25519 input/output triples, then the iterated test from the same section:
// k = u = the base point 9; each round sets (k, u) = (X25519(k, u), k).
void test_rfc7748_scalar_multiplication(void)
{
    x25519_case("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
                "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c",
                "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");
    x25519_case("4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d",
                "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493",
                "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957");

    uint8_t k[32] = {9}, u[32] = {9}, want[32];
    for (int i = 1; i <= 1000; i++)
    {
        uint8_t r[32];
        x25519(r, k, u);
        memcpy(u, k, 32);
        memcpy(k, r, 32);
        if (i == 1)
        {
            unhex("422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079", want);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(want, k, 32);
        }
    }
    unhex("684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51", want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, k, 32);
}

// RFC 7748 sec 6.1 prints Alice's and Bob's private keys, the two public keys X25519(a, 9) and
// X25519(b, 9), and the shared secret both sides reach.
void test_rfc7748_diffie_hellman_vector(void)
{
    uint8_t a[32], b[32], want[32], got[32], apub[32], bpub[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a);
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b);

    x25519_base(apub, a);
    unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, apub, 32);

    x25519_base(bpub, b);
    unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, bpub, 32);

    unhex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", want);
    x25519(got, a, bpub);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
    x25519(got, b, apub);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

// RFC 7748 sec 5: "implementations of X25519 ... MUST mask the most significant bit in the final
// byte" of a received u-coordinate. Every published vector is already canonical, so setting bit 255
// is the only way to reach that mask; the result may not move.
void test_rfc7748_high_bit_of_u_is_masked(void)
{
    uint8_t scalar[32], u[32], u_set[32], from_clear[32], from_set[32];
    unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", scalar);
    unhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u);
    memcpy(u_set, u, 32);
    u_set[31] |= 0x80u;

    x25519(from_clear, scalar, u);
    x25519(from_set, scalar, u_set);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(from_clear, from_set, 32);
}

// RFC 7748 sec 6.1: a peer may check the shared value for all-zero, which is what a small-order
// u-coordinate produces. u = 0 and u = 1 are two of the published small-order points.
void test_small_order_points_yield_zero(void)
{
    uint8_t scalar[32], u[32], out[32];
    static const uint8_t ZERO[32] = {0};
    unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", scalar);

    memset(u, 0, 32); // u = 0, the point of order 1
    x25519(out, scalar, u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO, out, 32);

    memset(u, 0, 32);
    u[0] = 1; // u = 1, the point of order 2
    x25519(out, scalar, u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO, out, 32);
}

// ---- Ed25519 (RFC 8032 sec 7.1) -------------------------------------------

static void ed_case(const char *seed_hex, const char *msg_hex, const char *pub_hex, const char *sig_hex)
{
    uint8_t seed[32], pub[32], sig[64], msg[64], got_pub[32], got_sig[64];
    unhex(seed_hex, seed);
    unhex(pub_hex, pub);
    unhex(sig_hex, sig);
    size_t mlen = unhex(msg_hex, msg);

    ed_pubkey(g_work, got_pub, seed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pub, got_pub, 32);

    ed_sign(g_work, got_sig, msg, mlen, seed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(sig, got_sig, 64);

    TEST_ASSERT_TRUE(ed_verify(g_work, pub, msg, mlen, sig));
}

// TEST 1 (empty message), TEST 2 (one octet), TEST 3 (two octets), TEST SHA(abc) (64 octets).
void test_rfc8032_signature_vectors(void)
{
    ed_case("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", "",
            "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
            "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46b"
            "d25bf5f0595bbe24655141438e7a100b");
    ed_case("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", "72",
            "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
            "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c"
            "387b2eaeb4302aeeb00d291612bb0c00");
    ed_case("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7", "af82",
            "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
            "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc659"
            "4a7c15e9716ed28dc027beceea1ec40a");
    ed_case("833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42",
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
            "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
            "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
            "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b58909351fc9ac90b3ecfdfbc7c66431e030"
            "3dca179c138ac17ad9bef1177331a704");
}

// A change to R, to S, to the message, or to the public key breaks the group equation.
void test_verify_refuses_tampering(void)
{
    uint8_t seed[32], pub[32], sig[64], bad[64], msg[2];
    unhex("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7", seed);
    unhex("fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", pub);
    unhex("6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc659"
          "4a7c15e9716ed28dc027beceea1ec40a",
          sig);
    unhex("af82", msg);
    TEST_ASSERT_TRUE(ed_verify(g_work, pub, msg, 2, sig));

    memcpy(bad, sig, 64);
    bad[0] ^= 0x01; // R
    TEST_ASSERT_FALSE(ed_verify(g_work, pub, msg, 2, bad));

    memcpy(bad, sig, 64);
    bad[32] ^= 0x01; // S, still canonical
    TEST_ASSERT_FALSE(ed_verify(g_work, pub, msg, 2, bad));

    uint8_t badmsg[2] = {0xaf, 0x83};
    TEST_ASSERT_FALSE(ed_verify(g_work, pub, badmsg, 2, sig));

    uint8_t badpub[32];
    memcpy(badpub, pub, 32);
    badpub[0] ^= 0x01;
    TEST_ASSERT_FALSE(ed_verify(g_work, badpub, msg, 2, sig));

    // The empty message is a different message from a one-octet one, even for the same key.
    TEST_ASSERT_FALSE(ed_verify(g_work, pub, msg, 1, sig));
}

// RFC 8032 sec 5.1.7 step 1 decodes S "in the range 0 <= s < L" and calls the signature invalid when
// that decoding fails. S and S+L both satisfy the group equation, so an implementation that skips
// the range check accepts a second, different encoding of the same signature.
void test_verify_refuses_non_canonical_s(void)
{
    // L = 2^252 + 27742317777372353535851937790883648493, little-endian, from RFC 8032 sec 5.1.
    static const uint8_t L_LE[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                     0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};
    uint8_t pub[32], sig[64], bad[64], msg[2];
    unhex("fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", pub);
    unhex("6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc659"
          "4a7c15e9716ed28dc027beceea1ec40a",
          sig);
    unhex("af82", msg);

    memcpy(bad, sig, 64);
    memcpy(bad + 32, L_LE, 32); // S = L, the first value out of range
    TEST_ASSERT_FALSE(ed_verify(g_work, pub, msg, 2, bad));

    memcpy(bad, sig, 64);
    memset(bad + 32, 0xFF, 32); // S = 2^256-1, far above L
    TEST_ASSERT_FALSE(ed_verify(g_work, pub, msg, 2, bad));
}

// RFC 8032 sec 5.1.7: a public key that does not decode to a curve point makes the signature
// invalid. Roughly half of all 32-octet strings are not valid encodings, so sweeping candidates
// with an in-range S drives the decode-failure arm; none can verify, valid point or not, because
// the message does not match the signature.
void test_verify_refuses_an_undecodable_public_key(void)
{
    uint8_t sig[64], msg[2] = {0x00, 0x01};
    unhex("6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc659"
          "4a7c15e9716ed28dc027beceea1ec40a",
          sig);
    for (int i = 0; i < 64; i++)
    {
        uint8_t cand[32];
        memset(cand, 0, 32);
        cand[0] = (uint8_t)i;
        cand[16] = (uint8_t)(i * 7 + 1);
        TEST_ASSERT_FALSE(ed_verify(g_work, cand, msg, 2, sig));
    }
}

// Signing is deterministic (RFC 8032 sec 5.1.6 derives the nonce from the key and the message), so
// the same inputs must produce the same 64 octets every time, and a long multi-block message must
// still round-trip.
void test_signing_is_deterministic_and_round_trips(void)
{
    uint8_t seed[32], pub[32], s1[64], s2[64], msg[200];
    for (int i = 0; i < 32; i++)
    {
        seed[i] = (uint8_t)(i * 7 + 1);
    }
    for (int i = 0; i < 200; i++)
    {
        msg[i] = (uint8_t)(i ^ 0x5a);
    }
    ed_pubkey(g_work, pub, seed);
    ed_sign(g_work, s1, msg, sizeof(msg), seed);
    ed_sign(g_work, s2, msg, sizeof(msg), seed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(s1, s2, 64);
    TEST_ASSERT_TRUE(ed_verify(g_work, pub, msg, sizeof(msg), s1));
}

// ---- GF(2^255-19) field arithmetic ----------------------------------------

// pack() emits the canonical little-endian encoding, so unpack then pack is the identity on any
// canonical input, and the encoding's high bit is ignored on the way in (RFC 7748 sec 5).
void test_gf_pack_unpack_round_trip(void)
{
    uint8_t in[32], out[32];
    protocore_gf a;
    unhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", in);
    protocore_gf_unpack(a, in);
    protocore_gf_pack(out, a);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 32);

    in[31] |= 0x80u;
    protocore_gf_unpack(a, in);
    protocore_gf_pack(out, a);
    in[31] &= 0x7Fu;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 32);
}

// a * 1 = a, a * 0 = 0, and a^2 is the same element as a * a: three identities the field must obey
// whatever representation the limbs use.
void test_gf_multiplication_identities(void)
{
    uint8_t bytes[32], p1[32], p2[32];
    protocore_gf a, one, zero, r1, r2;
    unhex("4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d", bytes);
    protocore_gf_unpack(a, bytes);

    uint8_t enc[32];
    memset(enc, 0, 32);
    enc[0] = 1;
    protocore_gf_unpack(one, enc);
    memset(enc, 0, 32);
    protocore_gf_unpack(zero, enc);

    protocore_gf_mul(r1, a, one);
    protocore_gf_pack(p1, r1);
    protocore_gf_pack(p2, a);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p2, p1, 32);

    protocore_gf_mul(r1, a, zero);
    protocore_gf_pack(p1, r1);
    memset(enc, 0, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(enc, p1, 32);

    protocore_gf_sq(r1, a);
    protocore_gf_mul(r2, a, a);
    protocore_gf_pack(p1, r1);
    protocore_gf_pack(p2, r2);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p2, p1, 32);
}

// a + b - b = a, and a * a^-1 = 1 for a non-zero a.
void test_gf_add_sub_and_inverse(void)
{
    uint8_t ab[32], bb[32], p[32], want[32];
    protocore_gf a, b, t, r;
    unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", ab);
    unhex("e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493", bb);
    protocore_gf_unpack(a, ab);
    protocore_gf_unpack(b, bb);

    protocore_gf_add(t, a, b);
    protocore_gf_sub(r, t, b);
    protocore_gf_pack(p, r);
    protocore_gf_pack(want, a);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, p, 32);

    protocore_gf_inv(t, a);
    protocore_gf_mul(r, a, t);
    protocore_gf_pack(p, r);
    memset(want, 0, 32);
    want[0] = 1;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, p, 32);
}

// cswap exchanges the pair when the bit is 1 and leaves it alone when the bit is 0 - the ladder's
// only data-dependent step, and the reason it is branch-free.
void test_gf_cswap(void)
{
    uint8_t ab[32], bb[32], a0[32], b0[32], pa[32], pb[32];
    protocore_gf a, b;
    unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", ab);
    unhex("e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493", bb);
    protocore_gf_unpack(a, ab);
    protocore_gf_unpack(b, bb);
    protocore_gf_pack(a0, a); // the canonical encodings, which is what unpack of a bit-255 input gives
    protocore_gf_pack(b0, b);

    protocore_gf_cswap(a, b, 0);
    protocore_gf_pack(pa, a);
    protocore_gf_pack(pb, b);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a0, pa, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b0, pb, 32);

    protocore_gf_cswap(a, b, 1);
    protocore_gf_pack(pa, a);
    protocore_gf_pack(pb, b);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b0, pa, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a0, pb, 32);
    TEST_ASSERT_TRUE(memcmp(a0, b0, 32) != 0); // the two really did move
}

// p-1 is the largest canonical element; p itself and 2^255-1 both reduce back into range, so pack
// must never emit an encoding at or above p.
void test_gf_pack_reduces_to_canonical(void)
{
    // p = 2^255 - 19, little-endian: ed ff ... ff 7f
    uint8_t p_le[32], out[32], want[32];
    protocore_gf a;
    memset(p_le, 0xFF, 32);
    p_le[0] = 0xED;
    p_le[31] = 0x7F;

    protocore_gf_unpack(a, p_le);
    protocore_gf_pack(out, a);
    memset(want, 0, 32); // p == 0 mod p
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 32);

    p_le[0] = 0xEC; // p-1, already canonical
    protocore_gf_unpack(a, p_le);
    protocore_gf_pack(out, a);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p_le, out, 32);
}
