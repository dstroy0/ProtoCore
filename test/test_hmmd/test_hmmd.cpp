// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Waveshare HMMD mmWave radar codec (services/peripherals/hmmd): decoding a report frame
// (detection flag, distance, all 16 gate energies), rejecting malformed frames, the byte-by-byte
// stream reassembler (resync past noise, split feeds, absurd-length drop), the exact bytes of the
// command encoders, and ACK decoding. Pure - the UART pump is ESP32-only.

#include "services/peripherals/hmmd/hmmd.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// A report frame: target detected at 137cm, gate energies 100..115 (gate i -> 100 + i).
// len = 35 = detect(1) + distance(2) + 16 gates * 2.
static uint8_t REPORT[PC_HMMD_FRAME_MAX];

static void build_report(uint8_t detect, uint16_t dist, uint16_t gate0)
{
    memset(REPORT, 0, sizeof(REPORT));
    REPORT[0] = 0xF4;
    REPORT[1] = 0xF3;
    REPORT[2] = 0xF2;
    REPORT[3] = 0xF1;
    REPORT[4] = (uint8_t)(PC_HMMD_REPORT_LEN & 0xFF);
    REPORT[5] = (uint8_t)(PC_HMMD_REPORT_LEN >> 8);
    REPORT[6] = detect;
    REPORT[7] = (uint8_t)(dist & 0xFF);
    REPORT[8] = (uint8_t)(dist >> 8);
    for (int i = 0; i < PC_HMMD_GATES; i++)
    {
        uint16_t e = (uint16_t)(gate0 + i);
        REPORT[9 + 2 * i] = (uint8_t)(e & 0xFF);
        REPORT[10 + 2 * i] = (uint8_t)(e >> 8);
    }
    REPORT[41] = 0xF8;
    REPORT[42] = 0xF7;
    REPORT[43] = 0xF6;
    REPORT[44] = 0xF5;
}

void test_frame_geometry_is_self_consistent()
{
    // 4 header + 2 length + 35 payload + 4 footer == 45, the reference library's kMaxFrameLength.
    TEST_ASSERT_EQUAL_UINT32(45, (uint32_t)PC_HMMD_FRAME_MAX);
    TEST_ASSERT_EQUAL_UINT32(35, (uint32_t)PC_HMMD_REPORT_LEN);
    TEST_ASSERT_EQUAL_UINT32(PC_HMMD_FRAME_MAX, (uint32_t)(4 + 2 + PC_HMMD_REPORT_LEN + 4));
    TEST_ASSERT_EQUAL_UINT32(PC_HMMD_REPORT_LEN, (uint32_t)(1 + 2 + 2 * PC_HMMD_GATES));
}

void test_parse_report()
{
    build_report(0x01, 137, 100);
    HmmdReport r;
    TEST_ASSERT_TRUE(pc_hmmd_parse_report(REPORT, PC_HMMD_FRAME_MAX, &r));
    TEST_ASSERT_EQUAL_UINT8(1, r.detected);
    TEST_ASSERT_EQUAL_UINT16(137, r.distance_cm);
    TEST_ASSERT_EQUAL_UINT16(100, r.gate_energy[0]);
    TEST_ASSERT_EQUAL_UINT16(115, r.gate_energy[PC_HMMD_GATES - 1]);
    TEST_ASSERT_TRUE(pc_hmmd_present(&r));
    TEST_ASSERT_EQUAL_UINT16(137, pc_hmmd_distance_cm(&r));
}

void test_parse_report_not_detected()
{
    build_report(0x00, 400, 7);
    HmmdReport r;
    TEST_ASSERT_TRUE(pc_hmmd_parse_report(REPORT, PC_HMMD_FRAME_MAX, &r));
    TEST_ASSERT_EQUAL_UINT8(0, r.detected);
    TEST_ASSERT_FALSE(pc_hmmd_present(&r));
    // distance is meaningless with no target, so the helper reports 0 rather than stale range
    TEST_ASSERT_EQUAL_UINT16(0, pc_hmmd_distance_cm(&r));
    TEST_ASSERT_EQUAL_UINT16(400, r.distance_cm); // ...but the raw field is still decoded
    TEST_ASSERT_FALSE(pc_hmmd_present(nullptr));
    TEST_ASSERT_EQUAL_UINT16(0, pc_hmmd_distance_cm(nullptr));
}

void test_reject_malformed_report()
{
    HmmdReport r;
    uint8_t bad[PC_HMMD_FRAME_MAX];

    build_report(0x01, 100, 1);
    memcpy(bad, REPORT, sizeof(bad));
    bad[0] = 0x00; // bad header
    TEST_ASSERT_FALSE(pc_hmmd_parse_report(bad, sizeof(bad), &r));

    memcpy(bad, REPORT, sizeof(bad));
    bad[44] = 0x00; // bad footer
    TEST_ASSERT_FALSE(pc_hmmd_parse_report(bad, sizeof(bad), &r));

    memcpy(bad, REPORT, sizeof(bad));
    bad[4] = 0x22; // length field that is not the one report length
    TEST_ASSERT_FALSE(pc_hmmd_parse_report(bad, sizeof(bad), &r));

    TEST_ASSERT_FALSE(pc_hmmd_parse_report(REPORT, PC_HMMD_FRAME_MAX - 1, &r)); // short
    TEST_ASSERT_FALSE(pc_hmmd_parse_report(REPORT, PC_HMMD_FRAME_MAX + 1, &r)); // long
    TEST_ASSERT_FALSE(pc_hmmd_parse_report(nullptr, PC_HMMD_FRAME_MAX, &r));
    TEST_ASSERT_FALSE(pc_hmmd_parse_report(REPORT, PC_HMMD_FRAME_MAX, nullptr));
}

void test_stream_resync_and_split()
{
    HmmdStream s;
    pc_hmmd_stream_reset(&s);
    HmmdReport r;
    int reports = 0;

    // leading noise, including a false partial header (F4 then a non-F3)
    const uint8_t noise[] = {0x00, 0xFF, 0xF4, 0x11, 0xF4, 0xF3, 0x99};
    for (unsigned i = 0; i < sizeof(noise); i++)
    {
        if (pc_hmmd_stream_push(&s, noise[i], &r))
        {
            reports++;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, reports);

    build_report(0x01, 250, 10);
    for (unsigned i = 0; i < PC_HMMD_FRAME_MAX; i++)
    {
        if (pc_hmmd_stream_push(&s, REPORT[i], &r))
        {
            reports++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, reports);
    TEST_ASSERT_EQUAL_UINT16(250, r.distance_cm);

    // a second frame back-to-back proves the stream reset itself
    build_report(0x00, 99, 3);
    for (unsigned i = 0; i < PC_HMMD_FRAME_MAX; i++)
    {
        if (pc_hmmd_stream_push(&s, REPORT[i], &r))
        {
            reports++;
        }
    }
    TEST_ASSERT_EQUAL_INT(2, reports);
    TEST_ASSERT_EQUAL_UINT8(0, r.detected);
}

void test_stream_absurd_length_drops()
{
    HmmdStream s;
    pc_hmmd_stream_reset(&s);
    HmmdReport r;
    // header + a length larger than the frame buffer must drop and resync, then decode the next
    const uint8_t huge[] = {0xF4, 0xF3, 0xF2, 0xF1, 0xFF, 0xFF};
    for (unsigned i = 0; i < sizeof(huge); i++)
    {
        TEST_ASSERT_FALSE(pc_hmmd_stream_push(&s, huge[i], &r));
    }

    build_report(0x01, 42, 1);
    int reports = 0;
    for (unsigned i = 0; i < PC_HMMD_FRAME_MAX; i++)
    {
        if (pc_hmmd_stream_push(&s, REPORT[i], &r))
        {
            reports++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, reports);
    TEST_ASSERT_EQUAL_UINT16(42, r.distance_cm);
}

void test_stream_push_rejects_null_out()
{
    HmmdStream s;
    pc_hmmd_stream_reset(&s);
    // s is valid but out is null: must be refused before touching s->phase, not just when s itself
    // is null (that half of the guard is covered by test_host_binding_stubs already).
    TEST_ASSERT_FALSE(pc_hmmd_stream_push(&s, 0xF4, nullptr));
}

void test_stream_header_resync_on_repeated_lead_byte()
{
    HmmdStream s;
    pc_hmmd_stream_reset(&s);
    HmmdReport r;

    // First 0xF4 starts a header match (hdr_match -> 1). A second 0xF4 is not the expected HDR[1]
    // (0xF3), but it IS HDR[0] again, so the resync must restart the match at [1] (buf[0]
    // re-primed) rather than dropping all the way back to [0].
    TEST_ASSERT_FALSE(pc_hmmd_stream_push(&s, 0xF4, &r));
    TEST_ASSERT_FALSE(pc_hmmd_stream_push(&s, 0xF4, &r));

    // Complete the header from that restarted position and prove the resync was correct by
    // decoding a whole valid frame afterward.
    const uint8_t rest_hdr[3] = {0xF3, 0xF2, 0xF1};
    for (unsigned i = 0; i < 3; i++)
    {
        TEST_ASSERT_FALSE(pc_hmmd_stream_push(&s, rest_hdr[i], &r));
    }

    build_report(0x01, 55, 20);
    int reports = 0;
    for (unsigned i = 4; i < PC_HMMD_FRAME_MAX; i++)
    {
        if (pc_hmmd_stream_push(&s, REPORT[i], &r))
        {
            reports++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, reports);
    TEST_ASSERT_EQUAL_UINT16(55, r.distance_cm);
}

void test_command_encoders()
{
    uint8_t f[32];

    // open command mode: word 0x00FF, value 0x0001 -> len 4
    static const uint8_t open_f[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF,
                                       0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_UINT32(14, (uint32_t)pc_hmmd_cmd_open(f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(open_f, f, 14);

    // close command mode: word 0x00FE, no value -> len 2
    static const uint8_t close_f[12] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_UINT32(12, (uint32_t)pc_hmmd_cmd_close(f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(close_f, f, 12);

    // firmware version: word 0x0000
    static const uint8_t fw_f[12] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_UINT32(12, (uint32_t)pc_hmmd_cmd_read_firmware(f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fw_f, f, 12);

    // serial number: word 0x0011
    TEST_ASSERT_EQUAL_UINT32(12, (uint32_t)pc_hmmd_cmd_read_serial(f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(0x11, f[6]);
    TEST_ASSERT_EQUAL_UINT8(0x00, f[7]);

    // parameter config: word 0x0008
    TEST_ASSERT_EQUAL_UINT32(12, (uint32_t)pc_hmmd_cmd_read_config(f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(0x08, f[6]);

    // register read: word 0x0002 with a caller-supplied selector
    const uint8_t sel[2] = {0x34, 0x12};
    TEST_ASSERT_EQUAL_UINT32(14, (uint32_t)pc_hmmd_cmd_read_register(f, sizeof(f), sel, sizeof(sel)));
    TEST_ASSERT_EQUAL_UINT8(0x04, f[4]); // len = word(2) + value(2)
    TEST_ASSERT_EQUAL_UINT8(0x02, f[6]);
    TEST_ASSERT_EQUAL_UINT8(0x34, f[8]);
    TEST_ASSERT_EQUAL_UINT8(0x12, f[9]);
}

void test_command_encoder_guards()
{
    uint8_t f[32];
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_hmmd_cmd_open(f, 13));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_hmmd_cmd_close(f, 11));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_hmmd_cmd_open(nullptr, sizeof(f)));
    // a non-zero value length with a null value pointer must be refused, not read
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_hmmd_cmd_build(f, sizeof(f), 0x0002, nullptr, 2));
}

void test_ack_decoding()
{
    HmmdAck a;
    // ACK to read-config: word 0x0108 (reply convention), then two data octets
    static const uint8_t ack[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0x08, 0x01, 0xAB, 0xCD, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_TRUE(pc_hmmd_parse_ack(ack, sizeof(ack), &a));
    TEST_ASSERT_EQUAL_UINT16(0x0108, a.command);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)a.payload_len);
    TEST_ASSERT_EQUAL_UINT8(0xAB, a.payload[0]);
    TEST_ASSERT_TRUE(pc_hmmd_ack_matches(&a, 0x0008)); // matches on the low octet
    TEST_ASSERT_FALSE(pc_hmmd_ack_matches(&a, 0x0011));
    TEST_ASSERT_FALSE(pc_hmmd_ack_matches(nullptr, 0x0008));

    // an ACK carrying only the command word has no payload
    static const uint8_t bare[12] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x01, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_TRUE(pc_hmmd_parse_ack(bare, sizeof(bare), &a));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)a.payload_len);
    TEST_ASSERT_NULL(a.payload);
}

void test_ack_rejects_malformed()
{
    HmmdAck a;
    static const uint8_t good[12] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x01, 0x04, 0x03, 0x02, 0x01};
    uint8_t bad[12];

    TEST_ASSERT_FALSE(pc_hmmd_parse_ack(nullptr, 12, &a));
    TEST_ASSERT_FALSE(pc_hmmd_parse_ack(good, 12, nullptr));
    TEST_ASSERT_FALSE(pc_hmmd_parse_ack(good, 11, &a)); // under the minimum

    memcpy(bad, good, 12);
    bad[0] = 0xF4; // report header, not a command header
    TEST_ASSERT_FALSE(pc_hmmd_parse_ack(bad, 12, &a));

    memcpy(bad, good, 12);
    bad[11] = 0xFF; // corrupt footer
    TEST_ASSERT_FALSE(pc_hmmd_parse_ack(bad, 12, &a));

    memcpy(bad, good, 12);
    bad[4] = 0x04; // declared length disagrees with the buffer
    TEST_ASSERT_FALSE(pc_hmmd_parse_ack(bad, 12, &a));

    memcpy(bad, good, 12);
    bad[4] = 0x01; // shorter than the command word itself
    TEST_ASSERT_FALSE(pc_hmmd_parse_ack(bad, 12, &a));
}

void test_host_binding_stubs()
{
    TEST_ASSERT_FALSE(pc_hmmd_begin(16, 17));
    TEST_ASSERT_FALSE(pc_hmmd_poll());
    TEST_ASSERT_NULL(pc_hmmd_last());
    pc_hmmd_stream_reset(nullptr); // must not fault
    HmmdReport r;
    TEST_ASSERT_FALSE(pc_hmmd_stream_push(nullptr, 0, &r));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_geometry_is_self_consistent);
    RUN_TEST(test_parse_report);
    RUN_TEST(test_parse_report_not_detected);
    RUN_TEST(test_reject_malformed_report);
    RUN_TEST(test_stream_resync_and_split);
    RUN_TEST(test_stream_absurd_length_drops);
    RUN_TEST(test_stream_push_rejects_null_out);
    RUN_TEST(test_stream_header_resync_on_repeated_lead_byte);
    RUN_TEST(test_command_encoders);
    RUN_TEST(test_command_encoder_guards);
    RUN_TEST(test_ack_decoding);
    RUN_TEST(test_ack_rejects_malformed);
    RUN_TEST(test_host_binding_stubs);
    return UNITY_END();
}
