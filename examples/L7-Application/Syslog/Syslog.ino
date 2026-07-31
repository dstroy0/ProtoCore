// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Syslog.ino
 * @brief Remote logging to a syslog server (RFC 5424 over UDP).
 *
 * Ships device log lines to a central syslog server as RFC 5424 UDP datagrams -
 * a zero-heap structured-logging sink for a fleet of devices. Here it logs at
 * boot, once a second, and once per HTTP request via the access-log hook.
 *
 * Point SYSLOG_SERVER at your collector. Test with any syslog receiver, e.g.:
 *   # rsyslog/journald on :514, or a quick netcat-style listener:
 *   python -c "import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);\
 *              s.bind(('0.0.0.0',5140));\
 *              [print(s.recvfrom(2048)[0].decode()) for _ in iter(int,1)]"
 * (then set SYSLOG_PORT to 5140 below)
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPC_ENABLE_SYSLOG=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_SYSLOG 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/syslog/syslog.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *SYSLOG_SERVER = "192.168.1.10"; // your syslog collector
static const uint16_t SYSLOG_PORT = 514;           // 514 = IANA syslog; use 5140 for an unprivileged listener

PC server;

// Per-request access log -> syslog.
static void access_log(const char *method, const char *path, int status, int len)
{
    char line[96];
    snprintf(line, sizeof(line), "%s %s -> %d (%d bytes)", method, path, status, len);
    pc_syslog_log(status >= 500   ? SyslogSeverity::SYSLOG_ERR
                  : status >= 400 ? SyslogSeverity::SYSLOG_WARNING
                                  : SyslogSeverity::SYSLOG_INFO,
                  line);
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

    pc_syslog_init(SYSLOG_SERVER, SYSLOG_PORT, "esp32-pc", "pc", SyslogFacility::SYSLOG_FAC_LOCAL0);
    pc_syslog_log(SyslogSeverity::SYSLOG_NOTICE, "device booted");

    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "ok"); });
    server.on_request_log(access_log); // every response is logged to syslog

    int32_t result = server.begin(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
    }
    else
    {
        Serial.println("HTTP server on :80; logging to syslog");
    }
}

void loop()
{
    server.handle();

    static uint32_t last = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        char hb[48];
        snprintf(hb, sizeof(hb), "heartbeat uptime=%lus heap=%u", (unsigned long)(millis() / 1000),
                 (unsigned)ESP.getFreeHeap());
        pc_syslog_log(SyslogSeverity::SYSLOG_INFO, hb);
    }
}
