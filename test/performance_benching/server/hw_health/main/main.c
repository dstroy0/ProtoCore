// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the hardware-health decision cores (server/signaling/hw_health): the
// four pure verdict functions an app feeds with samples it has already read from the hardware -
//   - protocore_hwhealth_rail_sample(): fold one rail millivolt reading into the worst-droop min + sag/
//     brownout counters (power-rail voltage-drop logger),
//   - protocore_hwhealth_rail_json(): serialize that monitor to a "/health" JSON fragment (strbuf-backed),
//   - protocore_hwhealth_spi_result(): the hysteretic SPI-clock backoff state machine (halve on a run of CRC
//     failures, step back up on a run of good transfers),
//   - protocore_hwhealth_gpio_short(): driven-vs-readback short-circuit verdict,
//   - protocore_hwhealth_cap_leak(): compare a measured RC decay time to expected (64-bit tolerance band).
// All five are pure (zero heap, no stdlib, no peripheral touched), so every call here exercises the
// real production code path. Deliberately out of scope: the ADC / SPI / GPIO reads that PRODUCE these
// samples - this rig has no rig hardware attached, and the library never does that I/O itself anyway
// (the app hands it numbers). No transport or linker stub is needed: nothing here calls out.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/hw_health -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/signaling/hw_health/hw_health.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t hw_health_work[16]; // the borrow an entry takes; HwHealth never reads it

void dbench_run(void)
{
    // Rail monitor: nominal 3.3V, warn 3.1V, crit 2.9V (same params the host test exercises).
    HwRailMonitor rail;
    HwHealth.rail.m = &rail;
    HwHealth.rail.nominal_mv = 3300;
    HwHealth.rail.warn_mv = 3100;
    HwHealth.rail.crit_mv = 2900;
    HwHealth.rail_init(hw_health_work);

    // SPI backoff: 8MHz start, floor 1MHz, ceil 8MHz, halve after 3 fails, double after 4 oks.
    HwSpiBackoff spi;
    HwHealth.spi.s = &spi;
    HwHealth.spi.start_hz = 8000000;
    HwHealth.spi.min_hz = 1000000;
    HwHealth.spi.max_hz = 8000000;
    HwHealth.spi.fail_trip = 3;
    HwHealth.spi.ok_trip = 4;
    HwHealth.spi_init(hw_health_work);

    static char json[96];

    for (;;)
    {
        DBENCH_BANNER("hw_health");
        volatile uint32_t sink = 0;

        // Power-rail voltage-drop logger: fold one sag-region sample (3050 mV < 3100 warn) into the
        // worst-droop min + counters.
        HwHealth.rail.m = &rail;
        HwHealth.rail.mv = 3050;
        DBENCH_OP("HwHealth.rail_sample", 200000, HwHealth.rail_sample(hw_health_work);
                  sink += (uint32_t)HwHealth.rail_verdict);

        // Serialize the monitor to the "/health" JSON fragment (strbuf u32 formatting).
        HwHealth.rail.m_ro = &rail;
        HwHealth.out_args.out = json;
        HwHealth.out_args.cap = sizeof(json);
        DBENCH_OP("HwHealth.rail_json", 100000, HwHealth.rail_json(hw_health_work); sink += (uint32_t)HwHealth.n);

        // Hysteretic SPI-clock backoff: feed a failing CRC result through the state machine.
        HwHealth.spi.s = &spi;
        HwHealth.spi.crc_ok = PROTO_FALSE;
        DBENCH_OP("HwHealth.spi_result", 200000, HwHealth.spi_result(hw_health_work); sink += HwHealth.hz);

        // GPIO short-circuit test: drove high, read low -> shorted to ground.
        HwHealth.probe.driven_high = PROTO_TRUE;
        HwHealth.probe.read_high = PROTO_FALSE;
        DBENCH_OP("HwHealth.gpio_short", 200000, HwHealth.gpio_short(hw_health_work);
                  sink += (uint32_t)HwHealth.gpio_verdict);

        // Cap-leakage: measured 90 ms vs 100 ms expected, 10% band (64-bit tolerance math).
        HwHealth.probe.measured_ms = 90;
        HwHealth.probe.expected_ms = 100;
        HwHealth.probe.tol_pct = 10;
        DBENCH_OP("HwHealth.cap_leak", 200000, HwHealth.cap_leak(hw_health_work);
                  sink += (uint32_t)HwHealth.cap_verdict);

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("hw_health")
