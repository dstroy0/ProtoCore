// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Zigbee EZSP / ASH framing codec (services/radio/zigbee/zigbee.h).
//
// test_ug101_rst_frame is the load-bearing case: the RST frame is the first thing a host ever sends
// an EmberZNet NCP, Silicon Labs UG101 prints it as C0 38 BC 7E, and the CRC half of that constant
// is derived here from the CRC-16/IBM-3740 definition step by step rather than from this codec's
// output. test_crc16_catalog_check_value pins the same CRC to the check value the CRC catalogue
// publishes, so a wrong polynomial, init or bit order fails before any framing is examined.

#include "services/radio/zigbee/zigbee.h"
#include <string.h>

#include <unity.h>

static uint8_t zigbee_work[16]; // the borrow an entry takes; Zigbee never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// The nine ASCII octets every CRC catalogue entry publishes its check value over.
static const uint8_t CHECK9[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

// CRC-16/IBM-3740 (poly 0x1021, init 0xFFFF, unreflected, no final XOR): check value 0x29B1.
void test_crc16_catalog_check_value(void)
{
    ZigbeeV.ash_crc16_args.buf = CHECK9;
    ZigbeeV.ash_crc16_args.len = sizeof(CHECK9);
    Zigbee.ash_crc16(zigbee_work);
    TEST_ASSERT_EQUAL_HEX16(0x29B1u, ZigbeeV.value);
}

// UG101: the RST frame carries control byte 0xC0 and no payload, so it goes out as C0 38 BC 7E.
//
// The CRC over the single octet 0xC0, from the definition (init 0xFFFF, XOR the octet into the high
// half, then eight shift-and-conditional-XOR-0x1021 steps):
//   0xFFFF ^ 0xC000                = 0x3FFF
//   <<1                            = 0x7FFE
//   <<1                            = 0xFFFC
//   <<1 = 0xFFF8 ^ 0x1021          = 0xEFD9
//   <<1 = 0xDFB2 ^ 0x1021          = 0xCF93
//   <<1 = 0x9F26 ^ 0x1021          = 0x8F07
//   <<1 = 0x1E0E ^ 0x1021          = 0x0E2F
//   <<1                            = 0x1C5E
//   <<1                            = 0x38BC
// and the CRC is sent most significant byte first: 38 BC.
void test_ug101_rst_frame(void)
{
    static const uint8_t WANT[4] = {ASH_RST, 0x38, 0xBC, ASH_FLAG};
    ZigbeeV.ash_crc16_args.buf = WANT;
    ZigbeeV.ash_crc16_args.len = 1;
    Zigbee.ash_crc16(zigbee_work);
    TEST_ASSERT_EQUAL_HEX16(0x38BCu, ZigbeeV.value);

    uint8_t out[16];
    ZigbeeV.ash_frame_encode_args.control = ASH_RST;
    ZigbeeV.ash_frame_encode_args.payload = NULL;
    ZigbeeV.ash_frame_encode_args.len = 0;
    ZigbeeV.ash_frame_encode_args.out = out;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(out);
    Zigbee.ash_frame_encode(zigbee_work);
    TEST_ASSERT_EQUAL_UINT16(4, ZigbeeV.value);
    TEST_ASSERT_EQUAL_MEMORY(WANT, out, 4);
}

// Append one octet with ASH stuffing, so a hand-built frame can be laid out octet by octet.
static void stuff(uint8_t *out, uint16_t *p, uint8_t b)
{
    if (b == 0x7E || b == 0x7D || b == 0x11 || b == 0x13 || b == 0x18 || b == 0x1A)
    {
        out[(*p)++] = ASH_ESCAPE;
        out[(*p)++] = (uint8_t)(b ^ 0x20);
        return;
    }
    out[(*p)++] = b;
}

// The CRC covers the control byte then the payload, and rides most significant octet first. A frame
// hand-built that way decodes; the same frame with the two CRC octets swapped does not.
void test_crc_covers_control_then_payload_msb_first(void)
{
    static const uint8_t JOINED[4] = {0x42, 0x01, 0x02, 0x03};
    ZigbeeV.ash_crc16_args.buf = JOINED;
    ZigbeeV.ash_crc16_args.len = sizeof(JOINED);
    Zigbee.ash_crc16(zigbee_work);
    const uint16_t crc = ZigbeeV.value;
    TEST_ASSERT_TRUE((uint8_t)(crc >> 8) != (uint8_t)(crc & 0xFF)); // the swap has to be observable

    uint8_t good[32];
    uint16_t g = 0;
    for (size_t i = 0; i < sizeof(JOINED); i++)
    {
        stuff(good, &g, JOINED[i]);
    }
    stuff(good, &g, (uint8_t)(crc >> 8));
    stuff(good, &g, (uint8_t)(crc & 0xFF));
    good[g++] = ASH_FLAG;

    uint8_t control = 0;
    uint8_t back[32];
    uint16_t back_len = 0;
    ZigbeeV.ash_frame_decode_args.raw = good;
    ZigbeeV.ash_frame_decode_args.len = g;
    ZigbeeV.ash_frame_decode_args.control = &control;
    ZigbeeV.ash_frame_decode_args.payload = back;
    ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(back);
    ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
    Zigbee.ash_frame_decode(zigbee_work);
    TEST_ASSERT_EQUAL_INT((int)g, ZigbeeV.n);
    TEST_ASSERT_EQUAL_HEX8(0x42, control);
    TEST_ASSERT_EQUAL_UINT16(3, back_len);
    TEST_ASSERT_EQUAL_MEMORY(JOINED + 1, back, 3);

    uint8_t swapped[32];
    uint16_t s = 0;
    for (size_t i = 0; i < sizeof(JOINED); i++)
    {
        stuff(swapped, &s, JOINED[i]);
    }
    stuff(swapped, &s, (uint8_t)(crc & 0xFF));
    stuff(swapped, &s, (uint8_t)(crc >> 8));
    swapped[s++] = ASH_FLAG;
    ZigbeeV.ash_frame_decode_args.raw = swapped;
    ZigbeeV.ash_frame_decode_args.len = s;
    ZigbeeV.ash_frame_decode_args.control = &control;
    ZigbeeV.ash_frame_decode_args.payload = back;
    ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(back);
    ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
    Zigbee.ash_frame_decode(zigbee_work);
    TEST_ASSERT_EQUAL_INT(-1, ZigbeeV.n);

    // And the encoder lays out the same octets the hand-built frame does.
    uint8_t out[32];
    ZigbeeV.ash_frame_encode_args.control = 0x42;
    ZigbeeV.ash_frame_encode_args.payload = JOINED + 1;
    ZigbeeV.ash_frame_encode_args.len = 3;
    ZigbeeV.ash_frame_encode_args.out = out;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(out);
    Zigbee.ash_frame_encode(zigbee_work);
    TEST_ASSERT_EQUAL_UINT16(g, ZigbeeV.value);
    TEST_ASSERT_EQUAL_MEMORY(good, out, g);
}

// UG101 reserves six octets, each escaped as 0x7D followed by the octet XOR 0x20:
//   7E -> 7D 5E   7D -> 7D 5D   11 -> 7D 31   13 -> 7D 33   18 -> 7D 38   1A -> 7D 3A
void test_reserved_octets_are_escaped(void)
{
    static const uint8_t RESERVED[6] = {0x7E, 0x7D, 0x11, 0x13, 0x18, 0x1A};
    static const uint8_t WANT[12] = {0x7D, 0x5E, 0x7D, 0x5D, 0x7D, 0x31, 0x7D, 0x33, 0x7D, 0x38, 0x7D, 0x3A};

    uint8_t out[48];
    ZigbeeV.ash_frame_encode_args.control = 0x00;
    ZigbeeV.ash_frame_encode_args.payload = RESERVED;
    ZigbeeV.ash_frame_encode_args.len = sizeof(RESERVED);
    ZigbeeV.ash_frame_encode_args.out = out;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(out);
    Zigbee.ash_frame_encode(zigbee_work);
    const uint16_t n = ZigbeeV.value;
    TEST_ASSERT_TRUE(n > 13);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]); // the control byte, not reserved
    TEST_ASSERT_EQUAL_MEMORY(WANT, out + 1, 12);
    TEST_ASSERT_EQUAL_HEX8(ASH_FLAG, out[n - 1]);
    for (uint16_t i = 0; i + 1 < n; i++)
    {
        TEST_ASSERT_TRUE(out[i] != ASH_FLAG); // the delimiter appears exactly once, at the end
    }
}

// Encode then decode returns the control byte and the payload unchanged.
void test_frame_round_trip(void)
{
    static const uint8_t PAYLOAD[8] = {0x7E, 0x00, 0x7D, 0x11, 0x13, 0x18, 0x1A, 0xFF};
    uint8_t frame[64];
    ZigbeeV.ash_frame_encode_args.control = 0x25;
    ZigbeeV.ash_frame_encode_args.payload = PAYLOAD;
    ZigbeeV.ash_frame_encode_args.len = sizeof(PAYLOAD);
    ZigbeeV.ash_frame_encode_args.out = frame;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(frame);
    Zigbee.ash_frame_encode(zigbee_work);
    const uint16_t n = ZigbeeV.value;
    TEST_ASSERT_TRUE(n > 0);

    uint8_t control = 0;
    uint8_t back[64];
    uint16_t back_len = 0;
    ZigbeeV.ash_frame_decode_args.raw = frame;
    ZigbeeV.ash_frame_decode_args.len = n;
    ZigbeeV.ash_frame_decode_args.control = &control;
    ZigbeeV.ash_frame_decode_args.payload = back;
    ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(back);
    ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
    Zigbee.ash_frame_decode(zigbee_work);
    TEST_ASSERT_EQUAL_INT((int)n, ZigbeeV.n);
    TEST_ASSERT_EQUAL_HEX8(0x25, control);
    TEST_ASSERT_EQUAL_UINT16(sizeof(PAYLOAD), back_len);
    TEST_ASSERT_EQUAL_MEMORY(PAYLOAD, back, sizeof(PAYLOAD));
}

// A zero-length payload is a whole frame: control + CRC + flag, and it decodes back to zero bytes.
void test_empty_payload_round_trip(void)
{
    uint8_t frame[16];
    ZigbeeV.ash_frame_encode_args.control = ASH_RSTACK;
    ZigbeeV.ash_frame_encode_args.payload = NULL;
    ZigbeeV.ash_frame_encode_args.len = 0;
    ZigbeeV.ash_frame_encode_args.out = frame;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(frame);
    Zigbee.ash_frame_encode(zigbee_work);
    const uint16_t n = ZigbeeV.value;
    TEST_ASSERT_EQUAL_UINT16(4, n);

    uint8_t control = 0;
    uint8_t back[16];
    uint16_t back_len = 0xFFFF;
    ZigbeeV.ash_frame_decode_args.raw = frame;
    ZigbeeV.ash_frame_decode_args.len = n;
    ZigbeeV.ash_frame_decode_args.control = &control;
    ZigbeeV.ash_frame_decode_args.payload = back;
    ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(back);
    ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
    Zigbee.ash_frame_decode(zigbee_work);
    TEST_ASSERT_EQUAL_INT(4, ZigbeeV.n);
    TEST_ASSERT_EQUAL_HEX8(ASH_RSTACK, control);
    TEST_ASSERT_EQUAL_UINT16(0, back_len);
}

// One flipped bit anywhere in the frame body must fail the CRC.
void test_decode_rejects_a_corrupted_frame(void)
{
    static const uint8_t PAYLOAD[5] = {0x00, 0x01, 0x02, 0x03, 0x04};
    uint8_t frame[32];
    ZigbeeV.ash_frame_encode_args.control = 0x35;
    ZigbeeV.ash_frame_encode_args.payload = PAYLOAD;
    ZigbeeV.ash_frame_encode_args.len = sizeof(PAYLOAD);
    ZigbeeV.ash_frame_encode_args.out = frame;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(frame);
    Zigbee.ash_frame_encode(zigbee_work);
    const uint16_t n = ZigbeeV.value;
    TEST_ASSERT_TRUE(n > 0);

    uint8_t control = 0;
    uint8_t back[32];
    uint16_t back_len = 0;
    for (uint16_t i = 0; i + 1 < n; i++)
    {
        uint8_t bad[32];
        memcpy(bad, frame, n);
        bad[i] = (uint8_t)(bad[i] ^ 0x01);
        if (bad[i] == ASH_FLAG || bad[i] == ASH_ESCAPE)
        {
            continue; // a flip onto a delimiter changes the framing, not the CRC
        }
        ZigbeeV.ash_frame_decode_args.raw = bad;
        ZigbeeV.ash_frame_decode_args.len = n;
        ZigbeeV.ash_frame_decode_args.control = &control;
        ZigbeeV.ash_frame_decode_args.payload = back;
        ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(back);
        ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
        Zigbee.ash_frame_decode(zigbee_work);
        TEST_ASSERT_EQUAL_INT(-1, ZigbeeV.n);
    }
}

// Framing faults are told apart from "need more bytes": no flag yet is 0, a broken frame is -1.
void test_decode_framing_faults(void)
{
    uint8_t control = 0;
    uint8_t back[64];
    uint16_t back_len = 0;

    static const uint8_t NO_FLAG[4] = {0xC0, 0x38, 0xBC, 0x00};
    ZigbeeV.ash_frame_decode_args.raw = NO_FLAG;
    ZigbeeV.ash_frame_decode_args.len = sizeof(NO_FLAG);
    ZigbeeV.ash_frame_decode_args.control = &control;
    ZigbeeV.ash_frame_decode_args.payload = back;
    ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(back);
    ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
    Zigbee.ash_frame_decode(zigbee_work);
    TEST_ASSERT_EQUAL_INT(0, ZigbeeV.n);
    ZigbeeV.ash_frame_decode_args.raw = NO_FLAG;
    ZigbeeV.ash_frame_decode_args.len = 0;
    ZigbeeV.ash_frame_decode_args.control = &control;
    ZigbeeV.ash_frame_decode_args.payload = back;
    ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(back);
    ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
    Zigbee.ash_frame_decode(zigbee_work);
    TEST_ASSERT_EQUAL_INT(0, ZigbeeV.n);

    static const uint8_t DANGLING[3] = {0xC0, ASH_ESCAPE, ASH_FLAG};
    ZigbeeV.ash_frame_decode_args.raw = DANGLING;
    ZigbeeV.ash_frame_decode_args.len = sizeof(DANGLING);
    ZigbeeV.ash_frame_decode_args.control = &control;
    ZigbeeV.ash_frame_decode_args.payload = back;
    ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(back);
    ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
    Zigbee.ash_frame_decode(zigbee_work);
    TEST_ASSERT_EQUAL_INT(-1, ZigbeeV.n);

    // Two octets cannot hold a control byte plus a two-octet CRC.
    static const uint8_t TOO_SHORT[3] = {0xC0, 0x38, ASH_FLAG};
    ZigbeeV.ash_frame_decode_args.raw = TOO_SHORT;
    ZigbeeV.ash_frame_decode_args.len = sizeof(TOO_SHORT);
    ZigbeeV.ash_frame_decode_args.control = &control;
    ZigbeeV.ash_frame_decode_args.payload = back;
    ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(back);
    ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
    Zigbee.ash_frame_decode(zigbee_work);
    TEST_ASSERT_EQUAL_INT(-1, ZigbeeV.n);
}

// A payload larger than the caller's buffer is refused, not truncated into it.
void test_decode_refuses_a_short_payload_buffer(void)
{
    static const uint8_t PAYLOAD[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t frame[32];
    ZigbeeV.ash_frame_encode_args.control = 0x01;
    ZigbeeV.ash_frame_encode_args.payload = PAYLOAD;
    ZigbeeV.ash_frame_encode_args.len = sizeof(PAYLOAD);
    ZigbeeV.ash_frame_encode_args.out = frame;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(frame);
    Zigbee.ash_frame_encode(zigbee_work);
    const uint16_t n = ZigbeeV.value;

    uint8_t control = 0;
    uint8_t small[4];
    uint16_t back_len = 0;
    ZigbeeV.ash_frame_decode_args.raw = frame;
    ZigbeeV.ash_frame_decode_args.len = n;
    ZigbeeV.ash_frame_decode_args.control = &control;
    ZigbeeV.ash_frame_decode_args.payload = small;
    ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(small);
    ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
    Zigbee.ash_frame_decode(zigbee_work);
    TEST_ASSERT_EQUAL_INT(-1, ZigbeeV.n);
}

// The decoder consumes exactly up to and including the first flag, leaving the next frame in place.
void test_decode_consumes_one_frame_from_a_stream(void)
{
    static const uint8_t A[2] = {0xAA, 0xBB};
    static const uint8_t B[3] = {0x01, 0x02, 0x03};
    uint8_t stream[64];
    ZigbeeV.ash_frame_encode_args.control = 0x10;
    ZigbeeV.ash_frame_encode_args.payload = A;
    ZigbeeV.ash_frame_encode_args.len = sizeof(A);
    ZigbeeV.ash_frame_encode_args.out = stream;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(stream);
    Zigbee.ash_frame_encode(zigbee_work);
    const uint16_t na = ZigbeeV.value;
    ZigbeeV.ash_frame_encode_args.control = 0x20;
    ZigbeeV.ash_frame_encode_args.payload = B;
    ZigbeeV.ash_frame_encode_args.len = sizeof(B);
    ZigbeeV.ash_frame_encode_args.out = stream + na;
    ZigbeeV.ash_frame_encode_args.cap = (uint16_t)(sizeof(stream) - na);
    Zigbee.ash_frame_encode(zigbee_work);
    const uint16_t nb = ZigbeeV.value;
    TEST_ASSERT_TRUE(na > 0 && nb > 0);

    uint8_t control = 0;
    uint8_t back[32];
    uint16_t back_len = 0;
    ZigbeeV.ash_frame_decode_args.raw = stream;
    ZigbeeV.ash_frame_decode_args.len = (uint16_t)(na + nb);
    ZigbeeV.ash_frame_decode_args.control = &control;
    ZigbeeV.ash_frame_decode_args.payload = back;
    ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(back);
    ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
    Zigbee.ash_frame_decode(zigbee_work);
    TEST_ASSERT_EQUAL_INT((int)na, ZigbeeV.n);
    TEST_ASSERT_EQUAL_HEX8(0x10, control);
    TEST_ASSERT_EQUAL_MEMORY(A, back, sizeof(A));

    ZigbeeV.ash_frame_decode_args.raw = stream + na;
    ZigbeeV.ash_frame_decode_args.len = nb;
    ZigbeeV.ash_frame_decode_args.control = &control;
    ZigbeeV.ash_frame_decode_args.payload = back;
    ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(back);
    ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
    Zigbee.ash_frame_decode(zigbee_work);
    TEST_ASSERT_EQUAL_INT((int)nb, ZigbeeV.n);
    TEST_ASSERT_EQUAL_HEX8(0x20, control);
    TEST_ASSERT_EQUAL_MEMORY(B, back, sizeof(B));
}

// Encode refuses a payload past the configured maximum, a null payload with a length, and a buffer
// one octet short of the exact frame.
void test_encode_bounds(void)
{
    uint8_t big[PROTOCORE_ZIGBEE_MAX_DATA + 1];
    memset(big, 0x41, sizeof(big));
    uint8_t out[PROTOCORE_ZIGBEE_MAX_DATA * 2 + 8];
    ZigbeeV.ash_frame_encode_args.control = 0x00;
    ZigbeeV.ash_frame_encode_args.payload = big;
    ZigbeeV.ash_frame_encode_args.len = sizeof(big);
    ZigbeeV.ash_frame_encode_args.out = out;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(out);
    Zigbee.ash_frame_encode(zigbee_work);
    TEST_ASSERT_EQUAL_UINT16(0, ZigbeeV.value);
    ZigbeeV.ash_frame_encode_args.control = 0x00;
    ZigbeeV.ash_frame_encode_args.payload = NULL;
    ZigbeeV.ash_frame_encode_args.len = 4;
    ZigbeeV.ash_frame_encode_args.out = out;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(out);
    Zigbee.ash_frame_encode(zigbee_work);
    TEST_ASSERT_EQUAL_UINT16(0, ZigbeeV.value);
    ZigbeeV.ash_frame_encode_args.control = 0x00;
    ZigbeeV.ash_frame_encode_args.payload = big;
    ZigbeeV.ash_frame_encode_args.len = 4;
    ZigbeeV.ash_frame_encode_args.out = NULL;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(out);
    Zigbee.ash_frame_encode(zigbee_work);
    TEST_ASSERT_EQUAL_UINT16(0, ZigbeeV.value);

    static const uint8_t SMALL[4] = {1, 2, 3, 4};
    ZigbeeV.ash_frame_encode_args.control = 0x00;
    ZigbeeV.ash_frame_encode_args.payload = SMALL;
    ZigbeeV.ash_frame_encode_args.len = sizeof(SMALL);
    ZigbeeV.ash_frame_encode_args.out = out;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(out);
    Zigbee.ash_frame_encode(zigbee_work);
    const uint16_t exact = ZigbeeV.value;
    TEST_ASSERT_TRUE(exact >= 8); // control + 4 payload + CRC(2) + flag
    ZigbeeV.ash_frame_encode_args.control = 0x00;
    ZigbeeV.ash_frame_encode_args.payload = SMALL;
    ZigbeeV.ash_frame_encode_args.len = sizeof(SMALL);
    ZigbeeV.ash_frame_encode_args.out = out;
    ZigbeeV.ash_frame_encode_args.cap = (uint16_t)(exact - 1);
    Zigbee.ash_frame_encode(zigbee_work);
    TEST_ASSERT_EQUAL_UINT16(0, ZigbeeV.value);
    ZigbeeV.ash_frame_encode_args.control = 0x00;
    ZigbeeV.ash_frame_encode_args.payload = SMALL;
    ZigbeeV.ash_frame_encode_args.len = sizeof(SMALL);
    ZigbeeV.ash_frame_encode_args.out = out;
    ZigbeeV.ash_frame_encode_args.cap = exact;
    Zigbee.ash_frame_encode(zigbee_work);
    TEST_ASSERT_EQUAL_UINT16(exact, ZigbeeV.value);
}

// A null raw pointer is reported as "nothing to frame yet", never dereferenced.
void test_decode_null_input(void)
{
    uint8_t back[8];
    uint16_t back_len = 0;
    ZigbeeV.ash_frame_decode_args.raw = NULL;
    ZigbeeV.ash_frame_decode_args.len = 8;
    ZigbeeV.ash_frame_decode_args.control = NULL;
    ZigbeeV.ash_frame_decode_args.payload = back;
    ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(back);
    ZigbeeV.ash_frame_decode_args.pay_len = &back_len;
    Zigbee.ash_frame_decode(zigbee_work);
    TEST_ASSERT_EQUAL_INT(0, ZigbeeV.n);
}
