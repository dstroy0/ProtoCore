// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Rtc.ino
 * @brief Keep accurate time with a DS3231/DS1307 RTC module (PC_ENABLE_RTC).
 *
 * A real-time-clock chip has its own coin-cell battery, so it keeps time even when the ESP32
 * is unplugged - and needs no network. This reads it over I2C and plugs it into the
 * time-source chain, so `pc_time_now()` has the correct time the instant the board boots,
 * offline. It also self-initializes: the first time the board reaches the internet it sets the
 * RTC from NTP, so from then on the RTC is the source of truth even with no network.
 *
 * Together with the GPS + NTP fallback (example 58), this is the middle of the chain:
 * GPS (best) -> RTC (offline, battery-backed) -> upstream NTP. Feed it to the NTP server to
 * hand accurate time to your whole LAN.
 *
 * Wiring (I2C): module SDA -> GPIO 21, SCL -> GPIO 22, VCC -> 3V3, GND -> GND (ESP32 default
 * I2C pins; change with Wire.begin(sda, scl) if yours differ).
 *
 * Build flags (PlatformIO): `-DPC_ENABLE_RTC=1 -DPC_ENABLE_TIME_SOURCE=1 -DPC_ENABLE_NTP=1`
 */

#define PC_ENABLE_RTC 1
#define PC_ENABLE_TIME_SOURCE 1
#define PC_ENABLE_NTP 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/peripherals/rtc/rtc.h"
#include "network_drivers/application/ntp_service/ntp_service.h"
#include "services/timing_position/time_source/time_source.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// Fallback used only to *set* the RTC the first time we reach the internet.
static uint32_t pc_ntp_source()
{
    return pc_ntp_synced() ? (uint32_t)pc_ntp_epoch() : 0;
}

void setup()
{
    Serial.begin(115200);
    pc_rtc_begin();

    // The RTC gives us time immediately - before WiFi, before anything - if it is set.
    uint32_t boot = pc_rtc_read_epoch();
    Serial.printf("RTC at boot: %lu %s\n", (unsigned long)boot, boot ? "(battery-backed time!)" : "(not set yet)");

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

    // The RTC is the primary source; upstream NTP is only for setting it.
    pc_time_source_add("rtc", 1, pc_rtc_time_source);
    pc_time_source_add("ntp", 2, pc_ntp_source);
    pc_ntp_begin(NULL, NULL, NULL);
}

void loop()
{
    // Self-initialize: once we have accurate internet time and the RTC is unset/wrong, write it.
    static bool pc_rtc_set = false;
    if (!pc_rtc_set && pc_ntp_synced())
    {
        uint32_t pc_rtc_now = pc_rtc_read_epoch();
        uint32_t pc_ntp_now = (uint32_t)pc_ntp_epoch();
        if (pc_rtc_now == 0 || (pc_ntp_now > pc_rtc_now ? pc_ntp_now - pc_rtc_now : pc_rtc_now - pc_ntp_now) > 5)
        {
            if (pc_rtc_set_epoch(pc_ntp_now))
            {
                Serial.printf("RTC set from NTP: %lu\n", (unsigned long)pc_ntp_now);
            }
        }
        pc_rtc_set = true;
    }

    static uint32_t last = 0;
    if (millis() - last > 5000)
    {
        last = millis();
        Serial.printf("[time] now=%lu source=%s\n", (unsigned long)pc_time_now(),
                      pc_time_source_active() ? pc_time_source_active() : "none");
    }
}
