# ServerSentEvents - server push with SSE

**Layer:** L6 Presentation · **Build flags:** none (core; SSE is on by default)

## What this example teaches

Server-Sent Events is the simplest way to push data to a browser: a long-lived
`text/event-stream` response over which the server emits named events whenever it
likes. This example registers an SSE endpoint, greets each subscriber, and
broadcasts a counter to all of them once a second.

**Registering the endpoint + greeting one subscriber.** `on_sse(path, cb)` makes
`/events` an SSE endpoint; the connect callback fires per new subscriber, where
`pc_sse_send()` pushes to just that client:

```cpp
void pc_sse_connect(uint8_t pc_sse_id) {
    server.pc_sse_send(pc_sse_id, "subscribed", "tick");   // (data, event-name) to this subscriber
}
server.on_sse("/events", pc_sse_connect);
```

**Broadcasting to everyone.** From `loop()`, `pc_sse_broadcast(path, data, event)`
sends to every subscriber of that endpoint - here a counter every second:

```cpp
char buf[24]; snprintf(buf, sizeof(buf), "%lu", n++);
server.pc_sse_broadcast("/events", buf, "tick");
```

The third argument names the event, so the browser listens with
`source.addEventListener('tick', ...)`. Each subscriber occupies one connection
slot for the life of the stream (capped at `MAX_SSE_CONNS`). The test page at `/`
opens an `EventSource` and appends each tick.

For SSE over TLS, see [SecureWebSocket](../SecureWebSocket); for full
bidirectional messaging, see [WebSocket](../WebSocket).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L6-Presentation/ServerSentEvents/ServerSentEvents.ino
```

Flash, then browse to `http://<ip>/` and watch the counter stream in.

## Annotated source

The complete sketch ([ServerSentEvents.ino](ServerSentEvents.ino)),
reproduced verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Test page: open an EventSource and append every "tick" event's data.
static const char PAGE[] = "<!doctype html><meta charset=utf-8><title>SSE</title><pre id=o></pre><script>"
                           "var s=new EventSource('/events');"
                           "s.addEventListener('tick',function(e){o.textContent+=e.data+'\\n'})</script>";

// Fires once per new subscriber; greet just this client.
void pc_sse_connect(uint8_t pc_sse_id)
{
    server.pc_sse_send(pc_sse_id, "subscribed", "tick"); // (data, event name)
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

    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/html", PAGE); });
    server.on_sse("/events", pc_sse_connect); // register the SSE endpoint
    server.begin(80);
}

void loop()
{
    server.handle();

    // Push a counter to every /events subscriber once a second.
    static unsigned long last = 0;
    static unsigned long n = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        char buf[24];
        snprintf(buf, sizeof(buf), "%lu", n++);
        server.pc_sse_broadcast("/events", buf, "tick"); // (path, data, event name)
    }
}
```
