# SnmpTrap - the agent pushes SNMP traps to a manager

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_SNMP`, `PC_ENABLE_SNMP_TRAP` (optional `PC_ENABLE_SNMP_V3`)

## What this example teaches

[SNMP](../SNMP) answered manager queries; this is the **outbound** direction.
The device sends an SNMPv2c trap to a manager every few seconds carrying a custom
enterprise trap OID and a `Gauge32` varbind (free heap) - the push side of network
monitoring, where the device alerts the manager instead of waiting to be polled.

**Build the varbind, then fire the trap:**

```cpp
SnmpVarbind vb;
memset(&vb, 0, sizeof(vb));
vb.oid = HEAP_OID;
vb.oid_len = sizeof(HEAP_OID) / sizeof(uint32_t);
vb.type = SnmpVbType::SNMP_VB_GAUGE32;
vb.ival = (long)ESP.getFreeHeap();

bool ok = pc_snmp_trap_v2c(MANAGER, TRAP_PORT, "public",
                        TRAP_OID, sizeof(TRAP_OID) / sizeof(uint32_t), &vb, 1);
```

`pc_snmp_trap_v2c(manager, port, community, trap_oid, trap_oid_len, varbinds, n)`
sends one notification with `n` attached varbinds. Point `MANAGER` at your trap
receiver (for example `snmptrapd` on UDP/162). For SNMPv3 traps, add
`-DPC_ENABLE_SNMP_V3=1` and call `pc_snmp_trap_v3` instead.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_SNMP=1 -DPC_ENABLE_SNMP_TRAP=1" \
  --lib="." examples/L7-Application/SnmpTrap/SnmpTrap.ino
```

```sh
# receive the traps on a host (community "public"):
snmptrapd -f -Lo -c snmptrapd.conf   # snmptrapd.conf: authCommunity log,execute,net public
```

## Annotated source

The complete sketch ([SnmpTrap.ino](SnmpTrap.ino)), reproduced verbatim with
added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_SNMP 1
#define PC_ENABLE_SNMP_TRAP 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/snmp/snmp_notify.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *MANAGER = "192.168.1.100"; // your trap receiver
static const uint16_t TRAP_PORT = 162;

// Enterprise trap OID under a private subtree (1.3.6.1.4.1.9999.x).
static const uint32_t TRAP_OID[] = {1, 3, 6, 1, 4, 1, 9999, 1, 0, 1};
// The OID of the Gauge32 binding we attach (free-heap gauge).
static const uint32_t HEAP_OID[] = {1, 3, 6, 1, 4, 1, 9999, 1, 1, 0};

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
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 5000)
    {
        last = millis();

        // One Gauge32 varbind: the current free heap.
        SnmpVarbind vb;
        memset(&vb, 0, sizeof(vb));
        vb.oid = HEAP_OID;
        vb.oid_len = sizeof(HEAP_OID) / sizeof(uint32_t);
        vb.type = SnmpVbType::SNMP_VB_GAUGE32;
        vb.ival = (long)ESP.getFreeHeap();

        bool ok = pc_snmp_trap_v2c(MANAGER, TRAP_PORT, "public", TRAP_OID, sizeof(TRAP_OID) / sizeof(uint32_t), &vb, 1);
        Serial.printf("trap -> %s : %s (heap=%ld)\n", MANAGER, ok ? "sent" : "failed", vb.ival);
    }
}
```
