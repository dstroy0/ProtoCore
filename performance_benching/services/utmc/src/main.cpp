// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the UTMC codec (services/transportation/utmc): the UK UTMC datex-lite
// request/response XML builders and the request parser. Pure string logic; no transport.
//
// Build/flash:  pio run -d performance_benching/device/utmc -t upload --upload-port COM7
#include "device_bench.h"
#include "services/transportation/utmc/utmc.h"
#include <Arduino.h>

static void utmc_bench_task(void *)
{
    static const char req_xml[] = "<Request><ObjectID>DET/1/1/1</ObjectID></Request>";

    for (;;)
    {
        Serial.printf("DB ==== utmc device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char out[256];
        DBENCH_OP("pc_utmc_request", 200000, sink += pc_utmc_request("DET/1/1/1", out, sizeof(out)));
        DBENCH_OP("pc_utmc_response", 200000,
                  sink += pc_utmc_response("DET/1/1/1", "42", 1, "2026-07-23T12:00:00Z", out, sizeof(out)));
        DBENCH_OP("pc_utmc_parse_request", 200000,
                  sink += pc_utmc_parse_request(req_xml, sizeof(req_xml) - 1, out, sizeof(out)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: utmc device microbench");
    xTaskCreatePinnedToCore(utmc_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
