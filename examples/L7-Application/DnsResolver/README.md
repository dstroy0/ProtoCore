# DnsResolver - DNS resolution with answer verification

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_DNS_RESOLVER`

## What this example teaches

Resolving a hostname is easy; trusting the answer is the interesting part. This
resolves a name to an IPv4 address and then _verifies_ the result, rejecting
suspicious answers - `0.0.0.0`, loopback, broadcast, multicast - that are classic
DNS-rebinding or spoof indicators. `GET /resolve?host=dns.google` returns the IP
and whether it passed verification.

**Resolve, then verify:**

```cpp
uint32_t ip = 0;
bool ok = pc_dns_resolver_resolve(host, &ip);   // false on lookup failure
// ...
pc_dns_resolver_verify(ip)                        // false for 0.0.0.0 / loopback / broadcast / multicast
```

`pc_dns_resolver_resolve()` does the lookup; `pc_dns_resolver_verify()` is the safety filter you
apply before you connect anywhere with the result.

**Where to resolve.** The lookup is blocking. This demo runs it in the handler for
clarity, but in a real app you should resolve off the request hot path (at setup or
from `loop()`) and cache the result, so a slow DNS server cannot stall request
handling.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_DNS_RESOLVER=1" \
  --lib="." examples/L7-Application/DnsResolver/DnsResolver.ino
```

```sh
curl "http://<ip>/resolve?host=dns.google"   # {"ip":"8.8.8.8","verified":true}
```

## Annotated source

The complete sketch ([DnsResolver.ino](DnsResolver.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_DNS_RESOLVER 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/network/dns_resolver.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

void setup()
{
    Serial.begin(115200);
    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
        delay(250);
    Serial.print("IP: ");
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/resolve", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *host = http_get_query(req, "host");
        if (!host)
        {
            server.send(id, 400, "application/json", "{\"error\":\"missing host\"}");
            return;
        }
        uint32_t ip = 0;
        bool ok = pc_dns_resolver_resolve(host, &ip);
        if (!ok)
        {
            server.send(id, 502, "application/json", "{\"error\":\"resolve failed\"}");
            return;
        }
        char b[80];
        snprintf(b, sizeof(b), "{\"ip\":\"%u.%u.%u.%u\",\"verified\":%s}", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                 (ip >> 8) & 0xFF, ip & 0xFF, pc_dns_resolver_verify(ip) ? "true" : "false");
        server.send(id, 200, "application/json", b);
    });
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
