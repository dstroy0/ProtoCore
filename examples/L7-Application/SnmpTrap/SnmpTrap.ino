// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SnmpTrap.ino
 * @brief Outbound SNMP notifications: the agent pushes traps to a manager.
 *
 * Sends an SNMPv2c trap to a manager every few seconds carrying a custom
 * enterprise trap OID and a Gauge32 binding (free heap). Point MANAGER at your
 * trap receiver (e.g. snmptrapd on UDP/162):
 *     snmptrapd -f -Lo -c snmptrapd.conf   # with "authCommunity log,execute,net public"
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPC_ENABLE_SNMP=1 -DPC_ENABLE_SNMP_TRAP=1
 *     ; for SNMPv3 traps also: -DPC_ENABLE_SNMP_V3=1 (then call pc_snmp_trap_v3)
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

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

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 5000)
    {
        last = millis();

        SnmpVarbind vb;
        memset(&vb, 0, sizeof(vb));
        vb.oid = HEAP_OID;
        vb.oid_len = sizeof(HEAP_OID) / sizeof(uint32_t);
        vb.type = (uint8_t)SnmpVbType::SNMP_VB_GAUGE32;
        vb.ival = (long)ESP.getFreeHeap();

        bool ok = pc_snmp_trap_v2c(MANAGER, TRAP_PORT, "public", TRAP_OID, sizeof(TRAP_OID) / sizeof(uint32_t), &vb, 1);
        Serial.printf("trap -> %s : %s (heap=%ld)\n", MANAGER, ok ? "sent" : "failed", vb.ival);
    }
}
