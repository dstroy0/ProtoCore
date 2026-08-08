// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Dashboard.ino
 * @brief Real-time SVG dashboard with live telemetry and WebSocket controls
 *        (PC_ENABLE_DASHBOARD).
 *
 * Declares a fixed compile-time widget table and serves it at /dashboard. Display
 * widgets (gauge / value / chart) update live over SSE; control widgets (toggle /
 * slider / button) send values back to the device over WebSocket, delivered to a
 * control callback. The sketch feeds readings with pc_dashboard_set(key, value)
 * and pushes a frame with pc_dashboard_publish().
 *
 * NOTE: enable the dashboard for the whole build (a .ino #define does not reach
 * the separately compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_DASHBOARD=1
 * (PC_ENABLE_SSE and PC_ENABLE_WEBSOCKET are on by default; SSE is required,
 * WebSocket enables the controls. Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it
 * builds as-is.)
 *
 * Flash, then open http://<ip>/dashboard.
 */

#define PC_ENABLE_DASHBOARD 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/web/dashboard/dashboard.h"
#include <math.h>


static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static const int LED_PIN = 2; // onboard LED on many ESP32 dev boards

// Compile-time widget table - the dashboard's deterministic source of truth.
// Display widgets are fed over SSE; control widgets send values back over WS.
static const pc_widget WIDGETS[] = {
    {pc_widget_type::PC_WIDGET_GAUGE, "Free heap", "heap", 0, 320000, "B"},
    {pc_widget_type::PC_WIDGET_VALUE, "Uptime", "uptime", 0, 0, "s"},
    {pc_widget_type::PC_WIDGET_CHART, "WiFi RSSI", "rssi", -100, 0, "dBm"},
    {pc_widget_type::PC_WIDGET_TOGGLE, "Onboard LED", "led", 0, 1, ""},
    {pc_widget_type::PC_WIDGET_SLIDER, "Brightness", "bright", 0, 255, ""},
    {pc_widget_type::PC_WIDGET_BUTTON, "Identify", "ident", 0, 0, ""},
};

// Invoked when a control widget sends a value from the browser.
static void on_control(const char *key, float value)
{
    Serial.printf("control %s = %.1f\n", key, (double)value);
    if (strcmp(key, "led") == 0)
    {
        digitalWrite(LED_PIN, value >= 0.5f ? HIGH : LOW);
    }
    else if (strcmp(key, "bright") == 0)
    {
        analogWrite(LED_PIN, (int)value);
    }
    // "ident" is momentary - just log it above.
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
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

    pc_dashboard_on_control(on_control);
    pc_dashboard_begin("/dashboard", WIDGETS, sizeof(WIDGETS) / sizeof(WIDGETS[0]));
    begin_http(80, NULL);
}

void loop()
{
    handle();

    // Push a telemetry frame once a second.
    static uint32_t last_ms = 0;
    uint32_t now = millis();
    if (now - last_ms >= 1000)
    {
        last_ms = now;
        pc_dashboard_set("heap", (float)ESP.getFreeHeap());
        pc_dashboard_set("uptime", (float)(now / 1000));
        pc_dashboard_set("rssi", (float)Physical.wifi->rssi());
        pc_dashboard_publish();
    }
}
