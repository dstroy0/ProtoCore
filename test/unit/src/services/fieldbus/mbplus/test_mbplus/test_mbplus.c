// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Modbus Plus HDLC frame codec (services/fieldbus/mbplus/mbplus.h).
//
// The frame check sequence is CRC-16/X-25, the HDLC FCS of ISO/IEC 13239, cataloged by the CRC
// RevEng catalogue as CRC-16/IBM-SDLC:
//   width=16 poly=0x1021 init=0xffff refin=true refout=true xorout=0xffff check=0x906e
// test_published_check_value pins the catalogue's own check value, the CRC of the ASCII string
// "123456789". A reflected CRC is the one implementations most often get wrong in exactly the way
// a round-trip test cannot see, because building and checking with the same wrong polynomial still
// agrees with itself, so this single published number is what makes the codec interoperable.

#include "services/fieldbus/mbplus/mbplus.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// CRC RevEng catalogue, CRC-16/IBM-SDLC: check=0x906e over the nine ASCII digits "123456789".
void test_published_check_value(void)
{
    static const uint8_t CHECK[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x906Eu, protocore_mbplus_crc(CHECK, sizeof(CHECK)));
    // init=0xffff and xorout=0xffff cancel over an empty message.
    TEST_ASSERT_EQUAL_HEX16(0x0000u, protocore_mbplus_crc(CHECK, 0));
}

// ISO/IEC 13239 fixes the HDLC flag octet, and a Modbus Plus segment carries stations 1..64.
void test_published_constants(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x7Eu, MBPLUS_FLAG); // 0111 1110
    TEST_ASSERT_EQUAL_UINT8(64u, MBPLUS_MAX_STATION);
    TEST_ASSERT_EQUAL_HEX8(0x00u, MBPLUS_CTRL_DATA);
    TEST_ASSERT_EQUAL_HEX8(0x01u, MBPLUS_CTRL_TOKEN);
}

// A frame is 7E, address, control, payload, CRC low octet, CRC high octet, 7E, and the CRC covers
// the address through the last payload octet, not the flags.
void test_frame_layout(void)
{
    static const uint8_t PAYLOAD[3] = {0x03, 0x00, 0x01}; // a Modbus read-holding-registers stub
    uint8_t out[16];
    size_t n = protocore_mbplus_build(5, MBPLUS_CTRL_DATA, PAYLOAD, sizeof(PAYLOAD), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(9u, n); // 1 + 1 + 1 + 3 + 2 + 1

    TEST_ASSERT_EQUAL_HEX8(0x7Eu, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x05u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[2]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, out + 3, sizeof(PAYLOAD));
    TEST_ASSERT_EQUAL_HEX8(0x7Eu, out[8]);

    uint16_t crc = protocore_mbplus_crc(out + 1, 5); // address + control + payload
    TEST_ASSERT_EQUAL_HEX8((uint8_t)crc, out[6]);    // low octet first, the HDLC order
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), out[7]);

    MbPlusFrame f;
    memset(&f, 0, sizeof(f));
    TEST_ASSERT_TRUE(protocore_mbplus_parse(out, n, &f));
    TEST_ASSERT_EQUAL_HEX8(5u, f.address);
    TEST_ASSERT_EQUAL_HEX8(MBPLUS_CTRL_DATA, f.control);
    TEST_ASSERT_EQUAL_size_t(sizeof(PAYLOAD), f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, f.payload, sizeof(PAYLOAD));
}

// A token pass carries no payload: flags, address, control, CRC, flag.
void test_token_frame_has_no_payload(void)
{
    uint8_t out[16];
    size_t n = protocore_mbplus_build(1, MBPLUS_CTRL_TOKEN, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(6u, n);
    TEST_ASSERT_EQUAL_HEX8(MBPLUS_CTRL_TOKEN, out[2]);

    MbPlusFrame f;
    memset(&f, 0, sizeof(f));
    TEST_ASSERT_TRUE(protocore_mbplus_parse(out, n, &f));
    TEST_ASSERT_EQUAL_HEX8(1u, f.address);
    TEST_ASSERT_EQUAL_HEX8(MBPLUS_CTRL_TOKEN, f.control);
    TEST_ASSERT_EQUAL_size_t(0u, f.payload_len);
    TEST_ASSERT_NULL(f.payload);
}

// Every address, control and payload length survives build then parse unchanged.
void test_round_trip(void)
{
    static const uint8_t PAYLOAD[8] = {0x7E, 0x00, 0xFF, 0x7D, 0x5E, 0x01, 0x02, 0x03};
    for (uint8_t addr = 1; addr <= MBPLUS_MAX_STATION; addr++)
    {
        for (size_t len = 0; len <= sizeof(PAYLOAD); len += 4)
        {
            uint8_t frame[24];
            size_t n = protocore_mbplus_build(addr, MBPLUS_CTRL_DATA, len ? PAYLOAD : NULL, len, frame, sizeof(frame));
            TEST_ASSERT_EQUAL_size_t(len + 6u, n);

            MbPlusFrame f;
            memset(&f, 0, sizeof(f));
            TEST_ASSERT_TRUE(protocore_mbplus_parse(frame, n, &f));
            TEST_ASSERT_EQUAL_HEX8(addr, f.address);
            TEST_ASSERT_EQUAL_size_t(len, f.payload_len);
            if (len)
            {
                TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, f.payload, len);
            }
        }
    }
}

// A single-bit change in the address, control, payload or CRC breaks the check and the frame is
// dropped. This is the whole reason the data link carries a CRC-16 rather than a parity octet.
void test_single_bit_corruption_is_refused(void)
{
    static const uint8_t PAYLOAD[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t frame[16];
    size_t len = protocore_mbplus_build(9, MBPLUS_CTRL_DATA, PAYLOAD, sizeof(PAYLOAD), frame, sizeof(frame));

    for (size_t i = 1; i + 1 < len; i++) // everything between the two flags
    {
        for (int bit = 0; bit < 8; bit++)
        {
            uint8_t bad[16];
            memcpy(bad, frame, len);
            bad[i] ^= (uint8_t)(1u << bit);
            MbPlusFrame f;
            TEST_ASSERT_FALSE(protocore_mbplus_parse(bad, len, &f));
        }
    }
}

// Both flags must be present, and a frame shorter than flag/address/control/CRC/flag is refused.
void test_parse_rejects_bad_framing(void)
{
    static const uint8_t PAYLOAD[2] = {0xAA, 0xBB};
    uint8_t frame[16];
    size_t len = protocore_mbplus_build(3, MBPLUS_CTRL_DATA, PAYLOAD, sizeof(PAYLOAD), frame, sizeof(frame));
    MbPlusFrame f;
    TEST_ASSERT_TRUE(protocore_mbplus_parse(frame, len, &f));

    uint8_t bad[16];
    memcpy(bad, frame, len);
    bad[0] = 0x00;
    TEST_ASSERT_FALSE(protocore_mbplus_parse(bad, len, &f)); // no opening flag

    memcpy(bad, frame, len);
    bad[len - 1] = 0x00;
    TEST_ASSERT_FALSE(protocore_mbplus_parse(bad, len, &f)); // no closing flag

    TEST_ASSERT_FALSE(protocore_mbplus_parse(frame, 5, &f)); // shorter than the smallest frame
    TEST_ASSERT_FALSE(protocore_mbplus_parse(NULL, len, &f));
    TEST_ASSERT_FALSE(protocore_mbplus_parse(frame, len, NULL));
}

// The token walks 1, 2, ... max, then back to 1, so N steps from any station return to it and no
// station is ever skipped or visited twice in one lap.
void test_token_ring_rotation(void)
{
    static const uint8_t MAX_STATIONS[3] = {1, 8, MBPLUS_MAX_STATION};
    for (size_t m = 0; m < sizeof(MAX_STATIONS) / sizeof(MAX_STATIONS[0]); m++)
    {
        uint8_t max = MAX_STATIONS[m];
        for (uint8_t start = 1; start <= max; start++)
        {
            uint8_t seen[MBPLUS_MAX_STATION + 1];
            memset(seen, 0, sizeof(seen));
            uint8_t at = start;
            for (uint8_t step = 0; step < max; step++)
            {
                TEST_ASSERT_TRUE(at >= 1 && at <= max);
                TEST_ASSERT_EQUAL_UINT8(0u, seen[at]); // no station twice in one lap
                seen[at] = 1;
                at = protocore_mbplus_next_token(at, max);
            }
            TEST_ASSERT_EQUAL_UINT8(start, at); // a full lap returns the token to its start
        }
    }

    // The wrap itself, and the fail-safe when no station is active.
    TEST_ASSERT_EQUAL_UINT8(2u, protocore_mbplus_next_token(1, MBPLUS_MAX_STATION));
    TEST_ASSERT_EQUAL_UINT8(1u, protocore_mbplus_next_token(MBPLUS_MAX_STATION, MBPLUS_MAX_STATION));
    TEST_ASSERT_EQUAL_UINT8(1u, protocore_mbplus_next_token(0, 0));
    // A station above the segment's highest active one hands the token back to the first.
    TEST_ASSERT_EQUAL_UINT8(1u, protocore_mbplus_next_token(50, 8));
}

// Station 0 and station 65 are off the segment, and a buffer that cannot hold the whole frame
// yields 0 rather than a frame with no closing flag.
void test_build_refuses_bad_arguments(void)
{
    static const uint8_t PAYLOAD[2] = {0xAA, 0xBB};
    uint8_t out[16];

    TEST_ASSERT_EQUAL_size_t(0u, protocore_mbplus_build(0, MBPLUS_CTRL_DATA, PAYLOAD, 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(8u, protocore_mbplus_build(1, MBPLUS_CTRL_DATA, PAYLOAD, 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(8u, protocore_mbplus_build(64, MBPLUS_CTRL_DATA, PAYLOAD, 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_mbplus_build(65, MBPLUS_CTRL_DATA, PAYLOAD, 2, out, sizeof(out)));

    TEST_ASSERT_EQUAL_size_t(0u, protocore_mbplus_build(1, MBPLUS_CTRL_DATA, PAYLOAD, 2, out, 7));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_mbplus_build(1, MBPLUS_CTRL_DATA, PAYLOAD, 2, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_mbplus_build(1, MBPLUS_CTRL_DATA, NULL, 2, out, sizeof(out)));
}
