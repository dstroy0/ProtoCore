# ResponseHeaders - custom headers and cookies

**Layer:** L7 Application · **Build flags:** none (core features only)

## What this example teaches

Handlers can queue extra response headers and cookies before they send. The
queued headers live in a fixed per-connection buffer, are injected into the next
`send()` / `send_empty()` / `redirect()` on that slot, and are cleared
automatically at the start of the next request.

**The three calls.**

```cpp
server.add_response_header(slot_id, "X-Api-Version", "1.2.5");          // arbitrary header line
server.set_cookie(slot_id, "session", "abc123", "Path=/; HttpOnly; Max-Age=3600"); // Set-Cookie + attrs
server.clear_response_headers(slot_id);                                 // discard anything queued so far
```

The `/cleared` route queues a header then calls `clear_response_headers()` before
sending, so the header does not appear - useful when a later code path decides the
earlier header was wrong. Oversized headers are dropped whole (never truncated
mid-line), preserving a valid response.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L7-Application/ResponseHeaders/ResponseHeaders.ino
```

```sh
curl -D - http://<ip>/headers     # X-Api-Version + Set-Cookie present
curl -D - http://<ip>/cleared     # no X-Scratch (cleared before send)
```

## Annotated source

The complete sketch ([ResponseHeaders.ino](ResponseHeaders.ino)),
reproduced verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// GET /headers - attach a custom header and a cookie, then respond.
void handle_headers(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    server.add_response_header(slot_id, "X-Api-Version", "1.2.5");
    server.set_cookie(slot_id, "session", "abc123", "Path=/; HttpOnly; Max-Age=3600");
    server.send(slot_id, 200, "text/plain", "custom header + cookie attached");
}

// GET /cleared - queue a header, then change our mind before sending.
void handle_cleared(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    server.add_response_header(slot_id, "X-Scratch", "should-not-appear");
    server.clear_response_headers(slot_id); // discard the queued header
    server.send(slot_id, 200, "text/plain", "headers were cleared before send");
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

    // Keep the radio responsive (the Arduino default modem sleep delays the
    // first packet after an idle gap).

    server.on("/headers", HttpMethod::HTTP_GET, handle_headers);
    server.on("/cleared", HttpMethod::HTTP_GET, handle_cleared);

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
