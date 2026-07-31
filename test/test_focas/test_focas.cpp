// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the FANUC FOCAS Ethernet codec (services/machine_tool/focas): the request builders and the
// response parsers. Big-endian, 10-octet frame envelope; layout reverse-engineered vs pyfanuc.
// Pure host tests.

#include "services/machine_tool/focas/focas.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// Open request: magic + version 1 + FTYPE_OPN_REQU 0x0101 + length 2 + FRAME_DST 0x0002.
void test_build_open()
{
    uint8_t buf[32];
    size_t n = pc_focas_build_open(buf, sizeof(buf));
    const uint8_t expect[] = {0xA0, 0xA0, 0xA0, 0xA0, 0x00, 0x01, 0x01, 0x01, 0x00, 0x02, 0x00, 0x02};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_size_t(12, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// Close request: FTYPE_CLS_REQU 0x0201, empty payload.
void test_build_close()
{
    uint8_t buf[32];
    size_t n = pc_focas_build_close(buf, sizeof(buf));
    const uint8_t expect[] = {0xA0, 0xA0, 0xA0, 0xA0, 0x00, 0x01, 0x02, 0x01, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// SysInfo (1/1/0x18): command frame, selector + five zero args - byte-exact.
void test_build_sysinfo()
{
    uint8_t buf[64];
    size_t n = pc_focas_build_sysinfo(buf, sizeof(buf));
    const uint8_t expect[] = {
        0xA0, 0xA0, 0xA0, 0xA0,             // magic
        0x00, 0x01,                         // version 1
        0x21, 0x01,                         // FTYPE_VAR_REQU
        0x00, 0x1A,                         // payload length 26
        0x00, 0x01, 0x00, 0x01, 0x00, 0x18, // selector 1/1/0x18
        0x00, 0x00, 0x00, 0x00,             // v1
        0x00, 0x00, 0x00, 0x00,             // v2
        0x00, 0x00, 0x00, 0x00,             // v3
        0x00, 0x00, 0x00, 0x00,             // v4
        0x00, 0x00, 0x00, 0x00              // v5
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_size_t(36, n); // 10 envelope + 26 body
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// Read position: kind = absolute (v1=4), axis 0 (v2=0). Args are big-endian i32.
void test_build_read_position()
{
    uint8_t buf[64];
    size_t n = pc_focas_build_read_position(buf, sizeof(buf), FocasPosKind::absolute, 0);
    TEST_ASSERT_EQUAL_size_t(36, n);
    TEST_ASSERT_EQUAL_HEX8(0x21, buf[6]);  // command frame
    TEST_ASSERT_EQUAL_HEX8(0x26, buf[15]); // c3 = 0x26
    // v1 (position kind) at payload offset 6 -> buf offset 16.
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[16]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[17]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[18]);
    TEST_ASSERT_EQUAL_HEX8(0x04, buf[19]); // absolute = 4
}

// Read CNC parameter range: first/last/axis map to v1/v2/v3 as big-endian i32.
void test_build_read_param()
{
    uint8_t buf[64];
    size_t n = pc_focas_build_read_param(buf, sizeof(buf), 6510, 6510, 1);
    TEST_ASSERT_EQUAL_size_t(36, n);
    TEST_ASSERT_EQUAL_HEX8(0x0E, buf[15]); // c3 = 0x0e
    // v1 = 6510 = 0x0000196E at buf offset 16.
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[16]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[17]);
    TEST_ASSERT_EQUAL_HEX8(0x19, buf[18]);
    TEST_ASSERT_EQUAL_HEX8(0x6E, buf[19]);
    // v2 = 6510 at buf offset 20.
    TEST_ASSERT_EQUAL_HEX8(0x19, buf[22]);
    TEST_ASSERT_EQUAL_HEX8(0x6E, buf[23]);
    // v3 = axis 1 at buf offset 24.
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[27]);
}

// The generic builder appends trailing extra data after the five arguments.
void test_build_request_extra()
{
    uint8_t buf[64];
    const uint8_t extra[] = {0xAA, 0xBB, 0xCC};
    size_t n = pc_focas_build_request(buf, sizeof(buf), FocasCommand::set_macro, 500, 0, 0, 0, 0, extra, sizeof(extra));
    TEST_ASSERT_EQUAL_size_t(10 + 26 + 3, n);
    // payload length field = 26 + 3 = 29.
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[8]);
    TEST_ASSERT_EQUAL_HEX8(0x1D, buf[9]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[36]); // extra begins right after the 26-octet body
    TEST_ASSERT_EQUAL_HEX8(0xCC, buf[38]);
}

// Parse a SysInfo command response: envelope + echoed selector + status + ODBSYS data.
void test_parse_sysinfo_response()
{
    const uint8_t frame[] = {
        0xA0, 0xA0, 0xA0, 0xA0,             // magic
        0x00, 0x01,                         // version
        0x21, 0x02,                         // FTYPE_VAR_RESP
        0x00, 0x20,                         // payload length 32
        0x00, 0x01, 0x00, 0x01, 0x00, 0x18, // echoed selector
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // status (return code 0)
        0x00, 0x12,                         // data length 18
        0x00, 0x00,                         // addinfo 0
        0x00, 0x08,                         // maxaxis 8
        0x33, 0x30,                         // cnctype "30"
        0x20, 0x4D,                         // mttype " M"
        0x47, 0x30, 0x31, 0x41,             // series "G01A"
        0x32, 0x37, 0x2E, 0x31,             // version "27.1"
        0x30, 0x33                          // axes "03"
    };
    FocasResponse resp;
    TEST_ASSERT_TRUE(pc_focas_parse_command_frame(frame, sizeof(frame), &resp));
    TEST_ASSERT_EQUAL_UINT16(1, resp.c1);
    TEST_ASSERT_EQUAL_UINT16(1, resp.c2);
    TEST_ASSERT_EQUAL_UINT16(0x18, resp.c3);
    TEST_ASSERT_EQUAL_INT16(0, resp.status);
    TEST_ASSERT_EQUAL_UINT16(18, resp.data_len);

    FocasSysInfo si;
    TEST_ASSERT_TRUE(pc_focas_parse_sysinfo(resp.data, resp.data_len, &si));
    TEST_ASSERT_EQUAL_UINT16(0, si.add_info);
    TEST_ASSERT_EQUAL_UINT16(8, si.max_axis);
    TEST_ASSERT_EQUAL_STRING("30", si.cnc_type);
    TEST_ASSERT_EQUAL_STRING(" M", si.mt_type);
    TEST_ASSERT_EQUAL_STRING("G01A", si.series);
    TEST_ASSERT_EQUAL_STRING("27.1", si.version);
    TEST_ASSERT_EQUAL_STRING("03", si.axes);
}

// Parse an alarm response: a single big-endian u32 bitmask, and a non-zero FOCAS return code.
void test_parse_alarm_and_status()
{
    const uint8_t frame[] = {
        0xA0, 0xA0, 0xA0, 0xA0, 0x00, 0x01, 0x21, 0x02, 0x00, 0x12, // envelope, payload 18
        0x00, 0x01, 0x00, 0x01, 0x00, 0x1A,                         // echoed selector 1/1/0x1a
        0xFF, 0xF6, 0x00, 0x00, 0x00, 0x00,                         // status = -10 (EW_NUMBER)
        0x00, 0x04,                                                 // data length 4
        0x00, 0x00, 0x00, 0x10                                      // alarm bitmask, bit 4
    };
    FocasResponse resp;
    TEST_ASSERT_TRUE(pc_focas_parse_command_frame(frame, sizeof(frame), &resp));
    TEST_ASSERT_EQUAL_UINT16(0x1A, resp.c3);
    TEST_ASSERT_EQUAL_INT16(-10, resp.status); // signed return code

    uint32_t alarm = 0;
    TEST_ASSERT_TRUE(pc_focas_parse_alarm(resp.data, resp.data_len, &alarm));
    TEST_ASSERT_EQUAL_HEX32(0x00000010, alarm);
}

// FANUC 8-octet value: data / base^exp, with the 0xFFFF "no value" sentinel.
void test_decode8_value()
{
    // 123.456 mm = 123456 / 10^3.
    const uint8_t v[] = {0x00, 0x01, 0xE2, 0x40, 0x00, 0x0A, 0x00, 0x03};
    FocasValue fv;
    TEST_ASSERT_TRUE(pc_focas_decode8(v, sizeof(v), &fv));
    TEST_ASSERT_TRUE(fv.valid);
    TEST_ASSERT_EQUAL_INT32(123456, fv.data);
    TEST_ASSERT_EQUAL_UINT8(10, fv.base);
    TEST_ASSERT_EQUAL_UINT8(3, fv.exp);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 123.456f, pc_focas_value_f(&fv));

    // Negative: -5.00 = -500 / 10^2.
    const uint8_t neg[] = {0xFF, 0xFF, 0xFE, 0x0C, 0x00, 0x0A, 0x00, 0x02};
    FocasValue nv;
    TEST_ASSERT_TRUE(pc_focas_decode8(neg, sizeof(neg), &nv));
    TEST_ASSERT_EQUAL_INT32(-500, nv.data);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, -5.0f, pc_focas_value_f(&nv));

    // 0xFFFF sentinel in octets 6-7 -> no value.
    const uint8_t none[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0xFF, 0xFF};
    FocasValue none_v;
    TEST_ASSERT_FALSE(pc_focas_decode8(none, sizeof(none), &none_v));
    TEST_ASSERT_FALSE(none_v.valid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pc_focas_value_f(&none_v));
}

void test_build_overflow_fails_closed()
{
    uint8_t tiny[8]; // open needs 12
    TEST_ASSERT_EQUAL_size_t(0, pc_focas_build_open(tiny, sizeof(tiny)));
    uint8_t small[20]; // sysinfo needs 36
    TEST_ASSERT_EQUAL_size_t(0, pc_focas_build_sysinfo(small, sizeof(small)));
}

void test_parse_guards()
{
    FocasFrame f;
    TEST_ASSERT_FALSE(pc_focas_parse_frame(nullptr, 32, &f));
    const uint8_t shortbuf[6] = {0xA0, 0xA0, 0xA0, 0xA0, 0x00, 0x01};
    TEST_ASSERT_FALSE(pc_focas_parse_frame(shortbuf, sizeof(shortbuf), &f)); // < 10

    // Bad magic.
    uint8_t badmagic[FOCAS_FRAME_HDR_LEN] = {0};
    badmagic[0] = 0xA1;
    TEST_ASSERT_FALSE(pc_focas_parse_frame(badmagic, sizeof(badmagic), &f));

    // Envelope length promises more payload than the buffer holds.
    const uint8_t liar[] = {0xA0, 0xA0, 0xA0, 0xA0, 0x00, 0x01, 0x21, 0x02, 0x00, 0xFF};
    TEST_ASSERT_FALSE(pc_focas_parse_frame(liar, sizeof(liar), &f));

    // A valid frame of the wrong type is rejected by the command-frame convenience parser.
    uint8_t openresp[] = {0xA0, 0xA0, 0xA0, 0xA0, 0x00, 0x01, 0x01, 0x02, 0x00, 0x00};
    FocasResponse r;
    TEST_ASSERT_TRUE(pc_focas_parse_frame(openresp, sizeof(openresp), &f));
    TEST_ASSERT_TRUE(f.type == FocasFrameType::open_resp);
    TEST_ASSERT_FALSE(pc_focas_parse_command_frame(openresp, sizeof(openresp), &r));

    // Response claims more data than the payload carries.
    const uint8_t badlen[] = {0x00, 0x01, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xAA};
    FocasResponse r2;
    TEST_ASSERT_FALSE(pc_focas_parse_response(badlen, sizeof(badlen), &r2)); // says 0x40, 1 follows
}

// The remaining typed builders share the generic request path; each pins its own selector.
void test_build_remaining_selectors()
{
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_size_t(36, pc_focas_build_read_alarm(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0x1A, buf[15]); // read_alarm c3 = 0x1a
    TEST_ASSERT_EQUAL_HEX8(0x21, buf[6]);  // command frame

    TEST_ASSERT_EQUAL_size_t(36, pc_focas_build_read_macro(buf, sizeof(buf), 100, 199));
    TEST_ASSERT_EQUAL_HEX8(0x15, buf[15]); // read_macro c3 = 0x15
    TEST_ASSERT_EQUAL_HEX8(0x64, buf[19]); // v1 = first = 100
    TEST_ASSERT_EQUAL_HEX8(0xC7, buf[23]); // v2 = last = 199

    TEST_ASSERT_EQUAL_size_t(36, pc_focas_build_read_feedrate(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0x24, buf[15]); // read_feedrate c3 = 0x24

    TEST_ASSERT_EQUAL_size_t(36, pc_focas_build_read_spindle(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0x25, buf[15]); // read_spindle c3 = 0x25
}

void test_build_request_guards()
{
    uint8_t buf[64];
    const uint8_t extra[] = {0xAA};
    // a declared extra length with no extra pointer
    TEST_ASSERT_EQUAL_size_t(
        0, pc_focas_build_request(buf, sizeof(buf), FocasCommand::set_macro, 0, 0, 0, 0, 0, nullptr, 1));
    // extra so long that the 16-bit payload-length field could not hold body + extra
    TEST_ASSERT_EQUAL_size_t(0, pc_focas_build_request(buf, sizeof(buf), FocasCommand::set_macro, 0, 0, 0, 0, 0, extra,
                                                       (size_t)0xFFFF - FOCAS_REQ_BODY_LEN + 1));
    // null destination
    TEST_ASSERT_EQUAL_size_t(0, pc_focas_build_open(nullptr, 32));
    TEST_ASSERT_EQUAL_size_t(0, pc_focas_build_close(nullptr, 32));
}

void test_parse_frame_rejects_each_magic_octet()
{
    // All four magic octets are checked independently.
    FocasFrame f;
    uint8_t frame[FOCAS_FRAME_HDR_LEN] = {0xA0, 0xA0, 0xA0, 0xA0, 0x00, 0x01, 0x21, 0x02, 0x00, 0x00};
    TEST_ASSERT_TRUE(pc_focas_parse_frame(frame, sizeof(frame), &f)); // control: intact magic
    for (int i = 0; i < 4; i++)
    {
        uint8_t bad[FOCAS_FRAME_HDR_LEN];
        memcpy(bad, frame, sizeof(frame));
        bad[i] = 0x00;
        TEST_ASSERT_FALSE(pc_focas_parse_frame(bad, sizeof(bad), &f));
    }
    TEST_ASSERT_FALSE(pc_focas_parse_frame(frame, sizeof(frame), nullptr)); // null out
    // a malformed envelope also fails the command-frame convenience wrapper
    FocasResponse r;
    uint8_t badmagic[FOCAS_FRAME_HDR_LEN];
    memcpy(badmagic, frame, sizeof(frame));
    badmagic[0] = 0x00;
    TEST_ASSERT_FALSE(pc_focas_parse_command_frame(badmagic, sizeof(badmagic), &r));
}

void test_parser_null_and_short_guards()
{
    // Every parser refuses a null input, a null out, and a buffer shorter than its fixed record.
    const uint8_t bytes[32] = {0};
    FocasResponse r;
    TEST_ASSERT_FALSE(pc_focas_parse_response(nullptr, FOCAS_RESP_HDR_LEN, &r));
    TEST_ASSERT_FALSE(pc_focas_parse_response(bytes, FOCAS_RESP_HDR_LEN, nullptr));
    TEST_ASSERT_FALSE(pc_focas_parse_response(bytes, FOCAS_RESP_HDR_LEN - 1, &r));
    TEST_ASSERT_TRUE(pc_focas_parse_response(bytes, FOCAS_RESP_HDR_LEN, &r)); // exact fit is enough

    FocasSysInfo si;
    TEST_ASSERT_FALSE(pc_focas_parse_sysinfo(nullptr, FOCAS_SYSINFO_LEN, &si));
    TEST_ASSERT_FALSE(pc_focas_parse_sysinfo(bytes, FOCAS_SYSINFO_LEN, nullptr));
    TEST_ASSERT_FALSE(pc_focas_parse_sysinfo(bytes, FOCAS_SYSINFO_LEN - 1, &si));

    uint32_t alarm = 0xDEAD;
    TEST_ASSERT_FALSE(pc_focas_parse_alarm(nullptr, 4, &alarm));
    TEST_ASSERT_FALSE(pc_focas_parse_alarm(bytes, 4, nullptr));
    TEST_ASSERT_FALSE(pc_focas_parse_alarm(bytes, 3, &alarm));
    TEST_ASSERT_EQUAL_HEX32(0xDEAD, alarm); // untouched by the rejected calls

    FocasValue fv;
    TEST_ASSERT_FALSE(pc_focas_decode8(nullptr, FOCAS_VALUE_LEN, &fv));
    TEST_ASSERT_FALSE(pc_focas_decode8(bytes, FOCAS_VALUE_LEN, nullptr));
    TEST_ASSERT_FALSE(pc_focas_decode8(bytes, FOCAS_VALUE_LEN - 1, &fv));
}

void test_decode8_base_and_sentinel_edges()
{
    // Only base 2 and base 10 are decimal-scaled, and the "no value" sentinel needs BOTH octets
    // 6 and 7 to be 0xFF.
    FocasValue fv;
    const uint8_t base2[] = {0x00, 0x00, 0x00, 0x0C, 0x00, 0x02, 0x00, 0x02}; // 12 / 2^2
    TEST_ASSERT_TRUE(pc_focas_decode8(base2, sizeof(base2), &fv));
    TEST_ASSERT_EQUAL_UINT8(2, fv.base);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 3.0f, pc_focas_value_f(&fv));

    const uint8_t base16[] = {0x00, 0x00, 0x00, 0x0C, 0x00, 0x10, 0x00, 0x02}; // base 16: unsupported
    TEST_ASSERT_FALSE(pc_focas_decode8(base16, sizeof(base16), &fv));
    TEST_ASSERT_FALSE(fv.valid);

    // octet 6 is 0xFF but octet 7 is not -> not the sentinel, still a real value
    const uint8_t not_sentinel[] = {0x00, 0x00, 0x00, 0x64, 0x00, 0x0A, 0xFF, 0x01};
    TEST_ASSERT_TRUE(pc_focas_decode8(not_sentinel, sizeof(not_sentinel), &fv));
    TEST_ASSERT_EQUAL_UINT8(1, fv.exp);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 10.0f, pc_focas_value_f(&fv));

    TEST_ASSERT_EQUAL_FLOAT(0.0f, pc_focas_value_f(nullptr)); // no value at all
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_open);
    RUN_TEST(test_build_close);
    RUN_TEST(test_build_sysinfo);
    RUN_TEST(test_build_read_position);
    RUN_TEST(test_build_read_param);
    RUN_TEST(test_build_request_extra);
    RUN_TEST(test_parse_sysinfo_response);
    RUN_TEST(test_parse_alarm_and_status);
    RUN_TEST(test_decode8_value);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_parse_guards);
    RUN_TEST(test_build_remaining_selectors);
    RUN_TEST(test_build_request_guards);
    RUN_TEST(test_parse_frame_rejects_each_magic_octet);
    RUN_TEST(test_parser_null_and_short_guards);
    RUN_TEST(test_decode8_base_and_sentinel_edges);
    return UNITY_END();
}
