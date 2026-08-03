// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the gRPC-Web message framing codec (services/iot/grpcweb): the message and
// trailer frame builders and the frame parser. Pure host tests.

#include "services/iot/grpcweb/grpcweb.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// A message frame is [flags=0][len BE32][body].
void test_frame_message_bytes()
{
    const uint8_t msg[] = {0x08, 0x96, 0x01}; // a protobuf message (field 1 = 150)
    uint8_t buf[16];
    size_t n = pc_grpcweb_frame_message(buf, sizeof(buf), msg, sizeof(msg), PROTO_FALSE);
    TEST_ASSERT_EQUAL_size_t(GRPCWEB_PREFIX_LEN + sizeof(msg), n);
    const uint8_t expect[] = {0x00, 0x00, 0x00, 0x00, 0x03, 0x08, 0x96, 0x01};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_compressed_flag()
{
    const uint8_t msg[] = {0xFF};
    uint8_t buf[16];
    size_t n = pc_grpcweb_frame_message(buf, sizeof(buf), msg, sizeof(msg), PROTO_TRUE);
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_HEX8(GRPCWEB_FLAG_COMPRESSED, buf[0]);
}

void test_trailer_frame()
{
    uint8_t buf[64];
    size_t n = pc_grpcweb_frame_trailer(buf, sizeof(buf), 0, "OK");
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_EQUAL_HEX8(GRPCWEB_FLAG_TRAILER, buf[0]);
    // body = "grpc-status:0\r\ngrpc-message:OK\r\n" (32 octets)
    const char *body = "grpc-status:0\r\ngrpc-message:OK\r\n";
    size_t blen = strlen(body);
    TEST_ASSERT_EQUAL_size_t(GRPCWEB_PREFIX_LEN + blen, n);
    uint32_t len = ((uint32_t)buf[1] << 24) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 8) | buf[4];
    TEST_ASSERT_EQUAL_UINT32(blen, len);
    TEST_ASSERT_EQUAL_MEMORY(body, buf + GRPCWEB_PREFIX_LEN, blen);
}

void test_trailer_message()
{
    uint8_t buf[64];
    // A non-zero status (13 = INTERNAL) carrying a human-readable message.
    size_t n = pc_grpcweb_frame_trailer(buf, sizeof(buf), 13, "boom");
    GrpcWebFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_grpcweb_parse(buf, n, &f, &c));
    TEST_ASSERT_TRUE(f.trailer);

    int status;
    TEST_ASSERT_TRUE(pc_grpcweb_trailer_status(f.body, f.body_len, &status));
    TEST_ASSERT_EQUAL_INT(13, status);

    const char *msg = NULL;
    size_t mlen = 0;
    TEST_ASSERT_TRUE(pc_grpcweb_trailer_message(f.body, f.body_len, &msg, &mlen));
    TEST_ASSERT_EQUAL_size_t(4, mlen);
    TEST_ASSERT_EQUAL_MEMORY("boom", msg, 4);

    // A status-only trailer has no grpc-message line.
    n = pc_grpcweb_frame_trailer(buf, sizeof(buf), 5, NULL);
    TEST_ASSERT_TRUE(pc_grpcweb_parse(buf, n, &f, &c));
    TEST_ASSERT_FALSE(pc_grpcweb_trailer_message(f.body, f.body_len, &msg, &mlen));

    // A null body is rejected.
    TEST_ASSERT_FALSE(pc_grpcweb_trailer_message(NULL, 10, &msg, &mlen));
}

void test_trailer_status_only()
{
    uint8_t buf[64];
    size_t n = pc_grpcweb_frame_trailer(buf, sizeof(buf), 5, NULL); // no message
    GrpcWebFrame f;
    size_t c;
    TEST_ASSERT_TRUE(pc_grpcweb_parse(buf, n, &f, &c));
    TEST_ASSERT_TRUE(f.trailer);
    int status;
    TEST_ASSERT_TRUE(pc_grpcweb_trailer_status(f.body, f.body_len, &status));
    TEST_ASSERT_EQUAL_INT(5, status);
}

// Two concatenated frames (a message then trailers) parse in sequence.
void test_parse_stream()
{
    const uint8_t msg[] = {0x12, 0x02, 'h', 'i'};
    uint8_t buf[128];
    size_t n = pc_grpcweb_frame_message(buf, sizeof(buf), msg, sizeof(msg), PROTO_FALSE);
    n += pc_grpcweb_frame_trailer(buf + n, sizeof(buf) - n, 0, "OK");

    size_t pos = 0;
    GrpcWebFrame f;
    size_t c;
    // frame 1: the message
    TEST_ASSERT_TRUE(pc_grpcweb_parse(buf + pos, n - pos, &f, &c));
    TEST_ASSERT_FALSE(f.trailer);
    TEST_ASSERT_FALSE(f.compressed);
    TEST_ASSERT_EQUAL_size_t(sizeof(msg), f.body_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, f.body, sizeof(msg));
    pos += c;
    // frame 2: the trailers
    TEST_ASSERT_TRUE(pc_grpcweb_parse(buf + pos, n - pos, &f, &c));
    TEST_ASSERT_TRUE(f.trailer);
    int status = -1;
    TEST_ASSERT_TRUE(pc_grpcweb_trailer_status(f.body, f.body_len, &status));
    TEST_ASSERT_EQUAL_INT(0, status);
    pos += c;
    TEST_ASSERT_EQUAL_size_t(n, pos); // consumed exactly the whole stream
}

void test_parse_incomplete()
{
    const uint8_t msg[] = {0xAA, 0xBB, 0xCC};
    uint8_t buf[16];
    size_t n = pc_grpcweb_frame_message(buf, sizeof(buf), msg, sizeof(msg), PROTO_FALSE);
    GrpcWebFrame f;
    size_t c;
    TEST_ASSERT_FALSE(pc_grpcweb_parse(buf, GRPCWEB_PREFIX_LEN - 1, &f, &c)); // prefix incomplete
    TEST_ASSERT_FALSE(pc_grpcweb_parse(buf, n - 1, &f, &c));                  // body short by one
}

void test_frame_overflow_fails_closed()
{
    const uint8_t msg[] = {1, 2, 3, 4};
    uint8_t small[6]; // 5 prefix + 4 body needs 9
    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame_message(small, sizeof(small), msg, sizeof(msg), PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame_trailer(small, 3, 0, "x")); // smaller than the prefix
}

// Frame builder null guards and every trailer-builder overflow/reject: the status-key
// put, a negative status, the status digits, and the grpc-message line.
void test_frame_and_trailer_guards()
{
    uint8_t buf[64];
    const uint8_t msg[] = {1, 2, 3};
    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame(NULL, sizeof(buf), 0, msg, 3)); // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame(buf, sizeof(buf), 0, NULL, 3)); // body_len but null body

    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame_trailer(buf, 8, 0, NULL));            // status key overflows
    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame_trailer(buf, sizeof(buf), -1, NULL)); // negative status
    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame_trailer(buf, 17, 5, NULL));           // status digits overflow
    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame_trailer(buf, 24, 0, "msg"));          // grpc-message line overflows
}

// The status extractor rejects a null body, a key not followed by a digit, and a body
// with no grpc-status line at all.
void test_trailer_status_parse_paths()
{
    int status = -1;
    TEST_ASSERT_FALSE(pc_grpcweb_trailer_status(NULL, 10, &status)); // null body
    const char *nondigit = "grpc-status:X";
    TEST_ASSERT_FALSE(pc_grpcweb_trailer_status((const uint8_t *)nondigit, strlen(nondigit), &status));
    const char *nokey = "foo:1\r\n";
    TEST_ASSERT_FALSE(pc_grpcweb_trailer_status((const uint8_t *)nokey, strlen(nokey), &status));
}

// A zero-length body: pc_grpcweb_frame must skip the (never-taken) memcpy and still emit a
// bare 5-octet prefix; also exercises the body_len==0 short-circuit in the overflow guard.
void test_frame_zero_length_body()
{
    uint8_t buf[16];
    size_t n = pc_grpcweb_frame_message(buf, sizeof(buf), NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_size_t(GRPCWEB_PREFIX_LEN, n);
    const uint8_t expect[GRPCWEB_PREFIX_LEN] = {0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, GRPCWEB_PREFIX_LEN);
}

// A body_len above what a 32-bit length prefix can express is rejected before any buffer
// access (the pointer is non-null but never dereferenced).
void test_frame_body_len_too_large()
{
    uint8_t buf[16];
    uint8_t dummy[1] = {0};
    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame(buf, sizeof(buf), 0, dummy, (size_t)0x100000000ULL));
}

// pc_grpcweb_frame_trailer null-buf guard, plus the "\r\n" put failing right after the
// status digits (distinct from the earlier key/digit overflow cases).
void test_trailer_frame_more_guards()
{
    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame_trailer(NULL, 64, 0, NULL)); // null buf
    uint8_t buf[64];
    // "grpc-status:" (12) + "0" (1) fits in 19, but the trailing "\r\n" (2 more) does not.
    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame_trailer(buf, 19, 0, NULL));
}

// An empty (non-null) message string takes the same no-message path as a null message.
void test_trailer_empty_message()
{
    uint8_t buf[64];
    size_t n = pc_grpcweb_frame_trailer(buf, sizeof(buf), 0, "");
    const char *body = "grpc-status:0\r\n";
    size_t blen = strlen(body);
    TEST_ASSERT_EQUAL_size_t(GRPCWEB_PREFIX_LEN + blen, n);
    TEST_ASSERT_EQUAL_MEMORY(body, buf + GRPCWEB_PREFIX_LEN, blen);
}

// Message-block overflow paths distinct from the "grpc-message:" key overflow already
// covered: the message body itself overflowing, and the trailing "\r\n" after it overflowing.
void test_trailer_message_body_and_crlf_overflow()
{
    uint8_t buf[64];
    // After "grpc-status:0\r\n" (15) the prefix is at 20; "grpc-message:" (13) fits exactly
    // at cap==33, leaving no room for the 1-octet message body.
    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame_trailer(buf, 33, 0, "X"));
    // With a 2-octet message, cap==35 fits the key and the body but not the trailing "\r\n".
    TEST_ASSERT_EQUAL_size_t(0, pc_grpcweb_frame_trailer(buf, 35, 0, "hi"));
}

// pc_grpcweb_parse null-pointer guards for the out-param and consumed-length arguments
// (the null-buf and short-length guards are already covered elsewhere).
void test_parse_null_guards()
{
    uint8_t buf[16] = {0};
    GrpcWebFrame f;
    size_t c;
    TEST_ASSERT_FALSE(pc_grpcweb_parse(NULL, sizeof(buf), &f, &c));  // null buf
    TEST_ASSERT_FALSE(pc_grpcweb_parse(buf, sizeof(buf), NULL, &c)); // null out
    TEST_ASSERT_FALSE(pc_grpcweb_parse(buf, sizeof(buf), &f, NULL)); // null consumed
}

// grpc-status only matches at the start of a line: a leading non-matching line forces the
// scan past i==0, and the real key is found later, preceded by '\n'.
void test_trailer_status_multiline()
{
    const char *body = "foo:1\r\ngrpc-status:9\r\n";
    int status = -1;
    TEST_ASSERT_TRUE(pc_grpcweb_trailer_status((const uint8_t *)body, strlen(body), &status));
    TEST_ASSERT_EQUAL_INT(9, status);
}

// Boundary cases in the digit check right after "grpc-status:": nothing follows the key at
// all, and a character below '0' follows it (as opposed to the already-covered above-'9' case).
void test_trailer_status_digit_bounds()
{
    int status = -1;
    const char *no_digit = "grpc-status:"; // key ends exactly at the buffer end
    TEST_ASSERT_FALSE(pc_grpcweb_trailer_status((const uint8_t *)no_digit, strlen(no_digit), &status));
    const char *low_char = "grpc-status:-1"; // '-' is below '0'
    TEST_ASSERT_FALSE(pc_grpcweb_trailer_status((const uint8_t *)low_char, strlen(low_char), &status));
}

// The digit-accumulation loop can stop for two different reasons: running out of buffer
// (no trailing delimiter at all) or hitting a non-digit above '9' (as opposed to the
// already-covered below-'0' terminator).
void test_trailer_status_digit_loop_bounds()
{
    int status = -1;
    const char *no_trailer = "grpc-status:7"; // digits run right up to the end of the buffer
    TEST_ASSERT_TRUE(pc_grpcweb_trailer_status((const uint8_t *)no_trailer, strlen(no_trailer), &status));
    TEST_ASSERT_EQUAL_INT(7, status);
    status = -1;
    const char *high_char = "grpc-status:5:"; // ':' is above '9'
    TEST_ASSERT_TRUE(pc_grpcweb_trailer_status((const uint8_t *)high_char, strlen(high_char), &status));
    TEST_ASSERT_EQUAL_INT(5, status);
}

// A null status out-param is a valid "just validate" call: parsing must still succeed.
void test_trailer_status_null_output()
{
    const char *body = "grpc-status:3";
    TEST_ASSERT_TRUE(pc_grpcweb_trailer_status((const uint8_t *)body, strlen(body), NULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_message_bytes);
    RUN_TEST(test_compressed_flag);
    RUN_TEST(test_trailer_frame);
    RUN_TEST(test_trailer_message);
    RUN_TEST(test_trailer_status_only);
    RUN_TEST(test_parse_stream);
    RUN_TEST(test_parse_incomplete);
    RUN_TEST(test_frame_overflow_fails_closed);
    RUN_TEST(test_frame_and_trailer_guards);
    RUN_TEST(test_trailer_status_parse_paths);
    RUN_TEST(test_frame_zero_length_body);
    RUN_TEST(test_frame_body_len_too_large);
    RUN_TEST(test_trailer_frame_more_guards);
    RUN_TEST(test_trailer_empty_message);
    RUN_TEST(test_trailer_message_body_and_crlf_overflow);
    RUN_TEST(test_parse_null_guards);
    RUN_TEST(test_trailer_status_multiline);
    RUN_TEST(test_trailer_status_digit_bounds);
    RUN_TEST(test_trailer_status_digit_loop_bounds);
    RUN_TEST(test_trailer_status_null_output);
    return UNITY_END();
}
