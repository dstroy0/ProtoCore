// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the pluggable monotonic clock (services/clock,
// src/server/clock/clock.h): pc_millis() on the platform default vs. a custom-clock override divided
// down to the internal 1000 Hz, the latency-budget bookkeeping (pc_lat_begin/pc_lat_end) that
// services like dma/hw_health drive per-transaction, and the pc_cycles_to_ns() conversion used to
// report every "DB ..." line in this very harness. All four are pure CPU-side math/bookkeeping -
// clock.h is header-only (test_matrix.json's native_clock env: "Header-only, so no src to build",
// no PC_ENABLE_CLOCK guard exists anywhere in the repo). Out of scope: the platform millis()/
// micros()/ESP.getCycleCount() calls themselves are on-chip counter reads (no I2C/SPI/UART/radio/
// socket), so they are timed as-is rather than stubbed - there is no external peripheral to remove.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/clock -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/clock/clock.h"
#include <Arduino.h>

// Free-running fake tick sources for the custom-clock benches below: deterministic, no hardware,
// just enough state change per call to keep the compiler from folding the call away.
static uint32_t g_fake_ticks = 0;
static uint32_t fake_tick_fn(void)
{
    return g_fake_ticks += 8; // 8 kHz-ish free-running source
}
static uint32_t g_fake_us = 0;
static uint32_t fake_us_fn(void)
{
    return g_fake_us += 10; // 1 MHz source, 10 us per call
}

static void clock_bench_task(void *)
{
    static pc_latency_stat lat;
    pc_lat_reset(&lat);

    for (;;)
    {
        Serial.printf("DB ==== clock device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;

        pc_set_clock(nullptr, 0); // platform default (millis())
        DBENCH_OP("pc_millis (platform default)", 200000, sink += pc_millis());

        pc_set_clock(fake_tick_fn, 8000); // 8 kHz source -> divided down to 1000 Hz internally
        DBENCH_OP("pc_millis (custom clock /8kHz)", 200000, sink += pc_millis());
        pc_set_clock(nullptr, 0); // revert

        pc_set_micros_clock(fake_us_fn, 1000000); // 1 MHz source -> 1:1 microseconds
        DBENCH_OP("pc_lat_begin+pc_lat_end", 100000, {
            uint32_t _t = pc_lat_begin();
            pc_lat_end(&lat, _t, 50);
        });
        pc_set_micros_clock(nullptr, 0); // revert

        DBENCH_OP("pc_cycles_to_ns", 200000, sink += pc_cycles_to_ns(54321u, 240));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: clock device microbench");
    xTaskCreatePinnedToCore(clock_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
