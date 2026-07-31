# Telemetry - moving-window stats, rate of change, and a totalizer

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_TELEMETRY`

## What this example teaches

A raw sensor reading is rarely what a dashboard wants. The telemetry helpers turn
a periodic sample into derived figures with zero heap: a moving-window
mean/stddev/min/max, the rate of change (slope) of the signal, and a run-time
totalizer that integrates the reading over time (an odometer). `GET /telemetry`
returns them as JSON for a dashboard or an alert rule.

**Caller-owned storage, three accumulators.** The window's ring buffer is yours
(no heap); each helper is initialized once:

```cpp
static float g_window_buf[16];   // caller-owned window storage
static pc_window g_window;
static pc_rate g_rate;
static pc_totalizer g_total;

pc_window_init(&g_window, g_window_buf, 16);
pc_rate_init(&g_rate);
pc_totalizer_init(&g_total);
```

**Fold each sample in, read the derived values out.** Once a second the loop pushes
a sample and updates the rate and totalizer:

```cpp
pc_window_push(&g_window, sample);                  // stats over the last 16 readings
g_last_rate = pc_rate_update(&g_rate, sample, now); // slope (units/s)
pc_totalizer_add(&g_total, sample, now);            // integrate over time
```

The handler reads back `pc_window_mean/stddev/min/max/count`, the last rate, and
`pc_totalizer_total` and serializes them. The example samples an ADC pin; swap
in any sensor.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_TELEMETRY=1" \
  --lib="." examples/L7-Application/Telemetry/Telemetry.ino
```

```sh
curl http://<ip>/telemetry   # {"samples":..,"mean":..,"stddev":..,"rate_per_s":..,"total":..}
```

## Annotated source

The complete sketch ([Telemetry.ino](Telemetry.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_TELEMETRY 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/telemetry/telemetry.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

static float g_window_buf[16]; // caller-owned window storage (no heap)
static pc_window g_window;
static pc_rate g_rate;
static pc_totalizer g_total;
static float g_last_rate = 0.0f;

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

    pc_window_init(&g_window, g_window_buf, 16);
    pc_rate_init(&g_rate);
    pc_totalizer_init(&g_total);

    server.on("/telemetry", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        char body[192];
        snprintf(body, sizeof(body),
                 "{\"samples\":%u,\"mean\":%.3f,\"stddev\":%.3f,\"min\":%.3f,\"max\":%.3f,"
                 "\"rate_per_s\":%.3f,\"total\":%.3f}",
                 (unsigned)pc_window_count(&g_window), pc_window_mean(&g_window), pc_window_stddev(&g_window),
                 pc_window_min(&g_window), pc_window_max(&g_window), g_last_rate,
                 pc_totalizer_total(&g_total));
        server.send(id, 200, "application/json", body);
    });

    server.begin(80);
}

void loop()
{
    server.handle();

    // Sample once a second and fold it into the telemetry helpers.
    static uint32_t last_ms = 0;
    uint32_t now = millis();
    if (now - last_ms >= 1000)
    {
        last_ms = now;
        float sample = (float)analogRead(34) * (3.3f / 4095.0f); // example: ADC voltage

        pc_window_push(&g_window, sample);                  // stats over the last 16 readings
        g_last_rate = pc_rate_update(&g_rate, sample, now); // slope (units/s)
        pc_totalizer_add(&g_total, sample, now);            // integrate the reading over time
    }
}
```
