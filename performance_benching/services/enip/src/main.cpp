// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the EtherNet/IP encapsulation codec (services/fieldbus/enip):
// building a RegisterSession request, building a SendRRData request that wraps a CIP message
// (Common Packet Format: Null Address item + Unconnected Data item), parsing the 24-octet
// encapsulation header back off the wire, and extracting the CIP reply out of a SendRRData
// command-data block - all pure (no heap, no sockets). Worked example for performance_benching/device/<service>/:
// a pure protocol codec with no hardware involved (contrast with performance_benching/device/ads1115, a peripheral
// driver where the bus transaction itself is stubbed), so every call here exercises the real
// production code path. Sample byte layouts are taken from test/test_enip/test_enip.cpp
// (already known-good, spec-conformant per the Wireshark ENIP dissector).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/enip -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/enip/enip.h"
#include <Arduino.h>

static void enip_bench_task(void *)
{
    // A (stub) CIP request, same bytes used by test_send_rr_data_round_trip.
    static const uint8_t cip[] = {0x4C, 0x20, 0x01, 0x24, 0x01};
    static const uint8_t sender_context[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    static uint8_t reg_buf[32];
    static uint8_t rr_buf[64];

    // Pre-build a SendRRData block once so parse/extract bench a real, valid wire capture.
    size_t rr_len = pc_eip_build_send_rr_data(rr_buf, sizeof(rr_buf), 0x12345678, sender_context, 5, cip, sizeof(cip));

    for (;;)
    {
        Serial.printf("DB ==== enip device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;

        DBENCH_OP("pc_eip_build_register_session", 100000,
                  sink += pc_eip_build_register_session(reg_buf, sizeof(reg_buf), sender_context));

        DBENCH_OP("pc_eip_build_send_rr_data", 50000,
                  sink +=
                  pc_eip_build_send_rr_data(rr_buf, sizeof(rr_buf), 0x12345678, sender_context, 5, cip, sizeof(cip)));

        DBENCH_OP("pc_eip_parse", 100000, {
            EipHeader h;
            const uint8_t *data;
            size_t data_len;
            sink += pc_eip_parse(rr_buf, rr_len, &h, &data, &data_len) ? 1 : 0;
        });

        DBENCH_OP("pc_eip_parse_send_rr_data", 100000, {
            EipHeader h;
            const uint8_t *data;
            size_t data_len;
            pc_eip_parse(rr_buf, rr_len, &h, &data, &data_len);
            const uint8_t *out_cip;
            size_t out_len;
            sink += pc_eip_parse_send_rr_data(data, data_len, &out_cip, &out_len) ? 1 : 0;
        });

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: enip device microbench");
    xTaskCreatePinnedToCore(enip_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
