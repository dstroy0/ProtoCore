// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SNTP.ino
 * @brief Wall-clock time via SNTP (PC_ENABLE_NTP).
 *
 * pc_ntp_begin(tz) starts the ESP-IDF SNTP client; the first sync lands a few
 * seconds later. pc_ntp_http_date() formats the current time as an RFC 7231
 * date string (returns 0 until synced). GET /time returns it.
 *
 * NOTE: this service is compiled into the library only when PC_ENABLE_NTP
 * is set for the whole build (a .ino #define does not reach the separately
 * compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_NTP=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Flash, open Serial @ 115200 for the IP, then GET http://<ip>/time.
 */

#define PC_ENABLE_NTP 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/timing_position/ntp_service/ntp_service.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


void handle_time(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char date[40];
    if (pc_ntp_http_date(date, sizeof(date)) == 0)
    {
        send_text(slot_id, 503, "text/plain", "Time not synced yet");
        return;
    }
    send_text(slot_id, 200, "text/plain", date);
}

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

    on_http("/time", HttpMethod::HTTP_GET, handle_time);
    begin_http(80);

    pc_ntp_begin("UTC0"); // POSIX TZ string; set your zone for local time
}

void loop()
{
    handle();
}
