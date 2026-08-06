// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file HaasMdc.ino
 * @brief Haas Machine Data Collection (MDC) collector - poll a Haas CNC control's `?Q` commands over
 *        its Ethernet port and decode the framed replies (PC_ENABLE_HAAS_MDC).
 *
 * services/machine_tool/haas_mdc is a pure codec for the documented Haas MDC protocol; the sketch owns the TCP
 * socket to the control (raw socket on the Setting-143 port, default 5051). Each poll:
 *
 *   ?Q100  -> serial number      ?Q102 -> model        ?Q104 -> mode
 *   ?Q500  -> program + status + parts    (two branches: PROGRAM,... vs STATUS, BUSY)
 *   ?Q600 <var> -> a macro / system variable
 *
 * A reply's payload is framed between STX (0x02) and ETB (0x17), then CR LF and a `>` prompt; the
 * codec scans for those delimiters, so read_frame just accumulates bytes through the ETB. Point
 * HAAS_IP at a control with MDC enabled (Setting 143). See README.
 *
 * Build flags (platformio.ini):  build_flags = -DPC_ENABLE_HAAS_MDC=1
 */

#define PC_ENABLE_HAAS_MDC 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/tcp.h"
#include "services/machine_tool/haas_mdc/haas_mdc.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *HAAS_IP = "192.168.1.60"; // a Haas control with MDC (Setting 143) enabled

static char c_cmd[24];

// Accumulate one framed reply: read bytes until the ETB (0x17) closes the payload, or a timeout.
static size_t read_frame(int cid, char *out, size_t cap)
{
    size_t o = 0;
    unsigned long deadline = millis() + 3000;
    while (o + 1 < cap && millis() < deadline)
    {
        if (!Tcp.client->available(cid))
        {
            continue;
        }
        uint8_t ch = 0;
        if (Tcp.client->read(cid, &ch, 1) != 1)
        {
            continue;
        }
        out[o++] = (char)ch;
        if (ch == (uint8_t)PC_HAAS_MDC_ETB)
        {
            break;
        }
    }
    out[o] = '\0';
    return o;
}

// Send a query, parse its reply into r; return false on no-frame / UNKNOWN.
static bool poll(int cid, size_t n, HaasMdcResp *r, const char *label)
{
    Tcp.client->send(cid, (const uint8_t *)c_cmd, n);
    char frame[192];
    size_t fl = read_frame(cid, frame, sizeof(frame));
    if (!pc_haas_mdc_parse(frame, fl, r))
    {
        Serial.printf("[haas] %s: no response\n", label);
        return false;
    }
    if (pc_haas_mdc_is_error(r))
    {
        Serial.printf("[haas] %s: UNKNOWN (unsupported)\n", label);
        return false;
    }
    return true;
}

static void query_value(int cid, uint16_t q, const char *label)
{
    HaasMdcResp r;
    if (!poll(cid, pc_haas_mdc_build_q(c_cmd, sizeof(c_cmd), q), &r, label))
    {
        return;
    }
    const char *v = nullptr;
    size_t vl = 0;
    if (pc_haas_mdc_value(&r, &v, &vl))
    {
        Serial.printf("[haas] %s: %.*s\n", label, (int)vl, v);
    }
}

static void run_session(const char *host)
{
    int cid = Tcp.client->open(host, PC_HAAS_MDC_TCP_PORT, 8000);
    if (cid < 0)
    {
        Serial.println("[haas] connect failed");
        return;
    }

    query_value(cid, HAAS_Q_SERIAL, "serial");
    query_value(cid, HAAS_Q_MODEL, "model");
    query_value(cid, HAAS_Q_MODE, "mode");

    // Q500: program + run status + parts counter (two-branch response).
    HaasMdcResp r;
    if (poll(cid, pc_haas_mdc_build_q(c_cmd, sizeof(c_cmd), HAAS_Q_PROGRAM_STATUS), &r, "status"))
    {
        HaasMdcStatus s;
        if (pc_haas_mdc_parse_status(&r, &s))
        {
            if (s.busy)
            {
                Serial.println("[haas] status: BUSY");
            }
            else
            {
                Serial.printf("[haas] program %.*s  status %.*s  parts %u\n", (int)s.program_len, s.program,
                              (int)s.status_len, s.status, (unsigned)s.parts);
            }
        }
    }

    // Q600: read macro variable #100.
    if (poll(cid, pc_haas_mdc_build_var(c_cmd, sizeof(c_cmd), 100), &r, "macro"))
    {
        uint32_t var = 0;
        const char *val = nullptr;
        size_t vl = 0;
        if (pc_haas_mdc_parse_macro(&r, &var, &val, &vl))
        {
            Serial.printf("[haas] macro #%u = %.*s\n", (unsigned)var, (int)vl, val);
        }
    }

    Tcp.client->close(cid);
    Serial.println("[haas] done");
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
        run_session(HAAS_IP); // Tcp.client->open resolves the dotted-quad host directly
    }
    delay(10);
}
