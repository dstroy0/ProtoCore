// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the hash-chained audit log (services/security/audit_log):
// pc_audit_append() (SHA-256 chain-hash a new record onto the ring, pc_sha256 HW-accelerated
// on ESP32), pc_audit_verify() (recompute the chain over the retained window), and the JSON
// renderers pc_audit_format()/pc_audit_dump_json() - all pure (fixed RAM ring, no heap, no
// storage/network sink attached), so every call here exercises the real production code path.
// Worked example for performance_benching/device/<service>/: a pure protocol/state codec with no hardware
// involved (contrast with performance_benching/device/ads1115, a peripheral driver where the bus transaction
// itself is stubbed).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/audit_log -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/security/audit_log/audit_log.h"
#include <Arduino.h>

static void audit_log_bench_task(void *)
{
    static char fmt_buf[256];
    static char dump_buf[8192];

    for (;;)
    {
        pc_audit_reset();
        pc_audit_set_sink(nullptr);

        Serial.printf("DB ==== audit_log device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());

        volatile uint32_t sink32 = 0;
        // Append: SHA-256(prev_hash || seq || ts || category || msg) chained onto the ring
        // (ring wraps after PC_AUDIT_LOG_ENTRIES records, exercising the moving-anchor eviction
        // path too) - the hot path a worker hits on every security-relevant event.
        DBENCH_OP("pc_audit_append", 5000,
                  sink32 +=
                  pc_audit_append(pc_audit_cat::PC_AUDIT_ACCESS, "GET /api/v1/sensors/42 200 OK from 10.0.0.5"));

        // Verify: recompute the chain hash over the full retained window (by now wrapped to
        // PC_AUDIT_LOG_ENTRIES records) - one SHA-256 per retained record.
        volatile bool sinkb = false;
        DBENCH_OP("pc_audit_verify (full ring)", 500, sinkb += pc_audit_verify(nullptr));
        (void)sinkb;

        // Format: render one retained record as a JSON object (hex hash + JSON-escaped msg).
        const pc_audit_entry *e = pc_audit_at(0);
        volatile int sinki = 0;
        DBENCH_OP("pc_audit_format", 20000, sinki += pc_audit_format(e, fmt_buf, sizeof(fmt_buf)));

        // Dump: verify + render the entire retained window as one JSON document (what an
        // endpoint handler would call to serve the audit log).
        DBENCH_OP("pc_audit_dump_json (full ring)", 1000, sinki += pc_audit_dump_json(dump_buf, sizeof(dump_buf)));
        (void)sinki;
        (void)sink32;

        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: audit_log device microbench");
    xTaskCreatePinnedToCore(audit_log_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
