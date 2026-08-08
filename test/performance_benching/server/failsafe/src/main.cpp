// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the software watchdog / deadlock detector (server/failsafe):
// the wrap-safe overdue predicate (pc_lifeline_overdue), the lifeline registry check-in
// (pc_failsafe_feed_at), the fire-once-per-episode breach evaluation (pc_failsafe_check_at), and
// the /health-style JSON serializer (pc_failsafe_json_at) - all pure (zero heap, a fixed-size
// static registry, no hardware). Uses the explicit *_at(now) core with a synthetic clock, the same
// host-testable surface test/test_failsafe/test_failsafe.cpp exercises, so nothing here depends on
// a real millis() tick. Worked example match: a pure protocol/data-structure codec with no hardware
// involved, so every call here exercises the real production code path (contrast with
// performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is stubbed); out of
// scope: pc_failsafe_register/feed/check (the pc_millis()-reading wrappers) - thin pass-throughs
// to the *_at core benched here, and pc_failsafe_on_breach (a one-time install, not a hot path).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/failsafe -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/failsafe.h"
#include <Arduino.h>

// Trivial breach callback (mirrors test_failsafe.cpp's on_breach): just counts fires so the
// fire-once-per-episode path is fully exercised by pc_failsafe_check_at below.
static volatile int s_fire_count = 0;
static void on_breach(int id, const char *name, void *arg)
{
    (void)id;
    (void)name;
    (void)arg;
    s_fire_count++;
}

static void failsafe_bench_task(void *)
{
    pc_failsafe_reset();
    pc_failsafe_on_breach(on_breach, nullptr);

    // Fill the registry (PC_FAILSAFE_MAX_LIFELINES lifelines), mirroring test_failsafe.cpp's
    // test_registry_full: each starts fed at t=1000 with a 500 ms deadline.
    for (int i = 0; i < PC_FAILSAFE_MAX_LIFELINES; i++)
    {
        pc_failsafe_register_at("lifeline", 500, 1000);
    }

    // Size the JSON buffer once up front (8 armed entries; digit widths are stable across the
    // repeating loop below, so this length stays accurate for every DBENCH_BULK pass).
    static char json[1024];
    size_t json_len = (size_t)pc_failsafe_json_at(5000, json, sizeof(json));

    for (;;)
    {
        Serial.printf("DB ==== failsafe device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile bool sinkb = false;
        volatile bool sinkf = false;
        volatile uint32_t sinkm = 0;
        volatile int sinkj = 0;

        // The wrap-safe overdue predicate - the hottest primitive, evaluated per lifeline per check.
        DBENCH_OP("pc_lifeline_overdue", 200000, sinkb = pc_lifeline_overdue(1600, 1000, 500));

        // Check-in: every armed lifeline calls this at least once per deadline window.
        DBENCH_OP("pc_failsafe_feed_at", 200000, sinkf = pc_failsafe_feed_at(0, 1700));

        // Full-registry evaluation (PC_FAILSAFE_MAX_LIFELINES=8 lines); now=100000 keeps every
        // lifeline solidly overdue without wraparound, so this exercises the fire-once-per-episode
        // path (first call in the loop fires id 0's callback since feed_at above just cleared its
        // breach; the remaining iterations hit the already-breached fast path).
        DBENCH_OP("pc_failsafe_check_at", 50000, sinkm += pc_failsafe_check_at(100000));

        // /health-style JSON serialization of the whole registry.
        DBENCH_BULK("pc_failsafe_json_at", 20000, json_len, sinkj += pc_failsafe_json_at(5000, json, sizeof(json)));

        (void)sinkb;
        (void)sinkf;
        (void)sinkm;
        (void)sinkj;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: failsafe device microbench");
    xTaskCreatePinnedToCore(failsafe_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
