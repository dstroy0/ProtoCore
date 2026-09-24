// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SNTP.ino
 * @brief Wall-clock time via SNTP (PROTOCORE_ENABLE_NTP).
 *
 * NtpService.begin (tz) starts the ESP-IDF SNTP client; the first sync lands a few
 * seconds later. NtpService.epoch reports the Unix time (0 until synced) and
 * HttpDate.format renders it as an RFC 7231 date string. GET /time returns it.
 *
 * NOTE: this service is compiled into the library only when PROTOCORE_ENABLE_NTP
 * is set for the whole build (a .ino #define does not reach the separately
 * compiled library). In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_NTP=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Flash, open Serial @ 115200 for the IP, then GET http://<ip>/time.
 */

#define PROTOCORE_ENABLE_NTP 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/application/ntp_service/ntp_service.h"
#include "shared/http_date/http_date.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static uint8_t date_work[16]; // HttpDate keeps no state in its borrow


void handle_time(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char date[PROTOCORE_HTTP_DATE_MAX];
    NtpService.epoch(protocore_ntp_service_span());
    HttpDateV.args.epoch = NtpServiceV.value;
    HttpDateV.args.out = date;
    HttpDateV.args.out_cap = sizeof(date);
    HttpDate.format(date_work);
    if (HttpDateV.n == 0)
    {
        send_text(slot_id, 503, "text/plain", "Time not synced yet");
        return;
    }
    send_text(slot_id, 200, "text/plain", date);
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

    on_http("/time", HTTP_GET, handle_time);
    begin_http(80, NULL);

    NtpServiceV.begin_args.tz = "UTC0"; // POSIX TZ string; set your zone for local time
    NtpServiceV.begin_args.server1 = NULL;
    NtpServiceV.begin_args.server2 = NULL;
    NtpService.begin(protocore_ntp_service_span());
}

void loop()
{
    handle();
}
