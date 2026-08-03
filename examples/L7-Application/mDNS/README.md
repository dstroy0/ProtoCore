# mDNS - advertise the device over mDNS / DNS-SD

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_MDNS`

## What this example teaches

mDNS (multicast DNS) plus DNS-SD lets a device be found on the LAN by name and by
service, with no central DNS server. `pc_mdns_begin(hostname, port)` claims
`<hostname>.local` and advertises an `_http._tcp` service, so a browser or a
DNS-SD tool finds the board without anyone knowing its IP.

**One call to claim a name and advertise HTTP:**

```cpp
server.begin(80);
if (pc_mdns_begin(HOSTNAME, 80)) {
    pc_mdns_txt("path", "/");              // TXT records shown by DNS-SD browsers
    pc_mdns_txt("fw", "1.0");
    pc_mdns_add_service("_https", "_tcp", 443); // advertise a second service
}
```

`pc_mdns_txt()` attaches Bonjour TXT key/value pairs to the primary service,
and `pc_mdns_add_service()` advertises additional services (here HTTPS on 443).
After flashing you can reach the board at `http://pc-demo.local/` and see it in
any DNS-SD browser (`dns-sd -B _http._tcp`, Avahi, Bonjour).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_MDNS=1" \
  --lib="." examples/L7-Application/mDNS/mDNS.ino
```

```sh
ping pc-demo.local
curl http://pc-demo.local/
dns-sd -B _http._tcp           # macOS / DNS-SD: discover the advertised service
```

## Annotated source

The complete sketch ([mDNS.ino](mDNS.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_MDNS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/application/mdns_service/mdns_service.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";
static const char *HOSTNAME = "pc-demo"; // -> pc-demo.local

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

    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "hello via mDNS"); });
    server.begin(80);

    // Claim <hostname>.local and advertise _http._tcp on port 80.
    if (pc_mdns_begin(HOSTNAME, 80))
    {
        // Bonjour TXT records (shown by DNS-SD browsers) + advertise HTTPS too.
        pc_mdns_txt("path", "/");
        pc_mdns_txt("fw", "1.0");
        pc_mdns_add_service("_https", "_tcp", 443);
        Serial.printf("mDNS: http://%s.local/\n", HOSTNAME);
    }
}

void loop()
{
    server.handle();
}
```
