// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/dds: the RTPS message + submessage framing codec.

#include "services/iot/dds/dds.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t GUID[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
static const uint8_t VENDOR[2] = {0x01, 0x03};

void test_header(void)
{
    uint8_t out[24];
    size_t n = pc_rtps_header(GUID, VENDOR, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(20, n);
    const uint8_t expect[20] = {'R', 'T', 'P', 'S', 2, 4, 0x01, 0x03, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 20);
    // Too small a buffer -> 0.
    TEST_ASSERT_EQUAL_size_t(0, pc_rtps_header(GUID, VENDOR, out, 10));
}

void test_submessage_endianness(void)
{
    uint8_t body[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint8_t out[16];
    // Little-endian (E flag set): octetsToNextHeader = 0x0008 -> 08 00.
    size_t n = pc_rtps_submessage(RTPS_SM_INFO_TS, RTPS_FLAG_ENDIAN, body, 8, out, sizeof(out));
    const uint8_t le[] = {0x09, 0x01, 0x08, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    TEST_ASSERT_EQUAL_size_t(sizeof(le), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(le, out, n);
    // Big-endian (E flag clear): 00 08.
    n = pc_rtps_submessage(RTPS_SM_DATA, 0x00, body, 8, out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x08, out[3]);
}

typedef struct
{
    int count;
    uint8_t ids[8];
    size_t lens[8];
    const uint8_t *bodies[8];
} Seen;
static void collect(uint8_t id, uint8_t flags, const uint8_t *body, size_t body_len, void *arg)
{
    (void)flags;
    Seen *s = (Seen *)arg;
    if (s->count < 8)
    {
        s->ids[s->count] = id;
        s->lens[s->count] = body_len;
        s->bodies[s->count] = body;
        s->count++;
    }
}

void test_parse_message(void)
{
    uint8_t msg[64];
    size_t n = pc_rtps_header(GUID, VENDOR, msg, sizeof(msg));
    uint8_t ts[8] = {0};
    n += pc_rtps_submessage(RTPS_SM_INFO_TS, RTPS_FLAG_ENDIAN, ts, 8, msg + n, sizeof(msg) - n);
    uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    n += pc_rtps_submessage(RTPS_SM_DATA, RTPS_FLAG_ENDIAN, data, 4, msg + n, sizeof(msg) - n);

    Seen s = {0, {0}, {0}};
    TEST_ASSERT_TRUE(pc_rtps_parse(msg, n, collect, &s));
    TEST_ASSERT_EQUAL_INT(2, s.count);
    TEST_ASSERT_EQUAL_HEX8(RTPS_SM_INFO_TS, s.ids[0]);
    TEST_ASSERT_EQUAL_size_t(8, s.lens[0]);
    TEST_ASSERT_EQUAL_HEX8(RTPS_SM_DATA, s.ids[1]);
    TEST_ASSERT_EQUAL_size_t(4, s.lens[1]);
}

void test_parse_rejects(void)
{
    uint8_t msg[24];
    pc_rtps_header(GUID, VENDOR, msg, sizeof(msg));
    // Bad magic.
    msg[0] = 'X';
    TEST_ASSERT_FALSE(pc_rtps_parse(msg, 20, NULL, NULL));
    msg[0] = 'R';
    // A newer minor version than ours is rejected.
    msg[5] = 99;
    TEST_ASSERT_FALSE(pc_rtps_parse(msg, 20, NULL, NULL));
    // Too short for a header.
    TEST_ASSERT_FALSE(pc_rtps_parse(msg, 10, NULL, NULL));
}

void test_rtps_build_guards()
{
    uint8_t out[8];
    uint8_t guid[12] = {0};
    uint8_t vendor[2] = {0};
    TEST_ASSERT_EQUAL_size_t(0, pc_rtps_header(guid, vendor, out, 4)); // cap too small
    uint8_t body[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_size_t(0, pc_rtps_submessage(0x15, 0, body, sizeof(body), out, 2)); // cap too small
}

// Every null-pointer guard on the header builder rejects independently.
void test_header_null_args(void)
{
    uint8_t out[24];
    TEST_ASSERT_EQUAL_size_t(0, pc_rtps_header(NULL, VENDOR, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_rtps_header(GUID, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_rtps_header(GUID, VENDOR, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(20, pc_rtps_header(GUID, VENDOR, out, sizeof(out))); // control
}

// The submessage builder rejects a null destination and a null body with a non-zero
// length, but a zero length with a null body is a legal empty submessage.
void test_submessage_null_args(void)
{
    uint8_t out[16];
    uint8_t body[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_size_t(0, pc_rtps_submessage(RTPS_SM_DATA, 0, body, 4, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_rtps_submessage(RTPS_SM_DATA, 0, NULL, 4, out, sizeof(out)));
    // body_len == 0 with a null body: header only, no memcpy.
    size_t n = pc_rtps_submessage(RTPS_SM_PAD, RTPS_FLAG_ENDIAN, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_HEX8(RTPS_SM_PAD, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[3]);
}

// A null message buffer is rejected before the length check.
void test_parse_null_msg(void)
{
    TEST_ASSERT_FALSE(pc_rtps_parse(NULL, 20, NULL, NULL));
}

// Each of the four magic bytes is checked individually.
void test_parse_rejects_each_magic_byte(void)
{
    uint8_t msg[24];
    for (int i = 0; i < 4; i++)
    {
        pc_rtps_header(GUID, VENDOR, msg, sizeof(msg));
        msg[i] = 'Z';
        TEST_ASSERT_FALSE(pc_rtps_parse(msg, 20, NULL, NULL));
    }
    pc_rtps_header(GUID, VENDOR, msg, sizeof(msg)); // control: intact magic parses
    TEST_ASSERT_TRUE(pc_rtps_parse(msg, 20, NULL, NULL));
}

// A different MAJOR version is rejected; an OLDER minor is accepted (RTPS is
// backward compatible), which is the arm the "newer minor" test cannot reach.
void test_parse_version_major_and_older_minor(void)
{
    uint8_t msg[24];
    pc_rtps_header(GUID, VENDOR, msg, sizeof(msg));
    msg[4] = 1; // major != ours
    TEST_ASSERT_FALSE(pc_rtps_parse(msg, 20, NULL, NULL));
    pc_rtps_header(GUID, VENDOR, msg, sizeof(msg));
    msg[5] = 0; // older minor: accepted
    TEST_ASSERT_TRUE(pc_rtps_parse(msg, 20, NULL, NULL));
}

// A big-endian submessage (E flag clear) decodes octetsToNextHeader MSB-first.
void test_parse_big_endian_submessage(void)
{
    uint8_t msg[64];
    size_t n = pc_rtps_header(GUID, VENDOR, msg, sizeof(msg));
    uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    n += pc_rtps_submessage(RTPS_SM_DATA, 0x00, data, 4, msg + n, sizeof(msg) - n);
    TEST_ASSERT_EQUAL_HEX8(0x00, msg[22]); // wire order really is big-endian
    TEST_ASSERT_EQUAL_HEX8(0x04, msg[23]);

    Seen s = {0, {0}, {0}, {NULL}};
    TEST_ASSERT_TRUE(pc_rtps_parse(msg, n, collect, &s));
    TEST_ASSERT_EQUAL_INT(1, s.count);
    TEST_ASSERT_EQUAL_HEX8(RTPS_SM_DATA, s.ids[0]);
    TEST_ASSERT_EQUAL_size_t(4, s.lens[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, s.bodies[0], 4);
}

// octetsToNextHeader == 0 means "extends to end of message" and terminates the
// walk. Here it is the last submessage, so the body is empty and reported as null.
void test_parse_zero_length_terminates(void)
{
    uint8_t msg[32];
    size_t n = pc_rtps_header(GUID, VENDOR, msg, sizeof(msg));
    n += pc_rtps_submessage(RTPS_SM_PAD, RTPS_FLAG_ENDIAN, NULL, 0, msg + n, sizeof(msg) - n);
    TEST_ASSERT_EQUAL_size_t(24, n);

    Seen s = {0, {0}, {0}, {NULL}};
    TEST_ASSERT_TRUE(pc_rtps_parse(msg, n, collect, &s));
    TEST_ASSERT_EQUAL_INT(1, s.count);
    TEST_ASSERT_EQUAL_size_t(0, s.lens[0]);
    TEST_ASSERT_NULL(s.bodies[0]); // empty body is handed over as NULL
}

// A submessage whose declared length runs past the end of the datagram is rejected.
void test_parse_rejects_truncated_submessage(void)
{
    uint8_t msg[32];
    pc_rtps_header(GUID, VENDOR, msg, sizeof(msg));
    msg[20] = RTPS_SM_DATA;
    msg[21] = RTPS_FLAG_ENDIAN;
    msg[22] = 100; // claims 100 body bytes; only 0 remain
    msg[23] = 0;
    Seen s = {0, {0}, {0}, {NULL}};
    TEST_ASSERT_FALSE(pc_rtps_parse(msg, 24, collect, &s));
    TEST_ASSERT_EQUAL_INT(0, s.count); // rejected before any callback
}

// Parsing with no callback still walks and validates the whole message.
void test_parse_without_callback(void)
{
    uint8_t msg[64];
    size_t n = pc_rtps_header(GUID, VENDOR, msg, sizeof(msg));
    uint8_t data[4] = {1, 2, 3, 4};
    n += pc_rtps_submessage(RTPS_SM_DATA, RTPS_FLAG_ENDIAN, data, 4, msg + n, sizeof(msg) - n);
    TEST_ASSERT_TRUE(pc_rtps_parse(msg, n, NULL, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_header);
    RUN_TEST(test_submessage_endianness);
    RUN_TEST(test_parse_message);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_rtps_build_guards);
    RUN_TEST(test_header_null_args);
    RUN_TEST(test_submessage_null_args);
    RUN_TEST(test_parse_null_msg);
    RUN_TEST(test_parse_rejects_each_magic_byte);
    RUN_TEST(test_parse_version_major_and_older_minor);
    RUN_TEST(test_parse_big_endian_submessage);
    RUN_TEST(test_parse_zero_length_terminates);
    RUN_TEST(test_parse_rejects_truncated_submessage);
    RUN_TEST(test_parse_without_callback);
    return UNITY_END();
}
