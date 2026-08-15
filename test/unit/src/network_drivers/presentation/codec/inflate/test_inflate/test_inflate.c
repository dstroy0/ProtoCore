// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the RFC 1951 INFLATE core (network_drivers/presentation/codec/inflate/inflate.h).
//
// test_rfc1951_hand_built_blocks is the load-bearing case: each stream in it is assembled bit by bit
// from RFC 1951 itself - sec 3.2.3's block header, sec 3.2.4's stored block, sec 3.2.6's fixed code
// table, sec 3.2.5's length and distance codes and sec 3.1.1's bit order - with the derivation
// written out beside the octets, so nothing here was produced by a compressor and copied.
//
// One case cannot be built that way. A dynamic-Huffman block (sec 3.2.7) carries its own code
// lengths, so its octets come from a reference ENCODER; the expected value is still the plaintext
// that was handed to it, never an implementation's output.

#include "network_drivers/presentation/codec/inflate/inflate.h"
#include <string.h>

#include <unity.h>

static uint8_t g_scratch[INFLATE_SCRATCH_SIZE];
static uint8_t g_out[1024];

void setUp(void)
{
    memset(g_out, 0, sizeof(g_out));
}

void tearDown(void)
{
}

static void expect_inflates_to(const uint8_t *src, size_t src_len, const char *exp, size_t exp_len)
{
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(INFLATE_OK,
                          Inflate.raw(src, src_len, g_out, sizeof(g_out), &out_len, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_EQUAL_size_t(exp_len, out_len);
    if (exp_len)
    {
        TEST_ASSERT_EQUAL_MEMORY(exp, g_out, exp_len);
    }
}

static void expect_malformed(const uint8_t *src, size_t src_len)
{
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(INFLATE_ERR_MALFORMED,
                          Inflate.raw(src, src_len, g_out, sizeof(g_out), &out_len, g_scratch, sizeof(g_scratch)));
}

// Four streams, each written out from the RFC's own tables.
//
// STORED, sec 3.2.4: BFINAL=1 and BTYPE=00 give the first octet 0x01, then the align, then LEN
// little-endian and NLEN its ones complement, then the octets themselves. For "Hi": LEN = 0x0002,
// NLEN = 0xfffd, so 01 02 00 fd ff 48 69.
//
// FIXED LITERAL, sec 3.2.6: BFINAL=1, BTYPE=01 puts bits 0..2 at 1,1,0. 'A' is 65, and literals
// 0-143 take the 8-bit codes from 00110000 up, so 'A' is 00110000 + 65 = 01110001 written most
// significant bit first into bits 3..10. End-of-block is symbol 256, the 7-bit 0000000, in bits
// 11..17.
//   bits  0..7  = 1,1,0,0,1,1,1,0 -> 0x73
//   bits  8..15 = 0,0,1,0,0,0,0,0 -> 0x04
//   bits 16..17 plus the pad      -> 0x00
//
// FIXED BACK-REFERENCE, sec 3.2.5: after the same literal 'A', length 3 is code 257 with no extra
// bits (the 7-bit 0000001, bits 11..17) and distance 1 is code 0 with no extra bits (the 5-bit
// 00000, bits 18..22), then end-of-block in bits 23..29. That copies three octets from one back, so
// the output is "AAAA".
//   bits 16..23 = 0,1,0,0,0,0,0,0 -> 0x02
//
// EMPTY, sec 3.2.3 plus 3.2.6: a final fixed block holding nothing but end-of-block.
//   bits 0..2 header, bits 3..9 the 7-bit 0000000 -> 0x03 0x00
void test_rfc1951_hand_built_blocks(void)
{
    static const uint8_t STORED_HI[] = {0x01, 0x02, 0x00, 0xfd, 0xff, 0x48, 0x69};
    expect_inflates_to(STORED_HI, sizeof(STORED_HI), "Hi", 2);

    static const uint8_t FIXED_A[] = {0x73, 0x04, 0x00};
    expect_inflates_to(FIXED_A, sizeof(FIXED_A), "A", 1);

    // "AB": 'B' is 66, so its code is 00110000 + 66 = 01110010 in bits 11..18.
    //   bits  8..15 = 0,0,1,0,1,1,1,0 -> 0x74
    //   bits 16..23 = 0,1,0,0,0,0,0,0 -> 0x02
    static const uint8_t FIXED_AB[] = {0x73, 0x74, 0x02, 0x00};
    expect_inflates_to(FIXED_AB, sizeof(FIXED_AB), "AB", 2);

    static const uint8_t FIXED_BACKREF[] = {0x73, 0x04, 0x02, 0x00};
    expect_inflates_to(FIXED_BACKREF, sizeof(FIXED_BACKREF), "AAAA", 4);

    static const uint8_t EMPTY[] = {0x03, 0x00};
    expect_inflates_to(EMPTY, sizeof(EMPTY), NULL, 0);
}

// RFC 7692 sec 7.2.2: a permessage-deflate payload carries no final block, and the receiver appends
// 00 00 ff ff before decompressing. The payload here is the same "Hi" as a NON-final fixed block
// (bit 0 = 0, so the first octet is 0xf2 rather than 0xf3), followed by the empty stored block that
// the marker completes.
//   'H' is 72 -> 01111000, 'i' is 105 -> 10011001
//   bits  0..7  = 0,1,0,0,1,1,1,1 -> 0xf2
//   bits  8..15 = 0,0,0,1,0,0,1,1 -> 0xc8
//   bits 16..23 = 0,0,1,0,0,0,0,0 -> 0x04
//   bits 24..31 = the end-of-block tail and the stored header, all zero -> 0x00
void test_permessage_deflate_payload_with_the_marker(void)
{
    static const uint8_t PAYLOAD[] = {0xf2, 0xc8, 0x04, 0x00};
    uint8_t buf[16];

    memcpy(buf, PAYLOAD, sizeof(PAYLOAD));
    buf[sizeof(PAYLOAD) + 0] = 0x00;
    buf[sizeof(PAYLOAD) + 1] = 0x00;
    buf[sizeof(PAYLOAD) + 2] = 0xff;
    buf[sizeof(PAYLOAD) + 3] = 0xff;

    expect_inflates_to(buf, sizeof(PAYLOAD) + 4, "Hi", 2);
}

// The plaintext handed to a reference encoder to obtain the dynamic-Huffman block below.
static const char DYN_TEXT[] = "the quick brown fox jumps over the lazy dog. "
                               "pack my box with five dozen liquor jugs. "
                               "how vexingly quick daft zebras jump!";

// One dynamic-Huffman block (sec 3.2.7): BFINAL=1, BTYPE=10, then the HLIT/HDIST/HCLEN counts, the
// code-length code lengths, the run-length coded literal/length and distance code lengths, and the
// data. That structure cannot be hand-packed at any reasonable cost, so these octets came out of a
// reference encoder; the value asserted is DYN_TEXT, which is what went in.
static const uint8_t DYN_BLOCK[] = {
    0x2d, 0x8d, 0xdb, 0x11, 0xc3, 0x20, 0x0c, 0x04, 0x5b, 0xb9, 0x34, 0xe0, 0x9e, 0x20, 0x16, 0xa0, 0x04, 0x23, 0x9b,
    0xa7, 0xa1, 0xfa, 0x68, 0x3c, 0xf9, 0xde, 0xbd, 0xbd, 0x1a, 0x08, 0x57, 0xe3, 0xf7, 0x17, 0x36, 0xcb, 0x48, 0x70,
    0x72, 0xe3, 0xd3, 0x8e, 0xb3, 0x40, 0x3a, 0x65, 0x54, 0xc5, 0xd1, 0xac, 0x89, 0x5d, 0xfc, 0x86, 0xd3, 0xa8, 0x77,
    0x4c, 0x58, 0x95, 0x06, 0xd7, 0x00, 0xc7, 0x9d, 0x14, 0x2d, 0x4a, 0x88, 0x7c, 0x35, 0xc9, 0xba, 0xf5, 0x65, 0x43,
    0x90, 0x81, 0x4e, 0x37, 0x27, 0x1f, 0xe7, 0x3f, 0xbf, 0x1b, 0x57, 0xb1, 0xc8, 0x66, 0x53, 0x9e, 0x83, 0xd7, 0x0f};

void test_dynamic_huffman_block(void)
{
    // sec 3.2.3: BTYPE = 10 is the dynamic form.
    TEST_ASSERT_EQUAL_HEX8(0x04, DYN_BLOCK[0] & 0x06u);
    expect_inflates_to(DYN_BLOCK, sizeof(DYN_BLOCK), DYN_TEXT, sizeof(DYN_TEXT) - 1);
}

// sec 3.2.3: BTYPE = 11 is reserved, so a block that claims it is not a stream.
void test_reserved_block_type_is_refused(void)
{
    static const uint8_t RESERVED[] = {0x06}; // BFINAL=0, BTYPE=11
    expect_malformed(RESERVED, sizeof(RESERVED));
}

// sec 3.2.4: NLEN is the ones complement of LEN, so a stored block whose pair disagrees is corrupt
// and must not be copied out.
void test_stored_block_nlen_must_be_the_complement(void)
{
    static const uint8_t BAD_NLEN[] = {0x01, 0x02, 0x00, 0xfd, 0xfe, 0x48, 0x69};
    expect_malformed(BAD_NLEN, sizeof(BAD_NLEN));

    // A stored block whose LEN claims more octets than the input holds.
    static const uint8_t SHORT_STORED[] = {0x01, 0x0a, 0x00, 0xf5, 0xff, 0x41};
    expect_malformed(SHORT_STORED, sizeof(SHORT_STORED));

    // A stored block that ends before its own LEN/NLEN.
    static const uint8_t NO_LEN[] = {0x01};
    expect_malformed(NO_LEN, sizeof(NO_LEN));
}

// sec 3.2.6: "Literal/length values 286-287 will never actually occur in the compressed data" and
// "distance codes 30-31 will never actually occur". A stream that emits one is malformed, not a
// stream carrying an unknown extension.
void test_symbols_that_never_occur_are_refused(void)
{
    // Symbol 286 is 280 + 6, so its 8-bit code is 11000000 + 6 = 11000110 in bits 3..10.
    //   bits 0..7 = 1,1,0,1,1,0,0,0 -> 0x1b
    //   bits 8..10 = 1,1,0          -> 0x03
    static const uint8_t LITLEN_286[] = {0x1b, 0x03};
    expect_malformed(LITLEN_286, sizeof(LITLEN_286));

    // Length code 257 (the 7-bit 0000001) then distance code 30 (the 5-bit 11110).
    //   bits 0..7  = 1,1,0,0,0,0,0,0 -> 0x03
    //   bits 8..15 = 0,1,1,1,1,1,0,0 -> 0x3e
    static const uint8_t DIST_30[] = {0x03, 0x3e};
    expect_malformed(DIST_30, sizeof(DIST_30));
}

// sec 3.2.5 bounds a distance by what has already been produced: the window is the output itself, so
// a back-reference reaching before the start of it has nothing to copy.
void test_distance_before_the_start_of_output_is_refused(void)
{
    // Length code 257 then distance code 1 (distance 2), with nothing produced yet.
    //   bits 0..7  = 1,1,0,0,0,0,0,0 -> 0x03
    //   bits 8..15 = 0,1,0,0,0,0,1,0 -> 0x42
    static const uint8_t TOO_FAR[] = {0x03, 0x42};
    expect_malformed(TOO_FAR, sizeof(TOO_FAR));
}

// A final block that ends before its end-of-block symbol is an incomplete stream, not an empty one.
void test_truncated_stream_is_refused(void)
{
    static const uint8_t FIXED_BACKREF[] = {0x73, 0x04, 0x02, 0x00};
    expect_malformed(FIXED_BACKREF, 2);
    expect_malformed(DYN_BLOCK, sizeof(DYN_BLOCK) / 2);

    // A dynamic block header cut off inside its HLIT/HDIST/HCLEN counts.
    static const uint8_t DYN_HDR[] = {0x05};
    expect_malformed(DYN_HDR, sizeof(DYN_HDR));
}

// The output buffer is the window, so a message larger than it is refused rather than wrapped.
void test_output_overflow_fails_closed(void)
{
    static const uint8_t FIXED_BACKREF[] = {0x73, 0x04, 0x02, 0x00};
    uint8_t tiny[2];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(INFLATE_ERR_OVERFLOW, Inflate.raw(FIXED_BACKREF, sizeof(FIXED_BACKREF), tiny, sizeof(tiny),
                                                            &out_len, g_scratch, sizeof(g_scratch)));

    // A stored block whose octets do not fit either.
    static const uint8_t STORED_HI[] = {0x01, 0x02, 0x00, 0xfd, 0xff, 0x48, 0x69};
    uint8_t one[1];
    TEST_ASSERT_EQUAL_INT(INFLATE_ERR_OVERFLOW, Inflate.raw(STORED_HI, sizeof(STORED_HI), one, sizeof(one), &out_len,
                                                            g_scratch, sizeof(g_scratch)));
}

// Working memory one octet short of the Huffman tables is refused before anything is decoded.
void test_scratch_too_small_fails_closed(void)
{
    static const uint8_t FIXED_A[] = {0x73, 0x04, 0x00};
    uint8_t small[INFLATE_SCRATCH_SIZE - 1];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(INFLATE_ERR_SCRATCH,
                          Inflate.raw(FIXED_A, sizeof(FIXED_A), g_out, sizeof(g_out), &out_len, small, sizeof(small)));
}

// Two blocks back to back in one stream: sec 3.2.3 lets a stream carry any number, and only the last
// sets BFINAL. A stored block followed by a final fixed block must concatenate.
void test_two_blocks_concatenate(void)
{
    // Non-final stored "Hi" (BFINAL=0 so the first octet is 0x00), then the final fixed "A".
    static const uint8_t TWO[] = {0x00, 0x02, 0x00, 0xfd, 0xff, 0x48, 0x69, 0x73, 0x04, 0x00};
    expect_inflates_to(TWO, sizeof(TWO), "HiA", 3);
}
