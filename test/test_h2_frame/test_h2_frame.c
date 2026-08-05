// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the HTTP/2 frame layer (network_drivers/presentation/http/http2/pc_h2_frame, RFC 9113):
// the 9-byte frame header parse/write (24-bit length, reserved-bit masking), SETTINGS build +
// parse (with validation), and the exact bytes of the SETTINGS-ACK / WINDOW_UPDATE / RST_STREAM
// / GOAWAY / PING-ACK / HEADERS / DATA builders.

#include "network_drivers/presentation/http/http2/h2_frame.h"
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
    uint8_t b[16];
    TEST_ASSERT_EQUAL_INT(9, (int)pc_h2_write_header(b, sizeof b, 0x123456, H2_HEADERS, 0x05, 0x7FFFFFFF));
    const uint8_t exp[9] = {0x12, 0x34, 0x56, 0x01, 0x05, 0x7F, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 9);
    H2FrameHeader h;
    TEST_ASSERT_TRUE(pc_h2_parse_header(b, 9, &h));
    TEST_ASSERT_EQUAL_UINT32(0x123456, h.length);
    TEST_ASSERT_EQUAL_UINT8(H2_HEADERS, h.type);
    TEST_ASSERT_EQUAL_UINT8(0x05, h.flags);
    TEST_ASSERT_EQUAL_UINT32(0x7FFFFFFF, h.stream_id);

    // The reserved high bit of the stream id must be ignored on parse.
    uint8_t r[9] = {0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_TRUE(pc_h2_parse_header(r, 9, &h));
    TEST_ASSERT_EQUAL_UINT32(0x7FFFFFFF, h.stream_id);

    // Length larger than 24 bits is refused.
    TEST_ASSERT_EQUAL_INT(0, (int)pc_h2_write_header(b, sizeof b, 0x1000000, H2_DATA, 0, 1));

    // A capacity smaller than the 9-byte header is refused directly (not just via a caller's guard).
    TEST_ASSERT_EQUAL_INT(0, (int)pc_h2_write_header(b, 8, 0, H2_DATA, 0, 1));
}

void test_settings_build_parse()
{
    const uint16_t ids[2] = {H2_SETTINGS_HEADER_TABLE_SIZE, H2_SETTINGS_INITIAL_WINDOW_SIZE};
    const uint32_t vals[2] = {4096, 65535};
    uint8_t f[64];
    size_t n = pc_h2_build_settings(f, sizeof f, ids, vals, 2);
    TEST_ASSERT_EQUAL_INT(9 + 12, (int)n);
    const uint8_t exp[21] = {0x00, 0x00, 0x0c, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                             0x00, 0x00, 0x10, 0x00, 0x00, 0x04, 0x00, 0x00, 0xff, 0xff};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, f, 21);

    H2Settings s;
    pc_h2_settings_defaults(&s);
    TEST_ASSERT_TRUE(pc_h2_parse_settings(f + 9, 12, &s));
    TEST_ASSERT_EQUAL_UINT32(4096, s.header_table_size);
    TEST_ASSERT_EQUAL_UINT32(65535, s.initial_window_size);
    TEST_ASSERT_EQUAL_UINT32(1, s.enable_push); // untouched default
}

void test_settings_validation()
{
    H2Settings s;
    pc_h2_settings_defaults(&s);
    // length not a multiple of 6
    const uint8_t bad_len[5] = {0, 1, 0, 0, 0};
    TEST_ASSERT_FALSE(pc_h2_parse_settings(bad_len, 5, &s));
    // ENABLE_PUSH must be 0 or 1
    const uint8_t bad_push[6] = {0x00, 0x02, 0x00, 0x00, 0x00, 0x02};
    TEST_ASSERT_FALSE(pc_h2_parse_settings(bad_push, 6, &s));
    // MAX_FRAME_SIZE below 2^14
    const uint8_t bad_mfs[6] = {0x00, 0x05, 0x00, 0x00, 0x00, 0x10};
    TEST_ASSERT_FALSE(pc_h2_parse_settings(bad_mfs, 6, &s));
}

void test_control_frames()
{
    uint8_t b[32];
    // SETTINGS ACK: length 0, type 4, flags ACK, stream 0
    TEST_ASSERT_EQUAL_INT(9, (int)pc_h2_build_settings_ack(b, sizeof b));
    const uint8_t ack[9] = {0, 0, 0, 0x04, 0x01, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ack, b, 9);

    // WINDOW_UPDATE stream 1, +1000
    TEST_ASSERT_EQUAL_INT(13, (int)pc_h2_build_window_update(b, sizeof b, 1, 1000));
    const uint8_t wu[13] = {0, 0, 4, 0x08, 0, 0, 0, 0, 1, 0, 0, 0x03, 0xE8};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wu, b, 13);

    // RST_STREAM stream 3, CANCEL(8)
    TEST_ASSERT_EQUAL_INT(13, (int)pc_h2_build_rst_stream(b, sizeof b, 3, H2_CANCEL));
    const uint8_t rst[13] = {0, 0, 4, 0x03, 0, 0, 0, 0, 3, 0, 0, 0, 0x08};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rst, b, 13);

    // GOAWAY last=5, NO_ERROR
    TEST_ASSERT_EQUAL_INT(17, (int)pc_h2_build_goaway(b, sizeof b, 5, H2_NO_ERROR));
    const uint8_t ga[17] = {0, 0, 8, 0x07, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ga, b, 17);

    // PING ACK echoes the 8 opaque bytes
    const uint8_t op[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_INT(17, (int)pc_h2_build_ping_ack(b, sizeof b, op));
    const uint8_t pg[17] = {0, 0, 8, 0x06, 0x01, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pg, b, 17);
}

void test_headers_and_data()
{
    uint8_t b[32];
    // HEADERS stream 1, one HPACK byte, end_stream -> flags END_HEADERS|END_STREAM = 0x05
    const uint8_t block[1] = {0x88};
    TEST_ASSERT_EQUAL_INT(10, (int)pc_h2_build_headers(b, sizeof b, 1, block, 1, PROTO_TRUE));
    const uint8_t hh[10] = {0, 0, 1, 0x01, 0x05, 0, 0, 0, 1, 0x88};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(hh, b, 10);

    // DATA stream 1, "hi", end_stream -> flags END_STREAM = 0x01
    TEST_ASSERT_EQUAL_INT(11, (int)pc_h2_build_data(b, sizeof b, 1, (const uint8_t *)"hi", 2, PROTO_TRUE));
    const uint8_t dd[11] = {0, 0, 2, 0x00, 0x01, 0, 0, 0, 1, 'h', 'i'};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(dd, b, 11);

    // HEADERS without end_stream -> flags END_HEADERS only = 0x04 (no END_STREAM).
    TEST_ASSERT_EQUAL_INT(10, (int)pc_h2_build_headers(b, sizeof b, 1, block, 1, PROTO_FALSE));
    const uint8_t hh2[10] = {0, 0, 1, 0x01, 0x04, 0, 0, 0, 1, 0x88};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(hh2, b, 10);

    // DATA without end_stream -> flags 0x00 (no END_STREAM).
    TEST_ASSERT_EQUAL_INT(11, (int)pc_h2_build_data(b, sizeof b, 1, (const uint8_t *)"hi", 2, PROTO_FALSE));
    const uint8_t dd2[11] = {0, 0, 2, 0x00, 0x00, 0, 0, 0, 1, 'h', 'i'};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(dd2, b, 11);

    // DATA with data_len 0 -> header only, memcpy skipped (data pointer not dereferenced).
    TEST_ASSERT_EQUAL_INT(9, (int)pc_h2_build_data(b, sizeof b, 1, NULL, 0, PROTO_TRUE));
    const uint8_t dd0[9] = {0, 0, 0, 0x00, 0x01, 0, 0, 0, 1};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(dd0, b, 9);
}

void test_preface()
{
    TEST_ASSERT_EQUAL_INT(24, (int)H2_PREFACE_LEN);
    TEST_ASSERT_EQUAL_INT(24, (int)strlen(H2_PREFACE));
    TEST_ASSERT_EQUAL_MEMORY("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", H2_PREFACE, 24);
}

void test_settings_all_ids_and_build_guards()
{
    H2Settings s;
    pc_h2_settings_defaults(&s);
    // Every recognized setting id with a valid value, plus an unknown id (ignored).
    const uint16_t ids[6] = {H2_SETTINGS_ENABLE_PUSH,          H2_SETTINGS_MAX_CONCURRENT_STREAMS,
                             H2_SETTINGS_INITIAL_WINDOW_SIZE,  H2_SETTINGS_MAX_FRAME_SIZE,
                             H2_SETTINGS_MAX_HEADER_LIST_SIZE, 0x99};
    const uint32_t vals[6] = {1, 100, 65535, 16384, 8192, 0};
    uint8_t f[128];
    size_t n = pc_h2_build_settings(f, sizeof f, ids, vals, 6);
    TEST_ASSERT_TRUE(pc_h2_parse_settings(f + 9, n - 9, &s));
    TEST_ASSERT_EQUAL_UINT32(1, s.enable_push);
    TEST_ASSERT_EQUAL_UINT32(100, s.max_concurrent_streams);
    TEST_ASSERT_EQUAL_UINT32(65535, s.initial_window_size);
    TEST_ASSERT_EQUAL_UINT32(16384, s.max_frame_size);
    TEST_ASSERT_EQUAL_UINT32(8192, s.max_header_list_size);
    // Upper-bound validation guards.
    const uint8_t bad_iws[6] = {0x00, 0x04, 0x80, 0x00, 0x00, 0x00}; // INITIAL_WINDOW_SIZE = 2^31
    TEST_ASSERT_FALSE(pc_h2_parse_settings(bad_iws, 6, &s));
    const uint8_t bad_mfs_hi[6] = {0x00, 0x05, 0x01, 0x00, 0x00, 0x00}; // MAX_FRAME_SIZE = 2^24
    TEST_ASSERT_FALSE(pc_h2_parse_settings(bad_mfs_hi, 6, &s));
    // A too-short frame header is rejected.
    H2FrameHeader h;
    uint8_t tiny[4] = {0};
    TEST_ASSERT_FALSE(pc_h2_parse_header(tiny, sizeof(tiny), &h));
    // Every builder fails closed on a buffer smaller than the frame header.
    uint8_t small[8];
    const uint16_t sid[1] = {1};
    const uint32_t sv[1] = {1};
    uint8_t op[8] = {0};
    uint8_t blk[4] = {0};
    TEST_ASSERT_EQUAL_size_t(0, pc_h2_build_settings(small, sizeof(small), sid, sv, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_h2_build_window_update(small, sizeof(small), 1, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_h2_build_rst_stream(small, sizeof(small), 1, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_h2_build_goaway(small, sizeof(small), 1, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_h2_build_ping_ack(small, sizeof(small), op));
    TEST_ASSERT_EQUAL_size_t(0, pc_h2_build_headers(small, sizeof(small), 1, blk, sizeof(blk), PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(0, pc_h2_build_data(small, sizeof(small), 1, blk, sizeof(blk), PROTO_FALSE));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_header_roundtrip);
    RUN_TEST(test_settings_build_parse);
    RUN_TEST(test_settings_validation);
    RUN_TEST(test_control_frames);
    RUN_TEST(test_headers_and_data);
    RUN_TEST(test_preface);
    RUN_TEST(test_settings_all_ids_and_build_guards);
    return UNITY_END();
}
