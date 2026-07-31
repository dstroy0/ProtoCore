// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the AMQP 0-9-1 frame codec (services/iot/amqp): the protocol header, the frame
// and method builders, the heartbeat, and the frame/method parsers. Pure host tests.

#include "services/iot/amqp/amqp.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_protocol_header()
{
    uint8_t buf[8];
    size_t n = pc_amqp_protocol_header(buf, sizeof(buf));
    const uint8_t expect[] = {'A', 'M', 'Q', 'P', 0, 0, 9, 1};
    TEST_ASSERT_EQUAL_size_t(8, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, 8);
}

// A Connection.Start method (class 10, method 10) on channel 0.
void test_build_method_bytes()
{
    const uint8_t args[] = {0x00};
    uint8_t buf[32];
    size_t n = pc_amqp_build_method(buf, sizeof(buf), 1, 10, 10, args, sizeof(args));
    const uint8_t expect[] = {
        0x01,                   // type METHOD
        0x00, 0x01,             // channel 1
        0x00, 0x00, 0x00, 0x05, // size 5
        0x00, 0x0A, 0x00, 0x0A, // class 10, method 10
        0x00,                   // args
        0xCE                    // frame-end
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_method_round_trip()
{
    const uint8_t args[] = {0x0A, 0x0B, 0x0C};
    uint8_t buf[32];
    size_t n = pc_amqp_build_method(buf, sizeof(buf), 7, 60, 40, args, sizeof(args)); // Basic.Publish-ish

    AmqpFrame f;
    size_t consumed;
    TEST_ASSERT_TRUE(pc_amqp_parse_frame(buf, n, &f, &consumed));
    TEST_ASSERT_EQUAL_HEX8(AMQP_FRAME_METHOD, f.type);
    TEST_ASSERT_EQUAL_UINT16(7, f.channel);
    TEST_ASSERT_EQUAL_size_t(n, consumed);

    uint16_t cls, meth;
    const uint8_t *a;
    size_t alen;
    TEST_ASSERT_TRUE(pc_amqp_parse_method(f.payload, f.payload_len, &cls, &meth, &a, &alen));
    TEST_ASSERT_EQUAL_UINT16(60, cls);
    TEST_ASSERT_EQUAL_UINT16(40, meth);
    TEST_ASSERT_EQUAL_size_t(sizeof(args), alen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(args, a, sizeof(args));
}

void test_heartbeat()
{
    uint8_t buf[16];
    size_t n = pc_amqp_build_heartbeat(buf, sizeof(buf));
    const uint8_t expect[] = {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCE};
    TEST_ASSERT_EQUAL_size_t(8, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    AmqpFrame f;
    size_t consumed;
    TEST_ASSERT_TRUE(pc_amqp_parse_frame(buf, n, &f, &consumed));
    TEST_ASSERT_EQUAL_HEX8(AMQP_FRAME_HEARTBEAT, f.type);
    TEST_ASSERT_EQUAL_size_t(0, f.payload_len);
}

void test_content_header()
{
    uint8_t buf[32];
    // Content header for a Basic (class 60) publish of a 5-octet body, no properties.
    size_t n = pc_amqp_build_content_header(buf, sizeof(buf), 7, 60, 5, 0, nullptr, 0);
    const uint8_t expect[] = {
        0x02, 0x00, 0x07,                               // type HEADER, channel 7
        0x00, 0x00, 0x00, 0x0E,                         // payload size 14
        0x00, 0x3C,                                     // class-id 60 (Basic)
        0x00, 0x00,                                     // weight 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, // body-size 5 (8 octets, big-endian)
        0x00, 0x00,                                     // property flags 0
        0xCE                                            // frame end
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    // It parses back as a HEADER frame whose payload is the 14-octet content header.
    AmqpFrame f;
    size_t consumed;
    TEST_ASSERT_TRUE(pc_amqp_parse_frame(buf, n, &f, &consumed));
    TEST_ASSERT_EQUAL_HEX8(AMQP_FRAME_HEADER, f.type);
    TEST_ASSERT_EQUAL_size_t(14, f.payload_len);
    TEST_ASSERT_EQUAL_size_t(n, consumed);

    // A property list is appended after the flags.
    const uint8_t props[] = {0xAB, 0xCD};
    n = pc_amqp_build_content_header(buf, sizeof(buf), 1, 60, 100, 0x8000, props, sizeof(props));
    TEST_ASSERT_EQUAL_size_t(8 + 14 + 2, n);
    TEST_ASSERT_EQUAL_HEX8(0xAB, buf[n - 3]); // the property octets precede the 0xCE end
    TEST_ASSERT_EQUAL_HEX8(0xCD, buf[n - 2]);

    // Guards: a null properties pointer with a nonzero length, a null buffer, and a too-small buffer.
    TEST_ASSERT_EQUAL_size_t(0, pc_amqp_build_content_header(buf, sizeof(buf), 1, 60, 5, 0x8000, nullptr, 2));
    TEST_ASSERT_EQUAL_size_t(0, pc_amqp_build_content_header(nullptr, sizeof(buf), 1, 60, 5, 0, nullptr, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_amqp_build_content_header(buf, 16, 1, 60, 5, 0, nullptr, 0)); // needs 22
}

// A stream of two frames parses one at a time via the consumed cursor.
void test_parse_stream()
{
    uint8_t buf[64];
    size_t n = pc_amqp_build_method(buf, sizeof(buf), 1, 10, 11, nullptr, 0);
    n += pc_amqp_build_heartbeat(buf + n, sizeof(buf) - n);

    size_t pos = 0;
    AmqpFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_amqp_parse_frame(buf + pos, n - pos, &f, &c));
    TEST_ASSERT_EQUAL_HEX8(AMQP_FRAME_METHOD, f.type);
    pos += c;
    TEST_ASSERT_TRUE(pc_amqp_parse_frame(buf + pos, n - pos, &f, &c));
    TEST_ASSERT_EQUAL_HEX8(AMQP_FRAME_HEARTBEAT, f.type);
    pos += c;
    TEST_ASSERT_EQUAL_size_t(n, pos);
}

void test_parse_rejects_bad()
{
    AmqpFrame f;
    size_t c;
    // A frame whose end octet is not 0xCE.
    const uint8_t bad_end[] = {0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0xAA, 0xFF};
    TEST_ASSERT_FALSE(pc_amqp_parse_frame(bad_end, sizeof(bad_end), &f, &c));
    // Truncated (declares 5 payload octets, buffer too short).
    const uint8_t trunc[] = {0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x00};
    TEST_ASSERT_FALSE(pc_amqp_parse_frame(trunc, sizeof(trunc), &f, &c));
    // A size field of 0xFFFFFFFF must fail closed, not wrap the bounds check (32-bit hardening).
    const uint8_t huge[] = {0x01, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    TEST_ASSERT_FALSE(pc_amqp_parse_frame(huge, sizeof(huge), &f, &c));
    // A method payload shorter than the 4-octet class/method id.
    const uint8_t short_method[] = {0x00, 0x0A};
    uint16_t cls, meth;
    const uint8_t *a;
    size_t alen;
    TEST_ASSERT_FALSE(pc_amqp_parse_method(short_method, sizeof(short_method), &cls, &meth, &a, &alen));
}

void test_build_overflow_fails_closed()
{
    const uint8_t args[] = {1, 2, 3, 4};
    uint8_t small[8]; // needs 8 + 4 + args
    TEST_ASSERT_EQUAL_size_t(0, pc_amqp_build_method(small, sizeof(small), 1, 10, 10, args, sizeof(args)));
    TEST_ASSERT_EQUAL_size_t(0, pc_amqp_protocol_header(small, 4));
}

void test_build_and_parse_guards()
{
    uint8_t buf[64];
    uint8_t payload[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_size_t(0, pc_amqp_build_frame(nullptr, sizeof(buf), 1, 0, payload, sizeof(payload))); // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_amqp_build_frame(buf, sizeof(buf), 1, 0, nullptr, 4));     // null payload
    TEST_ASSERT_EQUAL_size_t(0, pc_amqp_build_frame(buf, 4, 1, 0, payload, sizeof(payload))); // cap < total
    AmqpFrame fr;
    size_t consumed = 0;
    uint8_t tiny[3] = {1, 0, 0};
    TEST_ASSERT_FALSE(pc_amqp_parse_frame(tiny, sizeof(tiny), &fr, &consumed)); // too short
}

// null buf must fail closed (the !buf arm of the guard is otherwise never taken).
void test_protocol_header_null_buf()
{
    TEST_ASSERT_EQUAL_size_t(0, pc_amqp_protocol_header(nullptr, 8));
}

// A frame with real payload bytes (not the empty heartbeat payload) so the memcpy arm of
// pc_amqp_build_frame actually executes, then parses back to confirm the bytes survive.
void test_build_frame_with_payload_round_trip()
{
    const uint8_t payload[] = {0x10, 0x20, 0x30, 0x40, 0x50};
    uint8_t buf[32];
    size_t n = pc_amqp_build_frame(buf, sizeof(buf), AMQP_FRAME_HEADER, 3, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_size_t(AMQP_FRAME_OVERHEAD + sizeof(payload), n);
    TEST_ASSERT_EQUAL_HEX8(AMQP_FRAME_HEADER, buf[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, buf + 7, sizeof(payload));
    TEST_ASSERT_EQUAL_HEX8(AMQP_FRAME_END, buf[n - 1]);

    AmqpFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_amqp_parse_frame(buf, n, &f, &c));
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, sizeof(payload));
}

// A payload_len above the 32-bit size field's range must fail closed. buf/payload are real
// (non-null) so the earlier null checks stay false and this arm is the one that trips; the
// function returns before ever touching payload, so it need not actually be that large.
void test_build_frame_payload_len_overflow_fails_closed()
{
    uint8_t buf[16];
    uint8_t dummy_payload[1] = {0xAB};
    size_t huge_len = (size_t)0xFFFFFFFFu + 1; // > 0xFFFFFFFFu (host size_t is 64-bit)
    TEST_ASSERT_EQUAL_size_t(0, pc_amqp_build_frame(buf, sizeof(buf), 1, 0, dummy_payload, huge_len));
}

// null buf, and args_len set with a null args pointer, must both fail closed.
void test_build_method_guards()
{
    const uint8_t args[] = {1};
    uint8_t buf[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_amqp_build_method(nullptr, sizeof(buf), 1, 10, 10, args, sizeof(args)));
    TEST_ASSERT_EQUAL_size_t(0, pc_amqp_build_method(buf, sizeof(buf), 1, 10, 10, nullptr, 5));
}

// null buf and null out must both fail closed; a null consumed pointer on an otherwise-valid
// frame must still succeed (consumed is optional).
void test_parse_frame_optional_out_params()
{
    uint8_t buf[8] = {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCE};
    AmqpFrame f;
    size_t c;
    TEST_ASSERT_FALSE(pc_amqp_parse_frame(nullptr, sizeof(buf), &f, &c));
    TEST_ASSERT_FALSE(pc_amqp_parse_frame(buf, sizeof(buf), nullptr, &c));
    TEST_ASSERT_TRUE(pc_amqp_parse_frame(buf, sizeof(buf), &f, nullptr));
}

// A null payload must fail closed; all four output pointers are individually optional, so a
// call passing every one as null must still succeed and simply skip the assignments.
void test_parse_method_optional_out_params()
{
    uint16_t cls, meth;
    const uint8_t *a;
    size_t alen;
    TEST_ASSERT_FALSE(pc_amqp_parse_method(nullptr, 10, &cls, &meth, &a, &alen));

    const uint8_t payload[4] = {0x00, 0x0A, 0x00, 0x14};
    TEST_ASSERT_TRUE(pc_amqp_parse_method(payload, sizeof(payload), nullptr, nullptr, nullptr, nullptr));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_protocol_header);
    RUN_TEST(test_build_method_bytes);
    RUN_TEST(test_method_round_trip);
    RUN_TEST(test_heartbeat);
    RUN_TEST(test_content_header);
    RUN_TEST(test_parse_stream);
    RUN_TEST(test_parse_rejects_bad);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_build_and_parse_guards);
    RUN_TEST(test_protocol_header_null_buf);
    RUN_TEST(test_build_frame_with_payload_round_trip);
    RUN_TEST(test_build_frame_payload_len_overflow_fails_closed);
    RUN_TEST(test_build_method_guards);
    RUN_TEST(test_parse_frame_optional_out_params);
    RUN_TEST(test_parse_method_optional_out_params);
    return UNITY_END();
}
