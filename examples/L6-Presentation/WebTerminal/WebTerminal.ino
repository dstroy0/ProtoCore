// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file WebTerminal.ino
 * @brief Browser "web serial" terminal over WebSocket (docs CRT theme).
 *
 * Serves a self-contained green-phosphor terminal page (matching the docs site)
 * and a WebSocket endpoint: device output streams to every open browser, and
 * each line you type is delivered to a command callback. Zero-heap - rides the
 * library's existing WebSocket layer. The page auto-selects ws:// or wss://, so
 * it works unchanged once TLS is enabled.
 *
 * Flash, open Serial @ 115200 for the IP, then browse to:
 *   http://<ip>/terminal
 * Type "help". The device also prints the uptime every few seconds.
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see. The `#define` below documents intent, but for PlatformIO you must
 * enable it for the whole build, e.g. in platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_WEB_TERMINAL=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.) A define in the
 * sketch alone does not reach the separately-compiled library .cpp.
 */

#define PROTOCORE_ENABLE_WEB_TERMINAL 1

#include "protocore.h"
#include "numeros_scribo/numeros_scribo.h"
#include "network_drivers/physical/physical/physical.h"
#include "server/web/web_terminal/web_terminal.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// Each line's shape is a table, declared once. The library builds it by walking the table, so
// nothing parses a format string at runtime and no line can come out half-written.
// The fields are {kind, width, literal length, literal}; a valued field takes one argument below.
static const mmgr_field REPLY_HEAP[] = {
    {MMGR_FK_LIT, 0, 11, "free heap: "}, MMGR_U32, {MMGR_FK_LIT, 0, 7, " bytes\n"}, MMGR_END};
static const mmgr_field REPLY_UPTIME[] = {{MMGR_FK_LIT, 0, 8, "uptime: "}, MMGR_U32, {MMGR_FK_LIT, 0, 4, " ms\n"}, MMGR_END};
static const mmgr_field REPLY_ECHO[] = {{MMGR_FK_LIT, 0, 6, "echo: "}, MMGR_STR, {MMGR_FK_LIT, 0, 1, "\n"}, MMGR_END};
static const mmgr_field HEARTBEAT[] = {{MMGR_FK_LIT, 0, 7, "uptime "},
                                     MMGR_U32,
                                     {MMGR_FK_LIT, 0, 10, " ms, heap "},
                                     MMGR_U32,
                                     {MMGR_FK_LIT, 0, 1, "\n"},
                                     MMGR_END};

// Browser -> device: handle a typed command line.
void on_command(const char *line, uint8_t client_id)
{
    (void)client_id;
    if (strcmp(line, "help") == 0)
    {
        protocore_web_terminal_println("commands: help, heap, uptime, <echo>");
        return;
    }

    // Build the line from its spec, then hand the text to the terminal.
    char out[96];
    if (strcmp(line, "heap") == 0)
    {
        const mmgr_fval reply_heap_vals[] = {MMGR_VU32((uint32_t)ESP.getFreeHeap())};
        const NumerosCfg reply_heap_build = {out, sizeof(out), 0u, REPLY_HEAP, reply_heap_vals, 1u};
        numer.build(&reply_heap_build);
    }
    else if (strcmp(line, "uptime") == 0)
    {
        const mmgr_fval reply_uptime_vals[] = {MMGR_VU32((uint32_t)millis())};
        const NumerosCfg reply_uptime_build = {out, sizeof(out), 0u, REPLY_UPTIME, reply_uptime_vals, 1u};
        numer.build(&reply_uptime_build);
    }
    else
    {
        const mmgr_fval reply_echo_vals[] = {MMGR_VSTR(line)};
        const NumerosCfg reply_echo_build = {out, sizeof(out), 0u, REPLY_ECHO, reply_echo_vals, 1u};
        numer.build(&reply_echo_build);
    }
    protocore_web_terminal_print(out);
}

void setup()
{
    Serial.begin(115200);

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    while (!protocore_physical_wifi_up())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = protocore_physical_egress_ipv4(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
    Serial.println("Open http://<ip>/terminal in a browser");

    protocore_web_terminal_begin("/terminal");
    protocore_web_terminal_on_command(on_command);

    int32_t result = begin_http(80, NULL);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on port 80");
}

void loop()
{
    handle();

    // Device -> browsers: heartbeat every 3 s (only sent when someone's watching).
    static unsigned long last = 0;
    if (millis() - last >= 3000)
    {
        last = millis();
        if (protocore_web_terminal_client_count() > 0)
        {
            char out[96];
            const mmgr_fval heartbeat_vals[] = {MMGR_VU32((uint32_t)millis()), MMGR_VU32((uint32_t)ESP.getFreeHeap())};
            const NumerosCfg heartbeat_build = {out, sizeof(out), 0u, HEARTBEAT, heartbeat_vals, 2u};
            numer.build(&heartbeat_build);
            protocore_web_terminal_print(out);
        }
    }
}
