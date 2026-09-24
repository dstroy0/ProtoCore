// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file PartitionMonitor.ino
 * @brief Flash partition-map monitor endpoint (PROTOCORE_ENABLE_PARTITION_MONITOR).
 *
 * Serves the device's flash partition table as JSON at /partitions: each entry's
 * label, kind (factory / ota / nvs / littlefs / ...), type/subtype, flash offset,
 * size, and which app slot is currently running. Handy for diagnostics and OTA
 * dashboards - no special hardware needed.
 *
 * NOTE: enable it for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_PARTITION_MONITOR=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Flash, then GET http://<ip>/partitions.
 */

#define PROTOCORE_ENABLE_PARTITION_MONITOR 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "server/storage/partition_monitor/partition_monitor.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static uint8_t partition_monitor_work[16]; // the borrow a PartitionMonitor entry takes; it keeps no state in it


void setup()
{
    Serial.begin(115200);
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

    PartitionMonitorV.begin_args.path = "/partitions";
    PartitionMonitor.begin(partition_monitor_work);
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
