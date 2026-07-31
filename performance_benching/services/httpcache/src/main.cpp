// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the HTTP Cache-Control helpers (services/web/httpcache): the
// RFC 9111 (+ 8246 / 5861) directive builder, the tolerant directive parser, a first-class origin
// preset, and the freshness-lifetime calculation. Every op here is pure text - no heap, no stdlib
// allocation, no sockets - so like performance_benching/device/modbus (and unlike a peripheral driver such as
// performance_benching/device/ads1115) each call exercises the real production code path directly. There is no
// hardware or transport to stub: the caching proxy/tier (RAM/SD storage, cache key, invalidation)
// is a separate, out-of-scope piece; only the standards-mechanics text codec is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/httpcache -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/web/httpcache/httpcache.h"
#include <Arduino.h>

static void httpcache_bench_task(void *)
{
    // A fully-populated directive set (mirrors test_build_all_directives) - exercises every emit
    // branch of the builder (bare tokens, "key=value" deltas, and the bare max-stale case).
    pc_cache_control full;
    cache_control_init(&full);
    full.cc_private = true;
    full.no_cache = true;
    full.max_age = 10;
    full.s_maxage = 20;
    full.must_revalidate = true;
    full.proxy_revalidate = true;
    full.no_transform = true;
    full.must_understand = true;
    full.cc_immutable = true;
    full.stale_while_revalidate = 5;
    full.stale_if_error = 6;
    full.only_if_cached = true;
    full.min_fresh = 7;
    full.max_stale = 8;

    // A rich, spec-conformant response header to parse (copied from test_parse_all_directives).
    static const char kResponseHdr[] = "private, no-cache, no-transform, must-revalidate, proxy-revalidate, "
                                       "must-understand, immutable, only-if-cached, stale-while-revalidate=30";
    static const size_t kResponseLen = sizeof(kResponseHdr) - 1;

    // A freshness input with both s-maxage and max-age set (shared cache honors s-maxage first).
    pc_cache_control fresh;
    cache_control_init(&fresh);
    fresh.max_age = 100;
    fresh.s_maxage = 200;

    static char out[128];

    for (;;)
    {
        Serial.printf("DB ==== httpcache device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile long lsink = 0;
        pc_cache_control scratch;

        // Build the full directive set into a text buffer (all emit branches).
        DBENCH_OP("cache_control_build full", 50000, sink += cache_control_build(out, sizeof(out), &full));
        // Tolerant parse of a rich response Cache-Control value.
        DBENCH_OP("cache_control_parse response", 50000,
                  sink += (size_t)cache_control_parse(kResponseHdr, kResponseLen, &scratch));
        // First-class origin preset: fill for a 1-year immutable static asset.
        DBENCH_OP("cache_immutable_asset preset", 200000, cache_immutable_asset(&scratch, 31536000u));
        // RFC 9111 sec 4.2.1 freshness-lifetime precedence (shared cache).
        DBENCH_OP("cache_freshness_lifetime", 200000, lsink += cache_freshness_lifetime(&fresh, true, 999));

        (void)sink;
        (void)lsink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: httpcache device microbench");
    xTaskCreatePinnedToCore(httpcache_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
