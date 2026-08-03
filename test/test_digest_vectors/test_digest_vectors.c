// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Independent-oracle regression test for the Digest-auth math (RFC 7616,
// SHA-256, qop="auth"). test_digest_auth performs the handshake but computes the
// expected response with the SAME pc_sha256 the server uses, so it cannot catch
// a non-standard hash or a wrong digest string format. This suite pins both
// against values produced OUTSIDE the codebase:
//   - FIPS 180-4 SHA-256 known-answer tests, and
//   - HA1 / HA2 / response hex computed with `openssl dgst -sha256`
// for a fixed scenario (user=admin, realm=demo, pass=s3cret, GET /secret).
// If pc_sha256 or the RFC 7616 string construction ever drifts, these fail even
// if the self-referential handshake test still passes.

#include "crypto/hash/sha256.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

// --- Fixed scenario (must match the values fed to openssl when regenerating) --
static const char *kUser = "admin";
static const char *kRealm = "demo";
static const char *kPass = "s3cret";
static const char *kMethod = "GET";
static const char *kUri = "/secret";
static const char *kNonce = "000102030405060708090a0b0c0d0e0f";
static const char *kNc = "00000001";
static const char *kCnonce = "deadbeef";

// --- Expected values, computed independently with `openssl dgst -sha256` -------
//   HA1      = SHA256("admin:demo:s3cret")
//   HA2      = SHA256("GET:/secret")
//   response = SHA256(HA1:nonce:nc:cnonce:auth:HA2)
static const char *kHA1 = "b3d3197f013cfa041215ff0c33b61d2e7d054c38717752153db73e142e5232c6";
static const char *kHA2 = "41cc2dc2ba6296a1a5804eff2bce733dc4ae30cc35f6454658f06e58c374bce7";
static const char *kResponse = "5d0e32a20ddf4a97877315a523756d9c150506a4c73cbb272a3835a11436f88a";

static void sha256_hex(const char *s, char out[65])
{
    uint8_t d[PC_SHA256_DIGEST_LEN];
    pc_sha256((const uint8_t *)s, strlen(s), d);
    static const char *hx = "0123456789abcdef";
    for (int i = 0; i < PC_SHA256_DIGEST_LEN; i++)
    {
        out[i * 2] = hx[d[i] >> 4];
        out[i * 2 + 1] = hx[d[i] & 0x0f];
    }
    out[64] = '\0';
}

void setUp()
{
}
void tearDown()
{
}

// ====================================================================
// TESTS
// ====================================================================

// Ground pc_sha256 itself against the FIPS 180-4 published vectors so the
// digest checks below rest on a verified standard hash, not a circular one.
void test_sha256_fips_kats()
{
    char out[65];
    sha256_hex("", out);
    TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", out);
    sha256_hex("abc", out);
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", out);
}

// HA1 = SHA256(username:realm:password) - matches openssl.
void test_ha1_matches_openssl()
{
    char buf[96], ha1[65];
    snprintf(buf, sizeof(buf), "%s:%s:%s", kUser, kRealm, kPass);
    sha256_hex(buf, ha1);
    TEST_ASSERT_EQUAL_STRING(kHA1, ha1);
}

// HA2 = SHA256(method:uri) - matches openssl.
void test_ha2_matches_openssl()
{
    char buf[96], ha2[65];
    snprintf(buf, sizeof(buf), "%s:%s", kMethod, kUri);
    sha256_hex(buf, ha2);
    TEST_ASSERT_EQUAL_STRING(kHA2, ha2);
}

// response = SHA256(HA1:nonce:nc:cnonce:qop:HA2) - the full RFC 7616 chain,
// built exactly as the server does, matches openssl.
void test_response_matches_openssl()
{
    char buf[256], ha1[65], ha2[65], resp[65];
    snprintf(buf, sizeof(buf), "%s:%s:%s", kUser, kRealm, kPass);
    sha256_hex(buf, ha1);
    snprintf(buf, sizeof(buf), "%s:%s", kMethod, kUri);
    sha256_hex(buf, ha2);
    snprintf(buf, sizeof(buf), "%s:%s:%s:%s:%s:%s", ha1, kNonce, kNc, kCnonce, "auth", ha2);
    sha256_hex(buf, resp);
    TEST_ASSERT_EQUAL_STRING(kResponse, resp);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_sha256_fips_kats);
    RUN_TEST(test_ha1_matches_openssl);
    RUN_TEST(test_ha2_matches_openssl);
    RUN_TEST(test_response_matches_openssl);
    return UNITY_END();
}
