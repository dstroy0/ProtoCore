// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Z-Wave Serial API frame codec (services/radio/zwave/zwave.h).
//
// The Silicon Labs Serial API (INS12350) fixes the data frame as SOF | LEN | Type | Command | Data |
// Checksum, where LEN counts Type through Checksum and the checksum is 0xFF XOR-folded over LEN
// through the last data octet. test_ins12350_getversion_frame is the load-bearing case: the
// FUNC_ID_ZW_GET_VERSION request is the shortest legal frame there is, so its five octets pin LEN's
// meaning and the checksum's span at once, and the checksum octet is derived here from the XOR
// definition rather than copied from this codec.

#include "services/radio/zwave/zwave.h"
#include <string.h>

#include <unity.h>

static uint8_t zwave_work[16]; // the borrow an entry takes; Zwave never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// A host request for FUNC_ID_ZW_GET_VERSION (0x15), type REQ, carrying no data.
//
//   LEN counts Type(1) + Command(1) + Checksum(1)          = 0x03
//   Checksum = 0xFF ^ LEN ^ Type ^ Command
//            = 0xFF ^ 0x03 ^ 0x00 ^ 0x15
//            = 0xFC ^ 0x00 ^ 0x15
//            = 0xE9
//   so the frame is 01 03 00 15 E9, five octets.
void test_ins12350_getversion_frame(void)
{
    static const uint8_t WANT[5] = {ZWAVE_SOF, 0x03, ZWAVE_REQ, 0x15, 0xE9};
    uint8_t out[16];
    Zwave.build_frame_args.type = ZWAVE_REQ;
    Zwave.build_frame_args.cmd = 0x15;
    Zwave.build_frame_args.data = NULL;
    Zwave.build_frame_args.data_len = 0;
    Zwave.build_frame_args.out = out;
    Zwave.build_frame_args.cap = sizeof(out);
    Zwave.build_frame(zwave_work);
    TEST_ASSERT_EQUAL_UINT16(5, Zwave.value);
    TEST_ASSERT_EQUAL_MEMORY(WANT, out, 5);

    uint8_t type = 0xFF;
    uint8_t cmd = 0;
    const uint8_t *data = NULL;
    uint8_t data_len = 0xFF;
    Zwave.parse_frame_args.raw = WANT;
    Zwave.parse_frame_args.len = sizeof(WANT);
    Zwave.parse_frame_args.type = &type;
    Zwave.parse_frame_args.cmd = &cmd;
    Zwave.parse_frame_args.pdata = &data;
    Zwave.parse_frame_args.pdata_len = &data_len;
    Zwave.parse_frame(zwave_work);
    TEST_ASSERT_EQUAL_INT(5, Zwave.n);
    TEST_ASSERT_EQUAL_UINT8(ZWAVE_REQ, type);
    TEST_ASSERT_EQUAL_HEX8(0x15, cmd);
    TEST_ASSERT_EQUAL_UINT8(0, data_len);
}

// LEN counts Type + Command + Data + Checksum, so a frame with n data octets is n + 5 long and its
// LEN field reads n + 3.
void test_len_field_counts_type_through_checksum(void)
{
    for (uint8_t n = 0; n <= 8; n++)
    {
        uint8_t data[8];
        memset(data, 0xA5, sizeof(data));
        uint8_t out[32];
        Zwave.build_frame_args.type = ZWAVE_RES;
        Zwave.build_frame_args.cmd = 0x04;
        Zwave.build_frame_args.data = data;
        Zwave.build_frame_args.data_len = n;
        Zwave.build_frame_args.out = out;
        Zwave.build_frame_args.cap = sizeof(out);
        Zwave.build_frame(zwave_work);
        const uint16_t total = Zwave.value;
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(n + 5), total);
        TEST_ASSERT_EQUAL_HEX8(ZWAVE_SOF, out[0]);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(n + 3), out[1]);
    }
}

// The checksum is 0xFF XORed with LEN through the last data octet, and lands in the final position.
void test_checksum_span_and_position(void)
{
    static const uint8_t DATA[3] = {0x01, 0x0A, 0xAB};
    uint8_t out[32];
    Zwave.build_frame_args.type = ZWAVE_RES;
    Zwave.build_frame_args.cmd = 0x04;
    Zwave.build_frame_args.data = DATA;
    Zwave.build_frame_args.data_len = sizeof(DATA);
    Zwave.build_frame_args.out = out;
    Zwave.build_frame_args.cap = sizeof(out);
    Zwave.build_frame(zwave_work);
    const uint16_t n = Zwave.value;
    TEST_ASSERT_EQUAL_UINT16(8, n);

    uint8_t want = 0xFF;
    for (uint16_t i = 1; i < n - 1; i++) // LEN through the last data octet, excluding SOF
    {
        want = (uint8_t)(want ^ out[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(want, out[n - 1]);
}

// Build then parse returns the type, command and data unchanged.
void test_build_then_parse_round_trip(void)
{
    static const uint8_t DATA[3] = {0x01, 0x0A, 0xAB};
    uint8_t buf[32];
    Zwave.build_frame_args.type = ZWAVE_RES;
    Zwave.build_frame_args.cmd = 0x04;
    Zwave.build_frame_args.data = DATA;
    Zwave.build_frame_args.data_len = sizeof(DATA);
    Zwave.build_frame_args.out = buf;
    Zwave.build_frame_args.cap = sizeof(buf);
    Zwave.build_frame(zwave_work);
    const uint16_t n = Zwave.value;
    TEST_ASSERT_EQUAL_UINT16(8, n);

    uint8_t type = 0;
    uint8_t cmd = 0;
    const uint8_t *data = NULL;
    uint8_t data_len = 0;
    Zwave.parse_frame_args.raw = buf;
    Zwave.parse_frame_args.len = n;
    Zwave.parse_frame_args.type = &type;
    Zwave.parse_frame_args.cmd = &cmd;
    Zwave.parse_frame_args.pdata = &data;
    Zwave.parse_frame_args.pdata_len = &data_len;
    Zwave.parse_frame(zwave_work);
    TEST_ASSERT_EQUAL_INT(8, Zwave.n);
    TEST_ASSERT_EQUAL_UINT8(ZWAVE_RES, type);
    TEST_ASSERT_EQUAL_HEX8(0x04, cmd);
    TEST_ASSERT_EQUAL_UINT8(sizeof(DATA), data_len);
    TEST_ASSERT_EQUAL_MEMORY(DATA, data, sizeof(DATA));
}

// One flipped bit anywhere from LEN onward must fail the checksum. SOF is excluded: changing it
// makes the octet stop being a frame start, which is a different refusal.
void test_parse_rejects_a_corrupted_frame(void)
{
    static const uint8_t DATA[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t frame[32];
    Zwave.build_frame_args.type = ZWAVE_REQ;
    Zwave.build_frame_args.cmd = 0x13;
    Zwave.build_frame_args.data = DATA;
    Zwave.build_frame_args.data_len = sizeof(DATA);
    Zwave.build_frame_args.out = frame;
    Zwave.build_frame_args.cap = sizeof(frame);
    Zwave.build_frame(zwave_work);
    const uint16_t n = Zwave.value;
    TEST_ASSERT_EQUAL_UINT16(9, n);

    for (uint16_t i = 2; i < n; i++) // Type, Command, Data, Checksum
    {
        uint8_t bad[32];
        memcpy(bad, frame, n);
        bad[i] = (uint8_t)(bad[i] ^ 0x01);
        Zwave.parse_frame_args.raw = bad;
        Zwave.parse_frame_args.len = n;
        Zwave.parse_frame_args.type = NULL;
        Zwave.parse_frame_args.cmd = NULL;
        Zwave.parse_frame_args.pdata = NULL;
        Zwave.parse_frame_args.pdata_len = NULL;
        Zwave.parse_frame(zwave_work);
        TEST_ASSERT_EQUAL_INT(-1, Zwave.n);
    }
}

// An octet that is not SOF does not start a data frame; the control bytes are told apart by the
// helpers instead.
void test_parse_rejects_a_non_sof_start(void)
{
    static const uint8_t FRAME[5] = {0x00, 0x03, 0x00, 0x15, 0xE9};
    Zwave.parse_frame_args.raw = FRAME;
    Zwave.parse_frame_args.len = sizeof(FRAME);
    Zwave.parse_frame_args.type = NULL;
    Zwave.parse_frame_args.cmd = NULL;
    Zwave.parse_frame_args.pdata = NULL;
    Zwave.parse_frame_args.pdata_len = NULL;
    Zwave.parse_frame(zwave_work);
    TEST_ASSERT_EQUAL_INT(-1, Zwave.n);

    static const uint8_t ACK_ONLY[1] = {ZWAVE_ACK};
    Zwave.parse_frame_args.raw = ACK_ONLY;
    Zwave.parse_frame_args.len = 1;
    Zwave.parse_frame_args.type = NULL;
    Zwave.parse_frame_args.cmd = NULL;
    Zwave.parse_frame_args.pdata = NULL;
    Zwave.parse_frame_args.pdata_len = NULL;
    Zwave.parse_frame(zwave_work);
    TEST_ASSERT_EQUAL_INT(-1, Zwave.n);
}

// A frame that has not fully arrived asks for more octets rather than failing.
void test_parse_waits_for_the_rest(void)
{
    static const uint8_t FRAME[5] = {ZWAVE_SOF, 0x03, 0x00, 0x15, 0xE9};
    Zwave.parse_frame_args.raw = FRAME;
    Zwave.parse_frame_args.len = 0;
    Zwave.parse_frame_args.type = NULL;
    Zwave.parse_frame_args.cmd = NULL;
    Zwave.parse_frame_args.pdata = NULL;
    Zwave.parse_frame_args.pdata_len = NULL;
    Zwave.parse_frame(zwave_work);
    TEST_ASSERT_EQUAL_INT(0, Zwave.n);
    for (uint16_t have = 1; have < sizeof(FRAME); have++)
    {
        Zwave.parse_frame_args.raw = FRAME;
        Zwave.parse_frame_args.len = have;
        Zwave.parse_frame_args.type = NULL;
        Zwave.parse_frame_args.cmd = NULL;
        Zwave.parse_frame_args.pdata = NULL;
        Zwave.parse_frame_args.pdata_len = NULL;
        Zwave.parse_frame(zwave_work);
        TEST_ASSERT_EQUAL_INT(0, Zwave.n);
    }
    Zwave.parse_frame_args.raw = FRAME;
    Zwave.parse_frame_args.len = sizeof(FRAME);
    Zwave.parse_frame_args.type = NULL;
    Zwave.parse_frame_args.cmd = NULL;
    Zwave.parse_frame_args.pdata = NULL;
    Zwave.parse_frame_args.pdata_len = NULL;
    Zwave.parse_frame(zwave_work);
    TEST_ASSERT_EQUAL_INT(5, Zwave.n);
}

// A LEN below 3 cannot cover Type + Command + Checksum, and one past the configured data maximum is
// implausible: both are refused before any octet past the header is read.
void test_parse_rejects_an_out_of_range_len(void)
{
    for (uint8_t len = 0; len < 3; len++)
    {
        const uint8_t frame[4] = {ZWAVE_SOF, len, 0x00, 0x00};
        Zwave.parse_frame_args.raw = frame;
        Zwave.parse_frame_args.len = sizeof(frame);
        Zwave.parse_frame_args.type = NULL;
        Zwave.parse_frame_args.cmd = NULL;
        Zwave.parse_frame_args.pdata = NULL;
        Zwave.parse_frame_args.pdata_len = NULL;
        Zwave.parse_frame(zwave_work);
        TEST_ASSERT_EQUAL_INT(-1, Zwave.n);
    }
    const uint8_t too_long[4] = {ZWAVE_SOF, (uint8_t)(PROTOCORE_ZWAVE_MAX_DATA + 4), 0x00, 0x00};
    Zwave.parse_frame_args.raw = too_long;
    Zwave.parse_frame_args.len = sizeof(too_long);
    Zwave.parse_frame_args.type = NULL;
    Zwave.parse_frame_args.cmd = NULL;
    Zwave.parse_frame_args.pdata = NULL;
    Zwave.parse_frame_args.pdata_len = NULL;
    Zwave.parse_frame(zwave_work);
    TEST_ASSERT_EQUAL_INT(-1, Zwave.n);
}

// The Serial API's single-octet flow control: ACK 0x06, NAK 0x15, CAN 0x18, each distinct from the
// others and from SOF.
void test_control_octets(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, ZWAVE_SOF);
    TEST_ASSERT_EQUAL_HEX8(0x06, ZWAVE_ACK);
    TEST_ASSERT_EQUAL_HEX8(0x15, ZWAVE_NAK);
    TEST_ASSERT_EQUAL_HEX8(0x18, ZWAVE_CAN);

    Zwave.is_ack_args.b = ZWAVE_ACK;
    Zwave.is_ack(zwave_work);
    TEST_ASSERT_TRUE(Zwave.ok);
    Zwave.is_nak_args.b = ZWAVE_NAK;
    Zwave.is_nak(zwave_work);
    TEST_ASSERT_TRUE(Zwave.ok);
    Zwave.is_can_args.b = ZWAVE_CAN;
    Zwave.is_can(zwave_work);
    TEST_ASSERT_TRUE(Zwave.ok);

    Zwave.is_ack_args.b = ZWAVE_NAK;
    Zwave.is_ack(zwave_work);
    TEST_ASSERT_FALSE(Zwave.ok);
    Zwave.is_ack_args.b = ZWAVE_CAN;
    Zwave.is_ack(zwave_work);
    TEST_ASSERT_FALSE(Zwave.ok);
    Zwave.is_nak_args.b = ZWAVE_ACK;
    Zwave.is_nak(zwave_work);
    TEST_ASSERT_FALSE(Zwave.ok);
    Zwave.is_can_args.b = ZWAVE_ACK;
    Zwave.is_can(zwave_work);
    TEST_ASSERT_FALSE(Zwave.ok);
    Zwave.is_ack_args.b = ZWAVE_SOF;
    Zwave.is_ack(zwave_work);
    TEST_ASSERT_FALSE(Zwave.ok);

    uint8_t out[2] = {0xAA, 0xAA};
    Zwave.build_ack_args.out = out;
    Zwave.build_ack_args.cap = sizeof(out);
    Zwave.build_ack(zwave_work);
    TEST_ASSERT_EQUAL_UINT16(1, Zwave.value);
    TEST_ASSERT_EQUAL_HEX8(ZWAVE_ACK, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[1]); // one octet written, no more
    Zwave.build_ack_args.out = out;
    Zwave.build_ack_args.cap = 0;
    Zwave.build_ack(zwave_work);
    TEST_ASSERT_EQUAL_UINT16(0, Zwave.value);
    Zwave.build_ack_args.out = NULL;
    Zwave.build_ack_args.cap = 1;
    Zwave.build_ack(zwave_work);
    TEST_ASSERT_EQUAL_UINT16(0, Zwave.value);
}

// Build refuses a data length past the configured maximum, a null buffer, a null payload with a
// non-zero length, and a buffer one octet short of the exact frame.
void test_build_bounds(void)
{
    uint8_t data[PROTOCORE_ZWAVE_MAX_DATA + 1];
    memset(data, 0x5A, sizeof(data));
    uint8_t out[PROTOCORE_ZWAVE_MAX_DATA + 8];

    Zwave.build_frame_args.type = ZWAVE_REQ;
    Zwave.build_frame_args.cmd = 0x01;
    Zwave.build_frame_args.data = data;
    Zwave.build_frame_args.data_len = PROTOCORE_ZWAVE_MAX_DATA + 1;
    Zwave.build_frame_args.out = out;
    Zwave.build_frame_args.cap = sizeof(out);
    Zwave.build_frame(zwave_work);
    TEST_ASSERT_EQUAL_UINT16(0, Zwave.value);
    Zwave.build_frame_args.type = ZWAVE_REQ;
    Zwave.build_frame_args.cmd = 0x01;
    Zwave.build_frame_args.data = data;
    Zwave.build_frame_args.data_len = PROTOCORE_ZWAVE_MAX_DATA;
    Zwave.build_frame_args.out = out;
    Zwave.build_frame_args.cap = sizeof(out);
    Zwave.build_frame(zwave_work);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(PROTOCORE_ZWAVE_MAX_DATA + 5), Zwave.value);
    Zwave.build_frame_args.type = ZWAVE_REQ;
    Zwave.build_frame_args.cmd = 0x01;
    Zwave.build_frame_args.data = data;
    Zwave.build_frame_args.data_len = 4;
    Zwave.build_frame_args.out = NULL;
    Zwave.build_frame_args.cap = sizeof(out);
    Zwave.build_frame(zwave_work);
    TEST_ASSERT_EQUAL_UINT16(0, Zwave.value);
    Zwave.build_frame_args.type = ZWAVE_REQ;
    Zwave.build_frame_args.cmd = 0x01;
    Zwave.build_frame_args.data = NULL;
    Zwave.build_frame_args.data_len = 4;
    Zwave.build_frame_args.out = out;
    Zwave.build_frame_args.cap = sizeof(out);
    Zwave.build_frame(zwave_work);
    TEST_ASSERT_EQUAL_UINT16(0, Zwave.value);
    Zwave.build_frame_args.type = ZWAVE_REQ;
    Zwave.build_frame_args.cmd = 0x01;
    Zwave.build_frame_args.data = data;
    Zwave.build_frame_args.data_len = 4;
    Zwave.build_frame_args.out = out;
    Zwave.build_frame_args.cap = 8;
    Zwave.build_frame(zwave_work);
    TEST_ASSERT_EQUAL_UINT16(0, Zwave.value); // needs 9
    Zwave.build_frame_args.type = ZWAVE_REQ;
    Zwave.build_frame_args.cmd = 0x01;
    Zwave.build_frame_args.data = data;
    Zwave.build_frame_args.data_len = 4;
    Zwave.build_frame_args.out = out;
    Zwave.build_frame_args.cap = 9;
    Zwave.build_frame(zwave_work);
    TEST_ASSERT_EQUAL_UINT16(9, Zwave.value);
}

// Every out parameter is optional: a caller that only wants to know a frame arrived passes none.
void test_parse_accepts_null_out_parameters(void)
{
    static const uint8_t FRAME[5] = {ZWAVE_SOF, 0x03, 0x00, 0x15, 0xE9};
    Zwave.parse_frame_args.raw = FRAME;
    Zwave.parse_frame_args.len = sizeof(FRAME);
    Zwave.parse_frame_args.type = NULL;
    Zwave.parse_frame_args.cmd = NULL;
    Zwave.parse_frame_args.pdata = NULL;
    Zwave.parse_frame_args.pdata_len = NULL;
    Zwave.parse_frame(zwave_work);
    TEST_ASSERT_EQUAL_INT(5, Zwave.n);
    Zwave.parse_frame_args.raw = NULL;
    Zwave.parse_frame_args.len = 5;
    Zwave.parse_frame_args.type = NULL;
    Zwave.parse_frame_args.cmd = NULL;
    Zwave.parse_frame_args.pdata = NULL;
    Zwave.parse_frame_args.pdata_len = NULL;
    Zwave.parse_frame(zwave_work);
    TEST_ASSERT_EQUAL_INT(0, Zwave.n);
}
