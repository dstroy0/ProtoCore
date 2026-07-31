// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file HeidenhainLsv2.ino
 * @brief Heidenhain LSV/2 collector - talk to a Heidenhain TNC control over LSV/2-over-TCP and read its
 *        run state (PC_ENABLE_LSV2).
 *
 * services/machine_tool/lsv2 is a pure codec for the LSV/2 telegram protocol; the sketch owns the TCP socket to the
 * control (default port 19000). Each LSV/2 telegram is a 4-byte big-endian payload-length prefix, a
 * 4-character mnemonic, then the payload; the codec frames the requests and slices the replies. A
 * session here:
 *
 *   A_LG "INSPECT"          -> gain read access (control replies T_OK)
 *   R_RI <EXEC_STATE>       -> program execution state  (reply S_RI ... or T_ER)
 *   R_RI <SELECTED_PGM>     -> currently selected program
 *   A_LO                    -> drop all access rights
 *
 * Point TNC_IP at a Heidenhain control that has the LSV/2 / DNC interface enabled. See README.
 *
 * Build flags (platformio.ini):  build_flags = -DPC_ENABLE_LSV2=1
 */

#define PC_ENABLE_LSV2 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/client.h"
#include "services/machine_tool/lsv2/lsv2.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *TNC_IP = "192.168.1.50"; // a Heidenhain TNC with the LSV/2 interface enabled

static uint8_t c_tx[128];
static uint8_t c_rx[512];

// Read one LSV/2 response telegram: first the 8-byte header (length prefix + mnemonic), then the
// payload-length bytes the header declares (or a timeout).
static size_t read_response(int cid, uint8_t *out, size_t cap)
{
    size_t got = 0;
    unsigned long deadline = millis() + 3000;
    while (got < PC_LSV2_HEADER_LEN && millis() < deadline)
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
        if (got < cap)
        {
            out[got] = ch;
        }
        got++;
    }
    if (got < PC_LSV2_HEADER_LEN)
    {
        return got;
    }
    uint32_t plen = ((uint32_t)out[0] << 24) | ((uint32_t)out[1] << 16) | ((uint32_t)out[2] << 8) | out[3];
    size_t need = PC_LSV2_HEADER_LEN + plen;
    while (got < need && got < cap && millis() < deadline)
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
        out[got++] = ch;
    }
    return got;
}

// Send a pre-built request (tx_len bytes in c_tx), parse the reply into r; false on build / no-frame.
static bool txrx(int cid, size_t tx_len, Lsv2Telegram *r, const char *label)
{
    if (tx_len == 0)
    {
        Serial.printf("[lsv2] %s: request build failed\n", label);
        return false;
    }
    pc_client_send(cid, c_tx, tx_len);
    size_t n = read_response(cid, c_rx, sizeof(c_rx));
    size_t consumed = 0;
    if (!pc_lsv2_parse(c_rx, n, r, &consumed))
    {
        Serial.printf("[lsv2] %s: no / short response (%u bytes)\n", label, (unsigned)n);
        return false;
    }
    return true;
}

// Read one run-info selector and print the reply (S_RI payload, or the T_ER error class/code).
static void query_run_info(int cid, uint16_t sel, const char *label)
{
    Lsv2Telegram r;
    if (!txrx(cid, pc_lsv2_build_run_info(c_tx, sizeof(c_tx), sel), &r, label))
    {
        return;
    }
    uint8_t err_class = 0, err_code = 0;
    if (pc_lsv2_error(&r, &err_class, &err_code))
    {
        Serial.printf("[lsv2] %s: error class %u code %u\n", label, (unsigned)err_class, (unsigned)err_code);
        return;
    }
    Serial.printf("[lsv2] %s: %.4s, %u payload bytes:", label, r.mnemonic, (unsigned)r.payload_len);
    for (size_t i = 0; i < r.payload_len && i < 16; i++)
    {
        Serial.printf(" %02X", r.payload[i]);
    }
    Serial.println();
}

static void run_session(const char *host)
{
    int cid = pc_client_open(host, PC_LSV2_TCP_PORT, 8000);
    if (cid < 0)
    {
        Serial.println("[lsv2] connect failed");
        return;
    }

    // gain INSPECT (read) access rights
    Lsv2Telegram r;
    if (!txrx(cid, pc_lsv2_build_login(c_tx, sizeof(c_tx), PC_LSV2_LOGIN_INSPECT, nullptr), &r, "login") ||
        !pc_lsv2_is_ok(&r))
    {
        Serial.println("[lsv2] login INSPECT failed");
        pc_client_close(cid);
        return;
    }
    Serial.println("[lsv2] login INSPECT: T_OK");

    query_run_info(cid, LSV2_RI_EXEC_STATE, "exec-state");
    query_run_info(cid, LSV2_RI_SELECTED_PGM, "selected-pgm");
    query_run_info(cid, LSV2_RI_OVERRIDE, "override");

    // drop all access rights
    txrx(cid, pc_lsv2_build_logout(c_tx, sizeof(c_tx), nullptr), &r, "logout");
    pc_client_close(cid);
    Serial.println("[lsv2] done");
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
        run_session(TNC_IP); // pc_client_open resolves the dotted-quad host directly
    }
    delay(10);
}
