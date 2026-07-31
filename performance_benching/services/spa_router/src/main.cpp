// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SPA router core (services/web/spa_router): the path
// extension check (deep-link vs asset) and the conditional UI-fragment streamer that assembles a
// single-page-app shell one fragment at a time. Pure string logic; no server.
//
// Build/flash:  pio run -d performance_benching/device/spa_router -t upload --upload-port COM7
#include "device_bench.h"
#include "services/web/spa_router/spa_router.h"
#include <Arduino.h>

static void spa_router_bench_task(void *)
{
    static const pc_ui_fragment frags[3] = {
        {"head", "<head><title>PC</title></head>", nullptr},
        {"nav", "<nav>menu</nav>", nullptr},
        {"body", "<main>dashboard</main>", nullptr},
    };

    for (;;)
    {
        Serial.printf("DB ==== spa_router device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_OP("pc_spa_has_extension", 200000, sink += pc_spa_has_extension("/app/users/42"));
        static char out[128];
        DBENCH_OP("pc_ui_stream (3 fragments)", 100000, {
            pc_ui_stream s;
            pc_ui_stream_begin(&s, frags, 3, nullptr);
            while (!pc_ui_stream_done(&s))
            {
                sink += pc_ui_stream_next(&s, out, sizeof(out));
            }
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
    Serial.println("\nDB boot: spa_router device microbench");
    xTaskCreatePinnedToCore(spa_router_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
