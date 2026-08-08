// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SNMP.ino
 * @brief Zero-heap SNMP v1/v2c agent on UDP/161 (Get / GetNext / GetBulk / Set).
 *
 * Exposes the standard MIB-II system group plus a few private objects under an
 * enterprise subtree (1.3.6.1.4.1.49374): a read-only free-heap gauge and a
 * writable LED-state integer. The agent is a raw lwIP UDP socket - callback
 * driven, no per-loop servicing - and all of its buffers are static (no heap),
 * so it preserves the library's determinism guarantee.
 *
 * Flash, open Serial @ 115200 for the IP, then from a host with net-snmp:
 *   snmpget   -v2c -c public  <ip> sysDescr.0
 *   snmpwalk  -v2c -c public  <ip> system
 *   snmpwalk  -v2c -c public  <ip> 1.3.6.1.4.1.49374
 *   snmpget   -v2c -c public  <ip> 1.3.6.1.4.1.49374.10.0        # free heap (Gauge32)
 *   snmpset   -v2c -c private <ip> 1.3.6.1.4.1.49374.20.0 i 1    # LED on
 * (snmpbulkwalk also works thanks to GetBulk.)
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see. The `#define` below documents intent, but for PlatformIO you must
 * enable it for the whole build, e.g. in platformio.ini:
 *     build_flags = -DPC_ENABLE_SNMP=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.) A define in the
 * sketch alone does not reach the separately-compiled library .cpp.
 */

#define PC_ENABLE_SNMP 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/snmp/snmp_agent.h"

// SNMPv3 (USM) is an additional gated layer. Enable it for the whole build with
//     build_flags = -DPC_ENABLE_SNMP=1 -DPC_ENABLE_SNMP_V3=1
// then query with authPriv (HMAC-SHA-256 auth + AES-128 privacy), e.g.:
//   snmpget -v3 -u pc -l authPriv -a SHA-256 -A authpass12 -x AES -X privpass12 <ip> sysDescr.0
#if PC_ENABLE_SNMP_V3
#include "services/net/snmp/snmp_v3.h"
#endif

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif


// Private enterprise subtree: 1.3.6.1.4.1.49374
static const uint32_t OID_FREE_HEAP[] = {1, 3, 6, 1, 4, 1, 49374, 10, 0}; // Gauge32, read-only
static const uint32_t OID_LED[] = {1, 3, 6, 1, 4, 1, 49374, 20, 0};       // INTEGER, writable

// Dynamic read: report the current free heap as a Gauge32.
bool get_free_heap(SnmpValue *out)
{
    out->type = (uint8_t)SnmpTag::SNMP_GAUGE32;
    out->uval = (uint32_t)ESP.getFreeHeap();
    return true;
}

// Writable: drive the on-board LED from an INTEGER (0 = off, non-zero = on).
bool set_led(const SnmpValue *in)
{
    if (in->type != (uint8_t)SnmpTag::BER_INTEGER)
    {
        return false; // wrong type -> the agent replies wrongType
    }
    digitalWrite(LED_BUILTIN, in->ival ? HIGH : LOW);
    return true;
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);

    Physical.wifi->init(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Build the MIB: standard system group + private objects.
    pc_snmp_agent_init("public");              // read-only community
    pc_snmp_agent_set_rw_community("private"); // read-write community (authorizes Set)
    pc_snmp_agent_set_system("ProtoCore SNMP agent", "admin@example.com", "esp32-pc", "lab bench", 72);
    pc_snmp_agent_add_dynamic(OID_FREE_HEAP, 9, (uint8_t)SnmpTag::SNMP_GAUGE32, get_free_heap);
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

    int32_t result = begin_http(80, NULL);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
    }
}

void loop()
{
    handle(); // SNMP is serviced by lwIP callbacks; this drives the TCP server.
}
