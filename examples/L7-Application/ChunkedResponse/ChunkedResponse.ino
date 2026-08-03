// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ChunkedResponse.ino
 * @brief Streaming a response of unknown length with chunked transfer.
 *
 * send_chunked() writes the status + headers (Transfer-Encoding: chunked), then
 * pulls the body from a ChunkSource generator one piece at a time, adding the
 * chunk framing and the terminating chunk. The generator returns the next bytes
 * each call (0 to end) and tracks its position in a ctx, so the server can page an
 * arbitrarily large body to the socket in constant memory, pacing with the TCP
 * window across loops - no big buffer, no truncation.
 *
 * NOTE: the ctx must outlive the response (the body may finish on a later loop),
 * so it is static here, not on the handler's stack.
 *
 * Flash, open Serial @ 115200 for the IP, then:
 *   curl -N http://<ip>/stream     # watch 50 lines arrive incrementally
 *   curl -N http://<ip>/count      # one number per chunk
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// Source: emit `n` numbered lines, one line per chunk, resuming from ctx->i.
struct LinesCtx
{
    int i, n;
};
static size_t lines_source(uint8_t *buf, size_t cap, void *vctx)
{
    LinesCtx *c = (LinesCtx *)vctx;
    if (c->i >= c->n)
    {
        return 0; // done
    }
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
    {
        return (size_t)snprintf((char *)buf, cap, "counting up:\n");
    }
    if (s <= 11)
    {
        return (size_t)snprintf((char *)buf, cap, "  %d\n", s - 1);
    }
    if (s == 12)
    {
        return (size_t)snprintf((char *)buf, cap, "done\n");
    }
    return 0; // done
}

void handle_stream(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    static LinesCtx ctx; // static: must outlive send_chunked (body may span loops)
    ctx.i = 0;
    ctx.n = 50;
    send_chunked(slot_id, 200, "text/plain", lines_source, &ctx);
}

void handle_count(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    static CountCtx ctx;
    ctx.step = 0;
    send_chunked(slot_id, 200, "text/plain", count_source, &ctx);
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

    on_http("/stream", HTTP_GET, handle_stream);
    on_http("/count", HTTP_GET, handle_count);

    int32_t result = begin_http(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on port 80");
}

void loop()
{
    handle();
}
