// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the hardware-health decision cores (server/signaling/hw_health): the
// four pure verdict functions an app feeds with samples it has already read from the hardware -
//   - pc_hwhealth_rail_sample(): fold one rail millivolt reading into the worst-droop min + sag/
//     brownout counters (power-rail voltage-drop logger),
//   - pc_hwhealth_rail_json(): serialize that monitor to a "/health" JSON fragment (strbuf-backed),
//   - pc_hwhealth_spi_result(): the hysteretic SPI-clock backoff state machine (halve on a run of CRC
//     failures, step back up on a run of good transfers),
//   - pc_hwhealth_gpio_short(): driven-vs-readback short-circuit verdict,
//   - pc_hwhealth_cap_leak(): compare a measured RC decay time to expected (64-bit tolerance band).
// All five are pure (zero heap, no stdlib, no peripheral touched), so every call here exercises the
// real production code path. Deliberately out of scope: the ADC / SPI / GPIO reads that PRODUCE these
// samples - this rig has no rig hardware attached, and the library never does that I/O itself anyway
// (the app hands it numbers). No transport or linker stub is needed: nothing here calls out.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/hw_health -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/signaling/hw_health.h"
#include <Arduino.h>

static void hw_health_bench_task(void *)
{
    // Rail monitor: nominal 3.3V, warn 3.1V, crit 2.9V (same params the host test exercises).
    HwRailMonitor rail;
    pc_hwhealth_rail_init(&rail, 3300, 3100, 2900);

    // SPI backoff: 8MHz start, floor 1MHz, ceil 8MHz, halve after 3 fails, double after 4 oks.
    HwSpiBackoff spi;
    pc_hwhealth_spi_init(&spi, 8000000, 1000000, 8000000, 3, 4);

    static char json[96];

    for (;;)
    {
        Serial.printf("DB ==== hw_health device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;

        // Power-rail voltage-drop logger: fold one sag-region sample (3050 mV < 3100 warn) into the
        // worst-droop min + counters.
        DBENCH_OP("pc_hwhealth_rail_sample", 200000, sink += (uint32_t)pc_hwhealth_rail_sample(&rail, 3050));

        // Serialize the monitor to the "/health" JSON fragment (strbuf u32 formatting).
        DBENCH_OP("pc_hwhealth_rail_json", 100000, sink += (uint32_t)pc_hwhealth_rail_json(&rail, json, sizeof(json)));

        // Hysteretic SPI-clock backoff: feed a failing CRC result through the state machine.
        DBENCH_OP("pc_hwhealth_spi_result", 200000, sink += pc_hwhealth_spi_result(&spi, false));

        // GPIO short-circuit test: drove high, read low -> shorted to ground.
        DBENCH_OP("pc_hwhealth_gpio_short", 200000, sink += (uint32_t)pc_hwhealth_gpio_short(true, false));

        // Cap-leakage: measured 90 ms vs 100 ms expected, 10% band (64-bit tolerance math).
        DBENCH_OP("pc_hwhealth_cap_leak", 200000, sink += (uint32_t)pc_hwhealth_cap_leak(90, 100, 10));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: hw_health device microbench");
    xTaskCreatePinnedToCore(hw_health_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
