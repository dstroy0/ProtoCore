// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the AutomationDirect / Koyo DirectNET serial frame codec
// (services/fieldbus/directnet): building the SOH..ETB header (enquiry) frame, building and parsing the
// STX..ETX data frame, and the LRC (longitudinal XOR) checksum both frames rely on - all pure,
// zero heap, no UART involved. Worked example for performance_benching/device/<service>/: a pure protocol codec,
// so every call here exercises the real production code path (contrast with performance_benching/device/ads1115,
// a peripheral driver where the bus transaction itself is stubbed). The UART transport and the
// ENQ/ACK/NAK handshake sequencing are the device step and are out of scope on this rig, which has
// no DirectLOGIC PLC or RS-232/RS-485 transceiver attached.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/directnet -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/directnet/directnet.h"
#include <Arduino.h>

static void directnet_bench_task(void *)
{
    // Header/enquiry frame: slave 1, READ, V-memory address 0x0040, 2 blocks (matches
    // test/test_directnet/test_directnet.cpp's test_header_frame).
    static uint8_t hdr[16];
    // Data frame: STX + "ABCD" + ETX + LRC (matches test_data_frame_roundtrip).
    static const uint8_t payload[4] = {'A', 'B', 'C', 'D'};
    static uint8_t data_frame[16];
    size_t data_frame_len = pc_dnet_data(payload, sizeof(payload), data_frame, sizeof(data_frame));

    // Buffer for the standalone LRC bulk bench (10 bytes of frame body).
    static const uint8_t lrc_buf[10] = {'0', '1', 0x30, '0', '0', '4', '0', '0', '2', DnetByte::DNET_ETB};

    for (;;)
    {
        Serial.printf("DB ==== directnet device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint8_t sink8 = 0;
        volatile size_t sinkz = 0;
        volatile bool sinkb = false;

        DBENCH_BULK("pc_dnet_lrc", 100000, sizeof(lrc_buf), sink8 += pc_dnet_lrc(lrc_buf, sizeof(lrc_buf)));

        DBENCH_OP("pc_dnet_header build", 100000,
                  sinkz += pc_dnet_header(1, DnetByte::DNET_READ, 0x0040, 2, hdr, sizeof(hdr)));

        DBENCH_OP("pc_dnet_data build (4B)", 100000,
                  sinkz += pc_dnet_data(payload, sizeof(payload), data_frame, sizeof(data_frame)));

        {
            const uint8_t *d = nullptr;
            size_t dl = 0;
            DBENCH_OP("pc_dnet_data_parse", 100000, sinkb = pc_dnet_data_parse(data_frame, data_frame_len, &d, &dl));
        }

        (void)sink8;
        (void)sinkz;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: directnet device microbench");
    xTaskCreatePinnedToCore(directnet_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
