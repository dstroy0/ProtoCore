// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for services/web_terminal. NOTE: web_terminal is a thin *server
// binding* - it serves a terminal page and pumps I/O to connected clients over WebSocket + SSE, both
// of which have their own device benches (performance_benching/device/websocket, performance_benching/device/sse). It
// has no standalone pure codec of its own, so the only side-effect-free op to time is the line builder
// the line build (pc_frame_build over a static spec, what an application does before handing the
// text over) and the client-count getter. Kept for suite completeness; not a throughput number.
//
// Build/flash:  pio run -d performance_benching/device/web_terminal -t upload --upload-port COM7
#include "device_bench.h"
#include "mmgr/frame.h"
#include "services/web/web_terminal/web_terminal.h"
#include <Arduino.h>

static const pc_field BENCH_LINE[] = {{PC_FK_LIT, 0, 7, "sensor="}, PC_U32, {PC_FK_LIT, 0, 4, " rh="}, PC_U32,
                                      {PC_FK_LIT, 0, 7, "% heap="}, PC_U32, {PC_FK_LIT, 0, 1, "\n"},   PC_END};

static void web_terminal_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== web_terminal device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        char line[64];
        DBENCH_OP("web terminal line (frame build)", 200000, {
            sink += (uint32_t)pc_frame_build(line, sizeof(line), BENCH_LINE,
                                             (const pc_fval[]){PC_VU32(214u), PC_VU32(48u), PC_VU32(131072u)}, 3);
        });
        DBENCH_OP("pc_web_terminal_client_count", 200000, sink += pc_web_terminal_client_count());
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: web_terminal device microbench");
    xTaskCreatePinnedToCore(web_terminal_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
