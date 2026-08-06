// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file MsgPack.ino
 * @brief Encode and decode compact binary MessagePack (PC_ENABLE_MSGPACK).
 *
 * GET /telemetry.msgpack encodes a small {heap, uptime, rssi} map with the
 * zero-heap MessagePack writer into a stack buffer and streams the bytes as
 * application/msgpack (via the binary-safe chunked writer). POST /decode runs the
 * other direction: it parses a posted MessagePack map with the cursor decoder
 * (pc_msgpack_peek / pc_msgpack_read_*, no heap, pointing into the request buffer) and
 * echoes the parsed integer fields as text. MessagePack is a widely-supported
 * compact binary format - the same idea as the CBOR example, in the format your
 * stack may prefer.
 *
 * NOTE: enable it for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_MSGPACK=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Try: curl -s http://<ip>/telemetry.msgpack | xxd
 *      printf '\x81\xa3led\x01' | curl -s --data-binary @- http://<ip>/decode
 */

#define PC_ENABLE_MSGPACK 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/presentation/codec/msgpack/msgpack.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// One MessagePack map {"heap","uptime","rssi"}, encoded into a ctx buffer and
// paged out by the chunk source (the same pattern scales to an arbitrarily large
// body: produce the next slice each call, return 0 when drained).
struct MpCtx
{
    uint8_t buf[64];
    size_t len, off;
};
static size_t pc_msgpack_source(uint8_t *out, size_t cap, void *vctx)
{
    MpCtx *c = (MpCtx *)vctx;
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

// Decodes a posted MessagePack map of {string: integer} and echoes each parsed
// field as "key=value" text. Shows the cursor decoder: read the map header, then
// each key (str) and value (int), checking pc_cspan_ok() at the end.
static void on_decode(uint8_t id, HttpReq *req)
{
    pc_cspan r;
    r = pc_cspan_from(req->body, req->body_len);
    size_t count;
    if (!pc_msgpack_read_map(&r, &count))
    {
        send_text(id, 400, "text/plain", "expected a MessagePack map");
        return;
    }
    char out[160];
    size_t o = 0;
    for (size_t i = 0; i < count && pc_cspan_ok(r); i++)
    {
        const char *key;
        size_t klen;
        int64_t val;
        if (!pc_msgpack_read_str(&r, &key, &klen) || !pc_msgpack_read_int(&r, &val))
        {
            break;
        }
        o += snprintf(out + o, sizeof(out) - o, "%.*s=%lld\n", (int)klen, key, (long long)val);
    }
    if (!pc_cspan_ok(r))
    {
        send_text(id, 400, "text/plain", "malformed MessagePack");
        return;
    }
    send_text(id, 200, "text/plain", out);
}

void setup()
{
    Serial.begin(115200);
    Physical.wifi->init(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/telemetry.msgpack", HTTP_GET, [](uint8_t id, HttpReq *) {
        static MpCtx ctx; // static: must outlive send_chunked
        pc_span w;
        w = pc_span_from(ctx.buf, sizeof(ctx.buf));
        pc_msgpack_map(&w, 3);
        pc_msgpack_str(&w, "heap");
        pc_msgpack_uint(&w, ESP.getFreeHeap());
        pc_msgpack_str(&w, "uptime");
        pc_msgpack_uint(&w, millis() / 1000);
        pc_msgpack_str(&w, "rssi");
        pc_msgpack_int(&w, Physical.wifi->rssi());
        ctx.len = pc_span_ok(w) ? pc_span_len(w) : 0;
        ctx.off = 0;
        send_chunked(id, 200, "application/msgpack", pc_msgpack_source, &ctx);
    });
    on_http("/decode", HTTP_POST, on_decode);
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
