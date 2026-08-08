# LogBuffer - a fixed-RAM rotating log buffer with severity traps

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_LOGBUF`

## What this example teaches

When there is no serial cable attached, you still want the last few log lines. This
keeps the most recent `PC_LOG_LINES` lines in a fixed RAM ring (oldest pruned on
overflow), serves them at `GET /logs`, and fires a trap callback on WARN+ lines -
here it just prints, but a real app could forward an SNMP trap
([SnmpTrap](../SnmpTrap)) or a webhook ([Webhook](../Webhook)).

**Set a severity trap, then log by level:**

```cpp
pc_log_set_trap(PC_LOG_WARN, on_trap); // trap on WARN and ERROR
pc_log(PC_LOG_INFO, "boot complete");
```

The trap fires only for lines at or above the threshold, so criticals get pushed
while routine INFO lines just accumulate in the ring:

```cpp
static void on_trap(uint8_t level, const char *line) {
    Serial.printf("[trap] %s\n", line); // forward criticals here (SNMP trap / webhook)
}
```

**Dump the ring on demand.** `pc_log_dump()` writes the whole buffer into a
caller-owned array sized `PC_LOG_LINES * PC_LOG_LINE_LEN`:

```cpp
char buf[PC_LOG_LINES * PC_LOG_LINE_LEN];
pc_log_dump(buf, sizeof(buf));
server.send(id, 200, "text/plain", buf);
```

The loop logs a heap/uptime line every five seconds, escalating to WARN (which
trips the trap) when heap runs low.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_LOGBUF=1" \
  --lib="." examples/L7-Application/LogBuffer/LogBuffer.ino
```

```sh
curl http://<ip>/logs   # the last PC_LOG_LINES lines, oldest first
```

## Annotated source

The complete sketch ([LogBuffer.ino](LogBuffer.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_LOGBUF 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "server/logbuf.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Fired for every line at or above the trap threshold.
static void on_trap(uint8_t level, const char *line)
{
    Serial.printf("[trap] %s\n", line); // forward criticals here (SNMP trap / webhook)
    (void)level;
}

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

    pc_log_set_trap(PC_LOG_WARN, on_trap); // trap on WARN and ERROR
    pc_log(PC_LOG_INFO, "boot complete");

    server.on("/logs", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        char buf[PC_LOG_LINES * PC_LOG_LINE_LEN];
        pc_log_dump(buf, sizeof(buf));
        server.send(id, 200, "text/plain", buf);
    });
    server.begin(80);
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 5000)
    {
        last = millis();
        char msg[64];
        uint32_t heap = ESP.getFreeHeap();
        snprintf(msg, sizeof(msg), "heap=%u uptime=%lus", (unsigned)heap, millis() / 1000);
        pc_log(heap < 20000 ? PC_LOG_WARN : PC_LOG_INFO, msg); // WARN trips the trap
    }
    server.handle();
}
```
