// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the base64 codec (network_drivers/presentation/codec/base64/base64.h).
//
// test_rfc4648_section_10_vectors is the load-bearing case: RFC 4648 sec 10 publishes the seven
// BASE64() vectors verbatim, and reproducing them octet for octet in both directions is what makes
// this codec trustworthy. Every other expectation comes from the tables and rules of the same
// document: sec 4 Table 1 assigns '+' and '/' to values 62 and 63, sec 5 Table 2 assigns '-' and '_'
// to the same two values, and sec 3.3 states that an implementation MUST reject encoded data holding
// characters outside its alphabet.

#include "network_drivers/presentation/codec/base64/base64.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}

void tearDown(void)
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
    TEST_ASSERT_EQUAL_size_t(strlen(want), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)want, out, n);
}

// RFC 4648 sec 10, printed verbatim:
//   BASE64("") = ""            BASE64("foob")   = "Zm9vYg=="
//   BASE64("f") = "Zg=="       BASE64("fooba")  = "Zm9vYmE="
//   BASE64("fo") = "Zm8="      BASE64("foobar") = "Zm9vYmFy"
//   BASE64("foo") = "Zm9v"
void test_rfc4648_section_10_vectors(void)
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

// The two alphabets differ only at values 62 and 63: sec 4 Table 1 spells them '+' and '/',
// sec 5 Table 2 spells them '-' and '_'.
//
// FF EF FF is 111111 111110 111111 111111, so its four six-bit values are 63, 62, 63, 63 and the two
// spellings are "/+//" and "_-__" respectively.
void test_rfc4648_alphabets_are_the_two_tables(void)
{
    static const uint8_t IN[3] = {0xFF, 0xEF, 0xFF};
    char std_enc[8];
    char url_enc[8];

    Base64.encode(IN, sizeof(IN), std_enc);
    TEST_ASSERT_EQUAL_STRING("/+//", std_enc);

    size_t n = Base64.url_encode(IN, sizeof(IN), url_enc);
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_STRING("_-__", url_enc);

    uint8_t out[4];
    TEST_ASSERT_EQUAL_size_t(3, Base64.url_decode("_-__", 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(IN, out, 3);
}

// sec 3.3: characters outside the alphabet in use MUST be rejected. Each alphabet's 62/63 characters
// are outside the other's, so a JWS segment (RFC 7515) decodes as base64url alone and never as a mix.
void test_each_alphabet_rejects_the_others_characters(void)
{
    uint8_t out[8];
    TEST_ASSERT_EQUAL_size_t(0, Base64.url_decode("/+//", 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, Base64.decode("_-__", out, sizeof(out)));
}

// sec 3.3 again, plus sec 3.2: padding brings the encoded form to a multiple of four characters, so
// a short quad and a pad anywhere but the tail of the final quad are not encoded data.
void test_decode_rejects_malformed(void)
{
    uint8_t out[64];
    TEST_ASSERT_EQUAL_size_t(0, Base64.decode("Zm9", out, sizeof(out)));          // not a multiple of 4
    TEST_ASSERT_EQUAL_size_t(0, Base64.decode("Zm=v", out, sizeof(out)));         // pad before the tail
    TEST_ASSERT_EQUAL_size_t(0, Base64.decode("Zg=x", out, sizeof(out)));         // lone pad in the 3rd slot
    TEST_ASSERT_EQUAL_size_t(0, Base64.decode("Zm9vYg==Zm9v", out, sizeof(out))); // padding mid-stream
    TEST_ASSERT_EQUAL_size_t(0, Base64.decode("Zm9 ", out, sizeof(out)));         // space is not in the alphabet
    TEST_ASSERT_EQUAL_size_t(0, Base64.decode("Z@9v", out, sizeof(out)));         // nor is '@'
}

// Every quad yields three octets, so a destination that cannot hold them is refused rather than
// overrun: "Zm9vYmFy" is 6 octets and a 2-octet destination cannot take it.
void test_decode_refuses_a_short_destination(void)
{
    uint8_t small[2];
    TEST_ASSERT_EQUAL_size_t(0, Base64.decode("Zm9vYmFy", small, sizeof(small)));

    uint8_t exact[6];
    TEST_ASSERT_EQUAL_size_t(6, Base64.decode("Zm9vYmFy", exact, sizeof(exact)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"foobar", exact, 6);
}

// The guard holds on each of the three octets a quad writes, not only on the last.
void test_decode_guards_every_octet_of_a_quad(void)
{
    uint8_t out[4];
    TEST_ASSERT_EQUAL_size_t(0, Base64.decode("Zm9v", out, 0)); // refused before the 1st octet
    TEST_ASSERT_EQUAL_size_t(0, Base64.decode("Zm9v", out, 1)); // refused before the 2nd
    TEST_ASSERT_EQUAL_size_t(0, Base64.decode("Zm9v", out, 2)); // refused before the 3rd
    TEST_ASSERT_EQUAL_size_t(3, Base64.decode("Zm9v", out, 3));
}

// sec 5: base64url carries no padding, so its encoded length is the character count alone and the
// final group may be 2 or 3 characters. "Zm9v" is the sec 10 encoding of "foo" in an alphabet that
// happens to spell it identically, and a caller that hands in a padded string stops at the pad.
void test_url_decode_stops_at_padding(void)
{
    uint8_t out[8];
    TEST_ASSERT_EQUAL_size_t(3, Base64.url_decode("Zm9v=", 5, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"foo", out, 3);

    // The unpadded tails sec 3.2 allows: 2 characters carry one octet, 3 carry two.
    TEST_ASSERT_EQUAL_size_t(1, Base64.url_decode("Zg", 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"f", out, 1);
    TEST_ASSERT_EQUAL_size_t(2, Base64.url_decode("Zm8", 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"fo", out, 2);
}

// url_encode drops the padding sec 5 does not carry, so its output is the sec 10 encoding minus
// every '=' and its reported length is that character count.
void test_url_encode_carries_no_padding(void)
{
    char out[16];
    TEST_ASSERT_EQUAL_size_t(2, Base64.url_encode((const uint8_t *)"f", 1, out));
    TEST_ASSERT_EQUAL_STRING("Zg", out);
    TEST_ASSERT_EQUAL_size_t(3, Base64.url_encode((const uint8_t *)"fo", 2, out));
    TEST_ASSERT_EQUAL_STRING("Zm8", out);
    TEST_ASSERT_EQUAL_size_t(4, Base64.url_encode((const uint8_t *)"foo", 3, out));
    TEST_ASSERT_EQUAL_STRING("Zm9v", out);
    TEST_ASSERT_EQUAL_size_t(0, Base64.url_encode((const uint8_t *)"", 0, out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// The same bound on the streaming decoder: the first octet completes after two characters, and a
// destination with no room for it is refused.
void test_url_decode_refuses_a_short_destination(void)
{
    uint8_t out[2];
    TEST_ASSERT_EQUAL_size_t(0, Base64.url_decode("Zm9v", 4, out, 0));
    TEST_ASSERT_EQUAL_size_t(0, Base64.url_decode("Zm9v", 4, out, 2)); // 3 octets do not fit in 2
}

// Round-trip identity over every tail length (0, 1, 2 mod 3), in both alphabets. Encode and decode
// are separate code paths - a table lookup against a branchless classifier - so agreement across a
// deterministic corpus checks one against the other rather than against itself.
void test_round_trip_is_the_identity(void)
{
    uint32_t s = 0x12345678u; // fixed LCG seed: the same corpus on every run and every platform
    for (size_t len = 0; len <= 96; len++)
    {
        uint8_t in[96];
        for (size_t i = 0; i < len; i++)
        {
            s = (s * 1664525u) + 1013904223u;
            in[i] = (uint8_t)(s >> 24);
        }

        char enc[132];
        Base64.encode(in, len, enc);
        TEST_ASSERT_EQUAL_size_t(((len + 2u) / 3u) * 4u, strlen(enc));

        uint8_t dec[96];
        TEST_ASSERT_EQUAL_size_t(len, Base64.decode(enc, dec, sizeof(dec)));

        char uenc[132];
        size_t ulen = Base64.url_encode(in, len, uenc);
        uint8_t udec[96];
        TEST_ASSERT_EQUAL_size_t(len, Base64.url_decode(uenc, ulen, udec, sizeof(udec)));

        if (len > 0)
        {
            TEST_ASSERT_EQUAL_UINT8_ARRAY(in, dec, len);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(in, udec, len);
        }
    }
}
