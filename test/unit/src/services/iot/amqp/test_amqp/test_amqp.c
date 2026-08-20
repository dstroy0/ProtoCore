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
    Amqp.out.buf = buf;
    Amqp.out.cap = sizeof(buf);
    Amqp.protocol_header(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(8u, Amqp.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 8);
    TEST_ASSERT_EQUAL_MEMORY("AMQP", buf, 4); // literal-AMQP is the ASCII of those four octets

    // eight octets exactly; seven is not a protocol header
    Amqp.out.cap = 8;
    Amqp.protocol_header(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    Amqp.out.cap = 7;
    Amqp.protocol_header(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(0u, Amqp.n);

    Amqp.out.buf = NULL;
    Amqp.out.cap = 16;
    Amqp.protocol_header(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);
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

    Amqp.out.buf = buf;
    Amqp.out.cap = sizeof(buf);
    Amqp.frame.type = AMQP_FRAME_BODY;
    Amqp.frame.channel = 0x0102;
    Amqp.payload.data = PAYLOAD;
    Amqp.payload.len = sizeof(PAYLOAD);
    Amqp.build_frame(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Amqp.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));

    // an empty payload frames to the eight octets of overhead alone
    Amqp.payload.data = NULL;
    Amqp.payload.len = 0;
    Amqp.frame.channel = 0;
    Amqp.build_frame(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(8u, Amqp.n);
    static const uint8_t EMPTY[8] = {AMQP_FRAME_BODY, 0, 0, 0, 0, 0, 0, AMQP_FRAME_END};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(EMPTY, buf, 8);
}

// sec 4.2.3 requires a peer to check the frame-end before decoding the frame, so a frame whose
// final octet is not %xCE is refused rather than half-decoded.
void test_frame_end_is_checked_before_decoding(void)
{
    static const uint8_t PAYLOAD[] = {1, 2, 3, 4};
    uint8_t buf[32];

    Amqp.out.buf = buf;
    Amqp.out.cap = sizeof(buf);
    Amqp.frame.type = AMQP_FRAME_BODY;
    Amqp.frame.channel = 1;
    Amqp.payload.data = PAYLOAD;
    Amqp.payload.len = sizeof(PAYLOAD);
    Amqp.build_frame(amqp_work);
    size_t n = Amqp.n;

    Amqp.in.buf = buf;
    Amqp.in.len = n;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);

    for (int v = 0; v < 256; v++)
    {
        if (v == AMQP_FRAME_END)
        {
            continue;
        }
        buf[n - 1] = (uint8_t)v;
        Amqp.in.buf = buf;
        Amqp.in.len = n;
        Amqp.parse_frame(amqp_work);
        TEST_ASSERT_FALSE(Amqp.ok);
        TEST_ASSERT_EQUAL_UINT(0u, Amqp.consumed);
    }
}

// A frame survives build then parse with its type, channel and payload intact, and consumed spans
// the whole frame so the next one starts where it says.
void test_frame_round_trip_and_consumed(void)
{
    static const uint8_t PAYLOAD[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[64];

    Amqp.out.buf = buf;
    Amqp.out.cap = sizeof(buf);
    Amqp.frame.type = AMQP_FRAME_BODY;
    Amqp.frame.channel = 65535; // the widest channel the 2-octet field can name
    Amqp.payload.data = PAYLOAD;
    Amqp.payload.len = sizeof(PAYLOAD);
    Amqp.build_frame(amqp_work);
    size_t first = Amqp.n;

    // a second frame right behind it, so the consumed count has something to be wrong about
    Amqp.out.buf = buf + first;
    Amqp.out.cap = sizeof(buf) - first;
    Amqp.build_heartbeat(amqp_work);
    size_t total = first + Amqp.n;

    Amqp.in.buf = buf;
    Amqp.in.len = total;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(AMQP_FRAME_BODY, Amqp.frame.type);
    TEST_ASSERT_EQUAL_UINT16(65535u, Amqp.frame.channel);
    TEST_ASSERT_EQUAL_UINT(sizeof(PAYLOAD), Amqp.payload.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, Amqp.payload.data, sizeof(PAYLOAD));
    TEST_ASSERT_EQUAL_UINT(first, Amqp.consumed);

    Amqp.in.buf = buf + Amqp.consumed;
    Amqp.in.len = total - Amqp.consumed;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(AMQP_FRAME_HEARTBEAT, Amqp.frame.type);
    TEST_ASSERT_EQUAL_UINT(0u, Amqp.payload.len);
}

// A frame that is not fully buffered is a need-more, not a failure to be retried differently: every
// prefix short of the whole frame must report false without consuming anything.
void test_partial_frame_is_not_parsed(void)
{
    static const uint8_t PAYLOAD[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t buf[32];
    Amqp.out.buf = buf;
    Amqp.out.cap = sizeof(buf);
    Amqp.frame.type = AMQP_FRAME_BODY;
    Amqp.frame.channel = 1;
    Amqp.payload.data = PAYLOAD;
    Amqp.payload.len = sizeof(PAYLOAD);
    Amqp.build_frame(amqp_work);
    size_t n = Amqp.n;

    for (size_t shorter = 0; shorter < n; shorter++)
    {
        Amqp.in.buf = buf;
        Amqp.in.len = shorter;
        Amqp.parse_frame(amqp_work);
        TEST_ASSERT_FALSE(Amqp.ok);
        TEST_ASSERT_EQUAL_UINT(0u, Amqp.consumed);
    }
    Amqp.in.len = n;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);

    Amqp.in.buf = NULL;
    Amqp.in.len = n;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);
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

    Amqp.out.buf = buf;
    Amqp.out.cap = sizeof(buf);
    Amqp.frame.channel = 0; // connection class methods are global to the connection
    Amqp.method.class_id = 10;
    Amqp.method.method_id = 10;
    Amqp.method.args = ARGS;
    Amqp.method.args_len = sizeof(ARGS);
    Amqp.build_method(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Amqp.n);
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
        Amqp.out.buf = buf;
        Amqp.out.cap = sizeof(buf);
        Amqp.frame.channel = CASES[i].channel;
        Amqp.method.class_id = CASES[i].class_id;
        Amqp.method.method_id = CASES[i].method_id;
        Amqp.method.args = ARGS;
        Amqp.method.args_len = sizeof(ARGS);
        Amqp.build_method(amqp_work);
        TEST_ASSERT_TRUE(Amqp.ok);
        size_t n = Amqp.n;

        // scrub the handle so a value that survives is one the parse actually wrote
        Amqp.method.class_id = 0;
        Amqp.method.method_id = 0;
        Amqp.method.args = NULL;
        Amqp.method.args_len = 0;

        Amqp.in.buf = buf;
        Amqp.in.len = n;
        Amqp.parse_frame(amqp_work);
        TEST_ASSERT_TRUE(Amqp.ok);
        TEST_ASSERT_EQUAL_UINT(AMQP_FRAME_METHOD, Amqp.frame.type);
        TEST_ASSERT_EQUAL_UINT16(CASES[i].channel, Amqp.frame.channel);

        Amqp.parse_method(amqp_work);
        TEST_ASSERT_TRUE(Amqp.ok);
        TEST_ASSERT_EQUAL_UINT16(CASES[i].class_id, Amqp.method.class_id);
        TEST_ASSERT_EQUAL_UINT16(CASES[i].method_id, Amqp.method.method_id);
        TEST_ASSERT_EQUAL_UINT(sizeof(ARGS), Amqp.method.args_len);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(ARGS, Amqp.method.args, sizeof(ARGS));
    }
}

// A method payload is at least the two indices, so a payload shorter than four octets is not one.
void test_method_payload_shorter_than_its_indices(void)
{
    static const uint8_t SHORT[] = {0, 10, 0};
    Amqp.payload.data = SHORT;
    Amqp.payload.len = sizeof(SHORT);
    Amqp.parse_method(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);

    // exactly four octets is a method with no arguments
    static const uint8_t BARE[] = {0, 10, 0, 51}; // connection.close-ok
    Amqp.payload.data = BARE;
    Amqp.payload.len = sizeof(BARE);
    Amqp.parse_method(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT16(10u, Amqp.method.class_id);
    TEST_ASSERT_EQUAL_UINT16(51u, Amqp.method.method_id);
    TEST_ASSERT_EQUAL_UINT(0u, Amqp.method.args_len);

    Amqp.payload.data = NULL;
    Amqp.payload.len = 4;
    Amqp.parse_method(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);
}

// sec 4.2.6.1: a content header payload is class-id(2) weight(2) body-size(8) property-flags(2)
// then the property list. The weight field is unused and zero, and body-size is the sum of the body
// frames that follow. Class 60 is basic, and property flag bit 15 marks its first property,
// content-type.
void test_amqp091_content_header_layout(void)
{
    static const uint8_t PROPS[] = {0x0A, 't', 'e', 'x', 't', '/', 'p', 'l', 'a', 'i', 'n'};
    uint8_t buf[64];

    Amqp.out.buf = buf;
    Amqp.out.cap = sizeof(buf);
    Amqp.frame.channel = 1;
    Amqp.content.class_id = 60;
    Amqp.content.body_size = 0x0102030405060708ull;
    Amqp.content.property_flags = 0x8000;
    Amqp.content.property_list = PROPS;
    Amqp.content.property_list_len = sizeof(PROPS);
    Amqp.build_content_header(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(8u + 14u + sizeof(PROPS), Amqp.n);

    TEST_ASSERT_EQUAL_HEX8(AMQP_FRAME_HEADER, buf[0]);
    static const uint8_t HDR[7] = {AMQP_FRAME_HEADER, 0x00, 0x01, 0x00, 0x00, 0x00, 0x19}; // size 25
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HDR, buf, 7);

    static const uint8_t PAYLOAD_HEAD[14] = {0x00, 0x3C,                                     // class-id 60
                                             0x00, 0x00,                                     // weight, unused
                                             0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // body-size
                                             0x80, 0x00};                                    // property-flags
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD_HEAD, buf + 7, 14);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PROPS, buf + 21, sizeof(PROPS));
    TEST_ASSERT_EQUAL_HEX8(AMQP_FRAME_END, buf[Amqp.n - 1]);

    // and the whole thing parses back as one frame
    Amqp.in.buf = buf;
    Amqp.in.len = Amqp.n;
    size_t built = Amqp.n;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(AMQP_FRAME_HEADER, Amqp.frame.type);
    TEST_ASSERT_EQUAL_UINT(built, Amqp.consumed);
    TEST_ASSERT_EQUAL_UINT(built - AMQP_FRAME_OVERHEAD, Amqp.payload.len);
}

// sec 4.2.7 / the sec 4.2.1 grammar `heartbeat = %d8 %d0 %d0 frame-end`: type 8, channel 0, empty
// payload. It is eight octets and it always looks the same.
void test_amqp091_heartbeat(void)
{
    static const uint8_t WANT[8] = {8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, AMQP_FRAME_END};
    uint8_t buf[16];

    Amqp.out.buf = buf;
    Amqp.out.cap = sizeof(buf);
    Amqp.frame.channel = 1234; // ignored: a heartbeat is always channel 0
    Amqp.payload.data = WANT;
    Amqp.payload.len = 8;
    Amqp.build_heartbeat(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(8u, Amqp.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 8);

    Amqp.in.buf = buf;
    Amqp.in.len = 8;
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(AMQP_FRAME_HEARTBEAT, Amqp.frame.type);
    TEST_ASSERT_EQUAL_UINT16(0u, Amqp.frame.channel);
    TEST_ASSERT_EQUAL_UINT(0u, Amqp.payload.len);

    Amqp.out.cap = 7;
    Amqp.build_heartbeat(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);
}

// Every build refuses a buffer one octet short of the frame it would write, and refuses a payload
// length with no payload behind it. A truncated frame has no frame-end, which is exactly the state
// sec 4.2.3 tells a peer to treat as fatal.
void test_builds_refuse_a_short_buffer(void)
{
    static const uint8_t P[] = {1, 2, 3, 4};
    uint8_t buf[64];

    Amqp.out.buf = buf;
    Amqp.frame.type = AMQP_FRAME_BODY;
    Amqp.frame.channel = 1;
    Amqp.payload.data = P;
    Amqp.payload.len = sizeof(P);
    Amqp.out.cap = 12;
    Amqp.build_frame(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(12u, Amqp.n);
    Amqp.out.cap = 11;
    Amqp.build_frame(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(0u, Amqp.n);

    Amqp.out.cap = sizeof(buf);
    Amqp.payload.data = NULL;
    Amqp.payload.len = 4;
    Amqp.build_frame(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);

    Amqp.out.buf = NULL;
    Amqp.payload.data = P;
    Amqp.build_frame(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);

    Amqp.out.buf = buf;
    Amqp.method.args = P;
    Amqp.method.args_len = sizeof(P);
    Amqp.out.cap = 16; // 8 overhead + 4 indices + 4 args
    Amqp.build_method(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    Amqp.out.cap = 15;
    Amqp.build_method(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);
    Amqp.out.cap = sizeof(buf);
    Amqp.method.args = NULL;
    Amqp.build_method(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);

    Amqp.content.property_list = P;
    Amqp.content.property_list_len = sizeof(P);
    Amqp.out.cap = 26; // 8 overhead + 14 fixed fields + 4 list
    Amqp.build_content_header(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    Amqp.out.cap = 25;
    Amqp.build_content_header(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);
    Amqp.out.cap = sizeof(buf);
    Amqp.content.property_list = NULL;
    Amqp.build_content_header(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);
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

    Amqp.in.buf = frame;
    Amqp.in.len = sizeof(frame);
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(0u, Amqp.consumed);

    // one octet more than is present is refused just the same
    frame[3] = 0;
    frame[4] = 0;
    frame[5] = 0;
    frame[6] = 9; // 8 overhead + 9 payload needs 17 octets
    Amqp.in.len = sizeof(frame);
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_FALSE(Amqp.ok);

    frame[6] = 8; // 8 overhead + 8 payload is exactly the buffer
    frame[15] = AMQP_FRAME_END;
    Amqp.in.len = sizeof(frame);
    Amqp.parse_frame(amqp_work);
    TEST_ASSERT_TRUE(Amqp.ok);
    TEST_ASSERT_EQUAL_UINT(16u, Amqp.consumed);
}
