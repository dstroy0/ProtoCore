// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the HTTP byte-range parser (server/http_range):
// http_parse_byte_range() decodes a `Range: bytes=...` header against a known resource size - the
// per-request hot op for 206 Partial Content. Pure. Build/flash: pio run -d performance_benching/network_drivers/application/http_range -t
// upload
#include "device_bench.h"
#include "network_drivers/application/http_range.h"
#include <Arduino.h>

static void http_range_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== http_range device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile int sink = 0;
        size_t start, end;
        DBENCH_OP("http_parse_byte_range (500-999)", 200000,
                  sink += http_parse_byte_range("bytes=500-999", 65536, &start, &end));
        DBENCH_OP("http_parse_byte_range (suffix)", 200000,
                  sink += http_parse_byte_range("bytes=-500", 65536, &start, &end));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: http_range device microbench");
    xTaskCreatePinnedToCore(http_range_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}
void loop()
{
    delay(1000);
}
