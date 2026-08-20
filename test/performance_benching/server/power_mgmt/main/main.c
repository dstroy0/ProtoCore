// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SoC power governor (server/power_mgmt): the pure clock
// decision Power.decide - load-based scaling, the thermal hysteresis that keeps a part parked at
// the limit from oscillating, and post-brownout recovery - plus Power.json which serializes a
// plan. All of these take every input explicitly and touch no hardware, so each call here exercises
// the real production decision path (like performance_benching/device/modbus, a pure codec).
//
// Deliberately out of scope: the device binding (Power.brownout / Power.die_temp /
// Power.apply / Power.cpu_mhz / Power.gate_bt). Those read esp_reset_reason() and the die
// sensor and actually re-clock the core / release the BT domain - a real side effect on the SoC, not
// a deterministic bit of math - so they are never benched. They still compile into the library (their
// esp_* / Arduino symbols are provided by the framework) but this sketch never calls them.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/power_mgmt -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/core/power_mgmt/power_mgmt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The clock plan for @p load_pct at @p temp_c under @p cfg. */
static PowerPlan power_decide(const PowerCfg *cfg, uint8_t load_pct, int16_t temp_c, proto_bool brownout_boot,
                              uint32_t since_boot_ms, proto_bool was_throttled)
{
    PowerV.plan_args.cfg = cfg;
    PowerV.plan_args.load_pct = load_pct;
    PowerV.plan_args.temp_c = temp_c;
    PowerV.plan_args.brownout_boot = brownout_boot;
    PowerV.plan_args.since_boot_ms = since_boot_ms;
    PowerV.plan_args.was_throttled = was_throttled;
    Power.decide(protocore_power_mgmt_span());
    return PowerV.plan;
}

/** @brief Serialize @p plan and @p temp_c as JSON into @p out; the bytes written. */
static size_t power_json(const PowerPlan *plan, int16_t temp_c, char *out, size_t cap)
{
    PowerV.out_args.plan = plan;
    PowerV.out_args.temp_c = temp_c;
    PowerV.out_args.out = out;
    PowerV.out_args.cap = cap;
    Power.json(protocore_power_mgmt_span());
    return PowerV.n;
}

void dbench_run(void)
{
    // Governor limits pinned to the same values test/test_power_mgmt reasons about (which also match
    // the shipped PROTOCORE_POWER_* defaults), so these numbers describe the real decision.
    PowerCfg cfg;
    PowerV.cfg_out = &cfg;
    Power.defaults(protocore_power_mgmt_span());
    cfg.mhz_max = 240;
    cfg.mhz_min = 80;
    cfg.busy_pct = 40;
    cfg.temp_hot_c = 80;
    cfg.temp_cool_c = 70;
    cfg.recover_ms = 10000;

    static char json[128];

    for (;;)
    {
        DBENCH_BANNER("power_mgmt");
        volatile uint16_t sink_mhz = 0;
        volatile uint32_t sink_flags = 0;
        volatile size_t sink_len = 0;

        // Idle at a cool die: the load path drops to the floor (240 -> 80).
        DBENCH_OP("Power.decide idle->floor", 100000,
                  sink_mhz += power_decide(&cfg, 0, 40, PROTO_FALSE, 60000, PROTO_FALSE).cpu_mhz);

        // Busy at a cool die: the load path runs at the ceiling.
        DBENCH_OP("Power.decide busy->ceiling", 100000,
                  sink_mhz += power_decide(&cfg, 90, 40, PROTO_FALSE, 60000, PROTO_FALSE).cpu_mhz);

        // Hot die while busy: thermal outranks load and holds the floor (hysteresis branch, entering
        // throttle from was_throttled=false).
        DBENCH_OP("Power.decide hot->throttle", 100000,
                  sink_flags += power_decide(&cfg, 100, 85, PROTO_FALSE, 60000, PROTO_FALSE).throttled);

        // Brownout boot inside the settle window: recovery outranks everything and holds the floor.
        DBENCH_OP("Power.decide brownout->recover", 100000,
                  sink_flags += power_decide(&cfg, 100, 25, PROTO_TRUE, 0, PROTO_FALSE).recovering);

        // Serialize a plan to the {"cpu_mhz":...} JSON object (snprintf-backed, so a touch heavier).
        PowerPlan p = power_decide(&cfg, 90, 41, PROTO_FALSE, 60000, PROTO_FALSE);
        DBENCH_OP("Power.json serialize", 20000, sink_len += power_json(&p, 41, json, sizeof(json)));

        (void)sink_mhz;
        (void)sink_flags;
        (void)sink_len;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("power_mgmt")
