// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the RFC 1951 DEFLATE compressor
// (network_drivers/presentation/codec/deflate/deflate.h).
//
// test_rfc1951_fixed_block_bytes is the load-bearing case. Every other case here round-trips through
// INFLATE, which can only show that the two agree; this one derives the exact output octets from
// RFC 1951 alone - sec 3.2.3's block header, sec 3.2.6's fixed code table, sec 3.2.5's length and
// distance codes, sec 3.1.1's bit order, and RFC 7692 sec 7.2.1's stripped 00 00 ff ff tail - and
// each derivation is written out beside its bytes. A compressor that agreed with its own decompressor
// but wrote a non-conforming stream would pass every round-trip and fail here.

#include "network_drivers/presentation/codec/deflate/deflate/deflate.h"
#include "network_drivers/presentation/codec/inflate/inflate.h"
#include <string.h>

#include <unity.h>

static uint8_t inflate_work[16]; // the borrow an entry takes; Inflate never reads it

static uint8_t deflate_work[16]; // the borrow an entry takes; Deflate never reads it

static uint8_t g_dscratch[DEFLATE_SCRATCH_SIZE];
static uint8_t g_iscratch[INFLATE_SCRATCH_SIZE];
static uint8_t g_comp[2048];
static uint8_t g_plain[2048];

void setUp(void)
{
    memset(g_comp, 0, sizeof(g_comp));
    memset(g_plain, 0, sizeof(g_plain));
}

void tearDown(void)
{
}

static size_t compress(const uint8_t *src, size_t src_len)
{
    size_t clen = 0;
    Deflate.raw_args.src = src;
    Deflate.raw_args.src_len = src_len;
    Deflate.raw_args.dst = g_comp;
    Deflate.raw_args.dst_cap = sizeof(g_comp);
    Deflate.raw_args.out_len = &clen;
    Deflate.raw_args.scratch = g_dscratch;
    Deflate.raw_args.scratch_len = sizeof(g_dscratch);
    Deflate.raw(deflate_work);
    TEST_ASSERT_EQUAL_INT(DEFLATE_OK, Deflate.value);
    return clen;
}

// Compress, append the marker the sender strips (RFC 7692 sec 7.2.2), inflate, require the input
// back. Returns the compressed length so a case can also assert on the ratio.
static size_t round_trip(const uint8_t *src, size_t src_len)
{
    size_t clen = compress(src, src_len);

    TEST_ASSERT_TRUE(clen + 4 <= sizeof(g_comp));
    g_comp[clen + 0] = 0x00;
    g_comp[clen + 1] = 0x00;
    g_comp[clen + 2] = 0xff;
    g_comp[clen + 3] = 0xff;

    size_t plen = 0;
    Inflate.raw_args.src = g_comp;
    Inflate.raw_args.src_len = clen + 4;
    Inflate.raw_args.dst = g_plain;
    Inflate.raw_args.dst_cap = sizeof(g_plain);
    Inflate.raw_args.out_len = &plen;
    Inflate.raw_args.scratch = g_iscratch;
    Inflate.raw_args.scratch_len = sizeof(g_iscratch);
    Inflate.raw(inflate_work);
    TEST_ASSERT_EQUAL_INT(INFLATE_OK, Inflate.value);
    TEST_ASSERT_EQUAL_size_t(src_len, plen);
    if (src_len)
    {
        TEST_ASSERT_EQUAL_MEMORY(src, g_plain, src_len);
    }
    return clen;
}

// The exact octets, derived from RFC 1951 and RFC 7692 with no reference implementation involved.
//
// "A" alone. The block header is BFINAL=0 then BTYPE=01 (sec 3.2.3, written low bit first), so
// bit0 = 0, bit1 = 1, bit2 = 0. 'A' is 65 and sec 3.2.6 gives literals 0-143 the 8-bit codes
// 00110000 upward, so 'A' is 00110000 + 65 = 01110001, put out most significant bit first
// (sec 3.1.1) into bits 3..10. Then end-of-block, symbol 256, the 7-bit 0000000, into bits 11..17.
// Then the sync flush: BFINAL=0, BTYPE=00 (bits 18..20) and an align to the octet.
//
//   bits  0..7  = 0,1,0,0,1,1,1,0 -> 0x72
//   bits  8..15 = 0,0,1,0,0,0,0,0 -> 0x04
//   bits 16..23 = 0                -> 0x00
//
// The empty stored block's 00 00 ff ff tail is then dropped (RFC 7692 sec 7.2.1), so the payload is
// those three octets.
void test_rfc1951_fixed_block_bytes(void)
{
    static const uint8_t WANT_A[] = {0x72, 0x04, 0x00};
    size_t n = compress((const uint8_t *)"A", 1);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT_A), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT_A, g_comp, sizeof(WANT_A));

    // "Hi": 'H' is 72 -> 00110000 + 72 = 01111000, 'i' is 105 -> 00110000 + 105 = 10011001.
    //   bits  0..7  = 0,1,0,0,1,1,1,1 -> 0xf2
    //   bits  8..15 = 0,0,0,1,0,0,1,1 -> 0xc8
    //   bits 16..23 = 0,0,1,0,0,0,0,0 -> 0x04
    //   bits 24..31 = 0                -> 0x00
    static const uint8_t WANT_HI[] = {0xf2, 0xc8, 0x04, 0x00};
    n = compress((const uint8_t *)"Hi", 2);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT_HI), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT_HI, g_comp, sizeof(WANT_HI));

    // "AAAA": one literal 'A', then a back-reference of length 3 at distance 1. sec 3.2.5 gives
    // length 3 the code 257 with no extra bits, which sec 3.2.6 codes as the 7-bit 0000001, and
    // distance 1 the code 0 with no extra bits, a 5-bit 00000.
    //   bits  0..2  header, 3..10 'A', 11..17 code 257, 18..22 distance 0, 23..29 end-of-block,
    //   bits 30..32 sync-flush header, then the align.
    //   bits  0..7  = 0,1,0,0,1,1,1,0 -> 0x72
    //   bits  8..15 = 0,0,1,0,0,0,0,0 -> 0x04
    //   bits 16..23 = 0,1,0,0,0,0,0,0 -> 0x02
    //   bits 24..31 = 0                -> 0x00
    //   bit  32     = 0                -> 0x00
    static const uint8_t WANT_AAAA[] = {0x72, 0x04, 0x02, 0x00, 0x00};
    n = compress((const uint8_t *)"AAAA", 4);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT_AAAA), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT_AAAA, g_comp, sizeof(WANT_AAAA));
}

// The payload never sets BFINAL: a permessage-deflate message is one block in a stream the peer
// keeps reading (RFC 7692 sec 7.2.1), so bit 0 of the first octet is 0 and bits 1..2 are BTYPE=01.
void test_payload_is_a_non_final_fixed_block(void)
{
    size_t n = compress((const uint8_t *)"whatever", 8);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_comp[0] & 0x01u); // BFINAL clear
    TEST_ASSERT_EQUAL_HEX8(0x02, g_comp[0] & 0x06u); // BTYPE = 01, fixed Huffman
}

// The stripped tail is exactly the four octets RFC 7692 sec 7.2.1 names, so appending them back is
// what makes the payload a complete stream again. Nothing is written past the reported length.
void test_marker_is_stripped_from_the_reported_length(void)
{
    size_t clen = compress((const uint8_t *)"marker", 6);
    // Deflate wrote clen + 4 octets and reported clen; the four past it are the marker itself.
    TEST_ASSERT_EQUAL_HEX8(0x00, g_comp[clen + 0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_comp[clen + 1]);
    TEST_ASSERT_EQUAL_HEX8(0xff, g_comp[clen + 2]);
    TEST_ASSERT_EQUAL_HEX8(0xff, g_comp[clen + 3]);
}

// Round-trip identity, the property any compressor must hold whatever it emits.
void test_round_trip_text(void)
{
    const char *s = "Hello, World! Hello, World! Hello, World!";
    round_trip((const uint8_t *)s, strlen(s));
}

void test_round_trip_empty(void)
{
    round_trip((const uint8_t *)"", 0);
}

void test_round_trip_single_byte(void)
{
    static const uint8_t ONE = 'Z';
    round_trip(&ONE, 1);
}

// Every octet value, so a literal outside the 8-bit code range (sec 3.2.6 gives 144-255 nine bits)
// is emitted at its own width.
void test_round_trip_every_octet_value(void)
{
    uint8_t buf[256];
    for (int i = 0; i < 256; i++)
    {
        buf[i] = (uint8_t)i;
    }
    round_trip(buf, sizeof(buf));
}

// Highly repetitive input must actually shrink, which is the only evidence that the LZ77 matcher
// finds back-references at all rather than emitting literals forever.
void test_repetitive_input_shrinks(void)
{
    uint8_t buf[512];
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)('A' + (i % 4));
    }
    size_t clen = round_trip(buf, sizeof(buf));
    TEST_ASSERT_TRUE(clen < sizeof(buf) / 4);
}

// A realistic frame with repeated field names compresses below its own size.
void test_json_frame_shrinks(void)
{
    const char *s = "{\"type\":\"telemetry\",\"temp\":21.5,\"hum\":48,\"temp\":21.5,\"hum\":48,\"temp\":21.5}";
    size_t n = strlen(s);
    TEST_ASSERT_TRUE(round_trip((const uint8_t *)s, n) < n);
}

// Far more positions than the bounded chain walk visits, all sharing one three-octet prefix and
// each followed by a distinct octet, so every candidate stops at the shortest match the format
// allows and the walk exhausts its budget rather than exiting early.
void test_hash_chain_exhaustion_round_trips(void)
{
    uint8_t buf[480];
    for (int k = 0; k < 120; k++)
    {
        buf[k * 4 + 0] = 'A';
        buf[k * 4 + 1] = 'B';
        buf[k * 4 + 2] = 'C';
        buf[k * 4 + 3] = (uint8_t)k;
    }
    round_trip(buf, sizeof(buf));
}

// Two copies of one three-octet tag farther apart than the compressor's window, with nothing else
// able to share their hash: the only candidate is out of range and must be discarded, not encoded
// as a distance the decoder would read as something else.
void test_match_past_the_window_is_not_used(void)
{
    uint8_t buf[600];
    memset(buf, 0, sizeof(buf));
    buf[0] = 'Q';
    buf[1] = 'R';
    buf[2] = 'S';
    buf[520] = 'Q';
    buf[521] = 'R';
    buf[522] = 'S';
    round_trip(buf, sizeof(buf));
}

// A match of the longest length sec 3.2.5 codes (258) and one at the window's far edge, both of
// which have to come back byte for byte.
void test_longest_match_round_trips(void)
{
    uint8_t buf[600];
    memset(buf, 'x', sizeof(buf));
    round_trip(buf, sizeof(buf));
}

static uint32_t g_rng = 0x1234abcdu;

static uint32_t rng(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

// Incompressible input takes the literal path at every position and still round-trips, including
// the 9-bit literals that make the output longer than the input.
void test_random_input_round_trips(void)
{
    uint8_t buf[512];
    for (int iter = 0; iter < 200; iter++)
    {
        size_t n = rng() % (sizeof(buf) + 1);
        for (size_t i = 0; i < n; i++)
        {
            buf[i] = (uint8_t)rng();
        }
        round_trip(buf, n);
    }
}

// A small alphabet makes matches everywhere, so the chain walk runs deep on every position.
void test_low_entropy_input_round_trips(void)
{
    uint8_t buf[512];
    for (int iter = 0; iter < 100; iter++)
    {
        size_t n = 1 + (rng() % sizeof(buf));
        for (size_t i = 0; i < n; i++)
        {
            buf[i] = (uint8_t)('a' + (rng() % 5));
        }
        round_trip(buf, n);
    }
}

// Output that will not fit is reported rather than written past the caller's buffer.
void test_output_overflow_fails_closed(void)
{
    uint8_t buf[256];
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)rng();
    }
    uint8_t tiny[16];
    size_t clen = 0;
    Deflate.raw_args.src = buf;
    Deflate.raw_args.src_len = sizeof(buf);
    Deflate.raw_args.dst = tiny;
    Deflate.raw_args.dst_cap = sizeof(tiny);
    Deflate.raw_args.out_len = &clen;
    Deflate.raw_args.scratch = g_dscratch;
    Deflate.raw_args.scratch_len = sizeof(g_dscratch);
    Deflate.raw(deflate_work);
    TEST_ASSERT_EQUAL_INT(DEFLATE_ERR_OVERFLOW, Deflate.value);
}

// Working memory one octet short of what the tables need is refused before anything is written.
void test_scratch_too_small_fails_closed(void)
{
    uint8_t small[DEFLATE_SCRATCH_SIZE - 1];
    size_t clen = 0;
    Deflate.raw_args.src = (const uint8_t *)"anything";
    Deflate.raw_args.src_len = 8;
    Deflate.raw_args.dst = g_comp;
    Deflate.raw_args.dst_cap = sizeof(g_comp);
    Deflate.raw_args.out_len = &clen;
    Deflate.raw_args.scratch = small;
    Deflate.raw_args.scratch_len = sizeof(small);
    Deflate.raw(deflate_work);
    TEST_ASSERT_EQUAL_INT(DEFLATE_ERR_SCRATCH, Deflate.value);
}
