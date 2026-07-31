// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Mitsubishi MELSEC MC binary 3E codec (services/fieldbus/melsec):
// pc_melsec_build_read() emits a QnA-compatible batch-read request (little-endian subheader 0x5000,
// command 0x0401, 24-bit head device + device code) into a caller buffer, and pc_melsec_parse_response()
// validates a 0xD000 response subheader/length/end-code and returns a view onto the read payload - both
// pure (no sockets, no heap). Like performance_benching/device/modbus, this is a pure protocol codec with no hardware
// involved, so every call here exercises the real production code path; the TCP/UDP send half is the
// application's and is deliberately out of scope on this peripheral-less rig.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/melsec -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/melsec/melsec.h"
#include <Arduino.h>

static void melsec_bench_task(void *)
{
    static uint8_t req[32];

    // Known-good 0xD000 response (5 word values), copied verbatim from test/test_melsec (test_parse_response_ok):
    // subheader D0 00, routing, data length = 12 (end code + 10 data), end code 0x0000 success, 5 LE words.
    static const uint8_t resp_ok[] = {
        0xD0, 0x00,                                                // subheader (response)
        0x00, 0xFF, 0xFF, 0x03, 0x00,                              // routing
        0x0C, 0x00,                                                // data length = 12 (end code + 10 data)
        0x00, 0x00,                                                // end code = success
        0x11, 0x11, 0x22, 0x22, 0x33, 0x33, 0x44, 0x44, 0x55, 0x55 // 5 word values
    };

    for (;;)
    {
        Serial.printf("DB ==== melsec device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile bool sinkb = false;
        MelsecResponse parsed;

        // Batch-read request build: read 5 words of D100 (data register), monitoring timer 0x0010 -
        // the exact args from test_melsec's test_build_read_bytes. Cheap pure builder -> large N.
        DBENCH_OP("pc_melsec_build_read D100 x5", 200000,
                  sink += pc_melsec_build_read(req, sizeof(req), MELSEC_DEV_D, 100, 5, 0x0010));

        // Response parse + validate over the known-good 0xD000 frame. Cheap pure parser -> large N.
        DBENCH_OP("pc_melsec_parse_response ok", 200000,
                  sinkb ^= pc_melsec_parse_response(resp_ok, sizeof(resp_ok), &parsed));

        (void)sink;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: melsec device microbench");
    xTaskCreatePinnedToCore(melsec_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
