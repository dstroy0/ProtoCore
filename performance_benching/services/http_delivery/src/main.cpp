// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the HTTP delivery cores (services/file_transfer/http_delivery): the three
// pure, zero-heap, zero-stdlib functions that make HTTP serving cheaper on a constrained device -
//   - pc_delivery_swr():          RFC 5861 stale-while-revalidate freshness decision (FRESH /
//                                  serve-stale-and-revalidate / EXPIRED) from age + max-age + swr.
//   - pc_delivery_cache_control(): builds the matching "public, max-age=N[, stale-while-revalidate=M]"
//                                  header into a caller buffer.
//   - pc_delivery_sw_manifest():  serializes the versioned {"version":..,"precache":[..]} JSON a
//                                  generated service worker precaches the app shell from.
// All three are pure math/string-building, so every call here exercises the real production code
// path - like performance_benching/device/modbus, a pure codec with no hardware involved. The Arduino-only route
// registration half (pc_delivery_serve_sw, http_delivery_routes.cpp) needs a live PC server + real
// sockets and is deliberately OUT OF SCOPE on this rig; only the deterministic cores are benched.
// Byte-range/206 serving is network_drivers/application/http_range.h's job, not this service's, so it is not benched
// here.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/http_delivery -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/file_transfer/http_delivery/http_delivery.h"
#include <Arduino.h>

static void http_delivery_bench_task(void *)
{
    // Realistic inputs copied from test/test_http_delivery/test_http_delivery.cpp (known-good, spec-
    // conformant): max-age=60 / swr=30, and the {"/","/app.js","/style.css"} @ "v42" precache list.
    static const char *const paths[3] = {"/", "/app.js", "/style.css"};
    static char cc[64];                       // Cache-Control header build target
    static char mf[PC_DELIVERY_MANIFEST_BUF]; // precache manifest build target (shipped buffer size)

    for (;;)
    {
        Serial.printf("DB ==== http_delivery device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile int sinkv = 0;
        volatile size_t sinkn = 0;

        // Freshness verdict: a single branch + one uint64 add, the per-request hot path. Cheap -> large N.
        DBENCH_OP("pc_delivery_swr (stale)", 200000, sinkv += (int)pc_delivery_swr(75, 60, 30));
        // Cache-Control builder: hand-rolled decimal format of two windows into a small buffer.
        DBENCH_OP("pc_delivery_cache_control", 100000, sinkn += pc_delivery_cache_control(60, 30, cc, sizeof(cc)));
        // SW precache manifest: JSON-escaped serialization of the versioned path list (per /precache.json request).
        DBENCH_OP("pc_delivery_sw_manifest x3", 50000,
                  sinkn += pc_delivery_sw_manifest(paths, 3, "v42", mf, sizeof(mf)));

        (void)sinkv;
        (void)sinkn;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: http_delivery device microbench");
    xTaskCreatePinnedToCore(http_delivery_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
