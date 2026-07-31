# UdpTelemetry - fire-and-forget UDP telemetry

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_UDP_TELEMETRY`

## What this example teaches

For high-frequency metrics you often do not want the cost or back-pressure of TCP.
This builds an InfluxDB line-protocol record and casts it to a collector over UDP
once a second - zero-heap, no ACK, no retry. Point it at Telegraf or InfluxDB's UDP
listener (or just `nc -u -l 8094` to watch the packets).

**Set the destination once:**

```cpp
pc_udp_telemetry_begin(COLLECTOR_IP, COLLECTOR_PORT);
```

**Build a line into a caller-owned buffer, then cast it.** `pc_line_*` appends
typed fields (the `i` suffix InfluxDB uses for integers comes from the `_int`/`_uint`
helpers); `pc_udp_telemetry_cast()` sends one datagram:

```cpp
char buf[PC_UDP_TELEMETRY_BUF];
pc_line line;
pc_line_init(&line, buf, sizeof(buf), "esp32");        // measurement name
pc_line_add_uint(&line, "heap", ESP.getFreeHeap());
pc_line_add_int(&line, "rssi", pc_net_rssi());
pc_line_add_float(&line, "temp", temperatureRead(), 1); // 1 decimal place
pc_udp_telemetry_cast(&line);                           // -> "esp32 heap=...i,rssi=...i,temp=..."
```

There is no server here - the device is purely a telemetry source - so `loop()`
just casts on a timer.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_UDP_TELEMETRY=1" \
  --lib="." examples/L7-Application/UdpTelemetry/UdpTelemetry.ino
```

```sh
nc -u -l 8094   # watch the line-protocol datagrams (set COLLECTOR_PORT to match)
```

## Annotated source

The complete sketch ([UdpTelemetry.ino](UdpTelemetry.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_UDP_TELEMETRY 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/udp_telemetry/udp_telemetry.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// Where the telemetry datagrams go (your collector's UDP host + port).
static const char *COLLECTOR_IP = "192.168.1.10";
static const uint16_t COLLECTOR_PORT = 8094;

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

    pc_udp_telemetry_begin(COLLECTOR_IP, COLLECTOR_PORT);
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        char buf[PC_UDP_TELEMETRY_BUF];
        pc_line line;
        pc_line_init(&line, buf, sizeof(buf), "esp32");
        pc_line_add_uint(&line, "heap", ESP.getFreeHeap());
        pc_line_add_int(&line, "rssi", pc_net_rssi());
        pc_line_add_float(&line, "temp", temperatureRead(), 1);
        if (pc_udp_telemetry_cast(&line))
            Serial.printf("cast: %s\n", buf);
    }
}
```
