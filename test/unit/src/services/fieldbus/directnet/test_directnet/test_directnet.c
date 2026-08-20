// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the DirectNET serial frame codec (services/fieldbus/directnet/directnet.h).
//
// DirectNET is a BSC-style character protocol: every delimiter it uses is an ANSI X3.4 (ASCII) C0
// control code, and every field it writes is an ASCII-hex digit. test_ascii_control_codes is the
// load-bearing case, because those eight code points are published assignments and a codec that
// spells SOH or ETB with any other octet cannot be framed by a peer. The frame cases carry their
// LRC worked out octet by octet in the comment, so a wrong XOR span cannot be reproduced here.

#include "services/fieldbus/directnet/directnet.h"
#include <string.h>

#include <unity.h>

static uint8_t directnet_work[16]; // the borrow an entry takes; Directnet never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// ANSI X3.4 C0 controls: SOH 0x01, STX 0x02, ETX 0x03, EOT 0x04, ENQ 0x05, ACK 0x06, NAK 0x15,
// ETB 0x17. The two request types are the ASCII digits '0' (0x30) and '8' (0x38).
void test_ascii_control_codes(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01u, DNET_SOH);
    TEST_ASSERT_EQUAL_HEX8(0x02u, DNET_STX);
    TEST_ASSERT_EQUAL_HEX8(0x03u, DNET_ETX);
    TEST_ASSERT_EQUAL_HEX8(0x04u, DNET_EOT);
    TEST_ASSERT_EQUAL_HEX8(0x05u, DNET_ENQ);
    TEST_ASSERT_EQUAL_HEX8(0x06u, DNET_ACK);
    TEST_ASSERT_EQUAL_HEX8(0x15u, DNET_NAK);
    TEST_ASSERT_EQUAL_HEX8(0x17u, DNET_ETB);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)'0', DNET_READ);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)'8', DNET_WRITE);
}

// A longitudinal XOR is its own inverse, so appending the LRC makes the whole block XOR to zero.
// That property holds for any content and is what a receiver relies on.
void test_lrc_block_xors_to_zero(void)
{
    static const uint8_t BODY[] = {0x31, 0x32, 0x33, 0x17};
    uint8_t block[5];
    memcpy(block, BODY, sizeof(BODY));
    DirectnetV.lrc_args.bytes = BODY;
    DirectnetV.lrc_args.len = sizeof(BODY);
    Directnet.lrc(directnet_work);
    block[4] = DirectnetV.value;
    DirectnetV.lrc_args.bytes = block;
    DirectnetV.lrc_args.len = sizeof(block);
    Directnet.lrc(directnet_work);
    TEST_ASSERT_EQUAL_HEX8(0x00u, DirectnetV.value);

    // An empty span has no octets to fold, so its LRC is the identity element.
    DirectnetV.lrc_args.bytes = BODY;
    DirectnetV.lrc_args.len = 0;
    Directnet.lrc(directnet_work);
    TEST_ASSERT_EQUAL_HEX8(0x00u, DirectnetV.value);
}

// Read 2 blocks at V-memory address 0x0040 from station 1. The header layout is
//   SOH [slave:2 hex][type:1][addr:4 hex][blocks:2 hex] ETB [LRC]
// so slave 1 renders "01", type DNET_READ is '0', 0x0040 renders "0040" and 2 renders "02":
//   01 | '0' '1' '0' '0' '0' '4' '0' '0' '2' | 17 | LRC
// The LRC covers slave..ETB, i.e. 30 31 30 30 30 34 30 30 32 17. The six 0x30 octets pair off to
// zero, leaving 0x31 ^ 0x34 ^ 0x32 ^ 0x17 = 0x05 ^ 0x32 ^ 0x17 = 0x37 ^ 0x17 = 0x20.
void test_header_frame(void)
{
    static const uint8_t WANT[12] = {0x01, '0', '1', '0', '0', '0', '4', '0', '0', '2', 0x17, 0x20};
    uint8_t out[16];
    DirectnetV.header_args.slave = 1;
    DirectnetV.header_args.type = DNET_READ;
    DirectnetV.header_args.address = 0x0040;
    DirectnetV.header_args.blocks = 2;
    DirectnetV.header_args.out = out;
    DirectnetV.header_args.cap = sizeof(out);
    Directnet.header(directnet_work);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), DirectnetV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));
}

// Nibbles above 9 render as uppercase 'A'..'F' (ASCII 0x41..0x46), the only spelling a DirectNET
// peer parses. Station 0xAB, write, address 0xCDEF, 0xFA blocks.
void test_header_hex_digits_are_uppercase(void)
{
    uint8_t out[16];
    DirectnetV.header_args.slave = 0xAB;
    DirectnetV.header_args.type = DNET_WRITE;
    DirectnetV.header_args.address = 0xCDEF;
    DirectnetV.header_args.blocks = 0xFA;
    DirectnetV.header_args.out = out;
    DirectnetV.header_args.cap = sizeof(out);
    Directnet.header(directnet_work);
    TEST_ASSERT_EQUAL_size_t(12u, DirectnetV.n);
    static const uint8_t BODY[10] = {'A', 'B', '8', 'C', 'D', 'E', 'F', 'F', 'A', 0x17};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BODY, out + 1, sizeof(BODY));
    // The whole framed block, LRC included, folds to zero.
    DirectnetV.lrc_args.bytes = out + 1;
    DirectnetV.lrc_args.len = 11;
    Directnet.lrc(directnet_work);
    TEST_ASSERT_EQUAL_HEX8(0x00u, DirectnetV.value);
}

// A data frame is STX + data + ETX + LRC, and the LRC covers data..ETX. For "ABCD":
//   41 ^ 42 = 03, ^ 43 = 40, ^ 44 = 04, ^ 03 (ETX) = 07.
void test_data_frame(void)
{
    static const uint8_t PAYLOAD[4] = {'A', 'B', 'C', 'D'};
    static const uint8_t WANT[7] = {0x02, 'A', 'B', 'C', 'D', 0x03, 0x07};
    uint8_t out[16];
    DirectnetV.data_args.data = PAYLOAD;
    DirectnetV.data_args.data_len = sizeof(PAYLOAD);
    DirectnetV.data_args.out = out;
    DirectnetV.data_args.cap = sizeof(out);
    DirectnetV.data(directnet_work);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), DirectnetV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));
}

// Build then parse returns the payload unchanged, for every length from empty upward.
void test_data_frame_round_trip(void)
{
    static const uint8_t PAYLOAD[8] = {0x00, 0x01, 0x7E, 0x7F, 0x80, 0xFE, 0xFF, 0x17};
    for (size_t n = 0; n <= sizeof(PAYLOAD); n++)
    {
        uint8_t frame[16];
        DirectnetV.data_args.data = n ? PAYLOAD : NULL;
        DirectnetV.data_args.data_len = n;
        DirectnetV.data_args.out = frame;
        DirectnetV.data_args.cap = sizeof(frame);
        DirectnetV.data(directnet_work);
        size_t len = DirectnetV.n;
        TEST_ASSERT_EQUAL_size_t(n + 3u, len);

        const uint8_t *data = NULL;
        size_t data_len = 123;
        DirectnetV.data_parse_args.frame = frame;
        DirectnetV.data_parse_args.len = len;
        DirectnetV.data_parse_args.data = &data;
        DirectnetV.data_parse_args.data_len = &data_len;
        Directnet.data_parse(directnet_work);
        TEST_ASSERT_TRUE(DirectnetV.ok);
        TEST_ASSERT_EQUAL_size_t(n, data_len);
        if (n)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, data, n);
        }
        else
        {
            TEST_ASSERT_NULL(data); // no octets between STX and ETX
        }
    }
}

// The two out-parameters are independently optional.
void test_data_parse_optional_outputs(void)
{
    static const uint8_t PAYLOAD[4] = {'A', 'B', 'C', 'D'};
    uint8_t frame[16];
    DirectnetV.data_args.data = PAYLOAD;
    DirectnetV.data_args.data_len = sizeof(PAYLOAD);
    DirectnetV.data_args.out = frame;
    DirectnetV.data_args.cap = sizeof(frame);
    DirectnetV.data(directnet_work);
    size_t len = DirectnetV.n;

    size_t data_len = 0;
    DirectnetV.data_parse_args.frame = frame;
    DirectnetV.data_parse_args.len = len;
    DirectnetV.data_parse_args.data = NULL;
    DirectnetV.data_parse_args.data_len = &data_len;
    Directnet.data_parse(directnet_work);
    TEST_ASSERT_TRUE(DirectnetV.ok);
    TEST_ASSERT_EQUAL_size_t(4u, data_len);

    const uint8_t *data = NULL;
    DirectnetV.data_parse_args.frame = frame;
    DirectnetV.data_parse_args.len = len;
    DirectnetV.data_parse_args.data = &data;
    DirectnetV.data_parse_args.data_len = NULL;
    Directnet.data_parse(directnet_work);
    TEST_ASSERT_TRUE(DirectnetV.ok);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, data, 4);
}

// A single-bit change anywhere inside the frame flips at least one LRC bit, so the parser refuses
// it. This is the whole point of the check character.
void test_single_octet_corruption_is_refused(void)
{
    static const uint8_t PAYLOAD[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t frame[16];
    DirectnetV.data_args.data = PAYLOAD;
    DirectnetV.data_args.data_len = sizeof(PAYLOAD);
    DirectnetV.data_args.out = frame;
    DirectnetV.data_args.cap = sizeof(frame);
    DirectnetV.data(directnet_work);
    size_t len = DirectnetV.n;
    for (size_t i = 0; i < len; i++)
    {
        uint8_t bad[16];
        memcpy(bad, frame, len);
        bad[i] ^= 0x01;
        const uint8_t *data;
        size_t data_len;
        DirectnetV.data_parse_args.frame = bad;
        DirectnetV.data_parse_args.len = len;
        DirectnetV.data_parse_args.data = &data;
        DirectnetV.data_parse_args.data_len = &data_len;
        Directnet.data_parse(directnet_work);
        TEST_ASSERT_FALSE(DirectnetV.ok);
    }
}

// Framing faults: no STX, no ETX before the LRC, and a block shorter than STX+ETX+LRC.
void test_data_parse_rejects_bad_framing(void)
{
    const uint8_t *data;
    size_t data_len;

    static const uint8_t NO_STX[4] = {0x00, 0x11, 0x03, 0x11 ^ 0x03};
    DirectnetV.data_parse_args.frame = NO_STX;
    DirectnetV.data_parse_args.len = sizeof(NO_STX);
    DirectnetV.data_parse_args.data = &data;
    DirectnetV.data_parse_args.data_len = &data_len;
    Directnet.data_parse(directnet_work);
    TEST_ASSERT_FALSE(DirectnetV.ok);

    static const uint8_t NO_ETX[4] = {0x02, 0x11, 0x22, 0x33};
    DirectnetV.data_parse_args.frame = NO_ETX;
    DirectnetV.data_parse_args.len = sizeof(NO_ETX);
    DirectnetV.data_parse_args.data = &data;
    DirectnetV.data_parse_args.data_len = &data_len;
    Directnet.data_parse(directnet_work);
    TEST_ASSERT_FALSE(DirectnetV.ok);

    static const uint8_t TOO_SHORT[2] = {0x02, 0x03};
    DirectnetV.data_parse_args.frame = TOO_SHORT;
    DirectnetV.data_parse_args.len = sizeof(TOO_SHORT);
    DirectnetV.data_parse_args.data = &data;
    DirectnetV.data_parse_args.data_len = &data_len;
    Directnet.data_parse(directnet_work);
    TEST_ASSERT_FALSE(DirectnetV.ok);

    DirectnetV.data_parse_args.frame = NULL;
    DirectnetV.data_parse_args.len = 5;
    DirectnetV.data_parse_args.data = &data;
    DirectnetV.data_parse_args.data_len = &data_len;
    Directnet.data_parse(directnet_work);
    TEST_ASSERT_FALSE(DirectnetV.ok);
}

// A buffer that cannot hold the whole frame yields 0 rather than a truncated one: a short frame is
// a different message, and a peer would accept it as one.
void test_builders_refuse_a_short_buffer(void)
{
    uint8_t out[16];
    static const uint8_t PAYLOAD[4] = {'A', 'B', 'C', 'D'};

    DirectnetV.header_args.slave = 1;
    DirectnetV.header_args.type = DNET_READ;
    DirectnetV.header_args.address = 0x40;
    DirectnetV.header_args.blocks = 2;
    DirectnetV.header_args.out = out;
    DirectnetV.header_args.cap = 11;
    Directnet.header(directnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, DirectnetV.n); // needs 12
    DirectnetV.header_args.slave = 1;
    DirectnetV.header_args.type = DNET_READ;
    DirectnetV.header_args.address = 0x40;
    DirectnetV.header_args.blocks = 2;
    DirectnetV.header_args.out = out;
    DirectnetV.header_args.cap = 12;
    Directnet.header(directnet_work);
    TEST_ASSERT_EQUAL_size_t(12u, DirectnetV.n);
    DirectnetV.header_args.slave = 1;
    DirectnetV.header_args.type = DNET_READ;
    DirectnetV.header_args.address = 0x40;
    DirectnetV.header_args.blocks = 2;
    DirectnetV.header_args.out = NULL;
    DirectnetV.header_args.cap = sizeof(out);
    Directnet.header(directnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, DirectnetV.n);

    DirectnetV.data_args.data = PAYLOAD;
    DirectnetV.data_args.data_len = 4;
    DirectnetV.data_args.out = out;
    DirectnetV.data_args.cap = 6;
    DirectnetV.data(directnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, DirectnetV.n); // needs 7
    DirectnetV.data_args.data = PAYLOAD;
    DirectnetV.data_args.data_len = 4;
    DirectnetV.data_args.out = out;
    DirectnetV.data_args.cap = 7;
    DirectnetV.data(directnet_work);
    TEST_ASSERT_EQUAL_size_t(7u, DirectnetV.n);
    DirectnetV.data_args.data = PAYLOAD;
    DirectnetV.data_args.data_len = 4;
    DirectnetV.data_args.out = NULL;
    DirectnetV.data_args.cap = sizeof(out);
    DirectnetV.data(directnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, DirectnetV.n);
    DirectnetV.data_args.data = NULL;
    DirectnetV.data_args.data_len = 4;
    DirectnetV.data_args.out = out;
    DirectnetV.data_args.cap = sizeof(out);
    DirectnetV.data(directnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, DirectnetV.n);
}
