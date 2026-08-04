// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file MeshCache.ino
 * @brief Share a warm edge cache across a fleet (PC_ENABLE_EDGE_MESH).
 *
 * Two (or more) edge nodes form a sibling cache: on a cold local miss a node asks its configured peers
 * over a plaintext ConnProto::PROTO_MESH link before hitting the origin, and pulls a fresh copy from
 * whichever peer already has it - so the origin is fetched once per fleet, not once per node. The pull
 * carries the object's freshness/age, so a sibling-fresh object is served for its remaining lifetime with
 * zero origin contact. A serving node answers only from its LOCAL cache (one hop - it never re-queries its
 * own origin or peers, so the fleet cannot loop). Pull only: no push, no invalidation.
 *
 * This is the edge-cache example (79) plus three calls: listen(MESH_PORT, PROTO_MESH) opens the
 * sibling port, pc_edge_cache_mesh_serve() answers peers from the local cache, and pc_edge_cache_add_peer()
 * lists a sibling to query on a miss. Flash the SAME sketch to both boards, giving each the OTHER's IP as
 * PEER_IP. Warm node A (request GET /cdn/<path> from A), then request the same URL from node B: B reports
 * X-Cache: MESH and the origin never sees B.
 *
 * NOTE (PlatformIO): the cache is compiled into the *library*, so the flags must reach the whole build:
 * build_flags = -DPC_ENABLE_EDGE_CACHE=1 -DPC_ENABLE_HTTP_CACHE=1 -DPC_ENABLE_HTTP_CLIENT=1
 * -DPC_ENABLE_EDGE_MESH=1. In the Arduino IDE they are set for you in build_opt.h.
 *
 * SIZING: build_opt.h dials the cache down (PC_EDGE_CACHE_SLOTS=2, PC_EDGE_FETCH_SLOTS=1,
 * PC_MESH_MAX_PEERS=1) so the sketch's static BSS fits the classic ESP32's smaller DRAM. On a PSRAM
 * board bump the slots/fetch/peer counts back up (the defaults are 4/2/4) for a warmer, larger cache.
 */

#define PC_ENABLE_EDGE_CACHE 1
#define PC_ENABLE_HTTP_CACHE 1
#define PC_ENABLE_HTTP_CLIENT 1
#define PC_ENABLE_EDGE_MESH 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/web/edge_cache/edge_cache_proxy.h"

// --- CHANGE ME: your WiFi ---
static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// --- CHANGE ME: the upstream origin both nodes cache in front of ---
static const char *ORIGIN = "http://192.168.1.60:8000";

// --- CHANGE ME: the OTHER node's IP (node A -> B's IP; node B -> A's IP) and the shared mesh port ---
static const char *PEER_IP = "192.168.1.51";
static const uint16_t MESH_PORT = 7645;


// GET /cache/stats - the cache counters as JSON (mesh_hits/mesh_misses show the sibling sharing at work).
static void handle_stats(uint8_t slot, HttpReq *req)
{
    (void)req;
    EdgeCacheStats st;
    pc_edge_cache_stats(&st);
    char body[320];
    snprintf(body, sizeof(body),
             "{\"hits\":%u,\"misses\":%u,\"mesh_hits\":%u,\"mesh_misses\":%u,\"revalidations\":%u,\"stores\":%u,"
             "\"evictions\":%u,\"purges\":%u}",
             (unsigned)st.hits, (unsigned)st.misses, (unsigned)st.mesh_hits, (unsigned)st.mesh_misses,
             (unsigned)st.revalidations_304, (unsigned)st.stores, (unsigned)st.evictions, (unsigned)st.purges);
    send_text(slot, 200, "application/json", body);
}

// POST /cache/purge - invalidate everything cached under the mapped prefix (local only).
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

    // Mesh: open the sibling port, answer peers from the local cache, and list the other node as a peer.
    int32_t li = listen(MESH_PORT, PROTO_MESH);
    if (li < 0)
    {
        Serial.println("mesh: could not open the sibling listener");
    }
    pc_edge_cache_mesh_serve();
    pc_edge_cache_add_peer(PEER_IP, MESH_PORT);

    on_http("/cache/stats", HTTP_GET, handle_stats);
    on_http("/cache/purge", HTTP_POST, handle_purge);
    begin_http(80, NULL); // serve HTTP on port 80

    Serial.printf("mesh edge cache in front of %s (peer %s:%u)\n", ORIGIN, PEER_IP, MESH_PORT);
    Serial.printf("GET http://%u.%u.%u.%u/cdn/<path> - a cold miss pulls from the peer (X-Cache: MESH) before the "
                  "origin\n",
                  (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
                  (unsigned)((ip >> 24) & 0xFF));
    Serial.println("GET /cache/stats for counters (watch mesh_hits); POST /cache/purge to invalidate /cdn/");
}

void loop()
{
    handle(); // the poll loop drives the sibling query, the origin fetch, and the cached send
}
