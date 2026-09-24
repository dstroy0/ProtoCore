// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file RadioPower.ino
 * @brief WiFi radio power controls (PROTOCORE_ENABLE_RADIO_POWER).
 *
 * Applies a WiFi modem-sleep mode (and an optional max-TX cap) after the link is
 * up, to lower average power on a battery device at the cost of some latency.
 * GET /radio reports the mode read back from the radio.
 *
 * NOTE: set the mode via build flags so it reaches the separately-compiled library:
 *     build_flags = -DPROTOCORE_ENABLE_RADIO_POWER=1 -DPROTOCORE_RADIO_WIFI_PS=1
 *   (0 = none, 1 = min modem, 2 = max modem; + optional -DPROTOCORE_RADIO_MAX_TX_DBM=11)
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_RADIO_POWER 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/physical/radio_power/radio_power.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


void setup()
{
    Serial.begin(115200);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Apply the configured modem-sleep / TX settings AFTER the link is up (the
    // WiFi connect path may set its own default first).
    Radio.power(protocore_radio_power_span());
    Radio.ps_mode(protocore_radio_power_span());
    Radio.ps.mode = Radio.mode;
    Radio.ps_name(protocore_radio_power_span());
    Serial.printf("radio modem-sleep: %s\n", Radio.text);

    on_http("/radio", HTTP_GET, [](uint8_t id, HttpReq *) {
        char b[48];
        Radio.ps_mode(protocore_radio_power_span());
        Radio.ps.mode = Radio.mode;
        Radio.ps_name(protocore_radio_power_span());
        snprintf(b, sizeof(b), "{\"modem_sleep\":\"%s\"}", Radio.text);
        send_text(id, 200, "application/json", b);
    });
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
