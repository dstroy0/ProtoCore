// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the INTERBUS summation-frame codec (services/fieldbus/interbus/interbus.h).
//
// The FCS is CRC-16/CCITT-FALSE, cataloged by the CRC RevEng catalogue as CRC-16/IBM-3740:
//   width=16 poly=0x1021 init=0xffff refin=false refout=false xorout=0x0000
//   check=0x29b1 residue=0x0000
// test_published_check_value pins the catalogue's own check value, the CRC of the ASCII string
// "123456789", so a wrong polynomial, a wrong seed or an accidental reflection is caught by one
// assertion. The frame cases then lean on the published residue: with xorout 0 and the FCS
// appended big-endian, the CRC over a whole valid frame is zero, whatever the frame carries.

#include "services/fieldbus/interbus/interbus.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// CRC RevEng catalogue, CRC-16/IBM-3740: check=0x29b1 over the nine ASCII digits "123456789".
void test_published_check_value(void)
{
    static const uint8_t CHECK[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x29B1u, protocore_interbus_fcs(CHECK, sizeof(CHECK)));
    // init=0xffff, xorout=0x0000: an empty message leaves the seed untouched.
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, protocore_interbus_fcs(CHECK, 0));
}

// The loopback word that opens every summation frame, all ones so an open ring cannot spell it.
void test_loopback_word(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, (uint16_t)PROTOCORE_INTERBUS_LOOPBACK);
}

// A frame is loopback(2) + device words(2 each, big-endian) + FCS(2), and the FCS covers the
// loopback and the words. Two device slices of one word each:
//   FF FF | 12 34 | 56 78 | FCS
void test_frame_layout(void)
{
    static const uint16_t WORDS[2] = {0x1234, 0x5678};
    uint8_t out[16];
    size_t n = protocore_interbus_build(WORDS, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(8u, n); // 2 + 2*2 + 2

    static const uint8_t HEAD[6] = {0xFF, 0xFF, 0x12, 0x34, 0x56, 0x78};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HEAD, out, sizeof(HEAD));

    // The trailing FCS is the CRC of everything before it, big-endian.
    uint16_t fcs = protocore_interbus_fcs(out, 6);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(fcs >> 8), out[6]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)fcs, out[7]);

    // residue=0x0000: the CRC over the frame including its own FCS folds to zero.
    TEST_ASSERT_EQUAL_HEX16(0x0000u, protocore_interbus_fcs(out, n));
}

// An empty ring is still a frame: loopback plus FCS, no device slices.
void test_zero_word_frame(void)
{
    uint8_t out[16];
    size_t n = protocore_interbus_build(NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4u, n);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, out[1]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, protocore_interbus_fcs(out, n));

    uint16_t words[4];
    size_t count = 99;
    TEST_ASSERT_TRUE(protocore_interbus_parse(out, n, words, 4, &count));
    TEST_ASSERT_EQUAL_size_t(0u, count);
}

// Every word slice survives assembly then disassembly, in order and with its octets unswapped.
void test_round_trip(void)
{
    static const uint16_t WORDS[6] = {0x0000, 0xFFFF, 0x00FF, 0xFF00, 0x8001, 0x1234};
    for (size_t n = 0; n <= sizeof(WORDS) / sizeof(WORDS[0]); n++)
    {
        uint8_t frame[32];
        size_t len = protocore_interbus_build(n ? WORDS : NULL, n, frame, sizeof(frame));
        TEST_ASSERT_EQUAL_size_t(4u + n * 2u, len);

        uint16_t got[8];
        size_t count = 0;
        memset(got, 0, sizeof(got));
        TEST_ASSERT_TRUE(protocore_interbus_parse(frame, len, got, 8, &count));
        TEST_ASSERT_EQUAL_size_t(n, count);
        for (size_t w = 0; w < n; w++)
        {
            TEST_ASSERT_EQUAL_HEX16(WORDS[w], got[w]);
        }
    }
}

// A single-bit change anywhere in the frame breaks the FCS, so the ring rejects it. This is the
// property a CRC-16 buys over a parity byte.
void test_single_bit_corruption_is_refused(void)
{
    static const uint16_t WORDS[3] = {0x1111, 0x2222, 0x3333};
    uint8_t frame[16];
    size_t len = protocore_interbus_build(WORDS, 3, frame, sizeof(frame));

    for (size_t i = 0; i < len; i++)
    {
        for (int bit = 0; bit < 8; bit += 3) // bits 0, 3, 6 of every octet
        {
            uint8_t bad[16];
            memcpy(bad, frame, len);
            bad[i] ^= (uint8_t)(1u << bit);
            uint16_t got[4];
            size_t count = 0;
            TEST_ASSERT_FALSE(protocore_interbus_parse(bad, len, got, 4, &count));
        }
    }
}

// A frame that does not open with the loopback word came from an open ring and is refused before
// its FCS is even considered.
void test_open_ring_is_refused(void)
{
    static const uint16_t WORDS[1] = {0xABCD};
    uint8_t frame[16];
    size_t len = protocore_interbus_build(WORDS, 1, frame, sizeof(frame));

    uint16_t got[4];
    size_t count = 0;
    TEST_ASSERT_TRUE(protocore_interbus_parse(frame, len, got, 4, &count));

    frame[0] = 0x00; // loopback no longer all ones
    TEST_ASSERT_FALSE(protocore_interbus_parse(frame, len, got, 4, &count));
}

// A word region that is not a whole number of 16-bit words, a frame shorter than loopback + FCS,
// and a word count the caller's buffer cannot hold are all refused.
void test_parse_refuses_malformed_lengths(void)
{
    uint8_t frame[16];
    static const uint16_t WORDS[2] = {0x1234, 0x5678};
    size_t len = protocore_interbus_build(WORDS, 2, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_size_t(8u, len);

    uint16_t got[4];
    size_t count = 0;
    TEST_ASSERT_FALSE(protocore_interbus_parse(frame, 7, got, 4, &count));   // odd word region
    TEST_ASSERT_FALSE(protocore_interbus_parse(frame, 3, got, 4, &count));   // shorter than loopback + FCS
    TEST_ASSERT_FALSE(protocore_interbus_parse(frame, len, got, 1, &count)); // 2 words, room for 1
    TEST_ASSERT_TRUE(protocore_interbus_parse(frame, len, got, 2, &count));  // exactly enough room

    TEST_ASSERT_FALSE(protocore_interbus_parse(NULL, len, got, 4, &count));
    TEST_ASSERT_FALSE(protocore_interbus_parse(frame, len, NULL, 4, &count));
    TEST_ASSERT_FALSE(protocore_interbus_parse(frame, len, got, 4, NULL));
}

// A buffer that cannot hold the whole frame yields 0 rather than a frame with no FCS.
void test_build_refuses_a_short_buffer(void)
{
    static const uint16_t WORDS[2] = {0x1234, 0x5678};
    uint8_t out[16];
    TEST_ASSERT_EQUAL_size_t(0u, protocore_interbus_build(WORDS, 2, out, 7));
    TEST_ASSERT_EQUAL_size_t(8u, protocore_interbus_build(WORDS, 2, out, 8));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_interbus_build(WORDS, 2, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_interbus_build(NULL, 2, out, sizeof(out)));
}
