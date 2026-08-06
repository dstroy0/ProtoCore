// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SSE codec (network_drivers/presentation/http/sse):
// pc_sse_format() builds one `event:/id:/data:` frame into a caller buffer - the per-event hot op.
// Pure; the pc_conn_send() wrapper (pc_sse_write) is out of scope. Build/flash: pio run -d
// performance_benching/network_drivers/presentation/sse -t upload
#include "device_bench.h"
#include "network_drivers/presentation/http/sse/sse.h"
#include <Arduino.h>

static void sse_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== sse device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile int sink = 0;
        static char buf[256];
        DBENCH_OP("pc_sse_format (data+event+id)", 200000,
                  sink += pc_sse_format(buf, sizeof(buf), "{\"temp\":21.4,\"rh\":48}", "telemetry", "42"));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: sse device microbench");
    xTaskCreatePinnedToCore(sse_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}
void loop()
{
    delay(1000);
}
