// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Telemetry.ino
 * @brief Moving-window stats, rate-of-change, and a totalizer (PC_ENABLE_TELEMETRY).
 *
 * Turns a periodic sensor reading into dashboard figures with zero heap: a
 * moving-window mean/stddev/min/max, the rate of change (slope) of the signal,
 * and a run-time totalizer that integrates the reading over time (an odometer).
 * GET /telemetry returns them as JSON for a dashboard or alert rule.
 *
 * NOTE: enable the helpers for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_TELEMETRY=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_TELEMETRY 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/telemetry/telemetry.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

static float g_window_buf[16]; // caller-owned window storage (no heap)
static pc_window g_window;
static pc_rate g_rate;
static pc_totalizer g_total;
static float g_last_rate = 0.0f;

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

    pc_window_init(&g_window, g_window_buf, 16);
    pc_rate_init(&g_rate);
    pc_totalizer_init(&g_total);

    server.on("/telemetry", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        char body[192];
        snprintf(body, sizeof(body),
                 "{\"samples\":%u,\"mean\":%.3f,\"stddev\":%.3f,\"min\":%.3f,\"max\":%.3f,"
                 "\"rate_per_s\":%.3f,\"total\":%.3f}",
                 (unsigned)pc_window_count(&g_window), pc_window_mean(&g_window), pc_window_stddev(&g_window),
                 pc_window_min(&g_window), pc_window_max(&g_window), g_last_rate, pc_totalizer_total(&g_total));
        server.send(id, 200, "application/json", body);
    });

    server.begin(80);
}

void loop()
{
    server.handle();

    // Sample once a second and fold it into the telemetry helpers.
    static uint32_t last_ms = 0;
    uint32_t now = millis();
    if (now - last_ms >= 1000)
    {
        last_ms = now;
        float sample = (float)analogRead(34) * (3.3f / 4095.0f); // example: ADC voltage

        pc_window_push(&g_window, sample);                  // stats over the last 16 readings
        g_last_rate = pc_rate_update(&g_rate, sample, now); // slope (units/s)
        pc_totalizer_add(&g_total, sample, now);            // integrate the reading over time
    }
}
