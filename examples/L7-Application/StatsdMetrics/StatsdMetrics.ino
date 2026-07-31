// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file StatsdMetrics.ino
 * @brief Push metrics to a StatsD collector (PC_ENABLE_STATSD).
 *
 * StatsD is the standard "push" metrics protocol - one UDP line per metric,
 * `name:value|type` - understood by Graphite/StatsD, Telegraf, Datadog, and friends. It is
 * the push counterpart to the Prometheus `/metrics` endpoint (example 21): instead of a
 * server scraping the device, the device shoves numbers out as things happen, which is handy
 * when the device sits behind NAT and nothing can reach in to scrape it.
 *
 * This sketch reports a few device metrics every 10 s: a counter, a gauge (free heap), and a
 * timing. Point STATSD_HOST at any StatsD-speaking collector on your network. To see them
 * quickly, run Telegraf with a `[[inputs.statsd]]` section, or the reference StatsD +
 * Graphite, or `nc -u -l 8125` to just watch the raw lines arrive.
 *
 * Build flags (PlatformIO): `-DPC_ENABLE_STATSD=1`
 */

#define PC_ENABLE_STATSD 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/statsd/statsd.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// Your StatsD collector (Telegraf / statsd / Datadog agent). 8125 is the standard port.
static const char *STATSD_HOST = "192.168.1.50";
static const uint16_t STATSD_PORT = 8125;

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

    // Every metric this device sends is tagged so the collector can group by device (DogStatsD
    // tag syntax; harmless with plain StatsD collectors that ignore it).
    pc_statsd_begin(STATSD_HOST, STATSD_PORT, "device:esp32-demo");
    Serial.printf("StatsD -> %s:%u\n", STATSD_HOST, STATSD_PORT);
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 10000)
    {
        last = millis();

        pc_statsd_count("esp32.loops", 1);                              // a counter (rate over time)
        pc_statsd_gauge("esp32.heap.free", (int64_t)ESP.getFreeHeap()); // a gauge (absolute level)
        pc_statsd_gauge("esp32.uptime.s", (int64_t)(millis() / 1000));

        uint32_t t0 = millis();
        // ... do some work you want to measure here ...
        pc_statsd_timing("esp32.loop.work_ms", millis() - t0); // a timing/duration

        Serial.println("pushed metrics");
    }
}
