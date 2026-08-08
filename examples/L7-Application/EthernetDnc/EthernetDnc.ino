// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file EthernetDnc.ino
 * @brief Drip-feed a G-code program to a CNC controller over TCP (PC_ENABLE_DNC).
 *
 * "DNC" (Distributed Numerical Control) is how a program is streamed to a machine-tool
 * controller a block at a time, with XON/XOFF flow control so the sender pauses when the
 * controller's small input buffer fills. Classically it ran over RS-232; many modern
 * controllers expose a raw TCP "program port" that speaks the same stream - that is
 * "Ethernet DNC".
 *
 * At boot the board joins WiFi, connects to the controller's program port, and drip-feeds a
 * short program with `dnc_stream`. The engine is transport-agnostic: this sketch supplies the
 * one piece of glue it needs - `cl_send` / `cl_recv` over `pc_client`, the shared outbound TCP
 * transport. `cl_recv` returns any reverse-channel bytes so the engine can honor XOFF/XON.
 *
 * Edit the lines marked "CHANGE ME" below, flash, and open Serial @ 115200.
 *
 * NOTE (PlatformIO): DNC is compiled into the *library*, so the flag must reach the whole build:
 * `build_flags = -DPC_ENABLE_DNC=1`. In the Arduino IDE it is set for you in build_opt.h.
 */

#define PC_ENABLE_DNC 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/tcp.h"
#include "services/machine_tool/dnc/dnc_stream.h" // dnc_stream + DncCfg / DncCode


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

// dnc_stream's transport seam, bound to pc_client.
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
        if (!Tcp.client->send(cid, data + sent, chunk))
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
    size_t n = Tcp.client->read(cid, buf, cap);
    if (n > 0)
    {
        return (int)n;
    }
    if (Tcp.client->is_closed(cid))
    {
        return -1;
    }
    delay(1);
    return 0;
}

void send_program()
{
    int cid = Tcp.client->open(CNC_HOST, CNC_PORT, 8000);
    if (cid < 0)
    {
        Serial.println("connect failed - is the controller's program port reachable?");
        return;
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

    Tcp.client->close(cid);
}

void setup()
{
    Serial.begin(115200);

    Physical.wifi->init(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    send_program();
}

void loop()
{
    delay(1000);
}
