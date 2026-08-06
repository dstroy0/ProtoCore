# ChunkedResponse - streaming a response of unknown length

**Layer:** L7 Application · **Build flags:** none (core features only)

## What this example teaches

When you do not know the response size up front (or it is large), you stream it
with HTTP chunked transfer encoding instead of building the whole body in a
buffer. `send_chunked()` writes the status and headers
(`Transfer-Encoding: chunked`), then pulls the body from your generator one piece
at a time, frames each as a chunk, and writes the terminating chunk - so output
size is unbounded but memory stays constant, and the send paces with the TCP
window (paging across loops) so a body larger than the send buffer is never
truncated.

**A pull generator (`ChunkSource`).** You pass a function that writes the next
body bytes into `buf` (up to `cap`) and returns the count, or `0` to end. It
tracks its position in a `ctx` you provide - one call produces one chunk:

```cpp
struct LinesCtx { int i, n; };
static size_t lines_source(uint8_t *buf, size_t cap, void *vctx) {
    LinesCtx *c = (LinesCtx *)vctx;
    if (c->i >= c->n) return 0;                                  // end of body
    int len = snprintf((char *)buf, cap, "line %d of %d\n", c->i + 1, c->n);
    c->i++;
    return (size_t)(len < (int)cap ? len : (int)cap);            // one chunk
}
...
static LinesCtx ctx; ctx.i = 0; ctx.n = 50;                      // must outlive the call
server.send_chunked(slot_id, 200, "text/plain", lines_source, &ctx);
```

The `ctx` must outlive `send_chunked()` (a large body finishes on a later loop),
so it is `static`, not on the stack. The same `send_chunked()` is what the binary
[CBOR](../../L6-Presentation/Cbor) and
[MessagePack](../../L6-Presentation/MsgPack) examples use to stream binary
payloads.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L7-Application/ChunkedResponse/ChunkedResponse.ino
```

```sh
curl -N http://<ip>/stream     # 50 lines arrive incrementally
curl -N http://<ip>/count      # one number per chunk
```

## Annotated source

The complete sketch ([ChunkedResponse.ino](ChunkedResponse.ino)),
reproduced verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Source: emit `n` numbered lines, one line per chunk, resuming from ctx->i.
struct LinesCtx
{
    int i, n;
};
static size_t lines_source(uint8_t *buf, size_t cap, void *vctx)
{
    LinesCtx *c = (LinesCtx *)vctx;
    if (c->i >= c->n)
        return 0; // done
    int len = snprintf((char *)buf, cap, "line %d of %d\n", c->i + 1, c->n);
    c->i++;
    return (size_t)(len < (int)cap ? len : (int)cap);
}

// Source: "counting up", then 0..10, then "done", one piece per chunk.
struct CountCtx
{
    int step;
};
static size_t count_source(uint8_t *buf, size_t cap, void *vctx)
{
    CountCtx *c = (CountCtx *)vctx;
    int s = c->step++;
    if (s == 0)
        return (size_t)snprintf((char *)buf, cap, "counting up:\n");
    if (s <= 11)
        return (size_t)snprintf((char *)buf, cap, "  %d\n", s - 1);
    if (s == 12)
        return (size_t)snprintf((char *)buf, cap, "done\n");
    return 0; // done
}

void handle_stream(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    static LinesCtx ctx; // static: must outlive send_chunked (body may span loops)
    ctx.i = 0;
    ctx.n = 50;
    server.send_chunked(slot_id, 200, "text/plain", lines_source, &ctx);
}

void handle_count(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    static CountCtx ctx;
    ctx.step = 0;
    server.send_chunked(slot_id, 200, "text/plain", count_source, &ctx);
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
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));


    server.on("/stream", HttpMethod::HTTP_GET, handle_stream);
    server.on("/count", HttpMethod::HTTP_GET, handle_count);

    int32_t result = server.begin(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on port 80");
}

void loop()
{
    server.handle();
}
```
