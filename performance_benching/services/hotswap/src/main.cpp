// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the removable-storage hot-swap state machine
// (services/storage/hotswap): the ABSENT/READY/FAULTED safeties that fault a volume after a run of
// consecutive I/O errors, reset the run on any success, rate-limit remount probes, and serialize a
// one-line /health status. Everything benched here is the pure host-testable core - each core
// function takes an explicit `now` and touches no clock, and pc_hotswap_json() reads only the
// statically-initialized owned singleton - so there is no filesystem, no SD/SPI bus, and no
// pc_millis() call on any path. The device binding (pc_hotswap_begin / _poll / _io) is
// deliberately out of scope: it exists only to drive an app's mount/unmount/card-detect callbacks
// against real removable media, which this rig has none of, and it does no CPU work of its own worth
// timing beyond the core transitions already measured below.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/hotswap -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/storage/hotswap/hotswap.h"
#include <Arduino.h>

static void hotswap_bench_task(void *)
{
    // Two cores parked in known, stable states so every timing block does the identical branch each
    // iteration (values copied straight from test/test_hotswap: threshold 3, 2000 ms probe interval).
    static HotswapCore ready;  // parked READY for the per-write io / redundant-probe hot paths
    static HotswapCore absent; // parked not-READY so due() exercises the wrap-safe unsigned delta
    static HotswapCore scratch;
    static char json[64];

    for (;;)
    {
        // Re-establish the known states at the top of each run.
        pc_hotswap_core_init(&ready, 3, 2000, 100000);
        pc_hotswap_core_probe(&ready, true, true, 100000); // -> READY
        pc_hotswap_core_init(&absent, 3, 2000, 100000);    // stays ABSENT

        Serial.printf("DB ==== hotswap device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;

        // The steady-state per-write outcome: a success while READY clears the failure run and keeps
        // the volume READY, so this is the exact branch a healthy filesystem write takes every time.
        DBENCH_OP("pc_hotswap_core_io ok", 200000, sink += pc_hotswap_core_io(&ready, true) ? 1u : 0u);

        // A redundant probe of an already-mounted volume: the full mount-decision work with no
        // transition, so it stays READY and repeats identically.
        DBENCH_OP("pc_hotswap_core_probe mount", 200000,
                  sink += pc_hotswap_core_probe(&ready, true, true, 100000) ? 1u : 0u);

        // The cheap per-loop "is a remount due?" check; a not-READY core reaches the wrap-safe
        // (now - last_probe_ms) >= interval delta rather than the READY early-out.
        DBENCH_OP("pc_hotswap_core_due", 200000, sink += pc_hotswap_core_due(&absent, 105000) ? 1u : 0u);

        // Cold init of a fresh core (clamps the threshold, back-dates the first probe).
        DBENCH_OP("pc_hotswap_core_init", 200000, pc_hotswap_core_init(&scratch, 3, 2000, 100000));

        // The /health serializer: snprintf of `{"storage":..,"mounts":N,"faults":N}` from the owned
        // singleton - the heaviest pure op here, hence the smaller N.
        DBENCH_OP("pc_hotswap_json", 50000, sink += pc_hotswap_json(json, sizeof(json)));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: hotswap device microbench");
    xTaskCreatePinnedToCore(hotswap_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
