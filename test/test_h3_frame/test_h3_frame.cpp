// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the HTTP/3 framing layer (network_drivers/presentation/http/http3/pc_h3_frame, RFC 9114
// sec 7): the type+length varint header parse/write (incl. a multi-byte length), the DATA /
// HEADERS / SETTINGS / GOAWAY builders, the SETTINGS payload round-trip + reserved-id rejection,
// and the reserved HTTP/2 frame-type check.

#include "network_drivers/presentation/http/http3/h3_frame.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_header_roundtrip()
{
    uint8_t b[8];
    // SETTINGS(4), length 0 -> two 1-byte varints.
    TEST_ASSERT_EQUAL_INT(2, (int)pc_h3_frame_write_header(b, sizeof b, H3FrameType::H3_SETTINGS, 0));
    const uint8_t exp[2] = {0x04, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 2);
    H3Frame f;
    TEST_ASSERT_TRUE(pc_h3_frame_parse(b, 2, &f));
    TEST_ASSERT_TRUE(f.type == H3FrameType::H3_SETTINGS && f.length == 0 && f.header_len == 2);

    // HEADERS(1), length 1000 -> a 2-byte length varint (0x43E8).
    TEST_ASSERT_EQUAL_INT(3, (int)pc_h3_frame_write_header(b, sizeof b, H3FrameType::H3_HEADERS, 1000));
    const uint8_t exp2[3] = {0x01, 0x43, 0xE8};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp2, b, 3);
    TEST_ASSERT_TRUE(pc_h3_frame_parse(b, 3, &f));
    TEST_ASSERT_TRUE(f.type == H3FrameType::H3_HEADERS && f.length == 1000 && f.header_len == 3);
}

void test_build_data_and_goaway()
{
    uint8_t b[32];
    TEST_ASSERT_EQUAL_INT(7, (int)pc_h3_build_data(b, sizeof b, (const uint8_t *)"hello", 5));
    const uint8_t data[7] = {0x00, 0x05, 'h', 'e', 'l', 'l', 'o'};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, b, 7);

    TEST_ASSERT_EQUAL_INT(3, (int)pc_h3_build_goaway(b, sizeof b, 8));
    const uint8_t ga[3] = {0x07, 0x01, 0x08};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ga, b, 3);
}

void test_settings_roundtrip()
{
    const uint64_t ids[2] = {H3Setting::H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY,
                             H3Setting::H3_SETTINGS_MAX_FIELD_SECTION_SIZE};
    const uint64_t vals[2] = {4096, 1048576};
    uint8_t b[32];
    size_t n = pc_h3_build_settings(b, sizeof b, ids, vals, 2);
    // header (type 0x04 + length 0x08) + payload: 01 5000 06 80100000
    const uint8_t exp[10] = {0x04, 0x08, 0x01, 0x50, 0x00, 0x06, 0x80, 0x10, 0x00, 0x00};
    TEST_ASSERT_EQUAL_INT(10, (int)n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 10);

    H3Frame f;
    TEST_ASSERT_TRUE(pc_h3_frame_parse(b, n, &f));
    TEST_ASSERT_TRUE(f.type == H3FrameType::H3_SETTINGS && f.length == 8);
    H3Settings s;
    pc_h3_settings_defaults(&s);
    TEST_ASSERT_TRUE(pc_h3_parse_settings(b + f.header_len, (size_t)f.length, &s));
    TEST_ASSERT_TRUE(s.pc_qpack_max_table_capacity == 4096);
    TEST_ASSERT_TRUE(s.max_field_section_size == 1048576);
    TEST_ASSERT_TRUE(s.pc_qpack_blocked_streams == 0);
}

void test_reserved()
{
    TEST_ASSERT_TRUE(pc_h3_frame_type_reserved(0x02));
    TEST_ASSERT_TRUE(pc_h3_frame_type_reserved(0x06));
    TEST_ASSERT_TRUE(pc_h3_frame_type_reserved(0x08));
    TEST_ASSERT_TRUE(pc_h3_frame_type_reserved(0x09));
    TEST_ASSERT_FALSE(pc_h3_frame_type_reserved(H3FrameType::H3_DATA));
    TEST_ASSERT_FALSE(pc_h3_frame_type_reserved(H3FrameType::H3_HEADERS));
    TEST_ASSERT_FALSE(pc_h3_frame_type_reserved(H3FrameType::H3_SETTINGS));

    // A SETTINGS payload using a reserved HTTP/2 settings id (0x02) must be rejected.
    H3Settings s;
    pc_h3_settings_defaults(&s);
    const uint8_t bad[2] = {0x02, 0x00};
    TEST_ASSERT_FALSE(pc_h3_parse_settings(bad, 2, &s));
}

// HEADERS frame wraps a QPACK field section verbatim.
void test_build_headers()
{
    const uint8_t block[2] = {0xAA, 0xBB};
    uint8_t b[8];
    size_t n = pc_h3_build_headers(b, sizeof b, block, 2);
    const uint8_t exp[4] = {0x01, 0x02, 0xAA, 0xBB};
    TEST_ASSERT_EQUAL_INT(4, (int)n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 4);
    H3Frame f;
    TEST_ASSERT_TRUE(pc_h3_frame_parse(b, n, &f));
    TEST_ASSERT_TRUE(f.type == H3FrameType::H3_HEADERS && f.length == 2);
}

// Every builder returns 0 when the buffer is too small, and the header writer honors cap.
void test_builder_overflow()
{
    uint8_t b[2];
    const uint8_t data[5] = {1, 2, 3, 4, 5};
    const uint64_t ids[1] = {H3Setting::H3_SETTINGS_QPACK_BLOCKED_STREAMS};
    const uint64_t vals[1] = {16};
    TEST_ASSERT_EQUAL_INT(0, (int)pc_h3_frame_write_header(b, 0, H3FrameType::H3_DATA, 0));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_h3_build_data(b, 2, data, 5)); // header fits, payload does not
    TEST_ASSERT_EQUAL_INT(0, (int)pc_h3_build_headers(b, 2, data, 5));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_h3_build_settings(b, 1, ids, vals, 1));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_h3_build_goaway(b, 1, 0x4000)); // stream id needs a 2-byte varint
}

// Truncated frame headers and SETTINGS payloads are rejected.
void test_parse_errors()
{
    H3Frame f;
    TEST_ASSERT_FALSE(pc_h3_frame_parse((const uint8_t *)"", 0, &f)); // no type
    const uint8_t no_len[1] = {0x04};                                 // type ok, no length varint
    TEST_ASSERT_FALSE(pc_h3_frame_parse(no_len, 1, &f));
    const uint8_t trunc_len[1] = {0x40}; // a 2-byte type varint with only 1 byte present
    TEST_ASSERT_FALSE(pc_h3_frame_parse(trunc_len, 1, &f));

    H3Settings s;
    pc_h3_settings_defaults(&s);
    const uint8_t id_no_val[1] = {0x01}; // QPACK_MAX_TABLE_CAPACITY id, no value
    TEST_ASSERT_FALSE(pc_h3_parse_settings(id_no_val, 1, &s));
    // An unknown / GREASE settings id is ignored (not an error).
    const uint8_t grease[2] = {0x21, 0x00};
    TEST_ASSERT_TRUE(pc_h3_parse_settings(grease, 2, &s));
}

// The QPACK_BLOCKED_STREAMS setting id (the third recognized id) round-trips like the other two.
void test_settings_blocked_streams()
{
    const uint64_t ids[1] = {H3Setting::H3_SETTINGS_QPACK_BLOCKED_STREAMS};
    const uint64_t vals[1] = {9};
    uint8_t b[8];
    size_t n = pc_h3_build_settings(b, sizeof b, ids, vals, 1);
    TEST_ASSERT_TRUE(n > 0);

    H3Frame f;
    TEST_ASSERT_TRUE(pc_h3_frame_parse(b, n, &f));
    H3Settings s;
    pc_h3_settings_defaults(&s);
    TEST_ASSERT_TRUE(pc_h3_parse_settings(b + f.header_len, (size_t)f.length, &s));
    TEST_ASSERT_TRUE(s.pc_qpack_blocked_streams == 9);
    // The other two fields stay at their defaults.
    TEST_ASSERT_TRUE(s.pc_qpack_max_table_capacity == 0);
    TEST_ASSERT_TRUE(s.max_field_section_size == 0xFFFFFFFFFFFFFFFFULL);
}

// A malformed id varint (not just a malformed value varint) inside a SETTINGS payload is rejected.
void test_parse_settings_id_decode_fails()
{
    H3Settings s;
    pc_h3_settings_defaults(&s);
    // 0xC0's top 2 bits select an 8-byte varint, but only 1 byte is present.
    const uint8_t bad_id[1] = {0xC0};
    TEST_ASSERT_FALSE(pc_h3_parse_settings(bad_id, 1, &s));
}

// pc_h3_build_data / pc_h3_build_headers: a zero-length payload skips the memcpy, and a cap too
// small even for the header (hn == 0) short-circuits before the length check.
void test_build_data_and_headers_edge_caps()
{
    uint8_t b[8];
    const uint8_t block[1] = {0xAA};

    // len == 0: header is written, memcpy is skipped.
    size_t n = pc_h3_build_data(b, sizeof b, block, 0);
    const uint8_t exp_data[2] = {0x00, 0x00};
    TEST_ASSERT_EQUAL_INT(2, (int)n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_data, b, 2);

    n = pc_h3_build_headers(b, sizeof b, block, 0);
    const uint8_t exp_headers[2] = {0x01, 0x00};
    TEST_ASSERT_EQUAL_INT(2, (int)n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_headers, b, 2);

    // cap == 0: the header write itself fails (hn == 0), short-circuiting the length check.
    TEST_ASSERT_EQUAL_INT(0, (int)pc_h3_build_data(b, 0, block, 0));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_h3_build_headers(b, 0, block, 0));
}

// pc_h3_build_settings: cap can be exactly enough for the header but run out mid-loop, either
// while encoding the id (a == 0) or, one byte later, while encoding the value (b == 0).
void test_build_settings_partial_overflow()
{
    const uint64_t ids[1] = {H3Setting::H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY}; // id 1, val 5: both 1-byte varints
    const uint64_t vals[1] = {5};
    uint8_t b[8];

    // header (type 0x04 + length 0x02) consumes exactly 2 bytes; no room left for the id varint.
    TEST_ASSERT_EQUAL_INT(0, (int)pc_h3_build_settings(b, 2, ids, vals, 1));
    // One more byte fits the id varint but leaves none for the value varint.
    TEST_ASSERT_EQUAL_INT(0, (int)pc_h3_build_settings(b, 3, ids, vals, 1));
}

// pc_h3_build_goaway: cap can be exactly enough for the header but too small for the stream id
// varint itself (a == 0).
void test_build_goaway_partial_overflow()
{
    uint8_t b[8];
    // stream id 64 needs a 2-byte varint; header (type 0x07 + length 0x02) consumes exactly 2
    // bytes, leaving none for it.
    TEST_ASSERT_EQUAL_INT(0, (int)pc_h3_build_goaway(b, 2, 64));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_header_roundtrip);
    RUN_TEST(test_build_data_and_goaway);
    RUN_TEST(test_settings_roundtrip);
    RUN_TEST(test_reserved);
    RUN_TEST(test_build_headers);
    RUN_TEST(test_builder_overflow);
    RUN_TEST(test_parse_errors);
    RUN_TEST(test_settings_blocked_streams);
    RUN_TEST(test_parse_settings_id_decode_fails);
    RUN_TEST(test_build_data_and_headers_edge_caps);
    RUN_TEST(test_build_settings_partial_overflow);
    RUN_TEST(test_build_goaway_partial_overflow);
    return UNITY_END();
}
