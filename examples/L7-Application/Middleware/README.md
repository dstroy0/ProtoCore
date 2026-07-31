# Middleware - a composable request pipeline

**Layer:** L7 Application · **Build flags:** none (core features only)

## What this example teaches

`use()` registers a middleware that runs - in registration order - before every
request reaches its route handler. Each middleware returns `MW_NEXT` to fall
through or `MW_HALT` to short-circuit (after sending its own response). A built-in
fixed-window rate limiter runs ahead of the chain. This shows logging, header
stamping, a hard gate, and the limiter together.

**The chain.** Three middlewares: one logs every request (even unmatched ones),
one stamps a response header, one blocks `/admin` outright:

```cpp
static MwResult mw_log(uint8_t slot_id, HttpReq *req) {
    Serial.printf("[req %lu] %s %s\n", ++request_count, req->method, req->path);
    return MwResult::MW_NEXT;                               // fall through
}
static MwResult mw_block_admin(uint8_t slot_id, HttpReq *req) {
    if (strcmp(req->path, "/admin") == 0) {
        server.send(slot_id, 403, "text/plain", "blocked by middleware");
        return MwResult::MW_HALT;                          // the route handler is never reached
    }
    return MwResult::MW_NEXT;
}
...
server.use(mw_log);
server.use(mw_brand);
server.use(mw_block_admin);
```

A middleware can queue response headers with `add_response_header()` (as
`mw_brand` does with `X-Powered-By`), so cross-cutting concerns live in one place
instead of in every handler.

**The rate limiter.** `enable_rate_limit(max, window_ms)` installs a fixed-window
limiter ahead of the chain; once more than `max` requests arrive within the
window, further requests get `429` + `Retry-After` automatically:

```cpp
server.enable_rate_limit(5, 10000); // 5 requests / 10 s
```

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L7-Application/Middleware/Middleware.ino
```

```sh
curl -D - http://<ip>/                         # see X-Powered-By stamped by middleware
for i in $(seq 1 8); do curl -s -o /dev/null -w "%{http_code} " http://<ip>/ping; done; echo  # some 429s
curl http://<ip>/admin                          # 403 from the gate middleware
```

## Annotated source

The complete sketch ([Middleware.ino](Middleware.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

static unsigned long request_count = 0;

// Logging middleware: observe every request (even unmatched 404s), fall through.
static MwResult mw_log(uint8_t slot_id, HttpReq *req)
{
    (void)slot_id;
    request_count++;
    Serial.printf("[req %lu] %s %s\n", request_count, req->method, req->path);
    return MwResult::MW_NEXT;
}

// Header-stamp middleware: queue a header onto every response.
static MwResult mw_brand(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    server.add_response_header(slot_id, "X-Powered-By", "ProtoCore");
    return MwResult::MW_NEXT;
}

// Gate middleware: block a path outright, demonstrating MW_HALT.
static MwResult mw_block_admin(uint8_t slot_id, HttpReq *req)
{
    if (strcmp(req->path, "/admin") == 0)
    {
        server.send(slot_id, 403, "text/plain", "blocked by middleware");
        return MwResult::MW_HALT; // handler is never reached
    }
    return MwResult::MW_NEXT;
}

void handle_root(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    server.send(slot_id, 200, "text/plain", "hello from the handler");
}

void handle_ping(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    server.send(slot_id, 200, "text/plain", "pong");
}

// Registered, but the gate middleware blocks it before we get here.
void handle_admin(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    server.send(slot_id, 200, "text/plain", "you should never see this");
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


    server.use(mw_log);                 // runs in registration order...
    server.use(mw_brand);
    server.use(mw_block_admin);
    server.enable_rate_limit(5, 10000); // ...behind a 5-req / 10-s limiter

    server.on("/", HttpMethod::HTTP_GET, handle_root);
    server.on("/ping", HttpMethod::HTTP_GET, handle_ping);
    server.on("/admin", HttpMethod::HTTP_GET, handle_admin);

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
