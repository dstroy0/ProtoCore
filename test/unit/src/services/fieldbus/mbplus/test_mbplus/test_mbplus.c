// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t mbplus_work[16]; // the borrow an entry takes; Mbplus never reads it

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
    Mbplus.crc_args.bytes = CHECK;
    Mbplus.crc_args.len = sizeof(CHECK);
    Mbplus.crc(mbplus_work);
    TEST_ASSERT_EQUAL_HEX16(0x906Eu, Mbplus.value);
    // init=0xffff and xorout=0xffff cancel over an empty message.
    Mbplus.crc_args.bytes = CHECK;
    Mbplus.crc_args.len = 0;
    Mbplus.crc(mbplus_work);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, Mbplus.value);
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
    Mbplus.build_args.address = 5;
    Mbplus.build_args.control = MBPLUS_CTRL_DATA;
    Mbplus.build_args.payload = PAYLOAD;
    Mbplus.build_args.payload_len = sizeof(PAYLOAD);
    Mbplus.build_args.out = out;
    Mbplus.build_args.cap = sizeof(out);
    Mbplus.build(mbplus_work);
    size_t n = Mbplus.n;
    TEST_ASSERT_EQUAL_size_t(9u, n); // 1 + 1 + 1 + 3 + 2 + 1

    TEST_ASSERT_EQUAL_HEX8(0x7Eu, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x05u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[2]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, out + 3, sizeof(PAYLOAD));
    TEST_ASSERT_EQUAL_HEX8(0x7Eu, out[8]);

    Mbplus.crc_args.bytes = out + 1;
    Mbplus.crc_args.len = 5;
    Mbplus.crc(mbplus_work);
    uint16_t crc = Mbplus.value;                  // address + control + payload
    TEST_ASSERT_EQUAL_HEX8((uint8_t)crc, out[6]); // low octet first, the HDLC order
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), out[7]);

    MbPlusFrame f;
    memset(&f, 0, sizeof(f));
    Mbplus.parse_args.frame = out;
    Mbplus.parse_args.len = n;
    Mbplus.parse_args.out = &f;
    Mbplus.parse(mbplus_work);
    TEST_ASSERT_TRUE(Mbplus.ok);
    TEST_ASSERT_EQUAL_HEX8(5u, f.address);
    TEST_ASSERT_EQUAL_HEX8(MBPLUS_CTRL_DATA, f.control);
    TEST_ASSERT_EQUAL_size_t(sizeof(PAYLOAD), f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, f.payload, sizeof(PAYLOAD));
}

// A token pass carries no payload: flags, address, control, CRC, flag.
void test_token_frame_has_no_payload(void)
{
    uint8_t out[16];
    Mbplus.build_args.address = 1;
    Mbplus.build_args.control = MBPLUS_CTRL_TOKEN;
    Mbplus.build_args.payload = NULL;
    Mbplus.build_args.payload_len = 0;
    Mbplus.build_args.out = out;
    Mbplus.build_args.cap = sizeof(out);
    Mbplus.build(mbplus_work);
    size_t n = Mbplus.n;
    TEST_ASSERT_EQUAL_size_t(6u, n);
    TEST_ASSERT_EQUAL_HEX8(MBPLUS_CTRL_TOKEN, out[2]);

    MbPlusFrame f;
    memset(&f, 0, sizeof(f));
    Mbplus.parse_args.frame = out;
    Mbplus.parse_args.len = n;
    Mbplus.parse_args.out = &f;
    Mbplus.parse(mbplus_work);
    TEST_ASSERT_TRUE(Mbplus.ok);
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
            Mbplus.build_args.address = addr;
            Mbplus.build_args.control = MBPLUS_CTRL_DATA;
            Mbplus.build_args.payload = len ? PAYLOAD : NULL;
            Mbplus.build_args.payload_len = len;
            Mbplus.build_args.out = frame;
            Mbplus.build_args.cap = sizeof(frame);
            Mbplus.build(mbplus_work);
            size_t n = Mbplus.n;
            TEST_ASSERT_EQUAL_size_t(len + 6u, n);

            MbPlusFrame f;
            memset(&f, 0, sizeof(f));
            Mbplus.parse_args.frame = frame;
            Mbplus.parse_args.len = n;
            Mbplus.parse_args.out = &f;
            Mbplus.parse(mbplus_work);
            TEST_ASSERT_TRUE(Mbplus.ok);
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
    Mbplus.build_args.address = 9;
    Mbplus.build_args.control = MBPLUS_CTRL_DATA;
    Mbplus.build_args.payload = PAYLOAD;
    Mbplus.build_args.payload_len = sizeof(PAYLOAD);
    Mbplus.build_args.out = frame;
    Mbplus.build_args.cap = sizeof(frame);
    Mbplus.build(mbplus_work);
    size_t len = Mbplus.n;

    for (size_t i = 1; i + 1 < len; i++) // everything between the two flags
    {
        for (int bit = 0; bit < 8; bit++)
        {
            uint8_t bad[16];
            memcpy(bad, frame, len);
            bad[i] ^= (uint8_t)(1u << bit);
            MbPlusFrame f;
            Mbplus.parse_args.frame = bad;
            Mbplus.parse_args.len = len;
            Mbplus.parse_args.out = &f;
            Mbplus.parse(mbplus_work);
            TEST_ASSERT_FALSE(Mbplus.ok);
        }
    }
}

// Both flags must be present, and a frame shorter than flag/address/control/CRC/flag is refused.
void test_parse_rejects_bad_framing(void)
{
    static const uint8_t PAYLOAD[2] = {0xAA, 0xBB};
    uint8_t frame[16];
    Mbplus.build_args.address = 3;
    Mbplus.build_args.control = MBPLUS_CTRL_DATA;
    Mbplus.build_args.payload = PAYLOAD;
    Mbplus.build_args.payload_len = sizeof(PAYLOAD);
    Mbplus.build_args.out = frame;
    Mbplus.build_args.cap = sizeof(frame);
    Mbplus.build(mbplus_work);
    size_t len = Mbplus.n;
    MbPlusFrame f;
    Mbplus.parse_args.frame = frame;
    Mbplus.parse_args.len = len;
    Mbplus.parse_args.out = &f;
    Mbplus.parse(mbplus_work);
    TEST_ASSERT_TRUE(Mbplus.ok);

    uint8_t bad[16];
    memcpy(bad, frame, len);
    bad[0] = 0x00;
    Mbplus.parse_args.frame = bad;
    Mbplus.parse_args.len = len;
    Mbplus.parse_args.out = &f;
    Mbplus.parse(mbplus_work);
    TEST_ASSERT_FALSE(Mbplus.ok); // no opening flag

    memcpy(bad, frame, len);
    bad[len - 1] = 0x00;
    Mbplus.parse_args.frame = bad;
    Mbplus.parse_args.len = len;
    Mbplus.parse_args.out = &f;
    Mbplus.parse(mbplus_work);
    TEST_ASSERT_FALSE(Mbplus.ok); // no closing flag

    Mbplus.parse_args.frame = frame;
    Mbplus.parse_args.len = 5;
    Mbplus.parse_args.out = &f;
    Mbplus.parse(mbplus_work);
    TEST_ASSERT_FALSE(Mbplus.ok); // shorter than the smallest frame
    Mbplus.parse_args.frame = NULL;
    Mbplus.parse_args.len = len;
    Mbplus.parse_args.out = &f;
    Mbplus.parse(mbplus_work);
    TEST_ASSERT_FALSE(Mbplus.ok);
    Mbplus.parse_args.frame = frame;
    Mbplus.parse_args.len = len;
    Mbplus.parse_args.out = NULL;
    Mbplus.parse(mbplus_work);
    TEST_ASSERT_FALSE(Mbplus.ok);
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
                Mbplus.next_token_args.current = at;
                Mbplus.next_token_args.max_station = max;
                Mbplus.next_token(mbplus_work);
                at = Mbplus.value;
            }
            TEST_ASSERT_EQUAL_UINT8(start, at); // a full lap returns the token to its start
        }
    }

    // The wrap itself, and the fail-safe when no station is active.
    Mbplus.next_token_args.current = 1;
    Mbplus.next_token_args.max_station = MBPLUS_MAX_STATION;
    Mbplus.next_token(mbplus_work);
    TEST_ASSERT_EQUAL_UINT8(2u, Mbplus.value);
    Mbplus.next_token_args.current = MBPLUS_MAX_STATION;
    Mbplus.next_token_args.max_station = MBPLUS_MAX_STATION;
    Mbplus.next_token(mbplus_work);
    TEST_ASSERT_EQUAL_UINT8(1u, Mbplus.value);
    Mbplus.next_token_args.current = 0;
    Mbplus.next_token_args.max_station = 0;
    Mbplus.next_token(mbplus_work);
    TEST_ASSERT_EQUAL_UINT8(1u, Mbplus.value);
    // A station above the segment's highest active one hands the token back to the first.
    Mbplus.next_token_args.current = 50;
    Mbplus.next_token_args.max_station = 8;
    Mbplus.next_token(mbplus_work);
    TEST_ASSERT_EQUAL_UINT8(1u, Mbplus.value);
}

// Station 0 and station 65 are off the segment, and a buffer that cannot hold the whole frame
// yields 0 rather than a frame with no closing flag.
void test_build_refuses_bad_arguments(void)
{
    static const uint8_t PAYLOAD[2] = {0xAA, 0xBB};
    uint8_t out[16];

    Mbplus.build_args.address = 0;
    Mbplus.build_args.control = MBPLUS_CTRL_DATA;
    Mbplus.build_args.payload = PAYLOAD;
    Mbplus.build_args.payload_len = 2;
    Mbplus.build_args.out = out;
    Mbplus.build_args.cap = sizeof(out);
    Mbplus.build(mbplus_work);
    TEST_ASSERT_EQUAL_size_t(0u, Mbplus.n);
    Mbplus.build_args.address = 1;
    Mbplus.build_args.control = MBPLUS_CTRL_DATA;
    Mbplus.build_args.payload = PAYLOAD;
    Mbplus.build_args.payload_len = 2;
    Mbplus.build_args.out = out;
    Mbplus.build_args.cap = sizeof(out);
    Mbplus.build(mbplus_work);
    TEST_ASSERT_EQUAL_size_t(8u, Mbplus.n);
    Mbplus.build_args.address = 64;
    Mbplus.build_args.control = MBPLUS_CTRL_DATA;
    Mbplus.build_args.payload = PAYLOAD;
    Mbplus.build_args.payload_len = 2;
    Mbplus.build_args.out = out;
    Mbplus.build_args.cap = sizeof(out);
    Mbplus.build(mbplus_work);
    TEST_ASSERT_EQUAL_size_t(8u, Mbplus.n);
    Mbplus.build_args.address = 65;
    Mbplus.build_args.control = MBPLUS_CTRL_DATA;
    Mbplus.build_args.payload = PAYLOAD;
    Mbplus.build_args.payload_len = 2;
    Mbplus.build_args.out = out;
    Mbplus.build_args.cap = sizeof(out);
    Mbplus.build(mbplus_work);
    TEST_ASSERT_EQUAL_size_t(0u, Mbplus.n);

    Mbplus.build_args.address = 1;
    Mbplus.build_args.control = MBPLUS_CTRL_DATA;
    Mbplus.build_args.payload = PAYLOAD;
    Mbplus.build_args.payload_len = 2;
    Mbplus.build_args.out = out;
    Mbplus.build_args.cap = 7;
    Mbplus.build(mbplus_work);
    TEST_ASSERT_EQUAL_size_t(0u, Mbplus.n);
    Mbplus.build_args.address = 1;
    Mbplus.build_args.control = MBPLUS_CTRL_DATA;
    Mbplus.build_args.payload = PAYLOAD;
    Mbplus.build_args.payload_len = 2;
    Mbplus.build_args.out = NULL;
    Mbplus.build_args.cap = sizeof(out);
    Mbplus.build(mbplus_work);
    TEST_ASSERT_EQUAL_size_t(0u, Mbplus.n);
    Mbplus.build_args.address = 1;
    Mbplus.build_args.control = MBPLUS_CTRL_DATA;
    Mbplus.build_args.payload = NULL;
    Mbplus.build_args.payload_len = 2;
    Mbplus.build_args.out = out;
    Mbplus.build_args.cap = sizeof(out);
    Mbplus.build(mbplus_work);
    TEST_ASSERT_EQUAL_size_t(0u, Mbplus.n);
}
