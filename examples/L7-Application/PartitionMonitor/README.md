# PartitionMonitor - a flash partition-map endpoint

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_PARTITION_MONITOR`

## What this example teaches

The ESP32's flash is carved into partitions (factory app, OTA slots, NVS,
LittleFS, ...). `pc_partition_monitor_begin()` serves that table as JSON at
`/partitions`: each entry's label, kind, type/subtype, flash offset, and size, plus
which app slot is currently running. It is a one-call diagnostics endpoint that
pairs well with OTA dashboards, and it needs no special hardware.

**One call mounts the endpoint:**

```cpp
pc_partition_monitor_begin(server, "/partitions");
```

The handler reads the partition table from the ESP-IDF API and serializes it
directly into the response (no heap).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_PARTITION_MONITOR=1" \
  --lib="." examples/L7-Application/PartitionMonitor/PartitionMonitor.ino
```

```sh
curl http://<ip>/partitions   # JSON: labels, offsets, sizes, running slot
```

## Annotated source

The complete sketch ([PartitionMonitor.ino](PartitionMonitor.ino)),
reproduced verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_PARTITION_MONITOR 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/storage/partition_monitor/partition_monitor.h"

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

    // Serve the flash partition table as JSON at /partitions.
    pc_partition_monitor_begin(server, "/partitions");
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
