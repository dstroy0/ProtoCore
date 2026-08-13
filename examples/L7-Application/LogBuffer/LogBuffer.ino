// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file LogBuffer.ino
 * @brief Fixed-RAM rotating log buffer with severity traps (PROTOCORE_ENABLE_LOGBUF).
 *
 * Keeps the last PROTOCORE_LOG_LINES log lines in RAM (oldest pruned on overflow),
 * serves them at GET /logs, and fires a trap on WARN+ lines (here it just prints,
 * but a real app could forward an SNMP trap or a webhook). Try: curl http://<ip>/logs
 *
 * NOTE: enable it for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_LOGBUF=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_LOGBUF 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "server/core/logbuf.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static void on_trap(uint8_t level, const char *line)
{
    Serial.printf("[trap] %s\n", line); // forward criticals here (SNMP trap / webhook)
    (void)level;
}

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

    protocore_log_set_trap(protocore_log_level::PROTOCORE_LOG_WARN, on_trap); // trap on WARN and ERROR
    protocore_log(protocore_log_level::PROTOCORE_LOG_INFO, "boot complete");

    on_http("/logs", HTTP_GET, [](uint8_t id, HttpReq *) {
        char buf[PROTOCORE_LOG_LINES * PROTOCORE_LOG_LINE_LEN];
        protocore_log_dump(buf, sizeof(buf));
        send_text(id, 200, "text/plain", buf);
    });
    begin_http(80, NULL);
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 5000)
    {
        last = millis();
        char msg[64];
        uint32_t heap = ESP.getFreeHeap();
        snprintf(msg, sizeof(msg), "heap=%u uptime=%lus", (unsigned)heap, millis() / 1000);
        protocore_log(heap < 20000 ? protocore_log_level::PROTOCORE_LOG_WARN : protocore_log_level::PROTOCORE_LOG_INFO, msg);
    }
    handle();
}
