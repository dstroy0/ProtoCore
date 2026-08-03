// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the DNP3 (IEEE 1815) data-link frame codec (services/energy/dnp3): CRC-16/DNP,
// the frame builder, and the CRC-validating, de-blocking parser. Pure host tests.

#include "services/energy/dnp3/dnp3.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// CRC-16/DNP canonical check value for "123456789" is 0xEA82.
void test_crc_check_value()
{
    TEST_ASSERT_EQUAL_HEX16(0xEA82, pc_dnp3_crc((const uint8_t *)"123456789", 9));
}

// A single-block frame has the documented header bytes (little-endian addresses).
void test_build_header_bytes()
{
    const uint8_t data[] = {'a', 'b', 'c'};
    uint8_t buf[32];
    size_t n = pc_dnp3_build_frame(buf, sizeof(buf), 0x44, 0x0004, 0x0001, data, sizeof(data));
    // 10 header + 3 data + 2 block CRC = 15
    TEST_ASSERT_EQUAL_size_t(15, n);
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x64, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x08, buf[2]); // LEN = 5 + 3
    TEST_ASSERT_EQUAL_HEX8(0x44, buf[3]); // control
    TEST_ASSERT_EQUAL_HEX8(0x04, buf[4]); // dest LSB
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[5]); // dest MSB
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[6]); // src LSB
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[7]); // src MSB
    // header CRC over the 8 header octets, low byte first.
    uint16_t hcrc = pc_dnp3_crc(buf, 8);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(hcrc & 0xFF), buf[8]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(hcrc >> 8), buf[9]);
}

void test_round_trip_single_block()
{
    const uint8_t data[] = {0xC0, 0x01, 0x3C, 0x02, 0x06}; // an app-layer fragment, say
    uint8_t buf[32];
    size_t n = pc_dnp3_build_frame(buf, sizeof(buf), 0x44, 0x1234, 0x0A0B, data, sizeof(data));
    TEST_ASSERT_GREATER_THAN(0, (int)n);

    Dnp3Frame f;
    uint8_t user[64];
    size_t user_len;
    TEST_ASSERT_TRUE(pc_dnp3_parse_frame(buf, n, &f, user, sizeof(user), &user_len));
    TEST_ASSERT_EQUAL_HEX8(0x44, f.control);
    TEST_ASSERT_EQUAL_HEX16(0x1234, f.dest);
    TEST_ASSERT_EQUAL_HEX16(0x0A0B, f.src);
    TEST_ASSERT_EQUAL_size_t(sizeof(data), user_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, user, sizeof(data));
}

// More than 16 user octets span multiple CRC'd blocks and reassemble.
void test_round_trip_multi_block()
{
    uint8_t data[20];
    for (size_t i = 0; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)(i + 1);
    }
    uint8_t buf[64];
    size_t n = pc_dnp3_build_frame(buf, sizeof(buf), 0x44, 1, 2, data, sizeof(data));
    // 10 header + 20 data + 2 blocks * 2 CRC = 34
    TEST_ASSERT_EQUAL_size_t(34, n);

    Dnp3Frame f;
    uint8_t user[32];
    size_t user_len;
    TEST_ASSERT_TRUE(pc_dnp3_parse_frame(buf, n, &f, user, sizeof(user), &user_len));
    TEST_ASSERT_EQUAL_size_t(20, user_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, user, sizeof(data));
}

void test_header_only_frame()
{
    uint8_t buf[16];
    size_t n = pc_dnp3_build_frame(buf, sizeof(buf), 0x49, 3, 4, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(10, n);      // header block only
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[2]); // LEN = 5

    Dnp3Frame f;
    size_t user_len = 999;
    TEST_ASSERT_TRUE(pc_dnp3_parse_frame(buf, n, &f, NULL, 0, &user_len));
    TEST_ASSERT_EQUAL_HEX8(0x49, f.control);
    TEST_ASSERT_EQUAL_size_t(0, user_len);
}

void test_parse_rejects_bad()
{
    const uint8_t data[] = {'x', 'y', 'z'};
    uint8_t buf[32];
    size_t n = pc_dnp3_build_frame(buf, sizeof(buf), 0x44, 1, 2, data, sizeof(data));

    Dnp3Frame f;
    uint8_t user[32];
    size_t user_len;

    // A corrupted data octet fails the block CRC.
    uint8_t corrupt[32];
    memcpy(corrupt, buf, n);
    corrupt[10] ^= 0xFF;
    TEST_ASSERT_FALSE(pc_dnp3_parse_frame(corrupt, n, &f, user, sizeof(user), &user_len));

    // A corrupted header octet fails the header CRC.
    memcpy(corrupt, buf, n);
    corrupt[3] ^= 0x01;
    TEST_ASSERT_FALSE(pc_dnp3_parse_frame(corrupt, n, &f, user, sizeof(user), &user_len));

    // Wrong start word.
    memcpy(corrupt, buf, n);
    corrupt[1] = 0x65;
    TEST_ASSERT_FALSE(pc_dnp3_parse_frame(corrupt, n, &f, user, sizeof(user), &user_len));

    // Truncated frame.
    TEST_ASSERT_FALSE(pc_dnp3_parse_frame(buf, n - 1, &f, user, sizeof(user), &user_len));

    // out_user too small.
    TEST_ASSERT_FALSE(pc_dnp3_parse_frame(buf, n, &f, user, 2, &user_len));
}

void test_build_overflow_fails_closed()
{
    const uint8_t data[] = {'a', 'b', 'c'};
    uint8_t small[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_frame(small, sizeof(small), 0x44, 1, 2, data, sizeof(data)));
    // Over the 250-octet user-data limit also fails.
    uint8_t big[300] = {0};
    uint8_t out[400];
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_frame(out, sizeof(out), 0x44, 1, 2, big, sizeof(big)));
}

// Frame parsing rejects null / too-short input and a LENGTH field below the overhead.
void test_dnp3_parse_guards()
{
    Dnp3Frame f;
    uint8_t user[256];
    size_t ul = 0;
    TEST_ASSERT_FALSE(pc_dnp3_parse_frame(NULL, 20, &f, user, sizeof(user), &ul));
    uint8_t tiny[4] = {0};
    TEST_ASSERT_FALSE(pc_dnp3_parse_frame(tiny, sizeof(tiny), &f, user, sizeof(user), &ul)); // < header block
    uint8_t bad_len[10] = {DNP3_START0, DNP3_START1, 3, 0, 0, 0, 0, 0, 0, 0};                // LENGTH 3 < overhead 5
    TEST_ASSERT_FALSE(pc_dnp3_parse_frame(bad_len, sizeof(bad_len), &f, user, sizeof(user), &ul));
}

// Build-side null guards not hit by the round-trip / overflow tests above: a null
// destination buffer, and a null user_data pointer paired with a nonzero length.
void test_build_frame_null_guards()
{
    const uint8_t data[] = {'a', 'b', 'c'};
    uint8_t buf[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_frame(NULL, sizeof(buf), 0x44, 1, 2, data, sizeof(data)));
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_frame(buf, sizeof(buf), 0x44, 1, 2, NULL, sizeof(data)));
}

// Parse-side guards not hit above: a null Dnp3Frame* out, a bad start0 octet (start1 left
// intact), a null out_user paired with nonzero user data, and a null out_user_len (which is
// optional - parsing still succeeds without it).
void test_parse_frame_null_guards()
{
    const uint8_t data[] = {'x', 'y', 'z'};
    uint8_t buf[32];
    size_t n = pc_dnp3_build_frame(buf, sizeof(buf), 0x44, 1, 2, data, sizeof(data));
    TEST_ASSERT_GREATER_THAN(0, (int)n);

    Dnp3Frame f;
    uint8_t user[32];
    size_t user_len;

    TEST_ASSERT_FALSE(pc_dnp3_parse_frame(buf, n, NULL, user, sizeof(user), &user_len));

    uint8_t corrupt[32];
    memcpy(corrupt, buf, n);
    corrupt[0] = 0x00; // bad start0, start1 untouched
    TEST_ASSERT_FALSE(pc_dnp3_parse_frame(corrupt, n, &f, user, sizeof(user), &user_len));

    TEST_ASSERT_FALSE(pc_dnp3_parse_frame(buf, n, &f, NULL, 0, &user_len));

    TEST_ASSERT_TRUE(pc_dnp3_parse_frame(buf, n, &f, user, sizeof(user), NULL));
}

// --- transport function (IEEE 1815 §8.2) ---
void test_transport_header_and_build()
{
    TEST_ASSERT_EQUAL_HEX8(0xC5, pc_dnp3_transport_header(true, true, 5)); // FIR+FIN+seq5
    TEST_ASSERT_EQUAL_HEX8(0x40, pc_dnp3_transport_header(true, false, 0));
    TEST_ASSERT_EQUAL_HEX8(0x80, pc_dnp3_transport_header(false, true, 0));

    const uint8_t app[3] = {0xC1, 0x01, 0x3C}; // e.g. an app-layer READ request head
    uint8_t seg[8];
    size_t n = pc_dnp3_build_transport_segment(seg, sizeof(seg), true, true, 5, app, 3);
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_HEX8(0xC5, seg[0]);
    TEST_ASSERT_EQUAL_MEMORY(app, seg + 1, 3);
    // Guards: buffer too small, and app data past the 249-octet segment limit.
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_transport_segment(seg, 2, true, true, 0, app, 3));
    uint8_t big[300];
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_transport_segment(big, sizeof(big), true, true, 0, big, 250));
}

void test_transport_single_and_multi()
{
    uint8_t buf[512];
    Dnp3TransportRx r;

    // Single-frame fragment (FIR+FIN, seq 7).
    pc_dnp3_transport_rx_init(&r, buf, sizeof(buf));
    const uint8_t s0[4] = {0xC7, 0xAA, 0xBB, 0xCC};
    TEST_ASSERT_EQUAL_INT(DNP3_TR_COMPLETE, pc_dnp3_transport_feed(&r, s0, 4));
    TEST_ASSERT_EQUAL_size_t(3, r.len);
    TEST_ASSERT_TRUE(r.done);

    // Three-segment fragment: FIR(seq3) + mid(seq4) + FIN(seq5).
    pc_dnp3_transport_rx_init(&r, buf, sizeof(buf));
    const uint8_t f1[3] = {0x43, 0x11, 0x22};
    const uint8_t f2[3] = {0x04, 0x33, 0x44};
    const uint8_t f3[2] = {0x85, 0x55};
    TEST_ASSERT_EQUAL_INT(DNP3_TR_PROGRESS, pc_dnp3_transport_feed(&r, f1, 3));
    TEST_ASSERT_EQUAL_INT(DNP3_TR_PROGRESS, pc_dnp3_transport_feed(&r, f2, 3));
    TEST_ASSERT_EQUAL_INT(DNP3_TR_COMPLETE, pc_dnp3_transport_feed(&r, f3, 2));
    TEST_ASSERT_EQUAL_size_t(5, r.len);
    const uint8_t expect[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
    TEST_ASSERT_EQUAL_MEMORY(expect, buf, 5);
}

void test_transport_errors()
{
    uint8_t buf[512];
    Dnp3TransportRx r;
    pc_dnp3_transport_rx_init(&r, buf, sizeof(buf));

    // A continuation with no active fragment is ignored.
    const uint8_t cont[2] = {0x01, 0x11};
    TEST_ASSERT_EQUAL_INT(DNP3_TR_IGNORED, pc_dnp3_transport_feed(&r, cont, 2));

    // Out of sequence: FIR seq0 then a segment with seq2 (expected 1) is discarded.
    const uint8_t a[2] = {0x40, 0xAA};
    const uint8_t bad[2] = {0x02, 0xBB};
    TEST_ASSERT_EQUAL_INT(DNP3_TR_PROGRESS, pc_dnp3_transport_feed(&r, a, 2));
    TEST_ASSERT_EQUAL_INT(DNP3_TR_IGNORED, pc_dnp3_transport_feed(&r, bad, 2));

    // Overflow: a 2-octet buffer cannot hold a 5-octet application chunk.
    uint8_t tiny[2];
    Dnp3TransportRx r2;
    pc_dnp3_transport_rx_init(&r2, tiny, sizeof(tiny));
    const uint8_t bigseg[6] = {0x40, 1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_INT(DNP3_TR_ERROR, pc_dnp3_transport_feed(&r2, bigseg, 6));

    // Null / empty guards.
    TEST_ASSERT_EQUAL_INT(DNP3_TR_IGNORED, pc_dnp3_transport_feed(&r, NULL, 5));
    TEST_ASSERT_EQUAL_INT(DNP3_TR_IGNORED, pc_dnp3_transport_feed(&r, a, 0));
    pc_dnp3_transport_rx_init(NULL, buf, 512); // must not crash
}

void test_app_request_roundtrip()
{
    // A READ request: AC = FIR|FIN, seq 3; FC READ; a small object header (group 1, var 0, qualifier 0x06).
    uint8_t ac = pc_dnp3_app_control(true, true, false, false, 3);
    TEST_ASSERT_EQUAL_HEX8(0xC3, ac); // FIR|FIN|seq3
    const uint8_t obj[3] = {0x01, 0x00, 0x06};
    uint8_t buf[16];
    size_t n = pc_dnp3_build_app_request(buf, sizeof(buf), ac, DNP3_FC_READ, obj, sizeof(obj));
    TEST_ASSERT_EQUAL_UINT(5, n);
    TEST_ASSERT_EQUAL_HEX8(0xC3, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(DNP3_FC_READ, buf[1]);

    Dnp3AppHeader h;
    TEST_ASSERT_TRUE(pc_dnp3_parse_app_header(buf, n, &h));
    TEST_ASSERT_TRUE(h.fir);
    TEST_ASSERT_TRUE(h.fin);
    TEST_ASSERT_FALSE(h.con);
    TEST_ASSERT_FALSE(h.uns);
    TEST_ASSERT_EQUAL_UINT8(3, h.seq);
    TEST_ASSERT_EQUAL_HEX8(DNP3_FC_READ, h.fc);
    TEST_ASSERT_FALSE(h.is_response);
    TEST_ASSERT_EQUAL_UINT16(0, h.iin);
    TEST_ASSERT_EQUAL_UINT(3, h.obj_len);
    TEST_ASSERT_EQUAL_HEX8(0x01, h.objects[0]);

    // A 2-octet fragment (header only, no objects) parses with a null object pointer.
    n = pc_dnp3_build_app_request(buf, sizeof(buf), ac, DNP3_FC_READ, NULL, 0);
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_TRUE(pc_dnp3_parse_app_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT(0, h.obj_len);
    TEST_ASSERT_NULL(h.objects);
}

void test_app_response_roundtrip()
{
    uint8_t ac = pc_dnp3_app_control(true, true, true, false, 5);    // FIR|FIN|CON seq5
    uint16_t iin = DNP3_IIN_DEVICE_RESTART | DNP3_IIN_CLASS1_EVENTS; // 0x0082
    const uint8_t obj[2] = {0x3C, 0x01};                             // class-0 object header stub
    uint8_t buf[16];
    size_t n = pc_dnp3_build_app_response(buf, sizeof(buf), ac, DNP3_FC_RESPONSE, iin, obj, sizeof(obj));
    TEST_ASSERT_EQUAL_UINT(6, n);         // AC + FC + 2 IIN + 2 object octets
    TEST_ASSERT_EQUAL_HEX8(0x82, buf[2]); // IIN1, little-endian
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]); // IIN2

    Dnp3AppHeader h;
    TEST_ASSERT_TRUE(pc_dnp3_parse_app_header(buf, n, &h));
    TEST_ASSERT_TRUE(h.is_response);
    TEST_ASSERT_EQUAL_HEX8(DNP3_FC_RESPONSE, h.fc);
    TEST_ASSERT_EQUAL_UINT8(5, h.seq);
    TEST_ASSERT_TRUE(h.con);
    TEST_ASSERT_EQUAL_UINT16(iin, h.iin);
    TEST_ASSERT_TRUE(h.iin & DNP3_IIN_DEVICE_RESTART);
    TEST_ASSERT_EQUAL_UINT(2, h.obj_len);

    // A response fragment truncated to 3 octets (missing an IIN octet) is rejected.
    TEST_ASSERT_FALSE(pc_dnp3_parse_app_header(buf, 3, &h));
    // A 1-octet fragment and null guards are rejected.
    TEST_ASSERT_FALSE(pc_dnp3_parse_app_header(buf, 1, &h));
    TEST_ASSERT_FALSE(pc_dnp3_parse_app_header(NULL, n, &h));
    // Build overflow fails closed.
    TEST_ASSERT_EQUAL_UINT(0, pc_dnp3_build_app_response(buf, 3, ac, DNP3_FC_RESPONSE, iin, obj, sizeof(obj)));
}

void test_object_header_forms()
{
    Dnp3ObjectHeader h;
    // Start-stop, 1-octet indexes: group 1 var 2 (binary inputs), 0..9 -> count 10, then 2 object octets.
    const uint8_t ss1[] = {0x01, 0x02, 0x00, 0x00, 0x09, 0xAA, 0xBB};
    TEST_ASSERT_TRUE(pc_dnp3_parse_object_header(ss1, sizeof(ss1), &h));
    TEST_ASSERT_EQUAL_UINT8(1, h.group);
    TEST_ASSERT_EQUAL_UINT8(2, h.variation);
    TEST_ASSERT_EQUAL_UINT8(0, h.prefix_code);
    TEST_ASSERT_EQUAL_UINT8(DNP3_RANGE_START_STOP_1, h.range_code);
    TEST_ASSERT_FALSE(h.is_count);
    TEST_ASSERT_EQUAL_UINT32(0, h.start);
    TEST_ASSERT_EQUAL_UINT32(9, h.stop);
    TEST_ASSERT_EQUAL_UINT32(10, h.count);
    TEST_ASSERT_EQUAL_size_t(2, h.objects_len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, h.objects[0]);

    // Start-stop, 2-octet indexes: group 30 var 1 (analog inputs), 100..200 -> count 101.
    const uint8_t ss2[] = {0x1E, 0x01, 0x01, 0x64, 0x00, 0xC8, 0x00};
    TEST_ASSERT_TRUE(pc_dnp3_parse_object_header(ss2, sizeof(ss2), &h));
    TEST_ASSERT_EQUAL_UINT8(30, h.group);
    TEST_ASSERT_EQUAL_UINT32(100, h.start);
    TEST_ASSERT_EQUAL_UINT32(200, h.stop);
    TEST_ASSERT_EQUAL_UINT32(101, h.count);
    TEST_ASSERT_NULL(h.objects); // nothing after the range

    // Start-stop, 4-octet indexes: 0..3 -> count 4.
    const uint8_t ss4[] = {0x1E, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(pc_dnp3_parse_object_header(ss4, sizeof(ss4), &h));
    TEST_ASSERT_EQUAL_UINT32(0, h.start);
    TEST_ASSERT_EQUAL_UINT32(3, h.stop);
    TEST_ASSERT_EQUAL_UINT32(4, h.count);

    // Count forms (1/2/4-octet): group 2 var 1 (binary input events).
    const uint8_t c1[] = {0x02, 0x01, 0x07, 0x03, 0x11, 0x22, 0x33};
    TEST_ASSERT_TRUE(pc_dnp3_parse_object_header(c1, sizeof(c1), &h));
    TEST_ASSERT_EQUAL_UINT8(DNP3_RANGE_COUNT_1, h.range_code);
    TEST_ASSERT_TRUE(h.is_count);
    TEST_ASSERT_EQUAL_UINT32(3, h.count);
    TEST_ASSERT_EQUAL_size_t(3, h.objects_len);
    const uint8_t c2[] = {0x02, 0x01, 0x08, 0x05, 0x00};
    TEST_ASSERT_TRUE(pc_dnp3_parse_object_header(c2, sizeof(c2), &h));
    TEST_ASSERT_EQUAL_UINT32(5, h.count);
    const uint8_t c4[] = {0x02, 0x01, 0x09, 0x0A, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(pc_dnp3_parse_object_header(c4, sizeof(c4), &h));
    TEST_ASSERT_EQUAL_UINT32(10, h.count);

    // No-range: group 60 var 1 (class 0 data), qualifier 0x06 -> all objects, no range field.
    const uint8_t allobj[] = {0x3C, 0x01, 0x06};
    TEST_ASSERT_TRUE(pc_dnp3_parse_object_header(allobj, sizeof(allobj), &h));
    TEST_ASSERT_EQUAL_UINT8(60, h.group);
    TEST_ASSERT_EQUAL_UINT8(DNP3_RANGE_NO_RANGE, h.range_code);
    TEST_ASSERT_EQUAL_UINT32(0, h.count);
    TEST_ASSERT_NULL(h.objects);

    // A qualifier prefix code (bits 6-4) is surfaced: 0x17 = prefix 1, count-1 range.
    const uint8_t pref[] = {0x02, 0x01, 0x17, 0x02};
    TEST_ASSERT_TRUE(pc_dnp3_parse_object_header(pref, sizeof(pref), &h));
    TEST_ASSERT_EQUAL_UINT8(1, h.prefix_code);
    TEST_ASSERT_EQUAL_UINT8(DNP3_RANGE_COUNT_1, h.range_code);
    TEST_ASSERT_EQUAL_UINT32(2, h.count);
}

void test_object_header_rejects()
{
    Dnp3ObjectHeader h;
    // Too short for even the 3-octet header.
    const uint8_t two[] = {0x01, 0x02};
    TEST_ASSERT_FALSE(pc_dnp3_parse_object_header(two, sizeof(two), &h));
    // Truncated range fields: start-stop-1 (needs 2), start-stop-2 (needs 4), count-2 (needs 2).
    const uint8_t t1[] = {0x01, 0x02, 0x00, 0x00};
    TEST_ASSERT_FALSE(pc_dnp3_parse_object_header(t1, sizeof(t1), &h));
    const uint8_t t2[] = {0x1E, 0x01, 0x01, 0x64, 0x00};
    TEST_ASSERT_FALSE(pc_dnp3_parse_object_header(t2, sizeof(t2), &h));
    const uint8_t t3[] = {0x02, 0x01, 0x08, 0x05};
    TEST_ASSERT_FALSE(pc_dnp3_parse_object_header(t3, sizeof(t3), &h));
    // An unsupported qualifier range form (0x0B, variable-format) is rejected.
    const uint8_t badq[] = {0x02, 0x01, 0x0B, 0x01};
    TEST_ASSERT_FALSE(pc_dnp3_parse_object_header(badq, sizeof(badq), &h));
    // Null guards.
    TEST_ASSERT_FALSE(pc_dnp3_parse_object_header(NULL, 5, &h));
    TEST_ASSERT_FALSE(pc_dnp3_parse_object_header(two, 5, NULL));
}

void test_build_object_header()
{
    uint8_t buf[16];
    Dnp3ObjectHeader h;

    // 1-octet start-stop: group 1 var 2 (binary inputs), 0..9.
    size_t n = pc_dnp3_build_object_header_range(buf, sizeof(buf), 1, 2, 0, 9);
    TEST_ASSERT_EQUAL_size_t(5, n);
    const uint8_t exp1[] = {0x01, 0x02, 0x00, 0x00, 0x09};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp1, buf, 5);
    TEST_ASSERT_TRUE(pc_dnp3_parse_object_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT8(DNP3_RANGE_START_STOP_1, h.range_code);
    TEST_ASSERT_EQUAL_UINT32(0, h.start);
    TEST_ASSERT_EQUAL_UINT32(9, h.stop);

    // Promotes to the 2-octet form when stop exceeds 255: group 30 var 1, 100..300.
    n = pc_dnp3_build_object_header_range(buf, sizeof(buf), 30, 1, 100, 300);
    TEST_ASSERT_EQUAL_size_t(7, n);
    const uint8_t exp2[] = {0x1E, 0x01, 0x01, 0x64, 0x00, 0x2C, 0x01};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp2, buf, 7);
    TEST_ASSERT_TRUE(pc_dnp3_parse_object_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT32(100, h.start);
    TEST_ASSERT_EQUAL_UINT32(300, h.stop);
    TEST_ASSERT_EQUAL_UINT32(201, h.count);

    // Promotes to the 4-octet form when stop exceeds 65535: 0..70000.
    n = pc_dnp3_build_object_header_range(buf, sizeof(buf), 30, 1, 0, 70000);
    TEST_ASSERT_EQUAL_size_t(11, n);
    TEST_ASSERT_EQUAL_HEX8(DNP3_RANGE_START_STOP_4, buf[2]);
    TEST_ASSERT_TRUE(pc_dnp3_parse_object_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT32(0, h.start);
    TEST_ASSERT_EQUAL_UINT32(70000, h.stop);

    // All-objects (qualifier 0x06): group 60 var 1 (Class-0 poll).
    n = pc_dnp3_build_object_header_all(buf, sizeof(buf), 60, 1);
    TEST_ASSERT_EQUAL_size_t(3, n);
    const uint8_t expall[] = {0x3C, 0x01, 0x06};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expall, buf, 3);
    TEST_ASSERT_TRUE(pc_dnp3_parse_object_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT8(DNP3_RANGE_NO_RANGE, h.range_code);

    // Guards: stop < start, a null buffer, and too-small buffers all fail closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_object_header_range(buf, sizeof(buf), 1, 2, 9, 0)); // stop < start
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_object_header_range(NULL, sizeof(buf), 1, 2, 0, 9));
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_object_header_range(buf, 4, 1, 2, 0, 9)); // needs 5
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_object_header_all(buf, 2, 60, 1));        // needs 3
}

void test_build_crob()
{
    uint8_t buf[16];

    // LATCH_ON, no trip/close, count 1, on/off time 0: control code = 0x03.
    size_t n = pc_dnp3_build_crob(buf, sizeof(buf), DNP3_CROB_OP_LATCH_ON, DNP3_CROB_TCC_NUL, false, 1, 0, 0);
    TEST_ASSERT_EQUAL_size_t(DNP3_CROB_LEN, n);
    const uint8_t latch[] = {0x03, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(latch, buf, n);

    // PULSE_ON + CLOSE, count 2, on-time 100 ms (0x64), off-time 500 ms (0x1F4): control = 0x01 | (1<<6) = 0x41.
    n = pc_dnp3_build_crob(buf, sizeof(buf), DNP3_CROB_OP_PULSE_ON, DNP3_CROB_TCC_CLOSE, false, 2, 100, 500);
    const uint8_t pulse[] = {0x41, 0x02, 0x64, 0x00, 0x00, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pulse, buf, n);

    // The clear bit sets 0x20; a TRIP with LATCH_OFF gives op 4 | clear | (2<<6) = 0x04 | 0x20 | 0x80 = 0xA4.
    n = pc_dnp3_build_crob(buf, sizeof(buf), DNP3_CROB_OP_LATCH_OFF, DNP3_CROB_TCC_TRIP, true, 1, 0, 0);
    TEST_ASSERT_EQUAL_HEX8(0xA4, buf[0]);

    // Guards: an op type / trip-close code beyond their fields, a null buffer, and a too-small buffer.
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_crob(buf, sizeof(buf), 0x10, 0, false, 1, 0, 0)); // op > 0x0F
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_crob(buf, sizeof(buf), 1, 0x04, false, 1, 0, 0)); // tcc > 3
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_crob(NULL, sizeof(buf), 1, 1, false, 1, 0, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_crob(buf, 10, 1, 1, false, 1, 0, 0)); // needs 11
}

void test_build_aob()
{
    uint8_t buf[16];

    // g41v1: a 32-bit signed setpoint 12345 (0x3039) little-endian + a status octet (0 in a request).
    size_t n = pc_dnp3_build_aob32(buf, sizeof(buf), 12345);
    TEST_ASSERT_EQUAL_size_t(DNP3_AOB_LEN, n);
    const uint8_t v1[] = {0x39, 0x30, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(v1, buf, n);

    // A negative setpoint rides the two's-complement bytes: -12345 = 0xFFFFCFC7.
    n = pc_dnp3_build_aob32(buf, sizeof(buf), -12345);
    const uint8_t v1neg[] = {0xC7, 0xCF, 0xFF, 0xFF, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(v1neg, buf, n);

    // g41v3: a 32-bit float setpoint 1.5 (IEEE-754 0x3FC00000) little-endian + status.
    n = pc_dnp3_build_aob_float(buf, sizeof(buf), 1.5f);
    TEST_ASSERT_EQUAL_size_t(DNP3_AOB_LEN, n);
    const uint8_t v3[] = {0x00, 0x00, 0xC0, 0x3F, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(v3, buf, n);

    // Guards: a null buffer and a too-small buffer (both variations need 5 octets).
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_aob32(NULL, sizeof(buf), 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_aob32(buf, 4, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_dnp3_build_aob_float(buf, 4, 1.0f));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_dnp3_parse_guards);
    RUN_TEST(test_crc_check_value);
    RUN_TEST(test_build_header_bytes);
    RUN_TEST(test_round_trip_single_block);
    RUN_TEST(test_round_trip_multi_block);
    RUN_TEST(test_header_only_frame);
    RUN_TEST(test_parse_rejects_bad);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_build_frame_null_guards);
    RUN_TEST(test_parse_frame_null_guards);
    RUN_TEST(test_transport_header_and_build);
    RUN_TEST(test_transport_single_and_multi);
    RUN_TEST(test_transport_errors);
    RUN_TEST(test_app_request_roundtrip);
    RUN_TEST(test_app_response_roundtrip);
    RUN_TEST(test_object_header_forms);
    RUN_TEST(test_object_header_rejects);
    RUN_TEST(test_build_object_header);
    RUN_TEST(test_build_crob);
    RUN_TEST(test_build_aob);
    return UNITY_END();
}
