// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file MsgPack.ino
 * @brief Encode and decode compact binary MessagePack (PROTOCORE_ENABLE_MSGPACK).
 *
 * GET /telemetry.msgpack encodes a small {heap, uptime, rssi} map with the
 * zero-heap MessagePack writer into a stack buffer and streams the bytes as
 * application/msgpack (via the binary-safe chunked writer). POST /decode runs the
 * other direction: it parses a posted MessagePack map with the cursor decoder
 * (MsgPack.peek / MsgPack.get_*, no heap, pointing into the request buffer) and
 * echoes the parsed integer fields as text. MessagePack is a widely-supported
 * compact binary format - the same idea as the CBOR example, in the format your
 * stack may prefer.
 *
 * NOTE: enable it for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_MSGPACK=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Try: curl -s http://<ip>/telemetry.msgpack | xxd
 *      printf '\x81\xa3led\x01' | curl -s --data-binary @- http://<ip>/decode
 */

#define PROTOCORE_ENABLE_MSGPACK 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
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
static size_t protocore_msgpack_source(uint8_t *out, size_t cap, void *vctx)
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
// each key (str) and value (int), checking r.err at the end.
static void on_decode(uint8_t id, HttpReq *req)
{
    mmgr_cspan r;
    const SpatiumCfg r_args = {.cbuf = req->body, .cap = req->body_len};
    r = spat.cfrom(&r_args);
    size_t count;
    if (!MsgPack.get_map(&r, &count))
    {
        send_text(id, 400, "text/plain", "expected a MessagePack map");
        return;
    }
    char out[160];
    size_t o = 0;
    for (size_t i = 0; i < count && !r.err; i++)
    {
        const char *key;
        size_t klen;
        int64_t val;
        if (!MsgPack.get_str(&r, &key, &klen) || !MsgPack.get_int(&r, &val))
        {
            break;
        }
        o += snprintf(out + o, sizeof(out) - o, "%.*s=%lld\n", (int)klen, key, (long long)val);
    }
    if (r.err)
    {
        send_text(id, 400, "text/plain", "malformed MessagePack");
        return;
    }
    send_text(id, 200, "text/plain", out);
}

void setup()
{
    Serial.begin(115200);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    while (!protocore_physical_wifi_up())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = protocore_physical_egress_ipv4(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/telemetry.msgpack", HTTP_GET, [](uint8_t id, HttpReq *) {
        static MpCtx ctx; // static: must outlive send_chunked
        mmgr_span w;
        const SpatiumCfg w_args = {.buf = ctx.buf, .cap = sizeof(ctx.buf)};
        w = spat.from(&w_args);
        MsgPack.put_map(&w, 3);
        MsgPack.put_str(&w, "heap");
        MsgPack.put_uint(&w, ESP.getFreeHeap());
        MsgPack.put_str(&w, "uptime");
        MsgPack.put_uint(&w, millis() / 1000);
        MsgPack.put_str(&w, "rssi");
        MsgPack.put_int(&w, protocore_physical_rssi());
        ctx.len = w.overflow ? 0 : w.pos;
        ctx.off = 0;
        send_chunked(id, 200, "application/msgpack", protocore_msgpack_source, &ctx);
    });
    on_http("/decode", HTTP_POST, on_decode);
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
