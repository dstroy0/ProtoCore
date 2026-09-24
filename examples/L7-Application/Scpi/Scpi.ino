// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Scpi.ino
 * @brief SCPI / IEEE 488.2 instrument controller - drive a bench instrument over TCP 5025
 *        (PROTOCORE_ENABLE_SCPI).
 *
 * services/instrumentation/scpi is a transport-agnostic codec: it builds command lines and parses replies; the
 * app owns the socket. This sketch opens the library's outbound TCP client (protocore_client) to an
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
 * Build flags (platformio.ini):  build_flags = -DPROTOCORE_ENABLE_SCPI=1 -DPROTOCORE_ENABLE_TCP_CLIENT=1 -DPROTOCORE_ENABLE_DNS_RESOLVER=1
 */

#define PROTOCORE_ENABLE_SCPI 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "services/instrumentation/scpi/scpi.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// Thin wrappers over the library's outbound TCP client (TcpClient). Each call names its slot in
// TcpClientV.cid and reads the outcome back off TcpClientV.
static int tcp_open(const char *host, uint16_t port, uint32_t timeout_ms)
{
    TcpClientV.dial.host = host;
    TcpClientV.dial.port = port;
    TcpClientV.dial.timeout_ms = timeout_ms;
    TcpClient.open(protocore_tcp_client_span());
    int cid = TcpClientV.i32;
    if (cid < 0)
    {
        return -1;
    }
    // open is non-blocking: wait for the handshake to finish (or the dial to fail).
    for (;;)
    {
        TcpClientV.cid = cid;
        TcpClient.connected(protocore_tcp_client_span());
        if (TcpClientV.ok)
        {
            return cid;
        }
        TcpClientV.cid = cid;
        TcpClient.is_closed(protocore_tcp_client_span());
        if (TcpClientV.ok)
        {
            TcpClientV.cid = cid;
            TcpClient.close(protocore_tcp_client_span());
            return -1;
        }
        delay(10);
    }
}

static bool tcp_send(int cid, const void *data, size_t len)
{
    TcpClientV.cid = cid;
    TcpClientV.io.data = data;
    TcpClientV.io.len = len;
    TcpClient.send(protocore_tcp_client_span());
    return TcpClientV.ok;
}

static size_t tcp_available(int cid)
{
    TcpClientV.cid = cid;
    TcpClient.available(protocore_tcp_client_span());
    return TcpClientV.n;
}

static size_t tcp_read(int cid, void *buf, size_t cap)
{
    TcpClientV.cid = cid;
    TcpClientV.io.buf = (uint8_t *)buf;
    TcpClientV.io.cap = cap;
    TcpClient.read(protocore_tcp_client_span());
    return TcpClientV.n;
}


static void tcp_close(int cid)
{
    TcpClientV.cid = cid;
    TcpClient.close(protocore_tcp_client_span());
}

// --- the target instrument ---
static const char *INSTRUMENT_IP = "192.168.1.60"; // its raw SCPI socket host
// PROTOCORE_SCPI_PORT (5025) is the de-facto raw-socket port; override if your instrument differs.

static char c_cmd[128];
static char c_resp[256];

// Send one command, read one newline-terminated response line. Returns the response length
// (excluding the newline / NUL), or 0 on timeout. @p cmd must already be newline-terminated
// (protocore_scpi_build does this).
static size_t scpi_exchange(int cid, const char *cmd, size_t cmd_len)
{
    if (cmd_len == 0 || !tcp_send(cid, cmd, cmd_len))
    {
        return 0;
    }
    size_t got = 0;
    unsigned long deadline = millis() + 3000;
    while (got < sizeof(c_resp) - 1 && millis() < deadline)
    {
        if (!tcp_available(cid))
        {
            continue;
        }
        uint8_t b = 0;
        if (tcp_read(cid, &b, 1) != 1)
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
    int cid = tcp_open(host, PROTOCORE_SCPI_PORT, 8000);
    if (cid < 0)
    {
        Serial.println("[scpi] connect failed");
        return;
    }

    size_t n;

    // 1) *IDN? - identify the instrument (4 comma-separated fields).
    n = protocore_scpi_build(c_cmd, sizeof(c_cmd), protocore_scpi_common(SCPI_IDN_Q), nullptr, 0);
    if (scpi_exchange(cid, c_cmd, n))
    {
        Serial.printf("[scpi] *IDN? -> %s\n", c_resp);
    }
    else
    {
        Serial.println("[scpi] *IDN? no reply");
    }

    // 2) *CLS - clear status byte + error queue (no response).
    n = protocore_scpi_build(c_cmd, sizeof(c_cmd), protocore_scpi_common(SCPI_CLS), nullptr, 0);
    tcp_send(cid, c_cmd, n);

    // 3) MEAS:VOLT:DC? - take a DC voltage reading and parse it as a number.
    n = protocore_scpi_build(c_cmd, sizeof(c_cmd), "MEASure:VOLTage:DC?", nullptr, 0);
    if (scpi_exchange(cid, c_cmd, n))
    {
        double volts = 0.0;
        if (protocore_scpi_parse_number(c_resp, strlen(c_resp), &volts))
        {
            Serial.printf("[scpi] DC voltage = %.6f V\n", volts);
        }
        else
        {
            Serial.printf("[scpi] unparseable reading: %s\n", c_resp);
        }
    }

    // 4) SYST:ERR? - read one entry off the instrument's error/event queue.
    n = protocore_scpi_build(c_cmd, sizeof(c_cmd), "SYSTem:ERRor?", nullptr, 0);
    if (scpi_exchange(cid, c_cmd, n))
    {
        Serial.printf("[scpi] SYST:ERR? -> %s\n", c_resp);
    }

    tcp_close(cid);
    Serial.println("[scpi] done");
}

void setup()
{
    Serial.begin(115200);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    static bool done = false;
    if (!done && millis() > 2000)
    {
        done = true;
        run_session(INSTRUMENT_IP); // tcp_open resolves the dotted-quad host directly
    }
    delay(10);
}
