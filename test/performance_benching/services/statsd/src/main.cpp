// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the StatsD metrics client (services/iot/statsd):
// pc_statsd_format() builds one `name:value|type|@rate|#tags` line into a caller buffer - the
// per-metric hot op before the UDP send. Pure; no socket.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/statsd -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/statsd/statsd.h"
#include <Arduino.h>

static void statsd_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== statsd device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char out[256];
        DBENCH_OP("pc_statsd_format (counter+tags)", 200000,
                  sink += pc_statsd_format(out, sizeof(out), "api.requests", "1", StatsdType::STATSD_COUNTER, 0.1f,
                                           "env:prod,host:pc-rig"));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: statsd device microbench");
    xTaskCreatePinnedToCore(statsd_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
