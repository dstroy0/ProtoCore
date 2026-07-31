// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the UDP telemetry line builder (services/iot/udp_telemetry): the
// InfluxDB line-protocol assembly (measurement + tags + int/uint/float fields) - the per-point hot op
// before the UDP send. Pure; no socket.
//
// Build/flash:  pio run -d performance_benching/device/udp_telemetry -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/udp_telemetry/udp_telemetry.h"
#include <Arduino.h>

static void udp_telemetry_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== udp_telemetry device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char buf[256];
        DBENCH_OP("pc_line build (2 tags, 3 fields)", 200000, {
            pc_line l;
            pc_line_init(&l, buf, sizeof(buf), "env");
            pc_line_add_tag(&l, "host", "rig-1");
            pc_line_add_tag(&l, "room", "lab");
            pc_line_add_float(&l, "temp", 21.5f, 1);
            pc_line_add_int(&l, "rssi", -42);
            pc_line_add_uint(&l, "uptime", 1234u);
            sink += pc_line_len(&l);
        });
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: udp_telemetry device microbench");
    xTaskCreatePinnedToCore(udp_telemetry_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
