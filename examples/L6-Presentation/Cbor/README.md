# Cbor - compact binary telemetry with CBOR

**Layer:** L6 Presentation · **Build flags:** `PC_ENABLE_CBOR`

## What this example teaches

CBOR (RFC 8949) encodes the same data as JSON in a fraction of the bytes, which
matters for high-rate telemetry or constrained uplinks. This serves a small
`{heap, uptime, rssi}` map as `application/cbor` using the zero-heap CBOR writer,
streamed through the binary-safe chunked writer.

**Encoding with `pc_span`.** You initialize the writer over a stack buffer,
declare the map size, then emit key/value pairs; `pc_cbor_ok()` reports overflow and
`pc_cbor_len()` gives the encoded length:

```cpp
uint8_t buf[64];
pc_span w;
w = pc_span_from( buf, sizeof(buf));
pc_cbor_map(&w, 3);                    // a 3-entry map
pc_cbor_str(&w, "heap"); pc_cbor_uint(&w, ESP.getFreeHeap());
pc_cbor_str(&w, "uptime"); pc_cbor_uint(&w, millis() / 1000);
pc_cbor_str(&w, "rssi"); pc_cbor_int(&w, pc_net_rssi());   // signed
ctx.len = pc_span_ok(w) ? pc_span_len(w) : 0;           // page these bytes out below
```

**Why `send_chunked()`.** The response is binary, so it is sent with the
binary-safe `send_chunked()` rather than the C-string `send()`. `send_chunked()`
pulls the body from a `ChunkSource` generator, which hands back the encoded bytes a
slice at a time (here the whole small map in one go) and returns 0 to finish:

```cpp
struct CborCtx { uint8_t buf[64]; size_t len, off; };
static size_t pc_cbor_source(uint8_t *out, size_t cap, void *vctx) {
    CborCtx *c = (CborCtx *)vctx;
    if (c->off >= c->len) return 0;                 // done
    size_t n = c->len - c->off; if (n > cap) n = cap;
    memcpy(out, c->buf + c->off, n); c->off += n; return n;
}
...
static CborCtx ctx;                                 // static: must outlive the call
/* encode into ctx.buf, set ctx.len/off ... */
server.send_chunked(id, 200, "application/cbor", pc_cbor_source, &ctx);
```

For a text-based compact binary alternative, see [MsgPack](../MsgPack).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_CBOR=1" \
  --lib="." examples/L6-Presentation/Cbor/Cbor.ino
```

```sh
curl -s http://<ip>/telemetry.cbor | xxd      # inspect the compact binary
```

## Annotated source

The complete sketch ([Cbor.ino](Cbor.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_CBOR 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/presentation/codec/cbor/cbor.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// One CBOR map {"heap","uptime","rssi"}, encoded into a ctx buffer and paged out
// by the chunk source (the same pattern scales to an arbitrarily large body).
struct CborCtx
{
    uint8_t buf[64];
    size_t len, off;
};
static size_t pc_cbor_source(uint8_t *out, size_t cap, void *vctx)
{
    CborCtx *c = (CborCtx *)vctx;
    if (c->off >= c->len)
        return 0; // done
    size_t n = c->len - c->off;
    if (n > cap)
        n = cap;
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
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/telemetry.cbor", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        static CborCtx ctx; // static: must outlive send_chunked
        pc_span w;
        w = pc_span_from( ctx.buf, sizeof(ctx.buf));
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
```
