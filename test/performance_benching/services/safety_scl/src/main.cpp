// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the IEC 61784-3 black-channel Safety Communication Layer
// (services/machine_tool/safety_scl): the per-frame safety verdict pc_scl_on_frame() (monitoring-counter
// sequence check + watchdog state machine, given the CRC-signature verdict as input) and the
// pc_scl_next_counter() modulus math. Pure (the CRC signature itself is computed elsewhere and
// passed in) - this is the deterministic SCL consequence logic run per received safety frame.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/safety_scl -t upload --upload-port COM7
#include "device_bench.h"
#include "services/machine_tool/safety_scl/safety_scl.h"
#include <Arduino.h>

static void safety_scl_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== safety_scl device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        SclConn c;
        pc_scl_init(&c, 1, 0, 100, 0);
        uint32_t counter = 1, t = 0;
        // A stream of valid, in-sequence frames: exercises the accept + watchdog-refresh hot path.
        DBENCH_OP("pc_scl_on_frame (valid seq)", 200000, {
            sink += pc_scl_on_frame(&c, true, counter, t);
            counter = pc_scl_next_counter(counter, 0);
            t += 1;
        });
        DBENCH_OP("pc_scl_next_counter", 200000, sink += pc_scl_next_counter(counter, 65535));
        DBENCH_OP("pc_scl_poll", 200000, sink += pc_scl_poll(&c, t));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: safety_scl device microbench");
    xTaskCreatePinnedToCore(safety_scl_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
