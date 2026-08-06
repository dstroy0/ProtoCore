// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for services/upload_service. NOTE: upload_service is a thin
// *server + filesystem binding* - pc_upload_begin() registers a route that streams an HTTP multipart
// upload straight to a file. The actual per-byte parsing work is done by the multipart presentation
// codec (benched at performance_benching/host_microbench / the multipart path) and the streaming body hook; this
// service adds only the route wiring and a byte counter. There is no standalone pure hot-path to
// time, so this sketch benches only the last-size getter, for suite completeness (not a throughput
// number).
//
// Build/flash:  pio run -d performance_benching/device/upload_service -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/application/upload_service/upload_service.h"
#include <Arduino.h>

static void upload_service_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== upload_service device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_OP("pc_upload_last_size (getter)", 200000, sink += pc_upload_last_size());
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: upload_service device microbench");
    xTaskCreatePinnedToCore(upload_service_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
