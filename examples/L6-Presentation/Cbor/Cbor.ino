// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Cbor.ino
 * @brief Serve telemetry as compact binary CBOR (PC_ENABLE_CBOR).
 *
 * Encodes a small {heap, uptime, rssi} map with the zero-heap CBOR writer into a
 * stack buffer and streams the bytes as application/cbor (via the chunked writer,
 * which is binary-safe). CBOR is a few bytes where the JSON equivalent is dozens,
 * which matters for high-rate telemetry or constrained uplinks.
 *
 * NOTE: enable it for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_CBOR=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Try: curl -s http://<ip>/telemetry.cbor | xxd
 */

#define PC_ENABLE_CBOR 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/presentation/codec/cbor/cbor.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// One CBOR map {"heap","uptime","rssi"}, encoded once into a ctx buffer and then
// paged out by the chunk source. (The body is tiny, but the same pattern serves an
// arbitrarily large one: produce the next slice each call, return 0 when drained.)
struct CborCtx
{
    uint8_t buf[64];
    size_t len, off;
};
static size_t pc_cbor_source(uint8_t *out, size_t cap, void *vctx)
{
    CborCtx *c = (CborCtx *)vctx;
    if (c->off >= c->len)
    {
        return 0;
    }
    size_t n = c->len - c->off;
    if (n > cap)
    {
        n = cap;
    }
    memcpy(out, c->buf + c->off, n);
    c->off += n;
    return n;
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

    server.on("/telemetry.cbor", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        static CborCtx ctx; // static: must outlive send_chunked
        pc_span w;
        w = pc_span_from(ctx.buf, sizeof(ctx.buf));
        pc_cbor_map(&w, 3);
        pc_cbor_str(&w, "heap");
        pc_cbor_uint(&w, ESP.getFreeHeap());
        pc_cbor_str(&w, "uptime");
        pc_cbor_uint(&w, millis() / 1000);
        pc_cbor_str(&w, "rssi");
        pc_cbor_int(&w, pc_net_rssi());
        ctx.len = pc_span_ok(w) ? pc_span_len(w) : 0;
        ctx.off = 0;
        server.send_chunked(id, 200, "application/cbor", pc_cbor_source, &ctx);
    });
    server.begin(80);
}

void loop()
{
    server.handle();
}
