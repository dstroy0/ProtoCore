// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Modbus Plus HDLC token-bus codec (services/fieldbus/mbplus):
// the CRC-16/X-25 FCS, the HDLC frame builder (7E addr ctrl payload CRClo CRChi 7E), the frame
// validator/parser, and the token-rotation ring helper - all pure (no heap, no stdlib, no bus). Like
// performance_benching/device/modbus, this is a pure protocol codec with no hardware involved: the physical 1 Mbit/s
// Modbus Plus token bus is hardware-gated and out of scope on this rig, so every call here exercises
// the real deterministic frame + token-MAC production code path directly.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/mbplus -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/mbplus/mbplus.h"
#include <Arduino.h>

static void mbplus_bench_task(void *)
{
    // CRC-16/X-25 check vector: CRC of "123456789" == 0x906E (from test/test_mbplus/test_mbplus.cpp).
    static const uint8_t crc_vec[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    // A realistic data-frame payload: Modbus Plus routing byte + a Modbus read PDU (from the test).
    static const uint8_t payload[3] = {0x10, 0x03, 0x00};
    // A pre-built, known-good frame for the parse bench: station 5, data control, 3-byte payload.
    static uint8_t frame[16];
    static size_t frame_len = 0;
    frame_len = pc_mbplus_build(5, Mbplus::MBPLUS_CTRL_DATA, payload, sizeof(payload), frame, sizeof(frame));

    static uint8_t out[32];

    for (;;)
    {
        Serial.printf("DB ==== mbplus device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile uint16_t sink16 = 0;
        volatile uint8_t sink8 = 0;
        volatile bool sinkb = false;

        // CRC-16/X-25 FCS over the 9-byte check vector - bulk op, so we also get ns/byte + MB/s.
        DBENCH_BULK("pc_mbplus_crc (X-25)", 100000, sizeof(crc_vec), sink16 += pc_mbplus_crc(crc_vec, sizeof(crc_vec)));
        // Build a full HDLC data frame (flags + addr + ctrl + payload + CRC).
        DBENCH_OP("pc_mbplus_build (data+3B)", 50000,
                  sink += pc_mbplus_build(5, Mbplus::MBPLUS_CTRL_DATA, payload, sizeof(payload), out, sizeof(out)));
        // Validate flags + CRC and parse the pre-built frame.
        MbPlusFrame f;
        DBENCH_OP("pc_mbplus_parse", 50000, sinkb = pc_mbplus_parse(frame, frame_len, &f));
        // Token-ring rotation helper (the token-bus MAC's next-holder computation).
        DBENCH_OP("pc_mbplus_next_token", 200000, sink8 += pc_mbplus_next_token(sink8, 64));

        (void)sink;
        (void)sink16;
        (void)sink8;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: mbplus device microbench");
    xTaskCreatePinnedToCore(mbplus_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
