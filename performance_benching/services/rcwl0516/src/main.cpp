// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the RCWL-0516 presence core (services/peripherals/rcwl0516): the pure
// debounce/hold state machine pc_presence_core_update() that turns a raw doppler-radar OUT-pin
// level into a debounced presence verdict + edge events. The GPIO read (pc_rcwl0516_poll) is
// real-hardware and out of scope; only the deterministic per-sample state machine is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/rcwl0516 -t upload --upload-port COM7
#include "device_bench.h"
#include "services/peripherals/rcwl0516/rcwl0516.h"
#include <Arduino.h>

static void rcwl0516_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== rcwl0516 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        PresenceCore c;
        pc_presence_core_init(&c, 50, 2000, 0);
        uint32_t t = 0;
        // Alternate the pin each call so both edges + debounce/hold paths are exercised.
        DBENCH_OP("pc_presence_core_update", 200000, {
            sink += pc_presence_core_update(&c, (t & 64) != 0, t);
            t += 3;
        });
        DBENCH_OP("pc_presence_core_get", 200000, sink += pc_presence_core_get(&c));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: rcwl0516 device microbench");
    xTaskCreatePinnedToCore(rcwl0516_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
