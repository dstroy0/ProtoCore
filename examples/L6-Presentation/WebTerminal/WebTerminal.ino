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
 *     build_flags = -DPC_ENABLE_WEB_TERMINAL=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.) A define in the
 * sketch alone does not reach the separately-compiled library .cpp.
 */

#define PC_ENABLE_WEB_TERMINAL 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/web/web_terminal/web_terminal.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// Each line's shape is a table, declared once. The library builds it by walking the table, so
// nothing parses a format string at runtime and no line can come out half-written.
// The fields are {kind, width, literal length, literal}; a valued field takes one argument below.
static const pc_field REPLY_HEAP[] = {
    {PC_FK_LIT, 0, 11, "free heap: "}, PC_U32, {PC_FK_LIT, 0, 7, " bytes\n"}, PC_END};
static const pc_field REPLY_UPTIME[] = {{PC_FK_LIT, 0, 8, "uptime: "}, PC_U32, {PC_FK_LIT, 0, 4, " ms\n"}, PC_END};
static const pc_field REPLY_ECHO[] = {{PC_FK_LIT, 0, 6, "echo: "}, PC_STR, {PC_FK_LIT, 0, 1, "\n"}, PC_END};
static const pc_field HEARTBEAT[] = {{PC_FK_LIT, 0, 7, "uptime "},
                                     PC_U32,
                                     {PC_FK_LIT, 0, 10, " ms, heap "},
                                     PC_U32,
                                     {PC_FK_LIT, 0, 1, "\n"},
                                     PC_END};

// Browser -> device: handle a typed command line.
void on_command(const char *line, uint8_t client_id)
{
    (void)client_id;
    if (strcmp(line, "help") == 0)
    {
        pc_web_terminal_println("commands: help, heap, uptime, <echo>");
        return;
    }

    // Build the line from its spec, then hand the text to the terminal.
    char out[96];
    if (strcmp(line, "heap") == 0)
    {
        pc_frame_build(out, sizeof(out), REPLY_HEAP, (uint32_t)ESP.getFreeHeap());
    }
    else if (strcmp(line, "uptime") == 0)
    {
        pc_frame_build(out, sizeof(out), REPLY_UPTIME, (uint32_t)millis());
    }
    else
    {
        pc_frame_build(out, sizeof(out), REPLY_ECHO, line);
    }
    pc_web_terminal_print(out);
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
    Serial.println("Open http://<ip>/terminal in a browser");

    pc_web_terminal_begin("/terminal");
    pc_web_terminal_on_command(on_command);

    int32_t result = begin_http(80);
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
        if (pc_web_terminal_client_count() > 0)
        {
            char out[96];
            pc_frame_build(out, sizeof(out), HEARTBEAT, (uint32_t)millis(), (uint32_t)ESP.getFreeHeap());
            pc_web_terminal_print(out);
        }
    }
}
