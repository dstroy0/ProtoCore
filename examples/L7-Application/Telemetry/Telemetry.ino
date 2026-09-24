// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Telemetry.ino
 * @brief Moving-window stats, rate-of-change, and a totalizer (PROTOCORE_ENABLE_TELEMETRY).
 *
 * Turns a periodic sensor reading into dashboard figures with zero heap: a
 * moving-window mean/stddev/min/max, the rate of change (slope) of the signal,
 * and a run-time totalizer that integrates the reading over time (an odometer).
 * GET /telemetry returns them as JSON for a dashboard or alert rule.
 *
 * NOTE: enable the helpers for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_TELEMETRY=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_TELEMETRY 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/iot/telemetry/telemetry.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static float g_window_buf[16]; // caller-owned window storage (no heap)
static TelemetryWindow g_window;
static TelemetryRate g_rate;
static TelemetryTotalizer g_total;
static float g_last_rate = 0.0f;
static uint8_t telemetry_work[16]; // Telemetry keeps no state in its borrow

// Run one window statistic over g_window and return the float it reports.
static float window_stat(void (*const entry)(uint8_t *work))
{
    TelemetryV.window.w = &g_window;
    entry(telemetry_work);
    return TelemetryV.f32;
}

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

    TelemetryV.window.w = &g_window;
    TelemetryV.window.buf = g_window_buf;
    TelemetryV.window.cap = 16;
    Telemetry.window_init(telemetry_work);
    TelemetryV.rate.r = &g_rate;
    Telemetry.rate_init(telemetry_work);
    TelemetryV.totalizer.t = &g_total;
    Telemetry.totalizer_init(telemetry_work);

    on_http("/telemetry", HTTP_GET, [](uint8_t id, HttpReq *) {
        char body[192];
        TelemetryV.window.w = &g_window;
        Telemetry.window_count(telemetry_work);
        unsigned count = TelemetryV.u16;
        float mean = window_stat(Telemetry.window_mean);
        float stddev = window_stat(Telemetry.window_stddev);
        float min = window_stat(Telemetry.window_min);
        float max = window_stat(Telemetry.window_max);
        TelemetryV.totalizer.t = &g_total;
        Telemetry.totalizer_total(telemetry_work);
        double total = TelemetryV.f64;
        snprintf(body, sizeof(body),
                 "{\"samples\":%u,\"mean\":%.3f,\"stddev\":%.3f,\"min\":%.3f,\"max\":%.3f,"
                 "\"rate_per_s\":%.3f,\"total\":%.3f}",
                 count, mean, stddev, min, max, g_last_rate, total);
        send_text(id, 200, "application/json", body);
    });

    begin_http(80, NULL);
}

void loop()
{
    handle();

    // Sample once a second and fold it into the telemetry helpers.
    static uint32_t last_ms = 0;
    uint32_t now = millis();
    if (now - last_ms >= 1000)
    {
        last_ms = now;
        float sample = (float)analogRead(34) * (3.3f / 4095.0f); // example: ADC voltage

        TelemetryV.window.w = &g_window; // stats over the last 16 readings
        TelemetryV.window.sample = sample;
        Telemetry.window_push(telemetry_work);

        TelemetryV.rate.r = &g_rate; // slope (units/s)
        TelemetryV.rate.value = sample;
        TelemetryV.rate.now_ms = now;
        Telemetry.rate_update(telemetry_work);
        g_last_rate = TelemetryV.f32;

        TelemetryV.totalizer.t = &g_total; // integrate the reading over time
        TelemetryV.totalizer.rate = sample;
        TelemetryV.totalizer.now_ms = now;
        Telemetry.totalizer_add(telemetry_work);
    }
}
