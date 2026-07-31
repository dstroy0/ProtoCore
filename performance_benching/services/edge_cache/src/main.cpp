// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CDN edge-cache pure engine (services/web/edge_cache):
// response header-field access, HTTP-date parsing (IMF-fixdate), RFC 9111 cache-key canonicalization
// + SHA-256 digest + Vary secondary-key serialization, and the L1 RAM store's O(slots) lookup - all
// pure (no sockets, no heap, no PC). Worked example for performance_benching/device/<service>/: a pure protocol
// codec with no hardware involved (contrast with performance_benching/device/ads1115, a peripheral driver where the
// bus transaction itself is stubbed). Deliberately out of scope: edge_cache_proxy (the socket glue),
// edge_cache_sd (the L2 SD tier), and edge_mesh (sibling replication) - none of those are pure, and
// this rig has no network/SD peripherals attached.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/edge_cache -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/web/edge_cache/edge_cache.h"
#include <Arduino.h>
#include <string.h>
#include <strings.h>

// Response head fixture (verbatim from test/test_edge_cache/test_edge_cache.cpp's RESP_HEAD).
static const char *RESP_HEAD = "HTTP/1.1 200 OK\r\n"
                               "ETag: \"abc123\"\r\n"
                               "Cache-Control:   max-age=60  \r\n"
                               "Last-Modified: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                               "Content-Type: text/html\r\n"
                               "\r\n";

// RFC 9110 sec 5.6.7 worked example date (also used verbatim in the host tests).
static const char *IMF_DATE = "Sun, 06 Nov 1994 08:49:37 GMT";

// Request-header lookup for edge_vary_serialize(), mirroring test_edge_cache.cpp's mock_lookup: a
// tiny fixed name->value table standing in for the real request-header accessor the socket glue
// (edge_cache_proxy) would supply.
static const char *vary_lookup(void *ctx, const char *name)
{
    (void)ctx;
    static const struct
    {
        const char *name;
        const char *value;
    } req_hdrs[] = {
        {"accept-encoding", "gzip"},
        {"accept-language", "en-US"},
    };
    for (const auto &h : req_hdrs)
    {
        if (strcasecmp(h.name, name) == 0)
        {
            return h.value;
        }
    }
    return nullptr;
}

static void edge_cache_bench_task(void *)
{
    static char hdr_out[64];
    static char key_out[128];
    static char vary_out[128];
    static uint8_t digest[32];

    // Canonical key built once; edge_key_digest is benched over its real length below.
    size_t key_len =
        edge_key_canon("GET", "Example.COM", "/a/b", "x=1", /*include_query=*/true, key_out, sizeof(key_out));

    // L1 store primed with one entry so edge_store_lookup exercises a real hit, not a full-table miss scan.
    static EdgeCacheStore store;
    edge_store_init(&store);
    edge_store_alloc(&store, "GET\nh\n/a", "");

    for (;;)
    {
        Serial.printf("DB ==== edge_cache device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile bool sinkb = false;
        volatile int64_t sink64 = 0;
        volatile size_t sinksz = 0;
        volatile void *sinkp = nullptr;

        DBENCH_OP("edge_header_value (ETag)", 100000,
                  sinkb = edge_header_value(RESP_HEAD, strlen(RESP_HEAD), "ETag", hdr_out, sizeof(hdr_out)));
        DBENCH_OP("edge_parse_http_date (IMF)", 100000, sink64 += edge_parse_http_date(IMF_DATE, strlen(IMF_DATE)));
        DBENCH_OP("edge_key_canon", 100000,
                  sinksz = edge_key_canon("GET", "Example.COM", "/a/b", "x=1", true, key_out, sizeof(key_out)));
        DBENCH_BULK("edge_key_digest (SHA-256)", 2000, key_len, edge_key_digest(key_out, key_len, digest));
        DBENCH_OP("edge_vary_serialize", 50000,
                  sinkb = edge_vary_serialize("Accept-Encoding, Accept-Language", vary_lookup, nullptr, vary_out,
                                              sizeof(vary_out)));
        DBENCH_OP("edge_store_lookup (L1 hit)", 50000, sinkp = edge_store_lookup(&store, "GET\nh\n/a", "", 100));

        (void)sinkb;
        (void)sink64;
        (void)sinksz;
        (void)sinkp;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: edge_cache device microbench");
    xTaskCreatePinnedToCore(edge_cache_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
