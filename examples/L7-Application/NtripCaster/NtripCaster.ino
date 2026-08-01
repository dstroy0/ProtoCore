// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file NtripCaster.ino
 * @brief GNSS RTK base + NTRIP caster, and a matching rover (PC_ENABLE_NTRIP_CASTER).
 *
 * Two ESP32-S3 boards, each with a GT-U7 GPS on Serial1 (GPS TX -> GPIO18, GPS RX -> GPIO17, PPS -> GPIO4):
 *
 *   BASE  (NTRIP_ROLE_BASE): reads the GT-U7's GGA fixes, "surveys in" its own antenna position by
 *         averaging fixes until the spread is small enough, converts the mean to ECEF, and serves it as
 *         an RTCM3 1005 station-reference message to any rover that connects to its NTRIP caster on
 *         port 2101, mountpoint "BASE1". `GET /` returns the source table.
 *
 *   ROVER (NTRIP_ROLE_ROVER): an NTRIP client - connects to the base's caster, subscribes to "BASE1",
 *         then syncs the RTCM3 stream, CRC-validates each frame, decodes the 1005, and prints the base's
 *         ECEF position (and the lat/lon it converts back to).
 *
 * Both modules are u-blox 6/7-class (GT-U7), which do NOT output raw carrier-phase measurements, so the
 * base cannot generate the RTCM *observation* (MSM) messages a rover needs to compute a true cm-level RTK
 * fix - that needs an F9P/M8T-class receiver (u-blox RXM-RAWX). This example therefore proves the whole
 * correction-transport pipeline end to end (survey-in -> caster -> NTRIP client -> RTCM3 decode); swapping
 * in a raw-capable receiver + MSM generation is the only missing piece for a real RTK fix.
 *
 * FLASH: set NTRIP_ROLE below (BASE on one board, ROVER on the other). On the rover, set BASE_IP to the
 * base board's IP (printed by the base at boot). Open Serial @ 115200 to watch each side.
 *
 * NOTE (PlatformIO): the caster is compiled into the *library*, so the flag must reach the whole build:
 * `build_flags = -DPC_ENABLE_NTRIP_CASTER=1`. In the Arduino IDE it is set in build_opt.h.
 */

#define PC_ENABLE_NTRIP_CASTER 1

// --- CHANGE ME: 1 = base (survey-in + caster), 0 = rover (NTRIP client) ---
#define NTRIP_ROLE_BASE 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/client.h"
#include "services/timing_position/gnss/gnss_survey.h"
#include "services/timing_position/gnss/rtcm3.h"
#include "services/timing_position/nmea0183/nmea0183.h"

// --- CHANGE ME: your WiFi ---
static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// GT-U7 wiring (per board): GPS TX -> GPIO18 (board RX), GPS RX -> GPIO17 (board TX), PPS -> GPIO4.
static const int GPS_RX_PIN = 18;
static const int GPS_TX_PIN = 17;
static const int PPS_PIN = 4;
static const uint32_t GPS_BAUD = 9600; // GT-U7 default

static const uint16_t CASTER_PORT = 2101; // IANA-registered NTRIP port
static const char *MOUNTPOINT = "BASE1";
static const uint16_t STATION_ID = 2003;

// Survey-in gate: average this many fixes and reach this 3-D spread (metres) before serving corrections.
static const uint32_t SURVEY_MIN_OBS = 60;    // ~1 min at 1 Hz
static const double SURVEY_ACC_LIMIT_M = 2.5; // GT-U7 single-point scatter is a few metres

// PPS pulse counter (one per second from the GPS): proves the timing pin and can gate survey sampling.
static volatile uint32_t s_pps_count = 0;
static void IRAM_ATTR on_pps()
{
    s_pps_count++;
}

// Read Serial1 into a line buffer; return true once a full NMEA sentence (\n-terminated) is ready.
static bool read_nmea_line(char *line, size_t cap, size_t *len)
{
    while (Serial1.available())
    {
        char c = (char)Serial1.read();
        if (c == '\n')
        {
            line[*len] = '\0';
            bool have = (*len > 0);
            *len = 0;
            if (have)
            {
                return true;
            }
        }
        else if (c != '\r' && *len < cap - 1)
        {
            line[(*len)++] = c;
        }
    }
    return false;
}

#if NTRIP_ROLE_BASE
// ============================ BASE: survey-in + NTRIP caster ============================
#include "services/timing_position/gnss/ntrip_caster_listener.h"

static GnssSurvey s_survey;
static bool s_surveyed = false;
static GnssEcef s_base_ecef;
static uint32_t s_last_bcast_ms = 0;

void setup()
{
    Serial.begin(115200);
    Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    pinMode(PPS_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(PPS_PIN), on_pps, RISING);
    pc_gnss_survey_reset(&s_survey);

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nBASE IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF),
                  (unsigned)((ip >> 24) & 0xFF)); // <- set this as BASE_IP on the rover

    NtripMount mount = {};
    mount.mountpoint = MOUNTPOINT;
    mount.identifier = "PC GT-U7 base";
    mount.format_details = "1005(1)"; // station reference once per second
    mount.nav_system = "GPS";
    mount.country = "USA";
    mount.generator = "PC";
    mount.nmea_required = false;

    int32_t li = listen(CASTER_PORT, ConnProto::PROTO_NTRIP_CASTER);
    if (li < 0 || !pc_ntrip_caster_add_mount((uint8_t)li, &mount, nullptr /*open access*/))
    {
        Serial.println("caster add_mount failed");
    }
    begin();
    Serial.printf("NTRIP caster: %u.%u.%u.%u:%u  mount /%s   (surveying in...)\n", (unsigned)(ip & 0xFF),
                  (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF),
                  CASTER_PORT, MOUNTPOINT);
}

void loop()
{
    handle(); // pumps rover connections + the caster

    char line[100];
    static size_t len = 0;
    if (read_nmea_line(line, sizeof(line), &len))
    {
        Nmea0183 m;
        if (pc_nmea0183_parse(line, strlen(line), &m) && m.type[0] == 'G' && m.type[1] == 'G' && m.type[2] == 'A')
        {
            if (!s_surveyed)
            {
                if (pc_gnss_survey_add_gga(&s_survey, &m))
                {
                    uint32_t n = pc_gnss_survey_count(&s_survey);
                    if (n % 10 == 0)
                    {
                        Serial.printf("survey: %u fixes, spread %.2f m\n", n, pc_gnss_survey_accuracy_m(&s_survey));
                    }
                    if (pc_gnss_survey_complete(&s_survey, SURVEY_MIN_OBS, SURVEY_ACC_LIMIT_M))
                    {
                        pc_gnss_survey_mean(&s_survey, &s_base_ecef);
                        s_surveyed = true;
                        Serial.printf("survey COMPLETE after %u fixes; serving 1005 for /%s\n", n, MOUNTPOINT);
                    }
                }
            }
        }
    }

    // Once surveyed, broadcast the station-reference 1005 once per second to all subscribers.
    if (s_surveyed && (uint32_t)(millis() - s_last_bcast_ms) >= 1000)
    {
        s_last_bcast_ms = millis();
        uint8_t frame[32];
        size_t n = pc_rtcm3_build_1005(frame, sizeof(frame), STATION_ID, pc_gnss_ecef_m_to_01mm(s_base_ecef.x),
                                       pc_gnss_ecef_m_to_01mm(s_base_ecef.y), pc_gnss_ecef_m_to_01mm(s_base_ecef.z));
        int sent = pc_ntrip_caster_broadcast(MOUNTPOINT, frame, n);
        if (sent > 0)
        {
            Serial.printf("1005 -> %d rover(s)  [pps=%u]\n", sent, s_pps_count);
        }
    }
}

#else
// ============================ ROVER: NTRIP client ============================

// --- CHANGE ME: the base board's IP (it prints "BASE IP:" at boot) ---
static const char *BASE_IP = "192.168.1.50";

int s_client = -1;
static uint8_t s_rtcm[512]; // RTCM sync/parse buffer
static size_t s_rtcm_len = 0;
static bool s_streaming = false;

static void connect_ntrip()
{
    Serial.printf("connecting to caster %s:%u /%s ...\n", BASE_IP, CASTER_PORT, MOUNTPOINT);
    s_client = pc_client_open(BASE_IP, CASTER_PORT, 8000);
    if (s_client < 0)
    {
        Serial.println("connect failed; retrying");
        return;
    }
    // NTRIP 1.0 request. (A caster that speaks 2.0 answers HTTP/1.1 200; we accept either.)
    char req[96];
    int rn =
        snprintf(req, sizeof(req), "GET /%s HTTP/1.0\r\nUser-Agent: NTRIP PC/1.0\r\nAccept: */*\r\n\r\n", MOUNTPOINT);
    if (rn > 0)
    {
        pc_client_send(s_client, req, (size_t)rn);
    }
    s_streaming = false;
    s_rtcm_len = 0;
}

void setup()
{
    Serial.begin(115200);
    Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN); // the rover's own GT-U7 (for a real fix)
    pinMode(PPS_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(PPS_PIN), on_pps, RISING);

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nROVER IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
    connect_ntrip();
}

// Feed one RTCM byte into the sync/parse buffer; decode any complete 1005 frames.
static void pc_rtcm_push(uint8_t b)
{
    if (s_rtcm_len < sizeof(s_rtcm))
    {
        s_rtcm[s_rtcm_len++] = b;
    }

    for (;;)
    {
        size_t off = pc_rtcm3_sync(s_rtcm, s_rtcm_len); // drop bytes before the next 0xD3 preamble
        if (off > 0)
        {
            memmove(s_rtcm, s_rtcm + off, s_rtcm_len - off);
            s_rtcm_len -= off;
        }
        Rtcm3Frame f;
        size_t used = pc_rtcm3_frame_parse(s_rtcm, s_rtcm_len, &f);
        if (used == 0)
        {
            return; // need more bytes for a full frame
        }

        if (f.crc_ok && f.msg_type == 1005)
        {
            Rtcm3StationArp arp;
            if (pc_rtcm3_parse_1005(f.payload, f.payload_len, &arp))
            {
                GnssEcef e = {arp.ecef_x_01mm / 10000.0, arp.ecef_y_01mm / 10000.0, arp.ecef_z_01mm / 10000.0};
                GnssGeodetic g;
                pc_gnss_ecef_to_geodetic(&e, &g);
                Serial.printf("RTCM 1005 sta=%u  ECEF %.3f, %.3f, %.3f m  ->  %.7f, %.7f  h=%.2f m  [pps=%u]\n",
                              arp.station_id, e.x, e.y, e.z, g.lat_deg, g.lon_deg, g.height_m, s_pps_count);
            }
        }
        else if (!f.crc_ok)
        {
            Serial.println("RTCM frame CRC mismatch (dropped)");
        }
        memmove(s_rtcm, s_rtcm + used, s_rtcm_len - used); // consume the frame
        s_rtcm_len -= used;
    }
}

void loop()
{
    if (pc_client_is_closed(s_client))
    {
        delay(2000);
        connect_ntrip();
        return;
    }
    while (pc_client_available(s_client))
    {
        uint8_t b = 0;
        if (pc_client_read(s_client, &b, 1) != 1)
        {
            break;
        }
        if (!s_streaming)
        {
            // Skip the response header up to the blank line; then bytes are raw RTCM.
            static uint8_t tail[4] = {0, 0, 0, 0};
            tail[0] = tail[1];
            tail[1] = tail[2];
            tail[2] = tail[3];
            tail[3] = b;
            if (tail[0] == '\r' && tail[1] == '\n' && tail[2] == '\r' && tail[3] == '\n')
            {
                s_streaming = true;
                Serial.println("stream started; decoding RTCM...");
            }
            continue;
        }
        pc_rtcm_push(b);
    }
}
#endif
