// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Mitsubishi MELSEC MC binary 3E codec (services/fieldbus/melsec): the batch-read
// request builder and the response parser. Little-endian fields; layout per a third-party
// MC implementation. Pure host tests.

#include "services/fieldbus/melsec/melsec.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// Read 5 words of D100 (data register) - the documented binary 3E layout.
void test_build_read_bytes()
{
    uint8_t buf[32];
    size_t n = pc_melsec_build_read(buf, sizeof(buf), MELSEC_DEV_D, 100, 5, 0x0010);
    const uint8_t expect[] = {
        0x50, 0x00,       // subheader (request)
        0x00,             // network
        0xFF,             // PC
        0xFF, 0x03,       // dest module I/O (0x03FF, LE)
        0x00,             // multidrop
        0x0C, 0x00,       // request data length = 12 (LE)
        0x10, 0x00,       // monitoring timer (LE)
        0x01, 0x04,       // command 0x0401 batch read (LE)
        0x00, 0x00,       // subcommand 0x0000 word (LE)
        0x64, 0x00, 0x00, // head device 100 (24-bit LE)
        0xA8,             // device code D
        0x05, 0x00        // 5 points (LE)
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_size_t(21, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_build_write_bytes()
{
    uint8_t buf[32];
    const uint8_t words[4] = {0x34, 0x12, 0x78, 0x56}; // two D words: 0x1234, 0x5678 (LE)
    size_t n = pc_melsec_build_write(buf, sizeof(buf), MELSEC_DEV_D, 100, 2, 0x0010, words, sizeof(words));
    const uint8_t expect[] = {
        0x50, 0x00,            // subheader (request)
        0x00,                  // network
        0xFF,                  // PC
        0xFF, 0x03,            // dest module I/O (0x03FF, LE)
        0x00,                  // multidrop
        0x10, 0x00,            // request data length = 16 (12 + 4 data, LE)
        0x10, 0x00,            // monitoring timer (LE)
        0x01, 0x14,            // command 0x1401 batch write (LE)
        0x00, 0x00,            // subcommand 0x0000 word (LE)
        0x64, 0x00, 0x00,      // head device 100 (24-bit LE)
        0xA8,                  // device code D
        0x02, 0x00,            // 2 points (LE)
        0x34, 0x12, 0x78, 0x56 // write data (two words)
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_size_t(25, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    // A zero-length write is a valid 21-octet frame with request data length 12.
    n = pc_melsec_build_write(buf, sizeof(buf), MELSEC_DEV_D, 100, 0, 0, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(21, n);
    TEST_ASSERT_EQUAL_HEX8(0x0C, buf[7]); // request data length 12

    // Guards: null data with a nonzero length, a too-small buffer, and a null out buffer all fail closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_melsec_build_write(buf, sizeof(buf), MELSEC_DEV_D, 100, 2, 0, NULL, 4));
    TEST_ASSERT_EQUAL_size_t(0, pc_melsec_build_write(buf, 24, MELSEC_DEV_D, 100, 2, 0, words, 4)); // needs 25
    TEST_ASSERT_EQUAL_size_t(0, pc_melsec_build_write(NULL, 32, MELSEC_DEV_D, 100, 2, 0, words, 4));
}

// The head device number occupies 3 little-endian octets.
void test_head_device_24bit()
{
    uint8_t buf[32];
    size_t n = pc_melsec_build_read(buf, sizeof(buf), MELSEC_DEV_M, 0x012345, 1, 0);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_EQUAL_HEX8(0x45, buf[15]);
    TEST_ASSERT_EQUAL_HEX8(0x23, buf[16]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[17]);
    TEST_ASSERT_EQUAL_HEX8(MELSEC_DEV_M, buf[18]);
}

void test_parse_response_ok()
{
    const uint8_t resp[] = {
        0xD0, 0x00,                                                // subheader (response)
        0x00, 0xFF, 0xFF, 0x03, 0x00,                              // routing
        0x0C, 0x00,                                                // data length = 12 (end code + 10 data)
        0x00, 0x00,                                                // end code = success
        0x11, 0x11, 0x22, 0x22, 0x33, 0x33, 0x44, 0x44, 0x55, 0x55 // 5 word values
    };
    MelsecResponse r;
    TEST_ASSERT_TRUE(pc_melsec_parse_response(resp, sizeof(resp), &r));
    TEST_ASSERT_EQUAL_HEX16(MELSEC_ENDCODE_OK, r.end_code);
    TEST_ASSERT_EQUAL_size_t(10, r.data_len);
    TEST_ASSERT_EQUAL_HEX8(0x11, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x55, r.data[9]);
}

void test_parse_response_error()
{
    const uint8_t resp[] = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x02, 0x00, // length = 2 (end code only)
                            0x51, 0xC0};                                          // end code 0xC051 (LE)
    MelsecResponse r;
    TEST_ASSERT_TRUE(pc_melsec_parse_response(resp, sizeof(resp), &r));
    TEST_ASSERT_EQUAL_HEX16(0xC051, r.end_code);
    TEST_ASSERT_EQUAL_size_t(0, r.data_len);
}

void test_parse_rejects_bad()
{
    MelsecResponse r;
    const uint8_t bad_sub[] = {0x50, 0x00, 0, 0xFF, 0xFF, 0x03, 0, 0x02, 0, 0, 0}; // request subheader, not response
    TEST_ASSERT_FALSE(pc_melsec_parse_response(bad_sub, sizeof(bad_sub), &r));
    const uint8_t trunc[] = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03,
                             0x00, 0x0C, 0x00, 0x00, 0x00, 0x11}; // says 12, only 3 follow
    TEST_ASSERT_FALSE(pc_melsec_parse_response(trunc, sizeof(trunc), &r));
}

void test_build_overflow_fails_closed()
{
    uint8_t small[16];
    TEST_ASSERT_EQUAL_size_t(0, pc_melsec_build_read(small, sizeof(small), MELSEC_DEV_D, 0, 1, 0)); // needs 21
}

// A null destination buffer fails closed regardless of the reported capacity.
void test_build_null_buf_fails_closed()
{
    TEST_ASSERT_EQUAL_size_t(0, pc_melsec_build_read(NULL, 32, MELSEC_DEV_D, 100, 5, 0x0010));
}

// The response parser rejects a null/short buffer and a data-length field smaller than
// the mandatory 2-octet end code.
void test_parse_guards()
{
    MelsecResponse r;
    TEST_ASSERT_FALSE(pc_melsec_parse_response(NULL, 16, &r)); // null buf
    const uint8_t shortr[10] = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x02, 0x00, 0x00};
    TEST_ASSERT_FALSE(pc_melsec_parse_response(shortr, sizeof(shortr), &r)); // len < minimum
    // Valid response header but the length field claims 1 octet - less than the end code.
    const uint8_t tiny_len[11] = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(pc_melsec_parse_response(tiny_len, sizeof(tiny_len), &r));
    // Null out-parameter.
    const uint8_t ok[11] = {0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(pc_melsec_parse_response(ok, sizeof(ok), NULL));
}

// Correct first subheader octet (0xD0) but a mismatched second octet is still rejected.
void test_parse_rejects_bad_second_subheader_octet()
{
    MelsecResponse r;
    const uint8_t bad_sub1[] = {0xD0, 0x01, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(pc_melsec_parse_response(bad_sub1, sizeof(bad_sub1), &r));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_read_bytes);
    RUN_TEST(test_build_write_bytes);
    RUN_TEST(test_head_device_24bit);
    RUN_TEST(test_parse_response_ok);
    RUN_TEST(test_parse_response_error);
    RUN_TEST(test_parse_rejects_bad);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_build_null_buf_fails_closed);
    RUN_TEST(test_parse_guards);
    RUN_TEST(test_parse_rejects_bad_second_subheader_octet);
    return UNITY_END();
}
