# DigestAuth - HTTP Digest authentication (password never on the wire)

**Layer:** L6 Presentation · **Build flags:** `PC_ENABLE_AUTH` (on by default)

## What this example teaches

This is [BasicAuth](../BasicAuth) with one change: pass `digest=true` to the
authenticated `on()` overload and the route is protected with HTTP Digest
(RFC 7616, SHA-256, `qop="auth"`) instead of Basic. With Digest the password
never crosses the wire - only a salted hash of it does - and unauthenticated
requests get a `401` plus a `WWW-Authenticate: Digest` challenge automatically.

**One extra argument.** The signature is the same as Basic auth, with a trailing
`true`:

```cpp
// on(path, method, handler, realm, user, pass, digest=true)
server.on("/secret", HttpMethod::HTTP_GET, handle_secret, "demo", "admin", "s3cret", true);
```

The handler is reached only after the client computes a correct digest response
from the server's nonce - so the server never sees, stores, or transmits the
password in the clear.

> **Testing caveat (Windows curl).** `curl --digest` on Windows routes Digest
> through SSPI/SChannel, which rejects this SHA-256 / `qop="auth"` challenge
> (`SEC_E_QOP_NOT_SUPPORTED`) and never sends credentials. That is a Windows-curl
> limitation, not a server bug - the challenge is standard RFC 7616 and works with
> a browser, `wget`, or Linux/macOS curl.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_AUTH=1" \
  --lib="." examples/L6-Presentation/DigestAuth/DigestAuth.ino
```

```sh
curl --digest -u admin:s3cret http://<ip>/secret   # Linux/macOS curl, wget, or a browser
```

## Annotated source

The complete sketch ([DigestAuth.ino](DigestAuth.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// GET /secret - only reached after successful Digest authentication.
void handle_secret(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    server.send(slot_id, 200, "text/plain", "authenticated: top secret payload");
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


    // The trailing `true` selects Digest instead of Basic for this route.
    // on(path, method, handler, realm, user, pass, digest=true)
    server.on("/secret", HttpMethod::HTTP_GET, handle_secret, "demo", "admin", "s3cret", true);

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
