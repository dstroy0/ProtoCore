// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file RadioPower.ino
 * @brief WiFi radio power controls (PC_ENABLE_RADIO_POWER).
 *
 * Applies a WiFi modem-sleep mode (and an optional max-TX cap) after the link is
 * up, to lower average power on a battery device at the cost of some latency.
 * GET /radio reports the mode read back from the radio.
 *
 * NOTE: set the mode via build flags so it reaches the separately-compiled library:
 *     build_flags = -DPC_ENABLE_RADIO_POWER=1 -DPC_RADIO_WIFI_PS=1
 *   (0 = none, 1 = min modem, 2 = max modem; + optional -DPC_RADIO_MAX_TX_DBM=11)
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_RADIO_POWER 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/physical/radio_power.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


void setup()
{
    Serial.begin(115200);
    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
    {
        delay(250);
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Apply the configured modem-sleep / TX settings AFTER the link is up (the
    // WiFi connect path may set its own default first).
    Radio.power();
    Serial.printf("radio modem-sleep: %s\n", Radio.ps_name(Radio.ps_mode()));

    on_http("/radio", HTTP_GET, [](uint8_t id, HttpReq *) {
        char b[48];
        snprintf(b, sizeof(b), "{\"modem_sleep\":\"%s\"}", Radio.ps_name(Radio.ps_mode()));
        send_text(id, 200, "application/json", b);
    });
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
