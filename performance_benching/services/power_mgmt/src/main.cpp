// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SoC power governor (services/system/power_mgmt): the pure clock
// decision pc_power_plan() - load-based scaling, the thermal hysteresis that keeps a part parked at
// the limit from oscillating, and post-brownout recovery - plus pc_power_json() which serializes a
// plan. All of these take every input explicitly and touch no hardware, so each call here exercises
// the real production decision path (like performance_benching/device/modbus, a pure codec).
//
// Deliberately out of scope: the device binding (pc_power_brownout_boot / pc_power_temp_c /
// pc_power_apply / pc_power_cpu_mhz / pc_power_gate_bt). Those read esp_reset_reason() and the die
// sensor and actually re-clock the core / release the BT domain - a real side effect on the SoC, not
// a deterministic bit of math - so they are never benched. They still compile into the library (their
// esp_* / Arduino symbols are provided by the framework) but this sketch never calls them.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/power_mgmt -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/system/power_mgmt/power_mgmt.h"
#include <Arduino.h>

static void power_mgmt_bench_task(void *)
{
    // Governor limits pinned to the same values test/test_power_mgmt reasons about (which also match
    // the shipped PC_POWER_* defaults), so these numbers describe the real decision.
    PowerCfg cfg;
    pc_power_cfg_defaults(&cfg);
    cfg.mhz_max = 240;
    cfg.mhz_min = 80;
    cfg.busy_pct = 40;
    cfg.temp_hot_c = 80;
    cfg.temp_cool_c = 70;
    cfg.recover_ms = 10000;

    static char json[128];

    for (;;)
    {
        Serial.printf("DB ==== power_mgmt device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint16_t sink_mhz = 0;
        volatile uint32_t sink_flags = 0;
        volatile size_t sink_len = 0;

        // Idle at a cool die: the load path drops to the floor (240 -> 80).
        DBENCH_OP("pc_power_plan idle->floor", 100000,
                  sink_mhz += pc_power_plan(&cfg, 0, 40, false, 60000, false).cpu_mhz);

        // Busy at a cool die: the load path runs at the ceiling.
        DBENCH_OP("pc_power_plan busy->ceiling", 100000,
                  sink_mhz += pc_power_plan(&cfg, 90, 40, false, 60000, false).cpu_mhz);

        // Hot die while busy: thermal outranks load and holds the floor (hysteresis branch, entering
        // throttle from was_throttled=false).
        DBENCH_OP("pc_power_plan hot->throttle", 100000,
                  sink_flags += pc_power_plan(&cfg, 100, 85, false, 60000, false).throttled);

        // Brownout boot inside the settle window: recovery outranks everything and holds the floor.
        DBENCH_OP("pc_power_plan brownout->recover", 100000,
                  sink_flags += pc_power_plan(&cfg, 100, 25, true, 0, false).recovering);

        // Serialize a plan to the {"cpu_mhz":...} JSON object (snprintf-backed, so a touch heavier).
        PowerPlan p = pc_power_plan(&cfg, 90, 41, false, 60000, false);
        DBENCH_OP("pc_power_json serialize", 20000, sink_len += pc_power_json(&p, 41, json, sizeof(json)));

        (void)sink_mhz;
        (void)sink_flags;
        (void)sink_len;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: power_mgmt device microbench");
    xTaskCreatePinnedToCore(power_mgmt_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
