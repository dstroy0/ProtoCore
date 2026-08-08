// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the RFC 1951 DEFLATE core (network_drivers/presentation/codec/deflate).
// The encoder is checked by round-tripping through Inflate.raw(), which is itself
// validated against Python zlib in test_inflate - so a successful round-trip
// proves Deflate.raw() emits a valid RFC 1951 stream. The permessage-deflate
// payload (tail 00 00 ff ff stripped, RFC 7692 sender) gets the marker appended
// back before inflating, mirroring the receiver.

#include "network_drivers/presentation/codec/deflate/deflate.h"
#include "network_drivers/presentation/codec/inflate/inflate.h"
#include <string.h>

#include <unity.h>

static uint8_t g_dscratch[DEFLATE_SCRATCH_SIZE];
static uint8_t g_iscratch[INFLATE_SCRATCH_SIZE];
static uint8_t g_comp[2048];
static uint8_t g_decompressed[2048];

void setUp()
{
    memset(g_comp, 0, sizeof(g_comp));
    memset(g_decompressed, 0, sizeof(g_decompressed));
}
void tearDown()
{
}

// Compress src, append the RFC 7692 marker, inflate, and assert byte-identical.
// Returns the compressed length so callers can also assert on the ratio.
static size_t roundtrip(const uint8_t *src, size_t src_len)
{
    size_t clen = 0;
    int rc = (int)Deflate.raw(src, src_len, g_comp, sizeof(g_comp), &clen, g_dscratch, sizeof(g_dscratch));
    TEST_ASSERT_EQUAL_INT(DEFLATE_OK, rc);

    // Append the 00 00 ff ff marker the sender stripped (RFC 7692 sec 7.2.2).
    TEST_ASSERT_TRUE(clen + 4 <= sizeof(g_comp));
    g_comp[clen] = 0x00;
    g_comp[clen + 1] = 0x00;
    g_comp[clen + 2] = 0xff;
    g_comp[clen + 3] = 0xff;

    size_t dlen = 0;
    rc = (int)Inflate.raw(g_comp, clen + 4, g_decompressed, sizeof(g_decompressed), &dlen, g_iscratch,
                          sizeof(g_iscratch));
    TEST_ASSERT_EQUAL_INT(INFLATE_OK, rc);
    TEST_ASSERT_EQUAL_size_t(src_len, dlen);
    if (src_len)
    {
        TEST_ASSERT_EQUAL_MEMORY(src, g_decompressed, src_len);
    }
    return clen;
}

void test_roundtrip_text()
{
    const char *s = "Hello, World! Hello, World! Hello, World!";
    roundtrip((const uint8_t *)s, strlen(s));
}

void test_roundtrip_empty()
{
    roundtrip((const uint8_t *)"", 0);
}

void test_roundtrip_single_byte()
{
    const uint8_t b = 'Z';
    roundtrip(&b, 1);
}

void test_roundtrip_all_byte_values()
{
    uint8_t buf[256];
    for (int i = 0; i < 256; i++)
    {
        buf[i] = (uint8_t)i;
    }
    roundtrip(buf, sizeof(buf));
}

// Highly repetitive input must actually shrink (LZ77 back-references work).
void test_compresses_repetitive()
{
    uint8_t buf[512];
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)('A' + (i % 4));
    }
    size_t clen = roundtrip(buf, sizeof(buf));
    TEST_ASSERT_TRUE(clen < sizeof(buf) / 4); // should compress dramatically
}

// A realistic JSON-ish frame should compress below its original size.
void test_compresses_json()
{
    const char *s = "{\"type\":\"telemetry\",\"temp\":21.5,\"hum\":48,\"temp\":21.5,\"hum\":48,\"temp\":21.5}";
    size_t n = strlen(s);
    size_t clen = roundtrip((const uint8_t *)s, n);
    TEST_ASSERT_TRUE(clen < n);
}

// A run of far more than PC_MAX_CHAIN (64) positions sharing the same 3-byte hash
// ("ABC"), each followed by a distinct 4th byte so every candidate match stops
// at exactly PC_MIN_MATCH (3) and never reaches max_len. That forces the
// hash-chain walk to exhaust its PC_MAX_CHAIN budget - loop exit via chain == 0 -
// rather than stopping early on a length-based break or running off the end of
// the chain (PC_NONE sentinel).
void test_hash_chain_exhaustion()
{
    uint8_t buf[480];
    for (int k = 0; k < 120; k++)
    {
        buf[k * 4 + 0] = 'A';
        buf[k * 4 + 1] = 'B';
        buf[k * 4 + 2] = 'C';
        buf[k * 4 + 3] = (uint8_t)k; // unique per block -> caps every match at length 3
    }
    roundtrip(buf, sizeof(buf));
}

// Two occurrences of a distinctive 3-byte tag more than PC_WINDOW (512) bytes
// apart, with nothing else in the buffer able to collide with that hash
// bucket: the chain walk's first (and only) candidate for the second
// occurrence is farther than the window, so it must be discarded via the
// dist > PC_WINDOW break rather than matched against.
void test_match_distance_exceeds_window()
{
    uint8_t buf[600];
    memset(buf, 0, sizeof(buf));
    buf[0] = 'Q';
    buf[1] = 'R';
    buf[2] = 'S';
    buf[520] = 'Q';
    buf[521] = 'R';
    buf[522] = 'S';
    roundtrip(buf, sizeof(buf));
}

// Deterministic xorshift32 - same generator the pentest suite uses.
static uint32_t s_rng = 0x1234abcdu;
static uint32_t rng()
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

// Fuzz: random payloads of random lengths must always round-trip exactly. Random
// data is essentially incompressible, exercising the literal path and the bounded
// expansion (static-Huffman 9-bit literals) without ever corrupting output.
void test_fuzz_roundtrip()
{
    uint8_t buf[512];
    for (int iter = 0; iter < 400; iter++)
    {
        size_t n = rng() % (sizeof(buf) + 1);
        for (size_t i = 0; i < n; i++)
        {
            buf[i] = (uint8_t)rng();
        }
        roundtrip(buf, n);
    }
}

// Fuzz with a small alphabet -> lots of matches, exercising the chain walk.
void test_fuzz_low_entropy_roundtrip()
{
    uint8_t buf[512];
    for (int iter = 0; iter < 200; iter++)
    {
        size_t n = 1 + rng() % sizeof(buf);
        for (size_t i = 0; i < n; i++)
        {
            buf[i] = (uint8_t)('a' + (rng() % 5));
        }
        roundtrip(buf, n);
    }
}

void test_output_overflow_fails_closed()
{
    // Incompressible data into a too-small buffer must report overflow, not write
    // past dst_cap. (Random bytes expand slightly under static Huffman.)
    uint8_t buf[256];
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)rng();
    }
    uint8_t tiny[16];
    size_t clen = 0;
    int rc = (int)Deflate.raw(buf, sizeof(buf), tiny, sizeof(tiny), &clen, g_dscratch, sizeof(g_dscratch));
    TEST_ASSERT_EQUAL_INT(DEFLATE_ERR_OVERFLOW, rc);
}

void test_scratch_too_small_fails_closed()
{
    uint8_t small[DEFLATE_SCRATCH_SIZE - 1];
    size_t clen = 0;
    const char *s = "anything";
    int rc = (int)Deflate.raw((const uint8_t *)s, strlen(s), g_comp, sizeof(g_comp), &clen, small, sizeof(small));
    TEST_ASSERT_EQUAL_INT(DEFLATE_ERR_SCRATCH, rc);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_text);
    RUN_TEST(test_roundtrip_empty);
    RUN_TEST(test_roundtrip_single_byte);
    RUN_TEST(test_roundtrip_all_byte_values);
    RUN_TEST(test_compresses_repetitive);
    RUN_TEST(test_compresses_json);
    RUN_TEST(test_hash_chain_exhaustion);
    RUN_TEST(test_match_distance_exceeds_window);
    RUN_TEST(test_fuzz_roundtrip);
    RUN_TEST(test_fuzz_low_entropy_roundtrip);
    RUN_TEST(test_output_overflow_fails_closed);
    RUN_TEST(test_scratch_too_small_fails_closed);
    return UNITY_END();
}
