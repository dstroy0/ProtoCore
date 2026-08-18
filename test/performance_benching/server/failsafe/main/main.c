// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the software watchdog / deadlock detector (server/failsafe):
// the wrap-safe overdue predicate (protocore_lifeline_overdue), the lifeline registry check-in
// (Failsafe.feed), the fire-once-per-episode breach evaluation (Failsafe.check), and
// the /health-style JSON serializer (Failsafe.json) - all pure (zero heap, a fixed-size
// static registry, no hardware). Every call takes the clock as args.now, the same
// host-testable surface test/test_failsafe/test_failsafe.cpp exercises, so nothing here depends on
// a real millis() tick. Worked example match: a pure protocol/data-structure codec with no hardware
// involved, so every call here exercises the real production code path (contrast with
// performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is stubbed); out of
// scope: Failsafe.on_breach, a one-time install rather than a hot path.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/failsafe -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/core/failsafe.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Trivial breach callback (mirrors test_failsafe.cpp's on_breach): just counts fires so the
// fire-once-per-episode path is fully exercised by protocore_failsafe_check_at below.
static volatile int s_fire_count = 0;
static void on_breach(int id, const char *name, void *arg)
{
    (void)id;
    (void)name;
    (void)arg;
    s_fire_count++;
}

/** @brief Arm one lifeline named @p name against @p now with deadline @p deadline_ms. */
static int failsafe_add(const char *name, uint32_t deadline_ms, uint32_t now)
{
    Failsafe.args.name = name;
    Failsafe.args.deadline_ms = deadline_ms;
    Failsafe.args.now = now;
    Failsafe.add(protocore_failsafe_span());
    return Failsafe.i32;
}

/** @brief Check lifeline @p id in against @p now. */
static proto_bool failsafe_feed(int id, uint32_t now)
{
    Failsafe.args.id = id;
    Failsafe.args.now = now;
    Failsafe.feed(protocore_failsafe_span());
    return Failsafe.ok;
}

/** @brief Judge every armed lifeline against @p now; one bit per lifeline that went overdue. */
static uint32_t failsafe_check(uint32_t now)
{
    Failsafe.args.now = now;
    Failsafe.check(protocore_failsafe_span());
    return Failsafe.breached;
}

/** @brief Report every armed lifeline as JSON into @p out; the characters written. */
static int failsafe_json(uint32_t now, char *out, size_t cap)
{
    Failsafe.args.now = now;
    Failsafe.out_args.out = out;
    Failsafe.out_args.cap = cap;
    Failsafe.json(protocore_failsafe_span());
    return Failsafe.n;
}

void dbench_run(void)
{
    Failsafe.reset(protocore_failsafe_span());
    Failsafe.out_args.cb = on_breach;
    Failsafe.out_args.arg = NULL;
    Failsafe.on_breach(protocore_failsafe_span());

    // Fill the registry (PROTOCORE_FAILSAFE_MAX_LIFELINES lifelines), mirroring test_failsafe.cpp's
    // test_registry_full: each starts fed at t=1000 with a 500 ms deadline.
    for (int i = 0; i < PROTOCORE_FAILSAFE_MAX_LIFELINES; i++)
    {
        failsafe_add("lifeline", 500, 1000);
    }

    // Size the JSON buffer once up front (8 armed entries; digit widths are stable across the
    // repeating loop below, so this length stays accurate for every DBENCH_BULK pass).
    static char json[1024];
    size_t json_len = (size_t)failsafe_json(5000, json, sizeof(json));

    for (;;)
    {
        DBENCH_BANNER("failsafe");
        volatile bool sinkb = false;
        volatile bool sinkf = false;
        volatile uint32_t sinkm = 0;
        volatile int sinkj = 0;

        // The wrap-safe overdue predicate - the hottest primitive, evaluated per lifeline per check.
        DBENCH_OP("protocore_lifeline_overdue", 200000, sinkb = protocore_lifeline_overdue(1600, 1000, 500));

        // Check-in: every armed lifeline calls this at least once per deadline window.
        DBENCH_OP("Failsafe.feed", 200000, sinkf = failsafe_feed(0, 1700));

        // Full-registry evaluation (PROTOCORE_FAILSAFE_MAX_LIFELINES=8 lines); now=100000 keeps every
        // lifeline solidly overdue without wraparound, so this exercises the fire-once-per-episode
        // path (first call in the loop fires id 0's callback since feed_at above just cleared its
        // breach; the remaining iterations hit the already-breached fast path).
        DBENCH_OP("Failsafe.check", 50000, sinkm += failsafe_check(100000));

        // /health-style JSON serialization of the whole registry.
        DBENCH_BULK("Failsafe.json", 20000, json_len, sinkj += failsafe_json(5000, json, sizeof(json)));

        (void)sinkb;
        (void)sinkf;
        (void)sinkm;
        (void)sinkj;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("failsafe")
