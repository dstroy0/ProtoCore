# ForwardedTrust - lock out the real client behind a trusted reverse proxy

**Layer:** L6 Presentation · **Build flags:** `PC_ENABLE_FORWARDED_TRUST` (requires `PC_ENABLE_AUTH_LOCKOUT`, which requires `PC_ENABLE_AUTH`)

## What this example teaches

When the device sits behind a reverse proxy or load balancer, every request
arrives from the proxy's single TCP address. A per-IP brute-force lockout keyed
on that address is worthless: one abuser locks out **every** client at once, and
distinct clients cannot be told apart.

`PC_ENABLE_FORWARDED_TRUST` fixes this by keying the auth lockout on the
**original client** address the proxy reports in `Forwarded` (RFC 7239) or
`X-Forwarded-For` - but **only** when the request's real TCP peer is a proxy you
have explicitly trusted. That header is client-spoofable, so a direct, untrusted
client's header is **ignored**: it can neither dodge its own lockout nor frame
another address into one.

```cpp
// Trust the proxy in front of you. Only requests whose real TCP peer is in this
// CIDR have their forwarded client address believed.
pc_forwarded_trust_add_cidr("192.0.2.0/24");
```

The accept-time throttle and the IP allowlist deliberately keep using the real
TCP source (the proxy) - only the auth lockout follows the forwarded client.

## Security model

- **Empty trust table = trust nothing.** Before you add a CIDR, no forwarded
  header is ever believed (the lockout keys on the real TCP peer, as without the
  flag).
- **Untrusted peer -> header ignored.** A client connecting directly (not via a
  trusted proxy) cannot set `X-Forwarded-For` to escape its lockout or to lock
  out someone else.
- **Malformed / obfuscated / unspecified token -> fall back to the TCP peer.** An
  RFC 7239 `for=unknown` / `for="_hidden"`, a non-address, or `0.0.0.0` is never
  used as a key.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_AUTH=1 -DPC_ENABLE_AUTH_LOCKOUT=1 -DPC_ENABLE_FORWARDED_TRUST=1" \
  --lib="." examples/L6-Presentation/ForwardedTrust/ForwardedTrust.ino
```

From a host inside the trusted CIDR (standing in for the proxy):

```sh
# lock out ONE client, keyed on its forwarded address:
curl -u admin:wrong  -H 'X-Forwarded-For: 203.0.113.7'  http://<ip>/secret   # ...x THRESHOLD -> 429
# a DIFFERENT forwarded client is unaffected (per-client keying, not per-proxy):
curl -u admin:wrong  -H 'X-Forwarded-For: 198.51.100.9' http://<ip>/secret   # 401, not 429
# the right password clears that client's lockout:
curl -u admin:s3cret -H 'X-Forwarded-For: 203.0.113.7'  http://<ip>/secret   # 200
```

## Annotated source

The complete sketch ([ForwardedTrust.ino](ForwardedTrust.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// Enable the lockout AND the trusted-proxy resolver for the whole build (a .ino #define does not reach
// the separately compiled library - see build_opt.h / build_flags).
#define PC_ENABLE_AUTH_LOCKOUT 1
#define PC_ENABLE_FORWARDED_TRUST 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/security/forwarded_trust/forwarded_trust.h" // pc_forwarded_trust_add_cidr()

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
        delay(250);

    // Trust the reverse proxy(ies) in front of this device (set to YOUR proxy's subnet).
    pc_forwarded_trust_add_cidr("192.0.2.0/24");

    // Protected route. Behind a trusted proxy the lockout counts failures per ORIGINAL client, so one
    // abuser does not lock out everyone sharing the proxy's address; a direct client's spoofed header
    // is ignored.
    server.on(
        "/secret", HttpMethod::HTTP_GET,
        [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "authenticated!"); }, "Restricted", "admin",
        "s3cret");

    server.begin(80);
}

void loop()
{
    server.handle();
}
```
