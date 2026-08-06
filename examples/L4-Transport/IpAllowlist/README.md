# IpAllowlist - a source-IP accept-time firewall

**Layer:** L4 Transport · **Build flags:** `PC_ENABLE_IP_ALLOWLIST`

## What this example teaches

This drops any TCP connection whose source address falls outside a set of CIDR
rules - a coarse first-line firewall in front of _every_ listener (HTTP, WS,
TLS, etc.), evaluated at accept time before any bytes are read. Rules live in a
fixed BSS table, so there is no heap cost.

**Adding rules.** Unlike the throttles (which are flag-only), the allowlist has a
small API: add CIDR rules as text with `Tcp.listener->ip_allow_add_cidr("network/prefix")`.
IPv4 and IPv6 are both accepted; a bare address (no `/prefix`) is a single-host
rule (`/32` for v4, `/128` for v6):

```cpp
Tcp.listener->ip_allow_add_cidr("192.168.1.0/24"); // the local /24
Tcp.listener->ip_allow_add_cidr("10.0.0.5");       // one trusted host (-> /32)
Tcp.listener->ip_allow_add_cidr("2001:db8::/32");  // an IPv6 prefix
```

Matching is a full-address prefix compare per family, so a v4 peer is only ever
tested against v4 rules and a v6 peer only against v6 rules - there is no lossy
hashing or address flattening that a peer could exploit.

**Fail-open until you add a rule.** An _empty_ allowlist allows everything (so
enabling the feature before adding rules never locks you out). Add at least one
rule to actually restrict access.

**Know its limits.** This filters by source IP, which a determined attacker can
spoof, so treat it as a coarse first layer and pair it with the
[accept throttles](../AcceptThrottle) and real authentication. It is excellent
for "only my LAN may even open a socket."

The `listener.h` include is what brings in `Tcp.listener->ip_allow_add_cidr`.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_IP_ALLOWLIST=1" \
  --lib="." examples/L4-Transport/IpAllowlist/IpAllowlist.ino
```

Connect from an address inside `192.168.1.0/24` (allowed) and from one outside it
(connection dropped at accept).

## Annotated source

The complete sketch ([IpAllowlist.ino](IpAllowlist.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_IP_ALLOWLIST 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/tcp/tcp_listener.h" // Tcp.listener->ip_allow_add_cidr

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

    // Only these sources may connect; everything else is dropped at accept time.
    // (An empty allowlist would allow everything - add at least one rule.)
    Tcp.listener->ip_allow_add_cidr("192.168.1.0/24"); // local /24
    Tcp.listener->ip_allow_add_cidr("10.0.0.5");       // one trusted host (bare address -> /32)
    Tcp.listener->ip_allow_add_cidr("2001:db8::/32");  // an IPv6 prefix

    server.on("/", HttpMethod::HTTP_GET,
              [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "hello from an allowed address"); });
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
