// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CAN listen-only capture framing (services/system/bus_capture):
// can_to_socketcan() building the 16-byte Linux SocketCAN frame (big-endian can_id, EFF/RTR flags,
// length, data) for a standard data frame, an extended (29-bit) id frame, and an RTR frame, plus
// pc_pcap_global_header() writing the libpcap global header with the DLT_CAN_SOCKETCAN link type
// (shared_primitives/pcap.h). Every call here is pure (no heap, no bus) - worked example for
// performance_benching/device/<service>/: a pure protocol codec with no hardware involved, so every call exercises
// the real production code path (contrast with performance_benching/device/ads1115, a peripheral driver where the
// bus transaction itself is stubbed). bus_capture_begin()/poll()/end() - the ESP32 TWAI listen-only
// bind - are deliberately out of scope: this rig has no CAN transceiver wired to it, and installing
// a real TWAI driver would be hardware I/O this bench must never perform.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/bus_capture -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/system/bus_capture/bus_capture.h"
#include <Arduino.h>
#include <string.h>

static void bus_capture_bench_task(void *)
{
    // Standard data frame, id 0x123, 8 data bytes (test_standard_data_frame in test_bus_capture.cpp).
    static CanFrame std8;
    memset(&std8, 0, sizeof(std8));
    std8.id = 0x123;
    std8.extended = false;
    std8.rtr = false;
    std8.dlc = 8;
    for (int i = 0; i < 8; i++)
        std8.data[i] = (uint8_t)(0x10 + i);

    // Extended (29-bit) J1939-style id, 2 data bytes (test_extended_id_sets_eff).
    static CanFrame ext2;
    memset(&ext2, 0, sizeof(ext2));
    ext2.id = 0x18FEF100;
    ext2.extended = true;
    ext2.dlc = 2;
    ext2.data[0] = 0xAA;
    ext2.data[1] = 0xBB;

    // RTR frame, no data (test_rtr_flag_and_no_data).
    static CanFrame rtr4;
    memset(&rtr4, 0, sizeof(rtr4));
    rtr4.id = 0x7FF;
    rtr4.rtr = true;
    rtr4.dlc = 4;
    rtr4.data[0] = 0xFF;

    static uint8_t out[PC_SOCKETCAN_FRAME_LEN];
    static uint8_t pcap_hdr[PC_PCAP_GLOBAL_HDR_LEN];

    for (;;)
    {
        Serial.printf("DB ==== bus_capture device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_BULK("can_to_socketcan std8", 100000, PC_SOCKETCAN_FRAME_LEN,
                    sink += can_to_socketcan(&std8, out, sizeof(out)));
        DBENCH_OP("can_to_socketcan ext2", 100000, sink += can_to_socketcan(&ext2, out, sizeof(out)));
        DBENCH_OP("can_to_socketcan rtr4", 100000, sink += can_to_socketcan(&rtr4, out, sizeof(out)));
        DBENCH_OP("pc_pcap_global_header can", 100000,
                  sink += pc_pcap_global_header(pcap_hdr, sizeof(pcap_hdr), PC_DLT_CAN_SOCKETCAN));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: bus_capture device microbench");
    xTaskCreatePinnedToCore(bus_capture_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
