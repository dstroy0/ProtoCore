// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file EdgeCache.ino
 * @brief Cache an upstream origin at the edge (PC_ENABLE_EDGE_CACHE).
 *
 * The board acts as a caching reverse-proxy: a GET/HEAD under a mapped prefix is fetched from the
 * upstream origin once, cached in RAM, and subsequent hits are served from the board - honoring
 * Cache-Control / ETag / Last-Modified, revalidating stale entries with a conditional request, and
 * never stalling the server (the origin fetch runs asynchronously while the client request is
 * suspended). A /cache/stats route reports the counters and /cache/purge invalidates by prefix.
 *
 * Wiring is two calls: pc_edge_cache_map() binds a path prefix to an origin, pc_edge_cache_enable()
 * installs the cache on the server. Edit the "CHANGE ME" lines, flash, open Serial @ 115200, then
 * request GET /cdn/<path> and watch the X-Cache header flip from MISS to HIT.
 *
 * NOTE (PlatformIO): the cache is compiled into the *library*, so the flags must reach the whole build:
 * build_flags = -DPC_ENABLE_EDGE_CACHE=1 -DPC_ENABLE_HTTP_CACHE=1 -DPC_ENABLE_HTTP_CLIENT=1.
 * In the Arduino IDE they are set for you in build_opt.h.
 */

#define PC_ENABLE_EDGE_CACHE 1
#define PC_ENABLE_HTTP_CACHE 1
#define PC_ENABLE_HTTP_CLIENT 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/web/edge_cache/edge_cache_proxy.h"

// --- Optional L2 (SD) persistence tier: set PC_ENABLE_DBM=1 (with an SD card) to keep the cached set
// across a reboot. Evicted L1 entries spill to a dbm store on the WAL; a reboot replays the log and the
// first request for a persisted URL revalidates it with a cheap conditional GET instead of re-downloading.
#if PC_ENABLE_DBM
#include "services/storage/dbm/dbm.h"
#include "services/storage/wal/wal_fs.h"
#include "services/storage/wal/wal_store.h"
#include <SD.h>
#endif

// --- CHANGE ME: your WiFi ---
static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// --- CHANGE ME: the upstream origin the board caches in front of (plaintext http:// only in v1) ---
static const char *ORIGIN = "http://192.168.1.60:8000";


#if PC_ENABLE_DBM
// L2 persistence: a dbm store on a WAL file on the SD card. The file stays open for the store's lifetime.
static constexpr uint64_t L2_WAL_BYTES = 512 * 1024; // backing file size (holds the persisted cache set)
static fs::File g_wal_file;
static WalStore g_wal;
static pc_dbm g_l2;

// Mount (or format) the WAL file on SD, open the dbm over it, and bind it as the cache's L2 tier.
static bool setup_l2()
{
    if (!SD.begin())
    {
        return false;
    }
    if (!pc_wal_fs_prealloc(SD, "/edgecache.wal", L2_WAL_BYTES))
    {
        return false;
    }
    g_wal_file = SD.open("/edgecache.wal", "r+"); // random read+write, no truncation
    if (!g_wal_file)
    {
        return false;
    }
    WalDev dev = pc_wal_fs_dev(&g_wal_file, L2_WAL_BYTES);
    if (!pc_wal_store_mount(&g_wal, &dev) && !pc_wal_store_format(&g_wal, &dev)) // recover, else initialize
    {
        return false;
    }
    if (!pc_dbm_open(&g_l2, &g_wal)) // rebuild the index by replaying the log (this is reboot recovery)
    {
        return false;
    }
    pc_edge_cache_bind_sd(&g_l2);
    return true;
}
#endif

// GET /cache/stats - the cache counters as JSON (not itself cached: it is outside the /cdn/ prefix).
static void handle_stats(uint8_t slot, HttpReq *req)
{
    (void)req;
    EdgeCacheStats st;
    pc_edge_cache_stats(&st);
    char body[288];
    snprintf(body, sizeof(body),
             "{\"hits\":%u,\"misses\":%u,\"revalidations\":%u,\"replaces\":%u,\"stores\":%u,\"evictions\":%u,"
             "\"purges\":%u,\"l2_spills\":%u,\"l2_promotes\":%u}",
             (unsigned)st.hits, (unsigned)st.misses, (unsigned)st.revalidations_304, (unsigned)st.replaces_200,
             (unsigned)st.stores, (unsigned)st.evictions, (unsigned)st.purges, (unsigned)st.l2_spills,
             (unsigned)st.l2_promotes);
    send_text(slot, 200, "application/json", body);
}

// POST /cache/purge - invalidate everything cached under the mapped prefix.
static void handle_purge(uint8_t slot, HttpReq *req)
{
    (void)req;
    uint32_t n = pc_edge_cache_purge_prefix("/cdn/");
    char body[48];
    snprintf(body, sizeof(body), "{\"purged\":%u}", (unsigned)n);
    send_text(slot, 200, "application/json", body);
}

void setup()
{
    Serial.begin(115200);

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Cache everything under /cdn/ from the origin, then enable the cache on the server.
    pc_edge_cache_map("/cdn/", ORIGIN);
    pc_edge_cache_enable(server);
#if PC_ENABLE_DBM
    Serial.println(setup_l2() ? "L2 SD tier: mounted (cache survives reboot)" : "L2 SD tier: unavailable (no SD?)");
#endif
    on_http("/cache/stats", HTTP_GET, handle_stats);
    on_http("/cache/purge", HTTP_POST, handle_purge);
    begin_http(80, NULL); // serve HTTP on port 80 (begin() with no port opens no listener)

    Serial.printf("edge cache in front of %s\n", ORIGIN);
    Serial.printf("GET http://%u.%u.%u.%u/cdn/<path> - X-Cache: MISS then HIT\n", (unsigned)(ip & 0xFF),
                  (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
    Serial.println("GET /cache/stats for counters; POST /cache/purge to invalidate /cdn/");
}

void loop()
{
    handle(); // the server poll loop drives the async origin fetch + the cached send
#if PC_ENABLE_DBM
    // Make L2 spills durable on a cadence (batched appends are checkpointed in bulk, per the WAL design).
    static uint32_t last_sync = 0;
    if (millis() - last_sync > 5000)
    {
        last_sync = millis();
        pc_dbm_sync(&g_l2);
    }
#endif
}
