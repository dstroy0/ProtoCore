# BasicAuth - per-route HTTP Basic authentication

**Layer:** L6 Presentation · **Build flags:** `PC_ENABLE_AUTH` (on by default)

## What this example teaches

The library protects a route with credentials via an **auth-aware `on()`
overload**: you pass a realm, username, and password after the handler, and the
handler runs only if the credentials check passes. Otherwise the server answers
`401` with a `WWW-Authenticate` challenge automatically - you write no auth code.

**Protecting one route.** A public route uses the normal three-argument `on()`; a
protected route adds the realm/user/password (digest defaults to `false`, i.e.
Basic):

```cpp
server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "public page"); });

server.on("/secret", HttpMethod::HTTP_GET,
          [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "authenticated!"); },
          "Restricted", "admin", "s3cret");   // realm, user, pass
```

The handler body is reached only after a valid `Authorization: Basic` header.

> **Basic is base64, not encryption.** The credentials are merely base64-encoded
> on the wire, so use Basic only over HTTPS or an SSH tunnel on untrusted
> networks. For a scheme where the password never crosses the wire, pass
> `digest=true` - see [DigestAuth](../DigestAuth).

`PC_ENABLE_AUTH` is on by default; you only need to set it (to 0) to compile
auth out.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L6-Presentation/BasicAuth/BasicAuth.ino
```

```sh
curl http://<ip>/                       # public
curl -u admin:s3cret http://<ip>/secret # 200; without -u you get a 401 challenge
```

## Annotated source

The complete sketch ([BasicAuth.ino](BasicAuth.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

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

    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "public page"); });

    // Basic auth (digest defaults to false): realm, username, password. A request
    // without valid credentials gets 401 + WWW-Authenticate before the handler runs.
    server.on(
        "/secret", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "authenticated!"); },
        "Restricted", "admin", "s3cret");

    server.begin(80);
}

void loop()
{
    server.handle();
}
```
