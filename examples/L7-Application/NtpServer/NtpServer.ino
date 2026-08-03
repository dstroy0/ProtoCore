// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file NtpServer.ino
 * @brief Turn the ESP32 into a local NTP time server (PC_ENABLE_NTP_SERVER).
 *
 * The device answers NTP requests on UDP/123 so every gadget on your LAN can sync to it -
 * even with no internet. Its own time comes from a fallback chain (a "time source" list
 * queried best-first):
 *
 *   1. A **GPS receiver** on a serial port - the gold standard. GPS carries atomic-clock
 *      time; we read the standard `$GPRMC` sentence, turn it into a Unix timestamp, and
 *      serve it at stratum 1 (a reference clock). Wire the GPS module's TX to GPS_RX_PIN.
 *   2. **Upstream NTP** (`pool.ntp.org`) as a fallback for when there is no GPS fix yet and
 *      the device does have internet.
 *
 * So: GPS when locked, the internet pool otherwise, and the server keeps answering the LAN
 * throughout. Test it from any computer:  `sntp -d <device-ip>`  or  `w32tm /stripchart
 * /computer:<device-ip>`  (Windows)  or  `chronyc -a 'burst 1/1'` against it.
 *
 * Build flags (PlatformIO): `-DPC_ENABLE_NTP_SERVER=1 -DPC_ENABLE_TIME_SOURCE=1
 *                            -DPC_ENABLE_NMEA0183=1 -DPC_ENABLE_NTP=1`
 */

#define PC_ENABLE_NTP_SERVER 1
#define PC_ENABLE_TIME_SOURCE 1
#define PC_ENABLE_NMEA0183 1
#define PC_ENABLE_NTP 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/timing_position/nmea0183/nmea0183.h"
#include "network_drivers/application/ntp_server/ntp_server.h"
#include "network_drivers/application/ntp_service/ntp_service.h"
#include "services/timing_position/time_source/time_source.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// GPS module: connect its TX pin to this ESP32 RX pin; most GPS units talk NMEA at 9600.
static const int GPS_RX_PIN = 16;
static const int GPS_TX_PIN = 17; // unused (we only read), any free pin
static const uint32_t GPS_BAUD = 9600;

// Last good GPS fix, and when we saw it (to advance time between the 1 Hz sentences).
static volatile uint32_t g_gps_epoch = 0;
static volatile uint32_t g_gps_millis = 0;

// --- Days since 1970-01-01 for a civil date (proleptic Gregorian; H. Hinnant's algorithm) --
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097L + (long)doe - 719468L;
}
static int two(const char *p)
{
    return (p[0] - '0') * 10 + (p[1] - '0');
}

// Parse one $..RMC sentence: on a valid fix, set g_gps_epoch from its UTC time + date.
static void gps_feed_sentence(const char *line, size_t len)
{
    Nmea0183 s;
    if (!pc_nmea0183_parse(line, len, &s))
    {
        return;
    }
    if (strcmp(s.type, "RMC") != 0 || s.field_count < 10)
    {
        return;
    }
    if (!s.fields[2] || s.fields[2][0] != 'A') // A = valid fix, V = void
    {
        return;
    }
    const char *t = s.fields[1]; // hhmmss(.sss)
    const char *d = s.fields[9]; // ddmmyy
    if (s.field_len[1] < 6 || s.field_len[9] < 6)
    {
        return;
    }
    int hh = two(t), mi = two(t + 2), ss = two(t + 4);
    int dd = two(d), mo = two(d + 2), yy = two(d + 4);
    long days = days_from_civil(2000 + yy, mo, dd);
    g_gps_epoch = (uint32_t)(days * 86400L + hh * 3600L + mi * 60L + ss);
    g_gps_millis = millis();
}

// Pump the GPS serial port, splitting it into lines for gps_feed_sentence().
static void gps_poll()
{
    static char line[96];
    static size_t n = 0;
    while (Serial1.available())
    {
        char c = (char)Serial1.read();
        if (c == '\n' || c == '\r')
        {
            if (n)
            {
                gps_feed_sentence(line, n);
            }
            n = 0;
        }
        else if (n < sizeof(line) - 1)
        {
            line[n++] = c;
        }
        else
        {
            n = 0; // overlong line - drop it
        }
    }
}

// --- Time sources (queried best-first by pc_time_now()) ---
static uint32_t gps_time_source()
{
    if (g_gps_epoch == 0)
    {
        return 0; // no fix yet
    }
    uint32_t age = (millis() - g_gps_millis) / 1000u;
    if (age > 10)
    {
        return 0; // fix went stale (antenna unplugged?) - let the next source answer
    }
    return g_gps_epoch + age;
}
static uint32_t pc_ntp_upstream_source()
{
    return pc_ntp_synced() ? (uint32_t)pc_ntp_epoch() : 0;
}

void setup()
{
    Serial.begin(115200);
    Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

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

    // GPS is the primary (stratum 1); the public NTP pool is the fallback.
    pc_time_source_add("gps", 1, gps_time_source);
    pc_time_source_add("ntp", 2, pc_ntp_upstream_source);
    pc_ntp_begin(NULL, NULL, NULL); // start the upstream SNTP client for the fallback

    if (pc_ntp_server_begin(1, NTP_REFID_GPS))
    {
        Serial.println("NTP server listening on UDP/123 (point your devices at this IP)");
    }
    else
    {
        Serial.println("NTP server failed to bind :123");
    }
}

void loop()
{
    gps_poll();

    static uint32_t last = 0;
    if (millis() - last > 5000)
    {
        last = millis();
        uint32_t now = pc_time_now();
        const char *src = pc_time_source_active();
        Serial.printf("[ntp] epoch=%lu source=%s\n", (unsigned long)now, src ? src : "none-yet");
    }
}
