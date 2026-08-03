# RadioPower - WiFi radio power controls

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_RADIO_POWER` (+ `PC_RADIO_WIFI_PS`, optional `PC_RADIO_MAX_TX_DBM`)

## What this example teaches

On a battery device the WiFi radio dominates the power budget. This applies a WiFi
modem-sleep mode (and an optional max-TX-power cap) after the link comes up, trading
a little latency for lower average current. `GET /radio` reports the mode read back
from the radio.

**Apply after the link is up.** The WiFi connect path may set its own default first,
so the settings are applied once association completes:

```cpp
pc_radio_power_apply();   // applies the build-flag-configured modem-sleep / TX cap
Serial.printf("radio modem-sleep: %s\n", pc_radio_ps_name(pc_radio_ps_get()));
```

**The mode is a build flag, not a runtime call**, so it reaches the
separately-compiled library: `PC_RADIO_WIFI_PS` is `0` (none), `1` (min modem
sleep), or `2` (max modem sleep), with an optional `PC_RADIO_MAX_TX_DBM` cap.
`pc_radio_ps_get()` reads the live mode back and `pc_radio_ps_name()` turns
it into a string for the endpoint.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_RADIO_POWER=1 -DPC_RADIO_WIFI_PS=1" \
  --lib="." examples/L7-Application/RadioPower/RadioPower.ino
```

```sh
curl http://<ip>/radio   # {"modem_sleep":"min"}
```

## Annotated source

The complete sketch ([RadioPower.ino](RadioPower.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_RADIO_POWER 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/physical/radio_power.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
        delay(250);
    Serial.print("IP: ");
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Apply the configured modem-sleep / TX settings AFTER the link is up (the
    // WiFi connect path may set its own default first).
    pc_radio_power_apply();
    Serial.printf("radio modem-sleep: %s\n", pc_radio_ps_name(pc_radio_ps_get()));

    server.on("/radio", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        char b[48];
        snprintf(b, sizeof(b), "{\"modem_sleep\":\"%s\"}", pc_radio_ps_name(pc_radio_ps_get()));
        server.send(id, 200, "application/json", b);
    });
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
