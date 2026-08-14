// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the FANUC FOCAS Ethernet codec (services/machine_tool/focas/focas.h).
//
// FOCAS has NO public specification: it ships as a proprietary FANUC PC library and FANUC
// publishes no wire document, so no standard-published vector is obtainable for it. Expectations
// here come from the two sources that remain: the field layout focas.h itself states (10-octet
// envelope, magic A0 A0 A0 A0, version 1, the frame-type numbers, big-endian throughout, the
// 6-octet selector + five i32 body, the `data / base^exp` 8-octet value) and PROPERTIES that must
// hold whatever the layout is (build then parse is the identity, a declared length longer than the
// buffer is refused, a wrong magic is refused, a short buffer writes nothing).
//
// test_open_request_is_byte_exact is the load-bearing case: the open frame is the shortest complete
// telegram the protocol has, so writing all twelve of its octets out pins the magic, the version
// field, the frame-type number, the big-endian length and the FRAME_DST payload in one assertion.

#include "services/machine_tool/focas/focas.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Envelope: magic(4) + version(2) + type(2) + payload length(2), then payload.
// Open request payload is FRAME_DST = 0x0002, so the whole telegram is 12 octets.
void test_open_request_is_byte_exact(void)
{
    static const uint8_t WANT[] = {0xA0, 0xA0, 0xA0, 0xA0, 0x00, 0x01, 0x01, 0x01, 0x00, 0x02, 0x00, 0x02};
    uint8_t buf[32];
    size_t n = protocore_focas_build_open(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, n);
}

// Close request: type 0x0201, no payload, so the envelope alone.
void test_close_request_is_byte_exact(void)
{
    static const uint8_t WANT[] = {0xA0, 0xA0, 0xA0, 0xA0, 0x00, 0x01, 0x02, 0x01, 0x00, 0x00};
    uint8_t buf[32];
    size_t n = protocore_focas_build_close(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t((size_t)FOCAS_FRAME_HDR_LEN, n);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, n);
}

// A command request: type 0x2101, length 26 (= 6-octet selector + five i32), selector 1/1/0x18 for
// SysInfo, arguments all zero.
void test_sysinfo_request_is_byte_exact(void)
{
    static const uint8_t WANT[] = {
        0xA0, 0xA0, 0xA0, 0xA0,             // magic
        0x00, 0x01,                         // version 1
        0x21, 0x01,                         // command request
        0x00, 0x1A,                         // payload length 26
        0x00, 0x01, 0x00, 0x01, 0x00, 0x18, // selector c1=1 c2=1 c3=0x18
        0x00, 0x00, 0x00, 0x00,             // v1
        0x00, 0x00, 0x00, 0x00,             // v2
        0x00, 0x00, 0x00, 0x00,             // v3
        0x00, 0x00, 0x00, 0x00,             // v4
        0x00, 0x00, 0x00, 0x00              // v5
    };
    uint8_t buf[64];
    size_t n = protocore_focas_build_sysinfo(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t((size_t)FOCAS_FRAME_HDR_LEN + FOCAS_REQ_BODY_LEN, n);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, n);
}

// The five arguments are signed 32-bit big-endian. 6510 = 0x0000196E and -1 = 0xFFFFFFFF, so the
// same value spelled either way pins both the width and the byte order.
void test_arguments_are_big_endian_i32(void)
{
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_size_t(36u, protocore_focas_build_read_param(buf, sizeof(buf), 6510, 6510, 1));
    static const uint8_t WANT_ARGS[] = {
        0x00, 0x00, 0x19, 0x6E, // v1 = first  = 6510
        0x00, 0x00, 0x19, 0x6E, // v2 = last   = 6510
        0x00, 0x00, 0x00, 0x01, // v3 = axis   = 1
        0x00, 0x00, 0x00, 0x00, // v4
        0x00, 0x00, 0x00, 0x00  // v5
    };
    TEST_ASSERT_EQUAL_HEX8(0x0E, buf[15]); // c3 = 0x0e, cnc_rdparam
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_ARGS, buf + 16, sizeof(WANT_ARGS));

    // A negative argument is the two's-complement pattern, not a truncation or an absolute value.
    TEST_ASSERT_EQUAL_size_t(
        36u, protocore_focas_build_request(buf, sizeof(buf), FOCAS_CMD_READ_MACRO, -1, 0, 0, 0, 0, NULL, 0));
    static const uint8_t WANT_NEG[] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_NEG, buf + 16, sizeof(WANT_NEG));
}

// Position reads put the kind in v1 and the axis in v2; the kind numbers are the header's.
void test_position_kind_and_axis_are_the_first_two_arguments(void)
{
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_size_t(36u, protocore_focas_build_read_position(buf, sizeof(buf), FOCAS_POS_ABSOLUTE, 2));
    TEST_ASSERT_EQUAL_HEX8(0x26, buf[15]); // c3 = 0x26
    TEST_ASSERT_EQUAL_HEX8(0x04, buf[19]); // v1 = FOCAS_POS_ABSOLUTE = 4
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[23]); // v2 = axis 2
}

// Trailing data extends the declared payload length by exactly its own size and lands right after
// the 26-octet body.
void test_extra_data_extends_the_declared_length(void)
{
    static const uint8_t EXTRA[] = {0xAA, 0xBB, 0xCC};
    uint8_t buf[64];
    size_t n =
        protocore_focas_build_request(buf, sizeof(buf), FOCAS_CMD_SET_MACRO, 500, 0, 0, 0, 0, EXTRA, sizeof(EXTRA));
    TEST_ASSERT_EQUAL_size_t((size_t)FOCAS_FRAME_HDR_LEN + FOCAS_REQ_BODY_LEN + sizeof(EXTRA), n);
    // length field = 26 + 3 = 29 = 0x001D
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[8]);
    TEST_ASSERT_EQUAL_HEX8(0x1D, buf[9]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(EXTRA, buf + FOCAS_FRAME_HDR_LEN + FOCAS_REQ_BODY_LEN, sizeof(EXTRA));
}

// A buffer one octet short of the whole telegram writes nothing and reports 0.
void test_builders_refuse_a_short_buffer(void)
{
    uint8_t buf[64];
    memset(buf, 0x5A, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_focas_build_open(buf, 11));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_focas_build_close(buf, FOCAS_FRAME_HDR_LEN - 1));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_focas_build_sysinfo(buf, 35));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_focas_build_open(NULL, 64));
    TEST_ASSERT_EQUAL_HEX8(0x5A, buf[0]); // untouched
}

// Property: every builder's output parses back to the type and payload length it declared.
void test_built_frames_parse_back(void)
{
    uint8_t buf[64];
    FocasFrame f;

    TEST_ASSERT_TRUE(protocore_focas_parse_frame(buf, protocore_focas_build_open(buf, sizeof(buf)), &f));
    TEST_ASSERT_EQUAL_INT(FOCAS_FRAME_TYPE_OPEN_REQ, f.type);
    TEST_ASSERT_EQUAL_UINT16(FOCAS_PROTO_VERSION, f.version);
    TEST_ASSERT_EQUAL_UINT16(2u, f.payload_len);
    TEST_ASSERT_EQUAL_HEX16(FOCAS_FRAME_DST, (uint16_t)((f.payload[0] << 8) | f.payload[1]));

    TEST_ASSERT_TRUE(protocore_focas_parse_frame(buf, protocore_focas_build_close(buf, sizeof(buf)), &f));
    TEST_ASSERT_EQUAL_INT(FOCAS_FRAME_TYPE_CLOSE_REQ, f.type);
    TEST_ASSERT_EQUAL_UINT16(0u, f.payload_len);

    TEST_ASSERT_TRUE(protocore_focas_parse_frame(buf, protocore_focas_build_read_feedrate(buf, sizeof(buf)), &f));
    TEST_ASSERT_EQUAL_INT(FOCAS_FRAME_TYPE_COMMAND_REQ, f.type);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)FOCAS_REQ_BODY_LEN, f.payload_len);
}

// A wrong magic octet, a short buffer and a length field larger than the buffer are all refused.
void test_malformed_frames_are_refused(void)
{
    uint8_t buf[64];
    FocasFrame f;
    size_t n = protocore_focas_build_open(buf, sizeof(buf));

    buf[3] = 0xA1;
    TEST_ASSERT_FALSE(protocore_focas_parse_frame(buf, n, &f));
    buf[3] = 0xA0;
    TEST_ASSERT_TRUE(protocore_focas_parse_frame(buf, n, &f));

    TEST_ASSERT_FALSE(protocore_focas_parse_frame(buf, (size_t)FOCAS_FRAME_HDR_LEN - 1, &f));

    // declare a 3-octet payload but hand over only the 12 octets already written
    buf[9] = 0x03;
    TEST_ASSERT_FALSE(protocore_focas_parse_frame(buf, n, &f));
}

// A command response payload echoes the selector, carries a signed FOCAS return code, then a
// big-endian data length and that many data octets.
void test_command_response_decodes_selector_status_and_data(void)
{
    static const uint8_t FRAME[] = {
        0xA0, 0xA0, 0xA0, 0xA0,             // magic
        0x00, 0x01,                         // version
        0x21, 0x02,                         // command response
        0x00, 0x12,                         // payload length 18 = 14 header + 4 data
        0x00, 0x01, 0x00, 0x01, 0x00, 0x1A, // echoed selector 1/1/0x1a
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // status 0 = EW_OK
        0x00, 0x04,                         // data length 4
        0x00, 0x00, 0x01, 0x00              // data
    };
    FocasResponse r;
    TEST_ASSERT_TRUE(protocore_focas_parse_command_frame(FRAME, sizeof(FRAME), &r));
    TEST_ASSERT_EQUAL_UINT16(1u, r.c1);
    TEST_ASSERT_EQUAL_UINT16(1u, r.c2);
    TEST_ASSERT_EQUAL_UINT16(0x1Au, r.c3);
    TEST_ASSERT_EQUAL_INT16(0, r.status);
    TEST_ASSERT_EQUAL_UINT16(4u, r.data_len);

    uint32_t alarm = 0;
    TEST_ASSERT_TRUE(protocore_focas_parse_alarm(r.data, r.data_len, &alarm));
    TEST_ASSERT_EQUAL_HEX32(0x00000100u, alarm);
}

// The status field is a signed short: 0xFFF0 is -16, not 65520.
void test_status_is_a_signed_return_code(void)
{
    uint8_t payload[FOCAS_RESP_HDR_LEN];
    memset(payload, 0, sizeof(payload));
    payload[6] = 0xFF;
    payload[7] = 0xF0;
    FocasResponse r;
    TEST_ASSERT_TRUE(protocore_focas_parse_response(payload, sizeof(payload), &r));
    TEST_ASSERT_EQUAL_INT16(-16, r.status);
    TEST_ASSERT_EQUAL_UINT16(0u, r.data_len);
}

// A response whose data-length field runs past the payload is refused, as is a frame whose type is
// not the command response.
void test_response_truncation_and_wrong_type_are_refused(void)
{
    uint8_t payload[FOCAS_RESP_HDR_LEN + 2];
    memset(payload, 0, sizeof(payload));
    payload[12] = 0x00;
    payload[13] = 0x08; // claims 8 data octets, only 2 follow
    FocasResponse r;
    TEST_ASSERT_FALSE(protocore_focas_parse_response(payload, sizeof(payload), &r));
    TEST_ASSERT_FALSE(protocore_focas_parse_response(payload, (size_t)FOCAS_RESP_HDR_LEN - 1, &r));

    // an open response (0x0102) is a valid frame but not a command response
    uint8_t buf[64];
    size_t n = protocore_focas_build_open(buf, sizeof(buf));
    buf[6] = 0x01;
    buf[7] = 0x02;
    FocasFrame f;
    TEST_ASSERT_TRUE(protocore_focas_parse_frame(buf, n, &f));
    TEST_ASSERT_FALSE(protocore_focas_parse_command_frame(buf, n, &r));
}

// ODBSYS: two u16 then five fixed-width ASCII fields, each exposed NUL-terminated.
void test_sysinfo_splits_the_fixed_width_ascii_fields(void)
{
    static const uint8_t DATA[FOCAS_SYSINFO_LEN] = {
        0x00, 0x02, // add_info
        0x00, 0x08, // max_axis
        '3',  '0',  // cnc_type
        ' ',  'M',  // mt_type
        'F',  '0',  // series
        '0',  '1',  //
        '0',  '0',  // version
        '0',  '7',  //
        '0',  '3'   // axes
    };
    FocasSysInfo si;
    TEST_ASSERT_TRUE(protocore_focas_parse_sysinfo(DATA, sizeof(DATA), &si));
    TEST_ASSERT_EQUAL_UINT16(2u, si.add_info);
    TEST_ASSERT_EQUAL_UINT16(8u, si.max_axis);
    TEST_ASSERT_EQUAL_STRING("30", si.cnc_type);
    TEST_ASSERT_EQUAL_STRING(" M", si.mt_type);
    TEST_ASSERT_EQUAL_STRING("F001", si.series);
    TEST_ASSERT_EQUAL_STRING("0007", si.version);
    TEST_ASSERT_EQUAL_STRING("03", si.axes);

    TEST_ASSERT_FALSE(protocore_focas_parse_sysinfo(DATA, sizeof(DATA) - 1, &si));
}

// The 8-octet value is `data / base^exp`: 12345 / 10^3 = 12.345 and 48 / 2^4 = 3, both derived from
// the definition rather than read back out of the decoder.
void test_value8_scales_by_base_and_exponent(void)
{
    static const uint8_t DEC[FOCAS_VALUE_LEN] = {0x00, 0x00, 0x30, 0x39, 0x00, 0x0A, 0x00, 0x03};
    FocasValue v;
    TEST_ASSERT_TRUE(protocore_focas_decode8(DEC, sizeof(DEC), &v));
    TEST_ASSERT_TRUE(v.valid);
    TEST_ASSERT_EQUAL_INT32(12345, v.data);
    TEST_ASSERT_EQUAL_UINT8(10u, v.base);
    TEST_ASSERT_EQUAL_UINT8(3u, v.exp);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 12.345f, protocore_focas_value_f(&v));

    static const uint8_t BIN[FOCAS_VALUE_LEN] = {0x00, 0x00, 0x00, 0x30, 0x00, 0x02, 0x00, 0x04};
    TEST_ASSERT_TRUE(protocore_focas_decode8(BIN, sizeof(BIN), &v));
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 3.0f, protocore_focas_value_f(&v));

    // data is signed: 0xFFFFFFFF is -1, so -1 / 10^1 = -0.1
    static const uint8_t NEG[FOCAS_VALUE_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x0A, 0x00, 0x01};
    TEST_ASSERT_TRUE(protocore_focas_decode8(NEG, sizeof(NEG), &v));
    TEST_ASSERT_EQUAL_INT32(-1, v.data);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, -0.1f, protocore_focas_value_f(&v));

    // exponent 0 divides by one
    static const uint8_t UNIT[FOCAS_VALUE_LEN] = {0x00, 0x00, 0x00, 0x07, 0x00, 0x0A, 0x00, 0x00};
    TEST_ASSERT_TRUE(protocore_focas_decode8(UNIT, sizeof(UNIT), &v));
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 7.0f, protocore_focas_value_f(&v));
}

// The 0xFFFF exponent sentinel and any base other than 2 or 10 mark the value unusable, and an
// unusable value reads as zero rather than as garbage.
void test_value8_sentinel_and_unknown_base_are_invalid(void)
{
    FocasValue v;
    static const uint8_t SENTINEL[FOCAS_VALUE_LEN] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x0A, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(protocore_focas_decode8(SENTINEL, sizeof(SENTINEL), &v));
    TEST_ASSERT_FALSE(v.valid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, protocore_focas_value_f(&v));

    static const uint8_t BASE8[FOCAS_VALUE_LEN] = {0x00, 0x00, 0x00, 0x64, 0x00, 0x08, 0x00, 0x00};
    TEST_ASSERT_FALSE(protocore_focas_decode8(BASE8, sizeof(BASE8), &v));
    TEST_ASSERT_FALSE(v.valid);

    // fewer than eight octets is not a value at all
    static const uint8_t SHORT[7] = {0};
    TEST_ASSERT_FALSE(protocore_focas_decode8(SHORT, sizeof(SHORT), &v));
}
