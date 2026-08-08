// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// base64 codec tests, anchored on the RFC 4648 sec 10 vectors, both alphabets, and the constant-time
// decoder's edge cases (padding, invalid characters, wrong length, over-capacity). A round-trip fuzz over
// pseudo-random inputs cross-checks encode against the new constant-time decode. Timing-invariance of the
// decode is verified separately on the ESP32-S3 (CCOUNT).

#include "network_drivers/presentation/codec/base64/base64.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static void expect_encode(const char *in, const char *want)
{
    char out[128];
    Base64.encode((const uint8_t *)in, strlen(in), out);
    TEST_ASSERT_EQUAL_STRING(want, out);
}

static void expect_decode(const char *in, const char *want)
{
    uint8_t out[128];
    size_t n = Base64.decode(in, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(strlen(want), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)want, out, n);
}

// RFC 4648 sec 10 - the canonical standard-alphabet vectors, both directions.
static void test_rfc4648_vectors()
{
    expect_encode("", "");
    expect_encode("f", "Zg==");
    expect_encode("fo", "Zm8=");
    expect_encode("foo", "Zm9v");
    expect_encode("foob", "Zm9vYg==");
    expect_encode("fooba", "Zm9vYmE=");
    expect_encode("foobar", "Zm9vYmFy");

    expect_decode("Zg==", "f");
    expect_decode("Zm8=", "fo");
    expect_decode("Zm9v", "foo");
    expect_decode("Zm9vYg==", "foob");
    expect_decode("Zm9vYmE=", "fooba");
    expect_decode("Zm9vYmFy", "foobar");
}

// The '+' '/' (standard) vs '-' '_' (url) alphabets must decode the same bytes and reject each other.
static void test_alphabets()
{
    const uint8_t in[3] = {0xFF, 0xEF, 0xFF}; // -> six-bit values 63,62,63,63
    char std_enc[8], url_enc[8];
    Base64.encode(in, 3, std_enc);
    Base64.url_encode(in, 3, url_enc);
    TEST_ASSERT_EQUAL_STRING("/+//", std_enc);
    TEST_ASSERT_EQUAL_STRING("_-__", url_enc);

    uint8_t out[4];
    TEST_ASSERT_EQUAL_UINT(3, Base64.url_decode("_-__", 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 3);
    // base64url must reject the standard '+' '/' (RFC 7515 - one alphabet, not a mix).
    TEST_ASSERT_EQUAL_UINT(0, Base64.url_decode("/+//", 4, out, sizeof(out)));
    // standard decode must reject the url '-' '_'.
    TEST_ASSERT_EQUAL_UINT(0, Base64.decode("_-__", out, sizeof(out)));
}

static void test_decode_rejects_malformed()
{
    uint8_t out[64];
    TEST_ASSERT_EQUAL_UINT(0, Base64.decode("Zm9", out, sizeof(out)));          // length not a multiple of 4
    TEST_ASSERT_EQUAL_UINT(0, Base64.decode("Zm=v", out, sizeof(out)));         // '=' not at the tail
    TEST_ASSERT_EQUAL_UINT(0, Base64.decode("Zg=x", out, sizeof(out)));         // lone pad in the 3rd slot
    TEST_ASSERT_EQUAL_UINT(0, Base64.decode("Zm9vYg==Zm9v", out, sizeof(out))); // padding mid-stream
    TEST_ASSERT_EQUAL_UINT(0, Base64.decode("Zm9 ", out, sizeof(out)));         // space is not in the alphabet
    TEST_ASSERT_EQUAL_UINT(0, Base64.decode("Z@9v", out, sizeof(out)));         // '@' is not in the alphabet
}

static void test_decode_capacity_guard()
{
    uint8_t out[2];
    // "foobar" decodes to 6 bytes; a 2-byte buffer must fail rather than overrun.
    TEST_ASSERT_EQUAL_UINT(0, Base64.decode("Zm9vYmFy", out, sizeof(out)));
    // Exactly-fitting capacity succeeds.
    uint8_t exact[6];
    TEST_ASSERT_EQUAL_UINT(6, Base64.decode("Zm9vYmFy", exact, sizeof(exact)));
}

// Zero and one-byte capacities exercise the per-output-byte guard on the *first* and *second* byte of an
// unpadded quad (the third-byte guard is already hit by test_decode_capacity_guard above).
static void test_decode_capacity_guard_first_and_second_byte()
{
    uint8_t out0[1];
    TEST_ASSERT_EQUAL_UINT(0, Base64.decode("Zm9v", out0, 0)); // dst_cap==0: fails before byte 0
    uint8_t out1[1];
    TEST_ASSERT_EQUAL_UINT(0, Base64.decode("Zm9v", out1, 1)); // dst_cap==1: fails before byte 1
}

// Round-trip fuzz: encode a pseudo-random byte string, decode it back, require an exact match. Cross-checks
// the constant-time decoder against the (independent) encoder over every tail length (0/1/2 mod 3).
static void test_roundtrip_fuzz()
{
    uint32_t s = 0x12345678u; // deterministic LCG (no rand(): reproducible across runs/platforms)
    for (size_t len = 0; len <= 96; len++)
    {
        uint8_t in[96];
        for (size_t i = 0; i < len; i++)
        {
            s = s * 1664525u + 1013904223u;
            in[i] = (uint8_t)(s >> 24);
        }
        char enc[132];
        Base64.encode(in, len, enc);
        uint8_t dec[96];
        size_t n = Base64.decode(enc, dec, sizeof(dec));
        TEST_ASSERT_EQUAL_UINT(len, n);

        // base64url round-trip (no padding), decoded with the explicit length.
        char uenc[132];
        size_t ulen = Base64.url_encode(in, len, uenc);
        uint8_t udec[96];
        size_t un = Base64.url_decode(uenc, ulen, udec, sizeof(udec));
        TEST_ASSERT_EQUAL_UINT(len, un);

        if (len > 0) // Unity rejects a zero-length array compare (empty input is covered by the n==0 check)
        {
            TEST_ASSERT_EQUAL_UINT8_ARRAY(in, dec, len);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(in, udec, len);
        }
    }
}

// base64url decode stops at an optional trailing '=' (RFC 4648 sec 5 carries no padding, but a caller
// that hands in a padded string must still be handled: the scan stops there rather than misclassifying it).
static void test_url_decode_stops_at_padding()
{
    uint8_t out[8];
    size_t n = Base64.url_decode("Zm9v=", 5, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"foo", out, 3);
}

// url decode's own per-output-byte capacity guard: dst_cap==0 must fail as soon as 8 bits have
// accumulated (2 input characters), rather than overrun.
static void test_url_decode_capacity_guard()
{
    uint8_t out[1];
    TEST_ASSERT_EQUAL_UINT(0, Base64.url_decode("Zm9v", 4, out, 0));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_rfc4648_vectors);
    RUN_TEST(test_alphabets);
    RUN_TEST(test_decode_rejects_malformed);
    RUN_TEST(test_decode_capacity_guard);
    RUN_TEST(test_decode_capacity_guard_first_and_second_byte);
    RUN_TEST(test_url_decode_stops_at_padding);
    RUN_TEST(test_url_decode_capacity_guard);
    RUN_TEST(test_roundtrip_fuzz);
    return UNITY_END();
}
