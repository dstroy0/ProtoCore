// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Scpi.ino
 * @brief SCPI / IEEE 488.2 instrument controller - drive a bench instrument over TCP 5025
 *        (PC_ENABLE_SCPI).
 *
 * services/instrumentation/scpi is a transport-agnostic codec: it builds command lines and parses replies; the
 * app owns the socket. This sketch opens the library's outbound TCP client (pc_client) to an
 * instrument's raw SCPI socket (port 5025 - DMMs, scopes, supplies, function generators, SMUs,
 * loads all speak it) and runs a small session:
 *
 *   *IDN?            -> manufacturer, model, serial, firmware (4 comma-separated fields)
 *   *CLS             -> clear the status byte + error queue
 *   MEAS:VOLT:DC?    -> a DC voltage reading, parsed as a number
 *   SYST:ERR?        -> pop the instrument's error/event queue (0,"No error" when clean)
 *
 * The parsed values print over Serial. Point INSTRUMENT_IP at a real instrument, a Python
 * `pyvisa` server, or any SCPI simulator listening on port 5025. See the README to fake one with
 * a two-line `socat` / netcat responder for a dry run.
 *
 * Build flag (platformio.ini):  build_flags = -DPC_ENABLE_SCPI=1
 */

#define PC_ENABLE_SCPI 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/client.h"
#include "services/instrumentation/scpi/scpi.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// --- the target instrument ---
static const char *INSTRUMENT_IP = "192.168.1.60"; // its raw SCPI socket host
// PC_SCPI_PORT (5025) is the de-facto raw-socket port; override if your instrument differs.

static char c_cmd[128];
static char c_resp[256];

// Send one command, read one newline-terminated response line. Returns the response length
// (excluding the newline / NUL), or 0 on timeout. @p cmd must already be newline-terminated
// (pc_scpi_build does this).
static size_t scpi_exchange(int cid, const char *cmd, size_t cmd_len)
{
    if (cmd_len == 0 || !pc_client_send(cid, cmd, cmd_len))
    {
        return 0;
    }
    size_t got = 0;
    unsigned long deadline = millis() + 3000;
    while (got < sizeof(c_resp) - 1 && millis() < deadline)
    {
        if (!pc_client_available(cid))
        {
            continue;
        }
        uint8_t b = 0;
        if (pc_client_read(cid, &b, 1) != 1)
        {
            continue;
        }
        char ch = (char)b;
        if (ch == '\n')
        {
            break;
        }
        if (ch != '\r')
        {
            c_resp[got++] = ch;
        }
    }
    c_resp[got] = '\0';
    return got;
}

static void run_session(const char *host)
{
    int cid = pc_client_open(host, PC_SCPI_PORT, 8000);
    if (cid < 0)
    {
        Serial.println("[scpi] connect failed");
        return;
    }

    size_t n;

    // 1) *IDN? - identify the instrument (4 comma-separated fields).
    n = pc_scpi_build(c_cmd, sizeof(c_cmd), pc_scpi_common(SCPI_IDN_Q), nullptr, 0);
    if (scpi_exchange(cid, c_cmd, n))
    {
        Serial.printf("[scpi] *IDN? -> %s\n", c_resp);
    }
    else
    {
        Serial.println("[scpi] *IDN? no reply");
    }

    // 2) *CLS - clear status byte + error queue (no response).
    n = pc_scpi_build(c_cmd, sizeof(c_cmd), pc_scpi_common(SCPI_CLS), nullptr, 0);
    pc_client_send(cid, c_cmd, n);

    // 3) MEAS:VOLT:DC? - take a DC voltage reading and parse it as a number.
    n = pc_scpi_build(c_cmd, sizeof(c_cmd), "MEASure:VOLTage:DC?", nullptr, 0);
    if (scpi_exchange(cid, c_cmd, n))
    {
        double volts = 0.0;
        if (pc_scpi_parse_number(c_resp, strlen(c_resp), &volts))
        {
            Serial.printf("[scpi] DC voltage = %.6f V\n", volts);
        }
        else
        {
            Serial.printf("[scpi] unparseable reading: %s\n", c_resp);
        }
    }

    // 4) SYST:ERR? - read one entry off the instrument's error/event queue.
    n = pc_scpi_build(c_cmd, sizeof(c_cmd), "SYSTem:ERRor?", nullptr, 0);
    if (scpi_exchange(cid, c_cmd, n))
    {
        Serial.printf("[scpi] SYST:ERR? -> %s\n", c_resp);
    }

    pc_client_close(cid);
    Serial.println("[scpi] done");
}

void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    static bool done = false;
    if (!done && millis() > 2000)
    {
        done = true;
        run_session(INSTRUMENT_IP); // pc_client_open resolves the dotted-quad host directly
    }
    delay(10);
}
