# TimeSourceFallback - multi-source time with automatic fallback

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_NTP`, `PC_ENABLE_TIME_SOURCE`

## What this example teaches

Real devices have more than one clock: NTP when the network is up, a battery RTC
when it is not, maybe a GPS pulse. `PC_ENABLE_TIME_SOURCE` lets you register
several time sources with priorities and have the library pick the best available
one automatically. This registers NTP (priority 0) and an RTC stand-in (priority
1); `GET /time` reports the epoch and which source supplied it. It composes the
SNTP client from [SNTP](../SNTP).

**A source is a function returning epoch seconds, or 0 if it has no time.**

```cpp
static uint32_t src_ntp() { return pc_ntp_synced() ? (uint32_t)pc_ntp_epoch() : 0; }
static uint32_t src_rtc() { return RTC_BASE + (uint32_t)(millis() / 1000); }
```

**Register by priority; lowest value wins.** When NTP returns 0 (not synced yet)
the library falls through to the RTC; once NTP syncs it transparently takes over:

```cpp
pc_ntp_begin();                        // start SNTP
pc_time_source_add("ntp", 0, src_ntp); // preferred
pc_time_source_add("rtc", 1, src_rtc); // fallback
```

`pc_time_now()` returns the chosen epoch and `pc_time_source_active()` names
the source that supplied it. Swap the RTC stand-in for a real DS3231/PCF8523 I2C
read, and add a GPS source the same way (returning 0 with no fix).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_NTP=1 -DPC_ENABLE_TIME_SOURCE=1" \
  --lib="." examples/L7-Application/TimeSourceFallback/TimeSourceFallback.ino
```

```sh
curl http://<ip>/time   # {"epoch":...,"source":"rtc"} at boot, "ntp" after sync
```

## Annotated source

The complete sketch ([TimeSourceFallback.ino](TimeSourceFallback.ino)),
reproduced verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_NTP 1
#define PC_ENABLE_TIME_SOURCE 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/application/ntp_service/ntp_service.h"
#include "services/timing_position/time_source/time_source.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Priority 0: NTP - valid only once SNTP has synced (else 0 -> fall through).
static uint32_t src_ntp()
{
    return pc_ntp_synced() ? (uint32_t)pc_ntp_epoch() : 0;
}

// Priority 1: a coarse battery-RTC stand-in. A real device reads a DS3231/PCF8523
// over I2C here; this simulation is seeded at build time and counts via millis(),
// so the device always has a last-resort time.
static const uint32_t RTC_BASE = 1750000000u; // ~2025-06; replace with a real RTC read
static uint32_t src_rtc()
{
    return RTC_BASE + (uint32_t)(millis() / 1000);
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

    pc_ntp_begin();                        // start SNTP (GMT, pool.ntp.org)
    pc_time_source_add("ntp", 0, src_ntp); // preferred
    pc_time_source_add("rtc", 1, src_rtc); // fallback

    server.on("/time", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        char body[96];
        uint32_t epoch = pc_time_now();
        const char *src = pc_time_source_active();
        snprintf(body, sizeof(body), "{\"epoch\":%u,\"source\":\"%s\"}", (unsigned)epoch, src ? src : "none");
        server.send(id, 200, "application/json", body);
    });
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
