# SNTP - wall-clock time via SNTP

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_NTP`

## What this example teaches

An ESP32 boots with no idea what time it is. `pc_ntp_begin(tz)` starts the
ESP-IDF SNTP client (the first sync lands a few seconds later), and
`pc_ntp_http_date()` formats the current time as an RFC 7231 date string -
the same format HTTP `Date`/`Last-Modified` headers use. `GET /time` returns it,
or `503` until the first sync completes.

**Start the client, then format on demand.**

```cpp
pc_ntp_begin("UTC0"); // POSIX TZ string; set your zone for local time
```

```cpp
char date[40];
if (pc_ntp_http_date(date, sizeof(date)) == 0) {  // 0 = not synced yet
    server.send(slot_id, 503, "text/plain", "Time not synced yet");
    return;
}
server.send(slot_id, 200, "text/plain", date);
```

`pc_ntp_http_date()` returns 0 until the clock is set, so the handler can
distinguish "no time yet" from a real value and answer `503` in the meantime. The
TZ argument is a POSIX TZ string ("UTC0", "EST5EDT", "CET-1CEST", ...) so the
formatted time can be local.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_NTP=1" \
  --lib="." examples/L7-Application/SNTP/SNTP.ino
```

```sh
curl http://<ip>/time   # 503 for the first few seconds, then an RFC 7231 date
```

## Annotated source

The complete sketch ([SNTP.ino](SNTP.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_NTP 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/application/ntp_service/ntp_service.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

void handle_time(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char date[40];
    if (pc_ntp_http_date(date, sizeof(date)) == 0) // 0 -> clock not set yet
    {
        server.send(slot_id, 503, "text/plain", "Time not synced yet");
        return;
    }
    server.send(slot_id, 200, "text/plain", date);
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

    server.on("/time", HttpMethod::HTTP_GET, handle_time);
    server.begin(80);

    pc_ntp_begin("UTC0"); // POSIX TZ string; set your zone for local time
}

void loop()
{
    server.handle();
}
```
