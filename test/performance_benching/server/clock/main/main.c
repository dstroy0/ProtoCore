// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the pluggable monotonic clock (services/clock,
// src/server/clock/clock.h): Clock.millis on the platform default vs. a custom-clock override divided
// down to the internal 1000 Hz, the latency-budget bookkeeping (protocore_lat_begin/protocore_lat_end) that
// services like dma/hw_health drive per-transaction, and the protocore_cycles_to_ns() conversion used to
// report every "DB ..." line in this very harness. All four are pure CPU-side math/bookkeeping.
// Out of scope: the platform millis()/
// micros()/ESP.getCycleCount() calls themselves are on-chip counter reads (no I2C/SPI/UART/radio/
// socket), so they are timed as-is rather than stubbed - there is no external peripheral to remove.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/clock -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/clock/clock.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/** @brief Install @p fn as the millisecond source, counting at @p rate; NULL reverts to the platform's. */
static void set_ms_clock(protocore_clock_fn fn, uint32_t rate)
{
    Clock.src.fn = fn;
    Clock.src.ticks_per_second = rate;
    Clock.set_ms(Clock.internal);
}

/** @brief Install @p fn as the microsecond source, counting at @p rate; NULL reverts. */
static void set_us_clock(protocore_clock_fn fn, uint32_t rate)
{
    Clock.src.fn = fn;
    Clock.src.ticks_per_second = rate;
    Clock.set_us(Clock.internal);
}

/** @brief Milliseconds since boot. */
static uint32_t millis_now(void)
{
    Clock.millis(Clock.internal);
    return Clock.ms;
}

void dbench_run(void)
{
    static protocore_latency_stat lat;
    protocore_lat_reset(&lat);

    for (;;)
    {
        DBENCH_BANNER("clock");
        volatile uint32_t sink = 0;

        set_ms_clock(NULL, 0); // platform default (millis())
        DBENCH_OP("Clock.millis (platform default)", 200000, sink += millis_now());

        set_ms_clock(fake_tick_fn, 8000); // 8 kHz source -> divided down to 1000 Hz internally
        DBENCH_OP("Clock.millis (custom clock /8kHz)", 200000, sink += millis_now());
        set_ms_clock(NULL, 0); // revert

        set_us_clock(fake_us_fn, 1000000); // 1 MHz source -> 1:1 microseconds
        DBENCH_OP("protocore_lat_begin+protocore_lat_end", 100000, {
            uint32_t _t = protocore_lat_begin();
            protocore_lat_end(&lat, _t, 50);
        });
        set_us_clock(NULL, 0); // revert

        DBENCH_OP("protocore_cycles_to_ns", 200000, sink += protocore_cycles_to_ns(54321u, 240));

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("clock")
