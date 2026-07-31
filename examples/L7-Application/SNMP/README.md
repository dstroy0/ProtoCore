# SNMP - a zero-heap SNMP agent

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_SNMP` (optional `PC_ENABLE_SNMP_V3`)

## What this example teaches

This exposes the device over SNMP on UDP/161: the standard MIB-II system group
plus a few private objects under an enterprise subtree
(`1.3.6.1.4.1.49374`) - a read-only free-heap gauge and a writable LED integer.
It handles Get / GetNext / GetBulk / Set (so `snmpwalk` and `snmpbulkwalk` work),
is callback-driven over lwIP UDP, and keeps every buffer static.

**Build the MIB, then bind.** You set the communities and system group, register
objects (static, dynamic via a getter, or writable via a setter), then start the
agent:

```cpp
pc_snmp_agent_init("public");              // read-only community
pc_snmp_agent_set_rw_community("private"); // authorizes Set
pc_snmp_agent_set_system("...desc...", "admin@example.com", "esp32-pc", "lab bench");
pc_snmp_agent_add_dynamic(OID_FREE_HEAP, 9, SnmpTag::SNMP_GAUGE32, get_free_heap); // computed each Get
pc_snmp_agent_add_integer(OID_LED, 9, 0, set_led);                        // writable
pc_snmp_agent_begin_udp(161);
```

**Dynamic + writable objects.** A dynamic getter fills an `SnmpValue` on each Get;
a setter validates the incoming type (returning `wrongType` to the manager if it
mismatches) and applies the change:

```cpp
bool get_free_heap(SnmpValue *out) { out->type = SnmpTag::SNMP_GAUGE32; out->uval = ESP.getFreeHeap(); return true; }
bool set_led(const SnmpValue *in) {
    if (in->type != SnmpTag::BER_INTEGER) return false;       // -> wrongType
    digitalWrite(LED_BUILTIN, in->ival ? HIGH : LOW); return true;
}
```

**Optional SNMPv3 (USM).** Adding `-DPC_ENABLE_SNMP_V3=1` enables an authPriv
user (HMAC-SHA-256 auth + AES-128 privacy): `pc_snmp_v3_init` / `pc_snmp_v3_set_boots` /
`pc_snmp_v3_set_user`. For outbound trap notifications, see
[SnmpTrap](../SnmpTrap).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_SNMP=1" \
  --lib="." examples/L7-Application/SNMP/SNMP.ino
```

```sh
snmpwalk -v2c -c public  <ip> system
snmpget  -v2c -c public  <ip> 1.3.6.1.4.1.49374.10.0     # free heap (Gauge32)
snmpset  -v2c -c private <ip> 1.3.6.1.4.1.49374.20.0 i 1 # LED on
```

## Annotated source

The complete sketch ([SNMP.ino](SNMP.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_SNMP 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/snmp/snmp_agent.h"

// SNMPv3 (USM) is an additional gated layer. Enable it for the whole build with
//     build_flags = -DPC_ENABLE_SNMP=1 -DPC_ENABLE_SNMP_V3=1
// then query with authPriv (HMAC-SHA-256 auth + AES-128 privacy).
#if PC_ENABLE_SNMP_V3
#include "services/net/snmp/snmp_v3.h"
#endif

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

PC server;

// Private enterprise subtree: 1.3.6.1.4.1.49374
static const uint32_t OID_FREE_HEAP[] = {1, 3, 6, 1, 4, 1, 49374, 10, 0}; // Gauge32, read-only
static const uint32_t OID_LED[] = {1, 3, 6, 1, 4, 1, 49374, 20, 0};       // INTEGER, writable

// Dynamic read: report the current free heap as a Gauge32 (computed each Get).
bool get_free_heap(SnmpValue *out)
{
    out->type = SnmpTag::SNMP_GAUGE32;
    out->uval = (uint32_t)ESP.getFreeHeap();
    return true;
}

// Writable: drive the on-board LED from an INTEGER (0 = off, non-zero = on).
bool set_led(const SnmpValue *in)
{
    if (in->type != SnmpTag::BER_INTEGER)
        return false; // wrong type -> the agent replies wrongType
    digitalWrite(LED_BUILTIN, in->ival ? HIGH : LOW);
    return true;
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);

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

    // Build the MIB: standard system group + private objects.
    pc_snmp_agent_init("public");              // read-only community
    pc_snmp_agent_set_rw_community("private"); // read-write community (authorizes Set)
    pc_snmp_agent_set_system("ProtoCore SNMP agent", "admin@example.com", "esp32-pc", "lab bench");
    pc_snmp_agent_add_dynamic(OID_FREE_HEAP, 9, SnmpTag::SNMP_GAUGE32, get_free_heap);
    pc_snmp_agent_add_integer(OID_LED, 9, 0, set_led); // writable

#if PC_ENABLE_SNMP_V3
    // SNMPv3 USM: a single authPriv user (HMAC-SHA-256 + AES-128). For a unique
    // engine ID, derive it from the chip MAC; persist/increment engineBoots in NVS.
    pc_snmp_v3_init(nullptr, 0);
    pc_snmp_v3_set_boots(1);
    pc_snmp_v3_set_user("pc", "authpass12", "privpass12");
    Serial.println("SNMPv3 user 'pc' enabled (authPriv: SHA-256 / AES-128)");
#endif

    // Bind the agent to UDP/161 (raw lwIP, callback-driven).
    pc_snmp_agent_begin_udp(161);
    Serial.println("SNMP agent listening on UDP/161 (try: snmpwalk -v2c -c public <ip> system)");

    int32_t result = server.begin(80);
    if (result < 0)
        Serial.printf("begin() failed (error %d)\n", result);
}

void loop()
{
    server.handle(); // SNMP is serviced by lwIP callbacks; this drives the TCP server.
}
```
