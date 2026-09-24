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
 *     build_flags = -DPROTOCORE_ENABLE_SNMP=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.) A define in the
 * sketch alone does not reach the separately-compiled library .cpp.
 */

#define PROTOCORE_ENABLE_SNMP 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/net/snmp/snmp_agent/snmp_agent.h"

// SNMPv3 (USM) is an additional gated layer. Enable it for the whole build with
//     build_flags = -DPROTOCORE_ENABLE_SNMP=1 -DPROTOCORE_ENABLE_SNMP_V3=1
// then query with authPriv (HMAC-SHA-256 auth + AES-128 privacy), e.g.:
//   snmpget -v3 -u pc -l authPriv -a SHA-256 -A authpass12 -x AES -X privpass12 <ip> sysDescr.0
#if PROTOCORE_ENABLE_SNMP_V3
#include "services/net/snmp/snmp_v3/snmp_v3.h"
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
    out->type = (uint8_t)SNMP_TAG_SNMP_GAUGE32;
    out->uval = (uint32_t)ESP.getFreeHeap();
    return true;
}

// Writable: drive the on-board LED from an INTEGER (0 = off, non-zero = on).
bool set_led(const SnmpValue *in)
{
    if (in->type != (uint8_t)SNMP_TAG_BER_INTEGER)
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

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Build the MIB: standard system group + private objects.
    SnmpAgentV.community.ro = "public"; // read-only community
    SnmpAgent.init(protocore_snmp_agent_span());
    SnmpAgentV.community.rw = "private"; // read-write community (authorizes Set)
    SnmpAgent.set_rw_community(protocore_snmp_agent_span());
    SnmpAgentV.system.descr = "ProtoCore SNMP agent";
    SnmpAgentV.system.contact = "admin@example.com";
    SnmpAgentV.system.name = "esp32-pc";
    SnmpAgentV.system.location = "lab bench";
    SnmpAgentV.system.services = 72;
    SnmpAgent.set_system(protocore_snmp_agent_span());

    SnmpAgentV.object.oid = OID_FREE_HEAP;
    SnmpAgentV.object.oid_len = 9;
    SnmpAgentV.object.type = (uint8_t)SNMP_TAG_SNMP_GAUGE32;
    SnmpAgentV.object.getter = get_free_heap;
    SnmpAgent.add_dynamic(protocore_snmp_agent_span());

    SnmpAgentV.object.oid = OID_LED;
    SnmpAgentV.object.oid_len = 9;
    SnmpAgentV.object.ival = 0;
    SnmpAgentV.object.setter = set_led; // writable
    SnmpAgent.add_integer(protocore_snmp_agent_span());

#if PROTOCORE_ENABLE_SNMP_V3
    // SNMPv3 USM: a single authPriv user (HMAC-SHA-256 + AES-128). For a unique
    // engine ID, derive it from the chip MAC; persist/increment engineBoots in NVS.
    SnmpV3V.engine.engine_id = nullptr;
    SnmpV3V.engine.engine_id_len = 0;
    SnmpV3.init(protocore_snmp_v3_span());
    SnmpV3V.engine.boots = 1;
    SnmpV3.set_boots(protocore_snmp_v3_span());
    SnmpV3V.user.user = "pc";
    SnmpV3V.user.auth_pass = "authpass12";
    SnmpV3V.user.priv_pass = "privpass12";
    SnmpV3.set_user(protocore_snmp_v3_span());
    Serial.println("SNMPv3 user 'pc' enabled (authPriv: SHA-256 / AES-128)");
#endif

    // Bind the agent to UDP/161 (raw lwIP, callback-driven).
    SnmpAgentV.port = 161;
    SnmpAgent.listen(protocore_snmp_agent_span());
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
