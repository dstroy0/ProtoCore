# KeepAlive - HTTP/1.1 persistent connections

**Layer:** L4 Transport · **Build flags:** `PC_ENABLE_KEEPALIVE`

## What this example teaches

By default the server answers one request per TCP connection and closes it
(`Connection: close`). With keep-alive enabled, one connection serves many
requests - the big win when a browser loads a page plus its assets, or a client
polls an endpoint. The crucial point: **it is transparent to your handlers.** You
write the same routes; the server decides keep-alive vs close and emits the right
`Connection` header.

**The behavior rules.** Keep-alive follows the HTTP spec, with safety bounds:

- HTTP/1.1: the connection stays open unless the client sends `Connection: close`.
- HTTP/1.0: it closes unless the client sends `Connection: keep-alive`.
- Error responses (400/413/414) always close - the next request boundary is unknown after a parse error.
- Each connection serves at most `PC_KEEPALIVE_MAX_REQUESTS`, then closes.
- Idle connections are still reclaimed by the `conn_timeout` sweep.

**There is no API to learn.** Enabling the flag changes the transport behavior;
the handlers below are ordinary. The `/time` route exists just so a client can
poll repeatedly over the same socket and watch it being reused:

```cpp
server.on("/time", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
    char buf[32];
    snprintf(buf, sizeof(buf), "uptime_ms=%lu", (unsigned long)millis());
    server.send(id, 200, "text/plain", buf);
});
```

**Build-flag note.** `PC_ENABLE_KEEPALIVE` must reach the library build, so
pass it as a `build_flag` (an in-sketch `#define` only affects the sketch). You
can also tune `PC_KEEPALIVE_MAX_REQUESTS` the same way.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_KEEPALIVE=1" \
  --lib="." examples/L4-Transport/KeepAlive/KeepAlive.ino
```

```sh
curl -v http://<ip>/                              # note "Connection: keep-alive"
curl -v http://<ip>/ http://<ip>/time http://<ip>/   # one socket serves all three
```

## Annotated source

The complete sketch ([KeepAlive.ino](KeepAlive.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_KEEPALIVE 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// A small page that links to /time, so a browser makes several requests that all
// ride the one persistent connection.
static const char PAGE[] = "<!doctype html><html><body><h1>Keep-Alive demo</h1>"
                           "<p>This page and its requests are served over one persistent connection.</p>"
                           "<p><a href=\"/time\">/time</a></p></body></html>";

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

    // Ordinary handlers - keep-alive is handled by the transport, not here.
    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/html", PAGE); });

    // A tiny endpoint a client can poll repeatedly over the same socket.
    server.on("/time", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uptime_ms=%lu", (unsigned long)millis());
        server.send(id, 200, "text/plain", buf);
    });

    int32_t result = server.begin(80);
    if (result < 0)
        Serial.printf("begin() failed (error %d)\n", result);
    else
        Serial.println("HTTP server (keep-alive) on :80");
}

void loop()
{
    server.handle();
}
```
