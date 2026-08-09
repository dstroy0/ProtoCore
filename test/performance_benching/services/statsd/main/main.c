// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the StatsD metrics client (services/iot/statsd):
// pc_statsd_format() builds one `name:value|type|@rate|#tags` line into a caller buffer - the
// per-metric hot op before the UDP send. Pure; no socket.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/services/statsd -p COM7 flash monitor
#include "device_bench.h"
#include "services/iot/statsd/statsd.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>
#include <stdio.h>

static void statsd_bench_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        printf("DB ==== statsd device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)dbench_cpu_mhz());
        volatile size_t sink = 0;
        static char out[256];
        DBENCH_OP("pc_statsd_format (counter+tags)", 200000,
                  sink += pc_statsd_format(out, sizeof(out), "api.requests", "1", STATSD_COUNTER, 0.1f,
                                           "env:prod,host:pc-rig"));
        (void)sink;
        printf("DB ==== DONE ====\n");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void app_main(void);
void app_main(void)
{
    vTaskDelay(2500 / portTICK_PERIOD_MS);
    printf("\nDB boot: statsd device microbench\n");
    xTaskCreatePinnedToCore(statsd_bench_task, "dbench", 16384, NULL, 24, NULL, 1);
}
