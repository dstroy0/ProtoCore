// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the AMQP 0-9-1 frame codec (services/iot/amqp/amqp.h).
//
// The load-bearing case is test_amqp091_protocol_header. The AMQP Working Group's Protocol
// Specification v0-9-1 sec 4.2.2 writes the eight octets a client opens with as a grammar:
//     protocol-header = literal-AMQP protocol-id protocol-version
//     literal-AMQP    = %d65.77.81.80
//     protocol-id     = %d0
//     protocol-version = %d0.9.1
// A broker that reads anything else answers with its own protocol header and closes, so these eight
// octets are the whole handshake and there is no recovering from getting one of them wrong.
//
// The frame constants and the class / method indices come from the specification's own machine
// readable form (amqp0-9-1.stripped.xml, AMQP Working Group 2009), whose constant table reads
// frame-method 1, frame-header 2, frame-body 3, frame-heartbeat 8 and frame-end 206.

#include "services/iot/amqp/amqp.h"
#include <string.h>

#include <unity.h>

static uint8_t amqp_work[16]; // the borrow an entry takes; Amqp never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// sec 4.2.2: "AMQP" %d0 %d0.9.1, which is 41 4D 51 50 00 00 09 01.
void test_amqp091_protocol_header(void)
{
    static const uint8_t WANT[8] = {0x41, 0x4D, 0x51, 0x50, 0x00, 0x00, 0x09, 0x01};
    uint8_t buf[16];
    AmqpV.out.buf = buf;
    AmqpV.out.cap = sizeof(buf);
    Amqp.protocol_header(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(8u, AmqpV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 8);
    TEST_ASSERT_EQUAL_MEMORY("AMQP", buf, 4); // literal-AMQP is the ASCII of those four octets

    // eight octets exactly; seven is not a protocol header
    AmqpV.out.cap = 8;
    Amqp.protocol_header(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    AmqpV.out.cap = 7;
    Amqp.protocol_header(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, AmqpV.n);

    AmqpV.out.buf = NULL;
    AmqpV.out.cap = 16;
    Amqp.protocol_header(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);
}

// The specification's constant table: frame-method 1, frame-header 2, frame-body 3,
// frame-heartbeat 8, frame-end 206 (%xCE).
void test_amqp091_frame_constants(void)
{
    TEST_ASSERT_EQUAL_UINT(1u, AMQP_FRAME_METHOD);
    TEST_ASSERT_EQUAL_UINT(2u, AMQP_FRAME_HEADER);
    TEST_ASSERT_EQUAL_UINT(3u, AMQP_FRAME_BODY);
    TEST_ASSERT_EQUAL_UINT(8u, AMQP_FRAME_HEARTBEAT);
    TEST_ASSERT_EQUAL_HEX8(0xCE, AMQP_FRAME_END);
    TEST_ASSERT_EQUAL_UINT(206u, AMQP_FRAME_END);
    TEST_ASSERT_EQUAL_UINT(8u, AMQP_FRAME_OVERHEAD); // type(1) + channel(2) + size(4) + frame-end(1)
}

// sec 4.2.3: type(1) channel(2) size(4) payload(size) frame-end(1), with sec 4.2.5.1 putting every
// integer in network byte order, and the size field counting the payload alone.
void test_amqp091_frame_layout(void)
{
    static const uint8_t PAYLOAD[] = {0xAA, 0xBB, 0xCC};
    static const uint8_t WANT[] = {AMQP_FRAME_BODY, 0x01, 0x02, 0x00, 0x00, 0x00, 0x03, 0xAA, 0xBB, 0xCC,
                                   AMQP_FRAME_END};
    uint8_t buf[32];

    AmqpV.out.buf = buf;
    AmqpV.out.cap = sizeof(buf);
    AmqpV.frame.type = AMQP_FRAME_BODY;
    AmqpV.frame.channel = 0x0102;
    AmqpV.payload.data = PAYLOAD;
    AmqpV.payload.len = sizeof(PAYLOAD);
    Amqp.build_frame(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), AmqpV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));

    // an empty payload frames to the eight octets of overhead alone
    AmqpV.payload.data = NULL;
    AmqpV.payload.len = 0;
    AmqpV.frame.channel = 0;
    Amqp.build_frame(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(8u, AmqpV.n);
    static const uint8_t EMPTY[8] = {AMQP_FRAME_BODY, 0, 0, 0, 0, 0, 0, AMQP_FRAME_END};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(EMPTY, buf, 8);
}

// sec 4.2.3 requires a peer to check the frame-end before decoding the frame, so a frame whose
// final octet is not %xCE is refused rather than half-decoded.
void test_frame_end_is_checked_before_decoding(void)
{
    static const uint8_t PAYLOAD[] = {1, 2, 3, 4};
    uint8_t buf[32];

    AmqpV.out.buf = buf;
    AmqpV.out.cap = sizeof(buf);
    AmqpV.frame.type = AMQP_FRAME_BODY;
    AmqpV.frame.channel = 1;
    AmqpV.payload.data = PAYLOAD;
    AmqpV.payload.len = sizeof(PAYLOAD);
    Amqp.build_frame(amqp_work);
    size_t n = AmqpV.n;

    AmqpV.in.buf = buf;
    AmqpV.in.len = n;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);

    for (int v = 0; v < 256; v++)
    {
        if (v == AMQP_FRAME_END)
        {
            continue;
        }
        buf[n - 1] = (uint8_t)v;
        AmqpV.in.buf = buf;
        AmqpV.in.len = n;
        Amqp.parse_frame(amqp_work);
        TEST_ASSERT_FALSE(AmqpV.ok);
        TEST_ASSERT_EQUAL_UINT(0u, AmqpV.consumed);
    }
}

// A frame survives build then parse with its type, channel and payload intact, and consumed spans
// the whole frame so the next one starts where it says.
void test_frame_round_trip_and_consumed(void)
{
    static const uint8_t PAYLOAD[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[64];

    AmqpV.out.buf = buf;
    AmqpV.out.cap = sizeof(buf);
    AmqpV.frame.type = AMQP_FRAME_BODY;
    AmqpV.frame.channel = 65535; // the widest channel the 2-octet field can name
    AmqpV.payload.data = PAYLOAD;
    AmqpV.payload.len = sizeof(PAYLOAD);
    Amqp.build_frame(amqp_work);
    size_t first = AmqpV.n;

    // a second frame right behind it, so the consumed count has something to be wrong about
    AmqpV.out.buf = buf + first;
    AmqpV.out.cap = sizeof(buf) - first;
    Amqp.build_heartbeat(amqp_work);
    size_t total = first + AmqpV.n;

    AmqpV.in.buf = buf;
    AmqpV.in.len = total;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(AMQP_FRAME_BODY, AmqpV.frame.type);
    TEST_ASSERT_EQUAL_UINT16(65535u, AmqpV.frame.channel);
    TEST_ASSERT_EQUAL_UINT(sizeof(PAYLOAD), AmqpV.payload.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, AmqpV.payload.data, sizeof(PAYLOAD));
    TEST_ASSERT_EQUAL_UINT(first, AmqpV.consumed);

    AmqpV.in.buf = buf + AmqpV.consumed;
    AmqpV.in.len = total - AmqpV.consumed;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(AMQP_FRAME_HEARTBEAT, AmqpV.frame.type);
    TEST_ASSERT_EQUAL_UINT(0u, AmqpV.payload.len);
}

// A frame that is not fully buffered is a need-more, not a failure to be retried differently: every
// prefix short of the whole frame must report false without consuming anything.
void test_partial_frame_is_not_parsed(void)
{
    static const uint8_t PAYLOAD[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t buf[32];
    AmqpV.out.buf = buf;
    AmqpV.out.cap = sizeof(buf);
    AmqpV.frame.type = AMQP_FRAME_BODY;
    AmqpV.frame.channel = 1;
    AmqpV.payload.data = PAYLOAD;
    AmqpV.payload.len = sizeof(PAYLOAD);
    Amqp.build_frame(amqp_work);
    size_t n = AmqpV.n;

    for (size_t shorter = 0; shorter < n; shorter++)
    {
        AmqpV.in.buf = buf;
        AmqpV.in.len = shorter;
        Amqp.parse_frame(amqp_work);
        TEST_ASSERT_FALSE(AmqpV.ok);
        TEST_ASSERT_EQUAL_UINT(0u, AmqpV.consumed);
    }
    AmqpV.in.len = n;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);

    AmqpV.in.buf = NULL;
    AmqpV.in.len = n;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);
}

// sec 4.2.4: a method payload is class-id(2) method-id(2) then the arguments. The pair below is
// connection.start, which the specification's class table indexes at class 10, method 10 - the
// first method a broker ever sends.
void test_amqp091_method_frame_layout(void)
{
    static const uint8_t ARGS[] = {0x00, 0x09}; // version-major 0, version-minor 9
    static const uint8_t WANT[] = {
        AMQP_FRAME_METHOD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0A, 0x00, 0x0A, 0x00, 0x09, AMQP_FRAME_END};
    uint8_t buf[32];

    AmqpV.out.buf = buf;
    AmqpV.out.cap = sizeof(buf);
    AmqpV.frame.channel = 0; // connection class methods are global to the connection
    AmqpV.method.class_id = 10;
    AmqpV.method.method_id = 10;
    AmqpV.method.args = ARGS;
    AmqpV.method.args_len = sizeof(ARGS);
    Amqp.build_method(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), AmqpV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));
}

// parse_frame hands its payload straight to parse_method, and the class / method indices come back
// as the specification's table assigns them.
void test_method_round_trip_over_the_class_table(void)
{
    static const struct
    {
        uint16_t class_id;
        uint16_t method_id;
        uint16_t channel;
    } CASES[] = {
        {10, 10, 0}, // connection.start
        {10, 11, 0}, // connection.start-ok
        {10, 40, 0}, // connection.open
        {10, 50, 0}, // connection.close
        {20, 10, 1}, // channel.open
        {40, 10, 1}, // exchange.declare
        {50, 10, 1}, // queue.declare
        {50, 20, 1}, // queue.bind
        {60, 40, 1}, // basic.publish
        {60, 60, 1}, // basic.deliver
        {90, 10, 1}, // tx.select
    };
    static const uint8_t ARGS[] = {0x11, 0x22, 0x33};
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t buf[32];
        AmqpV.out.buf = buf;
        AmqpV.out.cap = sizeof(buf);
        AmqpV.frame.channel = CASES[i].channel;
        AmqpV.method.class_id = CASES[i].class_id;
        AmqpV.method.method_id = CASES[i].method_id;
        AmqpV.method.args = ARGS;
        AmqpV.method.args_len = sizeof(ARGS);
        Amqp.build_method(amqp_work);
        TEST_ASSERT_TRUE(AmqpV.ok);
        size_t n = AmqpV.n;

        // scrub the handle so a value that survives is one the parse actually wrote
        AmqpV.method.class_id = 0;
        AmqpV.method.method_id = 0;
        AmqpV.method.args = NULL;
        AmqpV.method.args_len = 0;

        AmqpV.in.buf = buf;
        AmqpV.in.len = n;
        Amqp.parse_frame(amqp_work);
        TEST_ASSERT_TRUE(AmqpV.ok);
        TEST_ASSERT_EQUAL_UINT(AMQP_FRAME_METHOD, AmqpV.frame.type);
        TEST_ASSERT_EQUAL_UINT16(CASES[i].channel, AmqpV.frame.channel);

        Amqp.parse_method(amqp_work);
        TEST_ASSERT_TRUE(AmqpV.ok);
        TEST_ASSERT_EQUAL_UINT16(CASES[i].class_id, AmqpV.method.class_id);
        TEST_ASSERT_EQUAL_UINT16(CASES[i].method_id, AmqpV.method.method_id);
        TEST_ASSERT_EQUAL_UINT(sizeof(ARGS), AmqpV.method.args_len);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(ARGS, AmqpV.method.args, sizeof(ARGS));
    }
}

// A method payload is at least the two indices, so a payload shorter than four octets is not one.
void test_method_payload_shorter_than_its_indices(void)
{
    static const uint8_t SHORT[] = {0, 10, 0};
    AmqpV.payload.data = SHORT;
    AmqpV.payload.len = sizeof(SHORT);
    Amqp.parse_method(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);

    // exactly four octets is a method with no arguments
    static const uint8_t BARE[] = {0, 10, 0, 51}; // connection.close-ok
    AmqpV.payload.data = BARE;
    AmqpV.payload.len = sizeof(BARE);
    Amqp.parse_method(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT16(10u, AmqpV.method.class_id);
    TEST_ASSERT_EQUAL_UINT16(51u, AmqpV.method.method_id);
    TEST_ASSERT_EQUAL_UINT(0u, AmqpV.method.args_len);

    AmqpV.payload.data = NULL;
    AmqpV.payload.len = 4;
    Amqp.parse_method(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);
}

// sec 4.2.6.1: a content header payload is class-id(2) weight(2) body-size(8) property-flags(2)
// then the property list. The weight field is unused and zero, and body-size is the sum of the body
// frames that follow. Class 60 is basic, and property flag bit 15 marks its first property,
// content-type.
void test_amqp091_content_header_layout(void)
{
    static const uint8_t PROPS[] = {0x0A, 't', 'e', 'x', 't', '/', 'p', 'l', 'a', 'i', 'n'};
    uint8_t buf[64];

    AmqpV.out.buf = buf;
    AmqpV.out.cap = sizeof(buf);
    AmqpV.frame.channel = 1;
    AmqpV.content.class_id = 60;
    AmqpV.content.body_size = 0x0102030405060708ull;
    AmqpV.content.property_flags = 0x8000;
    AmqpV.content.property_list = PROPS;
    AmqpV.content.property_list_len = sizeof(PROPS);
    Amqp.build_content_header(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(8u + 14u + sizeof(PROPS), AmqpV.n);

    TEST_ASSERT_EQUAL_HEX8(AMQP_FRAME_HEADER, buf[0]);
    static const uint8_t HDR[7] = {AMQP_FRAME_HEADER, 0x00, 0x01, 0x00, 0x00, 0x00, 0x19}; // size 25
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HDR, buf, 7);

    static const uint8_t PAYLOAD_HEAD[14] = {0x00, 0x3C,                                     // class-id 60
                                             0x00, 0x00,                                     // weight, unused
                                             0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // body-size
                                             0x80, 0x00};                                    // property-flags
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD_HEAD, buf + 7, 14);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PROPS, buf + 21, sizeof(PROPS));
    TEST_ASSERT_EQUAL_HEX8(AMQP_FRAME_END, buf[AmqpV.n - 1]);

    // and the whole thing parses back as one frame
    AmqpV.in.buf = buf;
    AmqpV.in.len = AmqpV.n;
    size_t built = AmqpV.n;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(AMQP_FRAME_HEADER, AmqpV.frame.type);
    TEST_ASSERT_EQUAL_UINT(built, AmqpV.consumed);
    TEST_ASSERT_EQUAL_UINT(built - AMQP_FRAME_OVERHEAD, AmqpV.payload.len);
}

// sec 4.2.7 / the sec 4.2.1 grammar `heartbeat = %d8 %d0 %d0 frame-end`: type 8, channel 0, empty
// payload. It is eight octets and it always looks the same.
void test_amqp091_heartbeat(void)
{
    static const uint8_t WANT[8] = {8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, AMQP_FRAME_END};
    uint8_t buf[16];

    AmqpV.out.buf = buf;
    AmqpV.out.cap = sizeof(buf);
    AmqpV.frame.channel = 1234; // ignored: a heartbeat is always channel 0
    AmqpV.payload.data = WANT;
    AmqpV.payload.len = 8;
    Amqp.build_heartbeat(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(8u, AmqpV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 8);

    AmqpV.in.buf = buf;
    AmqpV.in.len = 8;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(AMQP_FRAME_HEARTBEAT, AmqpV.frame.type);
    TEST_ASSERT_EQUAL_UINT16(0u, AmqpV.frame.channel);
    TEST_ASSERT_EQUAL_UINT(0u, AmqpV.payload.len);

    AmqpV.out.cap = 7;
    Amqp.build_heartbeat(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);
}

// Every build refuses a buffer one octet short of the frame it would write, and refuses a payload
// length with no payload behind it. A truncated frame has no frame-end, which is exactly the state
// sec 4.2.3 tells a peer to treat as fatal.
void test_builds_refuse_a_short_buffer(void)
{
    static const uint8_t P[] = {1, 2, 3, 4};
    uint8_t buf[64];

    AmqpV.out.buf = buf;
    AmqpV.frame.type = AMQP_FRAME_BODY;
    AmqpV.frame.channel = 1;
    AmqpV.payload.data = P;
    AmqpV.payload.len = sizeof(P);
    AmqpV.out.cap = 12;
    Amqp.build_frame(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(12u, AmqpV.n);
    AmqpV.out.cap = 11;
    Amqp.build_frame(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, AmqpV.n);

    AmqpV.out.cap = sizeof(buf);
    AmqpV.payload.data = NULL;
    AmqpV.payload.len = 4;
    Amqp.build_frame(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);

    AmqpV.out.buf = NULL;
    AmqpV.payload.data = P;
    Amqp.build_frame(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);

    AmqpV.out.buf = buf;
    AmqpV.method.args = P;
    AmqpV.method.args_len = sizeof(P);
    AmqpV.out.cap = 16; // 8 overhead + 4 indices + 4 args
    Amqp.build_method(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    AmqpV.out.cap = 15;
    Amqp.build_method(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);
    AmqpV.out.cap = sizeof(buf);
    AmqpV.method.args = NULL;
    Amqp.build_method(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);

    AmqpV.content.property_list = P;
    AmqpV.content.property_list_len = sizeof(P);
    AmqpV.out.cap = 26; // 8 overhead + 14 fixed fields + 4 list
    Amqp.build_content_header(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    AmqpV.out.cap = 25;
    Amqp.build_content_header(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);
    AmqpV.out.cap = sizeof(buf);
    AmqpV.content.property_list = NULL;
    Amqp.build_content_header(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);
}

// A size field naming more octets than are buffered is a need-more and never a read past the end,
// including the largest value the 4-octet field can hold.
void test_an_oversized_size_field_is_refused(void)
{
    uint8_t frame[16];
    frame[0] = AMQP_FRAME_BODY;
    frame[1] = 0;
    frame[2] = 1;
    frame[3] = 0xFF;
    frame[4] = 0xFF;
    frame[5] = 0xFF;
    frame[6] = 0xFF; // size 4294967295
    frame[7] = AMQP_FRAME_END;

    AmqpV.in.buf = frame;
    AmqpV.in.len = sizeof(frame);
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, AmqpV.consumed);

    // one octet more than is present is refused just the same
    frame[3] = 0;
    frame[4] = 0;
    frame[5] = 0;
    frame[6] = 9; // 8 overhead + 9 payload needs 17 octets
    AmqpV.in.len = sizeof(frame);
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_FALSE(AmqpV.ok);

    frame[6] = 8; // 8 overhead + 8 payload is exactly the buffer
    frame[15] = AMQP_FRAME_END;
    AmqpV.in.len = sizeof(frame);
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(AmqpV.ok);
    TEST_ASSERT_EQUAL_UINT(16u, AmqpV.consumed);
}
