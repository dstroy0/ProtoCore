// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file EthernetDnc.ino
 * @brief Drip-feed a G-code program to a CNC controller over TCP (PROTOCORE_ENABLE_DNC).
 *
 * "DNC" (Distributed Numerical Control) is how a program is streamed to a machine-tool
 * controller a block at a time, with XON/XOFF flow control so the sender pauses when the
 * controller's small input buffer fills. Classically it ran over RS-232; many modern
 * controllers expose a raw TCP "program port" that speaks the same stream - that is
 * "Ethernet DNC".
 *
 * At boot the board joins WiFi, connects to the controller's program port, and drip-feeds a
 * short program with `dnc_stream`. The engine is transport-agnostic: this sketch supplies the
 * one piece of glue it needs - `cl_send` / `cl_recv` over `TcpClient`, the shared outbound TCP
 * transport. `cl_recv` returns any reverse-channel bytes so the engine can honor XOFF/XON.
 *
 * Edit the lines marked "CHANGE ME" below, flash, and open Serial @ 115200.
 *
 * NOTE (PlatformIO): DNC is compiled into the *library*, so the flag must reach the whole build:
 * `build_flags = -DPROTOCORE_ENABLE_DNC=1`. In the Arduino IDE it is set for you in build_opt.h.
 */

#define PROTOCORE_ENABLE_DNC 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/tcp/client/client.h"
#include "services/machine_tool/dnc/dnc_stream/dnc_stream.h" // dnc_stream + DncCfg / DncCode


// --- CHANGE ME: your WiFi ---
static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// --- CHANGE ME: the controller's raw DNC program port ---
static const char *CNC_HOST = "192.168.1.60"; // the controller's IP address
static const uint16_t CNC_PORT = 5000;        // the raw program port (check your controller's manual)

// The G-code program to send (the % start/end markers are added by dnc_stream).
static const char *PROGRAM = "O0001 (DEMO)\n"
                             "N10 G21 G90\n"
                             "N20 G0 X0 Y0\n"
                             "N30 G1 X10 Y5 F100\n"
                             "N40 M30\n";

// dnc_stream's transport seam, bound to TcpClient.
static int cl_send(void *ctx, const uint8_t *data, size_t len)
{
    int cid = *(int *)ctx;
    size_t sent = 0;
    while (sent < len)
    {
        size_t chunk = len - sent;
        if (chunk > 0xFFFF)
        {
            chunk = 0xFFFF;
        }
        TcpClientV.cid = cid;
        TcpClientV.io.data = data + sent;
        TcpClientV.io.len = chunk;
        TcpClient.send(protocore_tcp_client_span());
        if (!TcpClientV.ok)
        {
            return -1;
        }
        sent += chunk;
    }
    return (int)len;
}

// Non-blocking read of any reverse-channel bytes (XON/XOFF). A short idle delay paces the engine's
// XOFF wait loop without busy-spinning; a closed connection is an error.
static int cl_recv(void *ctx, uint8_t *buf, size_t cap)
{
    int cid = *(int *)ctx;
    TcpClientV.cid = cid;
    TcpClient.available(protocore_tcp_client_span());
    if (TcpClientV.n > 0)
    {
        TcpClientV.cid = cid;
        TcpClientV.io.buf = buf;
        TcpClientV.io.cap = cap;
        TcpClient.read(protocore_tcp_client_span());
        return (int)TcpClientV.n;
    }
    TcpClientV.cid = cid;
    TcpClient.is_closed(protocore_tcp_client_span());
    if (TcpClientV.ok)
    {
        return -1;
    }
    delay(1);
    return 0;
}

void send_program()
{
    TcpClientV.dial.host = CNC_HOST;
    TcpClientV.dial.port = CNC_PORT;
    TcpClientV.dial.timeout_ms = 8000;
    TcpClient.open(protocore_tcp_client_span());
    int cid = TcpClientV.i32;
    if (cid < 0)
    {
        Serial.println("connect failed - is the controller's program port reachable?");
        return;
    }
    // open is non-blocking: wait for the handshake to finish (or the dial to fail).
    for (;;)
    {
        TcpClientV.cid = cid;
        TcpClient.connected(protocore_tcp_client_span());
        if (TcpClientV.ok)
        {
            break;
        }
        TcpClientV.cid = cid;
        TcpClient.is_closed(protocore_tcp_client_span());
        if (TcpClientV.ok)
        {
            Serial.println("connect failed - is the controller's program port reachable?");
            TcpClientV.cid = cid;
            TcpClient.close(protocore_tcp_client_span());
            return;
        }
        delay(10);
    }

    DncCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.code = DNC_CODE_ISO; // ISO 7-bit / ASCII; use DncCode::DNC_CODE_EIA for an EIA RS-244 controller
    cfg.crlf = true;                  // many controllers expect CR before the LF End-of-Block
    cfg.leader_len = 16;              // a short NUL runout before/after the program

    DncStreamResult rc = dnc_stream(&cfg, PROGRAM, strlen(PROGRAM), cl_send, cl_recv, &cid);
    if (rc == DNC_STREAM_OK)
    {
        Serial.println("program sent - the controller has the full drip-feed");
    }
    else
    {
        Serial.printf("drip-feed failed (DncStreamResult %d) - see the README troubleshooting table\n", (int)rc);
    }

    TcpClientV.cid = cid;
    TcpClient.close(protocore_tcp_client_span());
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

    send_program();
}

void loop()
{
    delay(1000);
}
