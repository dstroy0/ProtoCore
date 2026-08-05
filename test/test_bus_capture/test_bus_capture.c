// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the CAN listen-only capture framing (server/signaling/bus_capture): can_to_socketcan()
// building the 16-byte Linux SocketCAN frame (big-endian can_id, EFF / RTR flags, length, data)
// and the DLT_CAN_SOCKETCAN libpcap link type. Pure host tests.

#include "server/signaling/bus_capture.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_standard_data_frame()
{
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x123;
    f.extended = PROTO_FALSE;
    f.rtr = PROTO_FALSE;
    f.dlc = 8;
    for (int i = 0; i < 8; i++)
    {
        f.data[i] = (uint8_t)(0x10 + i);
    }

    uint8_t out[PC_SOCKETCAN_FRAME_LEN];
    TEST_ASSERT_EQUAL_UINT(PC_SOCKETCAN_FRAME_LEN, can_to_socketcan(&f, out, sizeof(out)));
    // can_id 0x00000123, big-endian, no flags.
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x23, out[3]);
    TEST_ASSERT_EQUAL_HEX8(8, out[4]); // length
    TEST_ASSERT_EQUAL_HEX8(0, out[5]);
    TEST_ASSERT_EQUAL_MEMORY(f.data, out + 8, 8);
}

void test_extended_id_sets_eff()
{
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x18FEF100; // a J1939-style 29-bit id
    f.extended = PROTO_TRUE;
    f.dlc = 2;
    f.data[0] = 0xAA;
    f.data[1] = 0xBB;

    uint8_t out[PC_SOCKETCAN_FRAME_LEN];
    can_to_socketcan(&f, out, sizeof(out));
    // can_id = id | EFF (0x80000000) = 0x98FEF100, big-endian.
    TEST_ASSERT_EQUAL_HEX8(0x98, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xF1, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[3]);
    TEST_ASSERT_EQUAL_HEX8(2, out[4]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[8]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, out[9]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[10]); // beyond dlc is zeroed
}

void test_rtr_flag_and_no_data()
{
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x7FF;
    f.rtr = PROTO_TRUE;
    f.dlc = 4;
    f.data[0] = 0xFF; // must not appear (RTR frames carry no data)

    uint8_t out[PC_SOCKETCAN_FRAME_LEN];
    can_to_socketcan(&f, out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8(0x40, out[0]); // RTR flag (0x40000000) in the top byte
    TEST_ASSERT_EQUAL_HEX8(0x07, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[3]);
    for (int i = 0; i < 8; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0, out[8 + i]);
    }
}

void test_masks_and_bounds()
{
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0xFFFFFFFF; // over-wide; a standard frame must mask to 11 bits
    f.extended = PROTO_FALSE;
    f.dlc = 20; // over-long; must clamp to 8
    uint8_t out[PC_SOCKETCAN_FRAME_LEN];
    can_to_socketcan(&f, out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]); // no EFF, id masked to 0x7FF
    TEST_ASSERT_EQUAL_HEX8(0x07, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[3]);
    TEST_ASSERT_EQUAL_HEX8(8, out[4]); // clamped

    uint8_t tiny[8];
    TEST_ASSERT_EQUAL_UINT(0, can_to_socketcan(&f, tiny, sizeof(tiny))); // too small
    TEST_ASSERT_EQUAL_UINT(0, can_to_socketcan(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, can_to_socketcan(&f, NULL, sizeof(out))); // null out buffer
}

void test_pcap_can_linktype()
{
    uint8_t g[PC_PCAP_GLOBAL_HDR_LEN];
    TEST_ASSERT_EQUAL_UINT(PC_PCAP_GLOBAL_HDR_LEN, pc_pcap_global_header(g, sizeof(g), PC_DLT_CAN_SOCKETCAN));
    TEST_ASSERT_EQUAL_HEX8(227, g[20]); // DLT_CAN_SOCKETCAN
}

void test_pcap_global_header_bounds()
{
    uint8_t g[PC_PCAP_GLOBAL_HDR_LEN];
    TEST_ASSERT_EQUAL_UINT(0, pc_pcap_global_header(NULL, sizeof(g), PC_DLT_CAN_SOCKETCAN)); // null out
    uint8_t tiny[8];
    TEST_ASSERT_EQUAL_UINT(0, pc_pcap_global_header(tiny, sizeof(tiny), PC_DLT_CAN_SOCKETCAN)); // too small
}

void test_pcap_record_header_bounds()
{
    uint8_t r[PC_PCAP_REC_HDR_LEN];
    TEST_ASSERT_EQUAL_UINT(PC_PCAP_REC_HDR_LEN, pc_pcap_record_header(r, sizeof(r), 1, 2, 3, 4)); // valid
    TEST_ASSERT_EQUAL_UINT(0, pc_pcap_record_header(NULL, sizeof(r), 1, 2, 3, 4));                // null out
    uint8_t tiny[8];
    TEST_ASSERT_EQUAL_UINT(0, pc_pcap_record_header(tiny, sizeof(tiny), 1, 2, 3, 4)); // too small
}

static void bus_sink_noop(const CanFrame *)
{
}

void test_host_twai_stubs_fail_closed()
{
    // On host there is no TWAI controller: begin fails closed and poll/end are safe no-ops.
    TEST_ASSERT_FALSE(bus_capture_begin(5, 4, 500000, bus_sink_noop));
    bus_capture_poll(); // no-op, must not crash
    bus_capture_end();  // no-op, must not crash
}

void test_host_can_stubs()
{
    // Host build: no TWAI/CAN peripheral. begin() fails; poll/end are no-ops.
    TEST_ASSERT_FALSE(bus_capture_begin(4, 5, 500000, NULL));
    bus_capture_poll();
    bus_capture_end();
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_standard_data_frame);
    RUN_TEST(test_extended_id_sets_eff);
    RUN_TEST(test_rtr_flag_and_no_data);
    RUN_TEST(test_masks_and_bounds);
    RUN_TEST(test_pcap_can_linktype);
    RUN_TEST(test_pcap_global_header_bounds);
    RUN_TEST(test_pcap_record_header_bounds);
    RUN_TEST(test_host_twai_stubs_fail_closed);
    RUN_TEST(test_host_can_stubs);
    return UNITY_END();
}
