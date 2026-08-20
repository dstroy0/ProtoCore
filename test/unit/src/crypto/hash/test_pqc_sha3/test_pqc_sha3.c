// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Keccak-f[1600] sponge (crypto/hash/sha3.h): SHA3-256, SHA3-512, SHAKE128 and
// SHAKE256, the symmetric core ML-KEM is built on.
//
// The load-bearing cases are the NIST CAVP FIPS 202 byte-oriented known-answer vectors below, taken
// verbatim from SHA3_256ShortMsg.rsp, SHA3_512ShortMsg.rsp, SHAKE128ShortMsg.rsp and
// SHAKE256ShortMsg.rsp. Each mode is pinned at four message lengths chosen for the padding rule:
// empty, a short message, rate-1 octets, and exactly one rate. rate-1 is the one that matters -
// there the domain-separation byte and the 0x80 terminator land on the SAME state octet and must
// merge (0x86 for SHA-3, 0x9F for SHAKE); at every other length they are separate writes and a
// wrong pad is invisible.

#include "crypto/hash/sha3/sha3.h"
#include <string.h>

#include <unity.h>

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

// ---- NIST CAVP FIPS 202 vectors -------------------------------------------
// SHA3-256, rate 136: Len 0, 24, 1080 (135 octets = rate-1) and 1088 (136 = one rate).
static const char *const S256_MSG[4] = {
    "", "b053fa",
    "b1f6076509938432145bb15dbe1a7b2e007934be5f753908b50fd24333455970a7429f2ffbd28bd6fe1804c4688311f3"
    "18fe3fcd9f6744410243e115bcb00d7e039a4fee4c326c2d119c42abd2e8f4155a44472643704cc0bc72403b8a8ab0fd"
    "4d68e04a059d6e5ed45033b906326abb4eb4147052779bad6a03b55ca5bd8b140e131bed2dfada",
    "56ea14d7fcb0db748ff649aaa5d0afdc2357528a9aad6076d73b2805b53d89e73681abfad26bee6c0f3d20215295f354"
    "f538ae80990d2281be6de0f6919aa9eb048c26b524f4d91ca87b54c0c54aa9b54ad02171e8bf31e8d158a9f586e92ffc"
    "e994ecce9a5185cc80364d50a6f7b94849a914242fcb73f33a86ecc83c3403630d20650ddb8cd9c4"};
static const char *const S256_MD[4] = {"a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a",
                                       "9d0ff086cd0ec06a682c51c094dc73abdc492004292344bd41b82a60498ccfdb",
                                       "f82d9602b231d332d902cb6436b15aef89acc591cb8626233ced20c0a6e80d7a",
                                       "4beae3515ba35ec8cbd1d94567e22b0d7809c466abfbafe9610349597ba15b45"};

// SHA3-512, rate 72: Len 0, 24, 568 (71 = rate-1) and 576 (72 = one rate).
static const char *const S512_MSG[4] = {
    "", "37d518",
    "b0de0430c200d74bf41ea0c92f8f28e11b68006a884e0d4b0d884533ee58b38a438cc1a75750b6434f467e2d0cd9aa40"
    "52ceb793291b93ef83fd5d8620456ce1aff2941b3605a4",
    "0ce9f8c3a990c268f34efd9befdb0f7c4ef8466cfdb01171f8de70dc5fefa92acbe93d29e2ac1a5c2979129f1ab08c0e"
    "77de7924ddf68a209cdfa0adc62f85c18637d9c6b33f4ff8"};
static const char *const S512_MD[4] = {
    "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a615b2123af1f5f94c11e3e9402c3ac558"
    "f500199d95b6d3e301758586281dcd26",
    "4aa96b1547e6402c0eee781acaa660797efe26ec00b4f2e0aec4a6d10688dd64cbd7f12b3b6c7f802e2096c041208b92"
    "89aec380d1a748fdfcd4128553d781e3",
    "9e9e469ca9226cd012f5c9cc39c96adc22f420030fcee305a0ed27974e3c802701603dac873ae4476e9c3d57e5552448"
    "3fc01adaef87daa9e304078c59802757",
    "b018a20fcf831dde290e4fb18c56342efe138472cbe142da6b77eea4fce52588c04c808eb32912faa345245a850346fa"
    "ec46c3a16d39bd2e1ddb1816bc57d2da"};

// SHAKE128, rate 168, 128-bit output: Len 0, 24, 1336 (167 = rate-1) and 1344 (168 = one rate).
static const char *const K128_MSG[4] = {
    "", "1b3b6e",
    "18636f702f216b1b9302e59d82192f4e002f82d526c3f04cbd4f9b9f0bcd2535ed7a67d326da66bdf7fc821ef0fff1a9"
    "05d56c81e4472856863908d104301133ad111e39552cd542ef78d9b35f20419b893f4a93aee848e9f86ae3fd53d27fea"
    "7fb1fc69631fa0f3a5ff51267785086ab4f682d42baf394b3b6992e9a0bb58a38ce0692df9bbaf183e18523ee1352c5f"
    "ad817e0c04a3e1c476be7f5e92f482a6fb29cd4bbf09ea",
    "5d9ff9fe63c328ddbe0c865ac6ba605c52a14ee8e4870ba320ce849283532f2551959e74cf1a54c8b30ed75dd92e0766"
    "37e4ad5213b3574e73d6640bd6245bc121378174dccdaa769e6e4f2dc650e1166c775d0a982021c0b160fe9438098e86"
    "b6cdc786f2a6d1ef68751551f7e99773daa28598d9961002c0b47ab511c8707df69f9b32796b723bf7685251d2c0d085"
    "67ad4e8540ddcc1b8a1a01f6c92aaaadcaf42301d9e53463"};
static const char *const K128_OUT[4] = {"7f9c2ba4e88f827d616045507605853e", "d7335497e4cd3666885edbb0824d7a75",
                                        "b7b9db481898f888e5ee4ed629859844", "f50af2684408915871948779a14c147c"};

// SHAKE256, rate 136, 256-bit output: Len 0, 24, 1080 (135 = rate-1) and 1088 (136 = one rate).
static const char *const K256_MSG[4] = {
    "", "21eda6",
    "362f1eb00b37a9613b1ae82b90452579d42f8b1f9ede95f86badc6cdf04c9b79af08be4bc94d7cac136979026b92a2d4"
    "4d2b642ea1431b47d75fce61367919f171486a007cc271d19de0d1c4c6a11c7a2251fe3aee0bb8938a7dd043d0eb0758"
    "a4768c95cc9f6f1703075839487879b47c29c10b2c3e5326ac8f363c65aa4ef76f1b8bd363eb60",
    "d8f12b97f81d47aebbfb7314ff04172cf2be71c3778e238bcccdeecb691fbd542b00e5b7b1a0abb507f107f781fea700"
    "ea7e375fdea9e029754a0ea62216774bda3c59e8783d022360fe9625621c0d93e27f7bc03632942150716f019d048a75"
    "2ccc0f93139c55df0f4aaa066a0550cf22e8c54e47d0475ba56b9842a392ffbc6bd98f1e4b64abd1"};
static const char *const K256_OUT[4] = {"46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f",
                                        "f7d02b4512be5ddcc25d148c71664dfd34e16abea26d6e7287f45e08ed6fcd87",
                                        "c6ce60c1852ea780ed845aac4ca6a30e09f5c0064c9675865178717cfeb1dc97",
                                        "e2e1c432dd07c2ee89a78f31211c92eeb5306c4fa4db93c4e5cd43080d6079e4"};

// SHA3_256LongMsg.rsp, first row: Len 2184 (273 octets), which is three absorb blocks at rate 136.
static const char *const S256_LONG_MSG =
    "b1caa396771a09a1db9bc20543e988e359d47c2a616417bbca1b62cb02796a888fc6eeff5c0b5c3d5062fcb4256f6ae1"
    "782f492c1cf03610b4a1fb7b814c057878e1190b9835425c7a4a0e182ad1f91535ed2a35033a5d8c670e21c575ff43c1"
    "94a58a82d4a1a44881dd61f9f8161fc6b998860cbe4975780be93b6f87980bad0a99aa2cb7556b478ca35d1f3746c33e"
    "2bb7c47af426641cc7bbb3425e2144820345e1d0ea5b7da2c3236a52906acdc3b4d34e474dd714c0c40bf006a3a1d889"
    "a632983814bbc4a14fe5f159aa89249e7c738b3b73666bac2a615a83fd21ae0a1ce7352ade7b278b587158fd2fabb217"
    "aa1fe31d0bda53272045598015a8ae4d8cec226fefa58daa05500906c4d85e7567";
static const char *const S256_LONG_MD = "cb5648a1d61c6c5bdacd96f81c9591debc3950dcf658145b8d996570ba881a05";

// The bytes the entries run out of. The borrow IS the sponge: a streaming absorb/squeeze run carries
// its state here rather than in a context the caller names, which is why no KeccakCtx appears below.
static uint8_t g_ws[PROTOCORE_SHA3_BORROW] __attribute__((aligned(8)));

// The namespace, called the way the vectors below read: operands in, one call, answer out.
static void sha3_256(uint8_t *out, const uint8_t *in, size_t inlen)
{
    Sha3V.digest_args.out = out;
    Sha3V.digest_args.in = in;
    Sha3V.digest_args.inlen = inlen;
    Sha3.sha3_256(g_ws);
}

static void sha3_512(uint8_t *out, const uint8_t *in, size_t inlen)
{
    Sha3V.digest_args.out = out;
    Sha3V.digest_args.in = in;
    Sha3V.digest_args.inlen = inlen;
    Sha3.sha3_512(g_ws);
}

static void shake128(uint8_t *out, size_t outlen, const uint8_t *in, size_t inlen)
{
    Sha3V.xof_args.out = out;
    Sha3V.xof_args.outlen = outlen;
    Sha3V.xof_args.in = in;
    Sha3V.xof_args.inlen = inlen;
    Sha3.shake128(g_ws);
}

static void shake256(uint8_t *out, size_t outlen, const uint8_t *in, size_t inlen)
{
    Sha3V.xof_args.out = out;
    Sha3V.xof_args.outlen = outlen;
    Sha3V.xof_args.in = in;
    Sha3V.xof_args.inlen = inlen;
    Sha3.shake256(g_ws);
}

static void shake128_absorb(const uint8_t *in, size_t inlen)
{
    Sha3V.shake128_absorb_args.in = in;
    Sha3V.shake128_absorb_args.inlen = inlen;
    Sha3.shake128_absorb(g_ws);
}

static void keccak_absorb(uint32_t rate, const uint8_t *in, size_t inlen, uint8_t domain)
{
    Sha3V.absorb_args.rate = rate;
    Sha3V.absorb_args.in = in;
    Sha3V.absorb_args.inlen = inlen;
    Sha3V.absorb_args.domain = domain;
    Sha3.absorb(g_ws);
}

static void keccak_squeeze(uint8_t *out, size_t outlen)
{
    Sha3V.squeeze_args.out = out;
    Sha3V.squeeze_args.outlen = outlen;
    Sha3.squeeze(g_ws);
}

static uint8_t g_msg[512];
static uint8_t g_want[64];
static uint8_t g_got[64];

void test_fips202_sha3_256(void)
{
    for (int i = 0; i < 4; i++)
    {
        size_t n = unhex(S256_MSG[i], g_msg);
        unhex(S256_MD[i], g_want);
        sha3_256(g_got, g_msg, n);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(g_want, g_got, 32, S256_MD[i]);
    }
}

void test_fips202_sha3_256_three_blocks(void)
{
    size_t n = unhex(S256_LONG_MSG, g_msg);
    TEST_ASSERT_EQUAL_UINT(273u, n);
    unhex(S256_LONG_MD, g_want);
    sha3_256(g_got, g_msg, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_want, g_got, 32);
}

void test_fips202_sha3_512(void)
{
    for (int i = 0; i < 4; i++)
    {
        size_t n = unhex(S512_MSG[i], g_msg);
        unhex(S512_MD[i], g_want);
        sha3_512(g_got, g_msg, n);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(g_want, g_got, 64, S512_MD[i]);
    }
}

void test_fips202_shake128(void)
{
    for (int i = 0; i < 4; i++)
    {
        size_t n = unhex(K128_MSG[i], g_msg);
        unhex(K128_OUT[i], g_want);
        shake128(g_got, 16, g_msg, n);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(g_want, g_got, 16, K128_OUT[i]);
    }
}

void test_fips202_shake256(void)
{
    for (int i = 0; i < 4; i++)
    {
        size_t n = unhex(K256_MSG[i], g_msg);
        unhex(K256_OUT[i], g_want);
        shake256(g_got, 32, g_msg, n);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(g_want, g_got, 32, K256_OUT[i]);
    }
}

// The rates the header names are the FIPS 202 ones: rate = 200 - 2*capacity/8, capacity twice the
// security strength. SHA3-256 -> 200-64 = 136, SHA3-512 -> 200-128 = 72, SHAKE128 -> 200-32 = 168,
// SHAKE256 -> 200-64 = 136.
void test_fips202_rates(void)
{
    TEST_ASSERT_EQUAL_UINT(136u, (unsigned)KECCAK_RATE_SHA3_256);
    TEST_ASSERT_EQUAL_UINT(72u, (unsigned)KECCAK_RATE_SHA3_512);
    TEST_ASSERT_EQUAL_UINT(168u, (unsigned)KECCAK_RATE_SHAKE128);
    TEST_ASSERT_EQUAL_UINT(136u, (unsigned)KECCAK_RATE_SHAKE256);
}

// SHA3-256 and SHAKE256 share the 136-octet rate and differ only in the domain-separation byte
// (0x06 against 0x1F), so the same message must not produce the same first 32 octets.
void test_domain_separation_splits_sha3_from_shake(void)
{
    uint8_t sha3[32], shake[32];
    size_t n = unhex(K256_MSG[2], g_msg);
    sha3_256(sha3, g_msg, n);
    shake256(shake, 32, g_msg, n);
    TEST_ASSERT_TRUE(memcmp(sha3, shake, 32) != 0);
}

// The XOF squeezes an arbitrary run, so pulling it in pieces must give the same octets as pulling it
// whole - including across the rate boundary, which is where ML-KEM's rejection sampler lands when
// it takes three octets at a time.
void test_incremental_squeeze_matches_one_shot(void)
{
    static const uint8_t MSG[3] = {'a', 'b', 'c'};
    uint8_t whole[400], split[400];

    shake128(whole, sizeof(whole), MSG, sizeof(MSG));
    shake128_absorb(MSG, sizeof(MSG));
    for (size_t off = 0; off < sizeof(split); off += 3) // three at a time, the sampler's step
    {
        size_t take = sizeof(split) - off < 3 ? sizeof(split) - off : 3;
        keccak_squeeze(split + off, take);
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(whole, split, sizeof(whole));

    shake256(whole, sizeof(whole), MSG, sizeof(MSG));
    keccak_absorb(KECCAK_RATE_SHAKE256, MSG, sizeof(MSG), 0x1F);
    keccak_squeeze(split, 100);       // inside the first 136-octet block
    keccak_squeeze(split + 100, 300); // across two more boundaries
    TEST_ASSERT_EQUAL_HEX8_ARRAY(whole, split, sizeof(whole));
}

// A SHAKE output of n octets is a prefix of any longer output of the same input: the sponge keeps
// squeezing the same stream rather than restarting it.
void test_shake_output_is_a_prefix_stream(void)
{
    static const uint8_t MSG[3] = {'a', 'b', 'c'};
    uint8_t shortr[32], longr[400];
    shake128(shortr, sizeof(shortr), MSG, sizeof(MSG));
    shake128(longr, sizeof(longr), MSG, sizeof(MSG));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(shortr, longr, sizeof(shortr));

    shake256(shortr, sizeof(shortr), MSG, sizeof(MSG));
    shake256(longr, sizeof(longr), MSG, sizeof(MSG));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(shortr, longr, sizeof(shortr));
}

// The absorb walks the whole message: a one-octet change anywhere, including in the last block and
// in the first, changes the digest.
void test_every_message_octet_reaches_the_digest(void)
{
    uint8_t base[32], moved[32];
    size_t n = unhex(S256_MSG[3], g_msg);
    sha3_256(base, g_msg, n);
    static const size_t POS[] = {0, 1, 67, 134, 135};
    for (size_t i = 0; i < sizeof(POS) / sizeof(POS[0]); i++)
    {
        g_msg[POS[i]] ^= 0x01;
        sha3_256(moved, g_msg, n);
        TEST_ASSERT_TRUE(memcmp(base, moved, 32) != 0);
        g_msg[POS[i]] ^= 0x01;
    }
}
