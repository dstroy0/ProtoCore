# Provisioning - first-boot WiFi setup via a captive portal

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_PROVISIONING`

## What this example teaches

On first boot, with no stored credentials, the device brings up a softAP
("PC-Setup") and a catch-all DNS responder (raw lwIP UDP, no add-on library),
so joining the AP with a phone pops a credentials form. The submitted SSID/PSK
persist to NVS; on the next boot the device finds them, connects as a station, and
serves normally. No external libraries are needed: just WiFi softAP, lwIP UDP, and
Preferences (NVS).

**The boot decision: load credentials, else open the portal.**

```cpp
char ssid[33], psk[64];
if (pc_provisioning_load(ssid, sizeof(ssid), psk, sizeof(psk))) {
    Physical.wifi->init(ssid, psk);          // we have creds -> normal station
    server.on("/", HttpMethod::HTTP_GET, handle_root);
    server.begin(80);
} else {
    server.begin(80);
    pc_provisioning_begin(server, "PC-Setup"); // no creds -> captive portal
}
```

`pc_provisioning_load()` returns false until the device has been provisioned;
`pc_provisioning_begin()` stands up the softAP, the catch-all DNS, and the
form handler that writes NVS and reboots. To re-provision later, call
`pc_provisioning_clear()` (for example from a button handler).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_PROVISIONING=1" \
  --lib="." examples/L7-Application/Provisioning/Provisioning.ino
```

Flash, join the WiFi network "PC-Setup" with a phone, open any page to reach
the captive portal, and submit your network's SSID/password. The device reboots
into station mode.

## Annotated source

The complete sketch ([Provisioning.ino](Provisioning.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_PROVISIONING 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/provisioning_service.h"

PC server;

void handle_root(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    server.send(slot_id, 200, "text/plain", "Provisioned - hello from station mode!");
}

void setup()
{
    Serial.begin(115200);

    char ssid[33];
    char psk[64];
    if (pc_provisioning_load(ssid, sizeof(ssid), psk, sizeof(psk)))
    {
        // Credentials present: connect as a normal station.
        Physical.wifi->init(ssid, psk);
        Serial.print("Connecting to ");
        Serial.println(ssid);
        while (!Physical.wifi->ready())
            delay(250);
        uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
        Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                      (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

        server.on("/", HttpMethod::HTTP_GET, handle_root);
        server.begin(80);
        Serial.println("Station mode; serving on port 80");
    }
    else
    {
        // No credentials: bring up the captive portal (softAP + catch-all DNS + form).
        server.begin(80);
        pc_provisioning_begin(server, "PC-Setup");
        Serial.println("Provisioning: join WiFi 'PC-Setup' and open any page");
    }
}

void loop()
{
    server.handle();
}
```
