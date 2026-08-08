// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file PartitionMonitor.ino
 * @brief Flash partition-map monitor endpoint (PC_ENABLE_PARTITION_MONITOR).
 *
 * Serves the device's flash partition table as JSON at /partitions: each entry's
 * label, kind (factory / ota / nvs / littlefs / ...), type/subtype, flash offset,
 * size, and which app slot is currently running. Handy for diagnostics and OTA
 * dashboards - no special hardware needed.
 *
 * NOTE: enable it for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_PARTITION_MONITOR=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Flash, then GET http://<ip>/partitions.
 */

#define PC_ENABLE_PARTITION_MONITOR 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/storage/partition_monitor/partition_monitor.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


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
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    pc_partition_monitor_begin("/partitions");
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
