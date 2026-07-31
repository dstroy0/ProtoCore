// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the DeviceNet link-adaptation codec (services/fieldbus/devicenet):
// the 4-group 11-bit CAN identifier encode/decode, the explicit-message header + fragmentation
// octets, a single-frame explicit-message build, and the fragmentation reassembler - all pure (no
// heap, no bus). Same shape as performance_benching/device/modbus: a pure protocol codec with no hardware involved,
// so every call here exercises the real production code path. The physical CAN transaction (ESP32
// TWAI peripheral or an MCP2515 over SPI) is explicitly out of scope - this rig has no CAN
// transceiver attached, and pc_devicenet_* never touches the bus itself, only the CanFrame struct
// (shared_primitives/can.h) and its own reassembly buffer.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/devicenet -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/devicenet/devicenet.h"
#include <Arduino.h>

static void devicenet_bench_task(void *)
{
    // Group 2 unconnected explicit request, mac 0x21 (test_devicenet.cpp: test_id_group2).
    static const uint32_t decode_test_id = 0x400u | (0x21u << 3) | DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ;
    // A tiny CIP get-attribute-ish body (test_devicenet.cpp: test_build_explicit_single_frame).
    static const uint8_t cip[3] = {0x0E, 0x20, 0x01};

    // Three-frame fragmentation roundtrip, 14 octets of reassembled data
    // (test_devicenet.cpp: test_frag_reassembly_roundtrip).
    static const uint8_t f0[8] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_FIRST, 0), 1, 2, 3, 4, 5, 6};
    static const uint8_t f1[8] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_MIDDLE, 1), 7, 8, 9, 10, 11, 12};
    static const uint8_t f2[4] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_LAST, 2), 13, 14};

    for (;;)
    {
        Serial.printf("DB ==== devicenet device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());

        volatile bool sinkb = false;
        volatile uint8_t sink8 = 0;
        uint32_t id = 0;
        DeviceNetId d;
        CanFrame frame;
        DeviceNetFragRx rx;

        DBENCH_OP("pc_devicenet_encode_id", 100000,
                  sinkb |= pc_devicenet_encode_id(&id, DeviceNetGroup::DEVICENET_GROUP_2,
                                                  DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ, 0x21));
        DBENCH_OP("pc_devicenet_decode_id", 100000, sinkb |= pc_devicenet_decode_id(decode_test_id, &d));
        DBENCH_OP("pc_devicenet_msg_header", 200000, sink8 += pc_devicenet_msg_header(true, false, 0x21));
        DBENCH_OP("pc_devicenet_frag_octet", 200000, sink8 += pc_devicenet_frag_octet(DEVICENET_FRAG_LAST, 5));
        DBENCH_OP("pc_devicenet_build_explicit", 100000,
                  sinkb |= pc_devicenet_build_explicit(&frame, DeviceNetGroup::DEVICENET_GROUP_2,
                                                       DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ, 0x21, cip, 3));
        // 3-frame reassembly per iteration: reset + FIRST + MIDDLE + LAST, 14 octets of payload data.
        DBENCH_BULK("pc_devicenet_frag_feed (3-frame reasm)", 50000, 14,
                    (pc_devicenet_frag_reset(&rx), pc_devicenet_frag_feed(&rx, f0, sizeof(f0)),
                     pc_devicenet_frag_feed(&rx, f1, sizeof(f1)), pc_devicenet_frag_feed(&rx, f2, sizeof(f2))));

        (void)sinkb;
        (void)sink8;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: devicenet device microbench");
    xTaskCreatePinnedToCore(devicenet_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
