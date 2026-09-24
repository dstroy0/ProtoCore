// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file TimeSourceFallback.ino
 * @brief Multi-source time fallback (PROTOCORE_ENABLE_TIME_SOURCE).
 *
 * Registers two time sources with priorities and lets the library fall back
 * automatically:
 *   - priority 0: NTP wall clock (returns 0 until the first SNTP sync)
 *   - priority 1: a battery-RTC stand-in that always has *a* time
 *
 * GET /time reports the current epoch and which source supplied it. At boot,
 * before NTP syncs, /time is served from the RTC; once NTP syncs it transparently
 * takes over (lower priority value wins). Swap the RTC stand-in for a real
 * DS3231/PCF8523 read, and add a GPS source the same way (return 0 with no fix).
 *
 * Flash, open Serial @ 115200 for the IP, then GET http://<ip>/time.
 */

#define PROTOCORE_ENABLE_NTP 1
#define PROTOCORE_ENABLE_TIME_SOURCE 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/application/ntp_service/ntp_service.h"
#include "services/timing_position/time_source/time_source.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// Priority 0: NTP - valid only once SNTP has synced (else 0 -> fall through).
static uint32_t src_ntp()
{
    NtpService.synced(protocore_ntp_service_span());
    if (!NtpServiceV.ok)
    {
        return 0;
    }
    NtpService.epoch(protocore_ntp_service_span());
    return (uint32_t)NtpServiceV.value;
}

// Priority 1: a coarse battery-RTC stand-in. A real device reads a DS3231/PCF8523
// over I2C here; this simulation is seeded at build time and counts via millis(),
// so the device always has a last-resort time.
static const uint32_t RTC_BASE = 1750000000u; // ~2025-06; replace with a real RTC read
static uint32_t src_rtc()
{
    return RTC_BASE + (uint32_t)(millis() / 1000);
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

    NtpServiceV.begin_args.tz = NULL; // start SNTP (GMT, pool.ntp.org)
    NtpServiceV.begin_args.server1 = NULL;
    NtpServiceV.begin_args.server2 = NULL;
    NtpService.begin(protocore_ntp_service_span());
    protocore_time_source_add("ntp", 0, src_ntp); // preferred
    protocore_time_source_add("rtc", 1, src_rtc); // fallback

    on_http("/time", HTTP_GET, [](uint8_t id, HttpReq *) {
        char body[96];
        uint32_t epoch = protocore_time_now();
        const char *src = protocore_time_source_active();
        snprintf(body, sizeof(body), "{\"epoch\":%u,\"source\":\"%s\"}", (unsigned)epoch, src ? src : "none");
        send_text(id, 200, "application/json", body);
    });
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
