// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Telnet.ino
 * @brief Line-oriented Telnet console (RFC 854) on port 23 (PROTOCORE_ENABLE_TELNET).
 *
 * Opens a Telnet listener via listen(23, ProtoConn::PROTO_TELNET). The server
 * negotiates echo + character mode, edits the line for you (backspace works),
 * and delivers each completed line to the command callback; respond with
 * Telnet.print/println/frame (operands in TelnetV).
 *
 * Telnet is PLAINTEXT - no auth, no encryption. Use only on a trusted LAN;
 * prefer SSH (SSH) or the WebSocket terminal (WebTerminal) otherwise.
 *
 * NOTE: this feature is compiled into the library only when PROTOCORE_ENABLE_TELNET
 * is set for the whole build (a .ino #define does not reach the separately
 * compiled library). In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_TELNET=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * Flash, open Serial @ 115200 for the IP, then: telnet <ip> 23  (type "help").
 */

#define PROTOCORE_ENABLE_TELNET 1

#include "protocore.h"
#include "mmgr/protoframe/protoframe.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/presentation/telnet/telnet.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// Each reply's shape is a table, declared once. The library builds it by walking the table, so
// nothing parses a format string at runtime and no line can come out half-written.
// The fields are {kind, width, literal length, literal}; a valued field takes one argument below.
static const protocore_field REPLY_HEAP[] = {
    {PROTOCORE_FK_LIT, 0, 11, "free heap: "}, PROTOCORE_U32, {PROTOCORE_FK_LIT, 0, 8, " bytes\r\n"}, PROTOCORE_END};
static const protocore_field REPLY_UPTIME[] = {{PROTOCORE_FK_LIT, 0, 8, "uptime: "}, PROTOCORE_U32, {PROTOCORE_FK_LIT, 0, 5, " ms\r\n"}, PROTOCORE_END};
static const protocore_field REPLY_ECHO[] = {{PROTOCORE_FK_LIT, 0, 6, "echo: "}, PROTOCORE_STR, {PROTOCORE_FK_LIT, 0, 2, "\r\n"}, PROTOCORE_END};

// Telnet's entries read their operands from TelnetV: a line in out.text, or a field set in
// out.spec / out.val / out.nv, broadcast to every connected client.
static void telnet_println(const char *text)
{
    TelnetV.out.text = text;
    Telnet.println(protocore_telnet_span());
}

static void telnet_frame(const protocore_field *spec, const protocore_fval *val, size_t nv)
{
    TelnetV.out.spec = spec;
    TelnetV.out.val = val;
    TelnetV.out.nv = nv;
    Telnet.frame(protocore_telnet_span());
}

void on_command(const char *line, uint8_t conn_id)
{
    (void)conn_id;
    if (strcmp(line, "help") == 0)
    {
        telnet_println("commands: help, heap, uptime, <echo>");
    }
    else if (strcmp(line, "heap") == 0)
    {
        const protocore_fval v[] = {PROTOCORE_VU32((uint32_t)ESP.getFreeHeap())};
        telnet_frame(REPLY_HEAP, v, 1);
    }
    else if (strcmp(line, "uptime") == 0)
    {
        const protocore_fval v[] = {PROTOCORE_VU32((uint32_t)millis())};
        telnet_frame(REPLY_UPTIME, v, 1);
    }
    else if (line[0])
    {
        const protocore_fval v[] = {PROTOCORE_VSTR(line)};
        telnet_frame(REPLY_ECHO, v, 1);
    }
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

    listen(23, PROTO_TELNET); // open the Telnet port
    TelnetV.cb = on_command;
    Telnet.on_command(protocore_telnet_span());

    begin_http(80, NULL); // also start HTTP (begin() activates all listeners)
    Serial.println("Telnet on port 23 (try: telnet <ip>)");
}

void loop()
{
    handle();
}
