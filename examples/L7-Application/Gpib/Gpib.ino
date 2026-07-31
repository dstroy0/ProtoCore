// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Gpib.ino
 * @brief GPIB-over-LAN controller - drive a legacy IEEE-488 instrument through a Prologix
 *        GPIB-Ethernet adapter (PC_ENABLE_GPIB), carrying SCPI.
 *
 * services/instrumentation/gpib is a pure codec for the Prologix `++` command set; the sketch owns the socket to
 * the adapter (raw TCP 1234). A line starting with `++` is a controller command; anything else is
 * data forwarded over GPIB to the addressed instrument. The session:
 *
 *   ++mode 1      (controller)     ++addr <N>   (the instrument's GPIB address)
 *   ++eos 2 (LF)  ++eoi 1  ++auto 0
 *   "*IDN?"  (escaped data)        ++read eoi   -> the identity string
 *   ++ver                         -> the adapter version
 *
 * Point ADAPTER_IP at a Prologix GPIB-Ethernet controller (or an AR488-Ethernet build). See README.
 *
 * Build flags (platformio.ini):  build_flags = -DPC_ENABLE_GPIB=1
 */

#define PC_ENABLE_GPIB 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/client.h"
#include "services/instrumentation/gpib/gpib.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *ADAPTER_IP = "192.168.1.70"; // a Prologix GPIB-Ethernet adapter
static const uint8_t INSTRUMENT_ADDR = 9;       // the instrument's primary GPIB address

static char c_cmd[64];
static uint8_t c_data[96];

// Read one newline-terminated line into out (CR stripped); return its length (0 on timeout).
static size_t read_line(int cid, char *out, size_t cap)
{
    size_t o = 0;
    unsigned long deadline = millis() + 3000;
    while (o + 1 < cap && millis() < deadline)
    {
        if (!pc_client_available(cid))
        {
            continue;
        }
        uint8_t ch = 0;
        if (pc_client_read(cid, &ch, 1) != 1)
        {
            continue;
        }
        if (ch == '\n')
        {
            break;
        }
        if (ch != '\r')
        {
            out[o++] = (char)ch;
        }
    }
    out[o] = '\0';
    return o;
}

static void run_session(const char *host)
{
    int cid = pc_client_open(host, PC_GPIB_PORT, 8000);
    if (cid < 0)
    {
        Serial.println("[gpib] connect failed");
        return;
    }

    // Configure the adapter as the controller-in-charge, targeting the instrument.
    pc_client_send(cid, c_cmd, pc_gpib_command(c_cmd, sizeof(c_cmd), "mode 1"));
    pc_client_send(cid, c_cmd, pc_gpib_addr(c_cmd, sizeof(c_cmd), INSTRUMENT_ADDR, -1));
    pc_client_send(cid, c_cmd, pc_gpib_eos(c_cmd, sizeof(c_cmd), GpibEos::LF));
    pc_client_send(cid, c_cmd, pc_gpib_command(c_cmd, sizeof(c_cmd), "eoi 1"));
    pc_client_send(cid, c_cmd, pc_gpib_command(c_cmd, sizeof(c_cmd), "auto 0"));

    // Send "*IDN?" as (escaped) data, then request a read until EOI.
    pc_client_send(cid, c_data, pc_gpib_build_data(c_data, sizeof(c_data), (const uint8_t *)"*IDN?", 5));
    pc_client_send(cid, c_cmd, pc_gpib_read(c_cmd, sizeof(c_cmd), GpibRead::UNTIL_EOI, 0));

    char resp[160];
    size_t r = read_line(cid, resp, sizeof(resp));
    if (r)
    {
        Serial.printf("[gpib] *IDN? -> %s\n", resp);
    }
    else
    {
        Serial.println("[gpib] no *IDN? response");
    }

    // Query the adapter's own version.
    pc_client_send(cid, c_cmd, pc_gpib_command(c_cmd, sizeof(c_cmd), "ver"));
    r = read_line(cid, resp, sizeof(resp));
    const char *ver = nullptr;
    size_t vlen = 0;
    if (pc_gpib_parse_version(resp, r, &ver, &vlen))
    {
        Serial.printf("[gpib] adapter version %.*s\n", (int)vlen, ver);
    }

    pc_client_close(cid);
    Serial.println("[gpib] done");
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
        run_session(ADAPTER_IP); // pc_client_open resolves the dotted-quad host directly
    }
    delay(10);
}
