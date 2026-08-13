// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the hash-chained audit log (services/security/audit_log):
// protocore_audit_append() (SHA-256 chain-hash a new record onto the ring, protocore_sha256 HW-accelerated
// on ESP32), protocore_audit_verify() (recompute the chain over the retained window), and the JSON
// renderers protocore_audit_format()/protocore_audit_dump_json() - all pure (fixed RAM ring, no heap, no
// storage/network sink attached), so every call here exercises the real production code path.
// Worked example for performance_benching/device/<service>/: a pure protocol/state codec with no hardware
// involved (contrast with performance_benching/device/ads1115, a peripheral driver where the bus transaction
// itself is stubbed).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/audit_log -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/security/audit_log/audit_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static char fmt_buf[256];
    static char dump_buf[8192];

    for (;;)
    {
        protocore_audit_reset();
        protocore_audit_set_sink(NULL);

        DBENCH_BANNER("audit_log");

        volatile uint32_t sink32 = 0;
        // Append: SHA-256(prev_hash || seq || ts || category || msg) chained onto the ring
        // (ring wraps after PROTOCORE_AUDIT_LOG_ENTRIES records, exercising the moving-anchor eviction
        // path too) - the hot path a worker hits on every security-relevant event.
        DBENCH_OP("protocore_audit_append", 5000,
                  sink32 += protocore_audit_append(PROTOCORE_AUDIT_ACCESS, "GET /api/v1/sensors/42 200 OK from 10.0.0.5"));

        // Verify: recompute the chain hash over the full retained window (by now wrapped to
        // PROTOCORE_AUDIT_LOG_ENTRIES records) - one SHA-256 per retained record.
        volatile bool sinkb = false;
        DBENCH_OP("protocore_audit_verify (full ring)", 500, sinkb += protocore_audit_verify(NULL));
        (void)sinkb;

        // Format: render one retained record as a JSON object (hex hash + JSON-escaped msg).
        const protocore_audit_entry *e = protocore_audit_at(0);
        volatile int sinki = 0;
        DBENCH_OP("protocore_audit_format", 20000, sinki += protocore_audit_format(e, fmt_buf, sizeof(fmt_buf)));

        // Dump: verify + render the entire retained window as one JSON document (what an
        // endpoint handler would call to serve the audit log).
        DBENCH_OP("protocore_audit_dump_json (full ring)", 1000, sinki += protocore_audit_dump_json(dump_buf, sizeof(dump_buf)));
        (void)sinki;
        (void)sink32;

        DBENCH_DONE();
    }
}

DBENCH_MAIN("audit_log")
