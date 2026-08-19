// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the HTTP Cache-Control helpers (network_drivers/presentation/http/httpcache): the
// RFC 9111 (+ 8246 / 5861) directive builder, the tolerant directive parser, a first-class origin
// preset, and the freshness-lifetime calculation. Every op here is pure text - no heap, no stdlib
// allocation, no sockets - so like performance_benching/device/modbus (and unlike a peripheral driver such as
// performance_benching/device/ads1115) each call exercises the real production code path directly. There is no
// hardware or transport to stub: the caching proxy/tier (RAM/SD storage, cache key, invalidation)
// is a separate, out-of-scope piece; only the standards-mechanics text codec is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/httpcache -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "network_drivers/presentation/http/httpcache/httpcache.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t httpcache_work[16]; // the borrow an entry takes; Httpcache never reads it

void dbench_run(void)
{
    // A fully-populated directive set (mirrors test_build_all_directives) - exercises every emit
    // branch of the builder (bare tokens, "key=value" deltas, and the bare max-stale case).
    protocore_cache_control full;
    Httpcache.control_init_args.cc = &full;
    Httpcache.control_init(httpcache_work);
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
    protocore_cache_control fresh;
    Httpcache.control_init_args.cc = &fresh;
    Httpcache.control_init(httpcache_work);
    fresh.max_age = 100;
    fresh.s_maxage = 200;

    static char out[128];

    for (;;)
    {
        DBENCH_BANNER("httpcache");
        volatile size_t sink = 0;
        volatile long lsink = 0;
        protocore_cache_control scratch;

        // The operands do not vary across iterations, so each is staged once above its macro;
        // only the call is inside the timed loop.
        Httpcache.control_build_args.buf = out;
        Httpcache.control_build_args.cap = sizeof(out);
        Httpcache.control_build_args.cc = &full;
        // Build the full directive set into a text buffer (all emit branches).
        DBENCH_OP("Httpcache.control_build full", 50000,
                  sink += (Httpcache.control_build(httpcache_work), Httpcache.n));

        Httpcache.control_parse_args.s = kResponseHdr;
        Httpcache.control_parse_args.len = kResponseLen;
        Httpcache.control_parse_args.cc = &scratch;
        // Tolerant parse of a rich response Cache-Control value.
        DBENCH_OP("Httpcache.control_parse response", 50000,
                  sink += (Httpcache.control_parse(httpcache_work), (size_t)Httpcache.ok));

        Httpcache.immutable_asset_args.cc = &scratch;
        Httpcache.immutable_asset_args.max_age = 31536000u;
        // First-class origin preset: fill for a 1-year immutable static asset.
        DBENCH_OP("Httpcache.immutable_asset preset", 200000, Httpcache.immutable_asset(httpcache_work));

        Httpcache.freshness_lifetime_args.cc = &fresh;
        Httpcache.freshness_lifetime_args.shared = true;
        Httpcache.freshness_lifetime_args.expires_minus_date = 999;
        // RFC 9111 sec 4.2.1 freshness-lifetime precedence (shared cache).
        DBENCH_OP("Httpcache.freshness_lifetime", 200000,
                  lsink += (Httpcache.freshness_lifetime(httpcache_work), Httpcache.value));

        (void)sink;
        (void)lsink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("httpcache")
