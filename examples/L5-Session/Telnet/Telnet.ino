// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Telnet.ino
 * @brief Line-oriented Telnet console (RFC 854) on port 23 (PC_ENABLE_TELNET).
 *
 * Opens a Telnet listener via listen(23, ConnProto::PROTO_TELNET). The server
 * negotiates echo + character mode, edits the line for you (backspace works),
 * and delivers each completed line to the command callback; respond with
 * pc_telnet_print/println/printf.
 *
 * Telnet is PLAINTEXT - no auth, no encryption. Use only on a trusted LAN;
 * prefer SSH (SSH) or the WebSocket terminal (WebTerminal) otherwise.
 *
 * NOTE: this feature is compiled into the library only when PC_ENABLE_TELNET
 * is set for the whole build (a .ino #define does not reach the separately
 * compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_TELNET=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Flash, open Serial @ 115200 for the IP, then: telnet <ip> 23  (type "help").
 */

#define PC_ENABLE_TELNET 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/presentation/telnet/telnet.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// Each reply's shape is a table, declared once. The library builds it by walking the table, so
// nothing parses a format string at runtime and no line can come out half-written.
// The fields are {kind, width, literal length, literal}; a valued field takes one argument below.
static const pc_field REPLY_HEAP[] = {
    {PC_FK_LIT, 0, 11, "free heap: "}, PC_U32, {PC_FK_LIT, 0, 8, " bytes\r\n"}, PC_END};
static const pc_field REPLY_UPTIME[] = {{PC_FK_LIT, 0, 8, "uptime: "}, PC_U32, {PC_FK_LIT, 0, 5, " ms\r\n"}, PC_END};
static const pc_field REPLY_ECHO[] = {{PC_FK_LIT, 0, 6, "echo: "}, PC_STR, {PC_FK_LIT, 0, 2, "\r\n"}, PC_END};

void on_command(const char *line, uint8_t conn_id)
{
    (void)conn_id;
    if (strcmp(line, "help") == 0)
    {
        pc_telnet_println("commands: help, heap, uptime, <echo>");
    }
    else if (strcmp(line, "heap") == 0)
    {
        pc_telnet_frame(REPLY_HEAP, (uint32_t)ESP.getFreeHeap());
    }
    else if (strcmp(line, "uptime") == 0)
    {
        pc_telnet_frame(REPLY_UPTIME, (uint32_t)millis());
    }
    else if (line[0])
    {
        pc_telnet_frame(REPLY_ECHO, line);
    }
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

    listen(23, ConnProto::PROTO_TELNET); // open the Telnet port
    pc_telnet_on_command(on_command);

    begin_http(80); // also start HTTP (begin() activates all listeners)
    Serial.println("Telnet on port 23 (try: telnet <ip>)");
}

void loop()
{
    handle();
}
