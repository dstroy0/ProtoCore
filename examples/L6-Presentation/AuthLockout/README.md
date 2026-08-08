# AuthLockout - brute-force lockout for HTTP auth

**Layer:** L6 Presentation · **Build flags:** `PC_ENABLE_AUTH_LOCKOUT` (requires `PC_ENABLE_AUTH`, on by default)

## What this example teaches

This puts a per-source-IP guard in front of authenticated routes. After a few
wrong passwords from one address, that address is locked out with exponential
backoff and gets `429 Too Many Requests` + `Retry-After` (without even checking
credentials) instead of unlimited guesses. A correct login clears the address
immediately. State lives in a fixed BSS table - no heap.

**It is transparent to your route.** You protect the route exactly as in
[BasicAuth](../BasicAuth); enabling the flag adds the lockout in front of
the credential check automatically:

```cpp
server.on(
    "/secret", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "authenticated!"); },
    "Restricted", "admin", "s3cret");
```

**Tuning.** The thresholds live in `protocore_config.h`:
`PC_AUTH_LOCKOUT_THRESHOLD` (failures before locking),
`PC_AUTH_LOCKOUT_BASE_MS` and `PC_AUTH_LOCKOUT_MAX_MS` (the backoff window,
which doubles per subsequent failure up to the max), and
`PC_AUTH_LOCKOUT_SLOTS` (how many addresses are tracked). Set them as build
flags alongside the enable flag.

**Build dependency.** `PC_ENABLE_AUTH_LOCKOUT` requires `PC_ENABLE_AUTH`
(which is on by default) - enforced by a compile-time `#error`.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_AUTH=1 -DPC_ENABLE_AUTH_LOCKOUT=1" \
  --lib="." examples/L6-Presentation/AuthLockout/AuthLockout.ino
```

```sh
# repeat until you get 429, then wait out Retry-After and use the right password:
curl -u admin:wrong  http://<ip>/secret    # ...x several -> 429 + Retry-After
curl -u admin:s3cret http://<ip>/secret    # clears the lockout for your IP
```

## Annotated source

The complete sketch ([AuthLockout.ino](AuthLockout.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_AUTH_LOCKOUT 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

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

    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "public page"); });

    // Protected route. Repeated wrong passwords from one IP trip the lockout
    // (429) with exponential backoff; the tuning lives in protocore_config.h
    // (PC_AUTH_LOCKOUT_THRESHOLD / _BASE_MS / _MAX_MS).
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
