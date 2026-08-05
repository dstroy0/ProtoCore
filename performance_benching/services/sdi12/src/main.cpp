// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SDI-12 sensor-bus codec (services/peripherals/sdi12): the command
// builders, the measurement-response parser, the data-value parser, and the SDI-12 CRC-16. All
// pure ASCII/codec logic - the 1200-baud UART line handling is real-hardware and out of scope; only
// the deterministic per-message CPU path is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/sdi12 -t upload --upload-port COM7
#include "device_bench.h"
#include "services/peripherals/sdi12/sdi12.h"
#include <Arduino.h>

static void sdi12_bench_task(void *)
{
    // aM! response: "0" + "012" (12 s) + "2" (2 values); aD0! values response (from test/test_sdi12).
    static const char measure_resp[] = "00122\r\n";
    static const char values_resp[] = "0+3.14-2.72\r\n";
    static const uint8_t crcbuf[16] = {'0', '+', '3', '.', '1', '4', '-', '2', '.', '7', '2', 0, 0, 0, 0, 0};

    for (;;)
    {
        Serial.printf("DB ==== sdi12 device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char buf[32];
        DBENCH_OP("pc_sdi12_build_measure (CRC)", 200000, sink += pc_sdi12_build_measure(buf, sizeof(buf), '3', true));
        char addr;
        uint16_t ready;
        uint8_t nval;
        DBENCH_OP("pc_sdi12_parse_measure", 200000,
                  sink += pc_sdi12_parse_measure(measure_resp, sizeof(measure_resp) - 1, &addr, &ready, &nval));
        float vals[8];
        size_t n;
        DBENCH_OP("pc_sdi12_parse_values", 200000,
                  sink += pc_sdi12_parse_values(values_resp, sizeof(values_resp) - 1, vals, 8, &n));
        DBENCH_BULK("pc_sdi12_crc16", 200000, 11, sink += pc_sdi12_crc16(crcbuf, 11));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: sdi12 device microbench");
    xTaskCreatePinnedToCore(sdi12_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
