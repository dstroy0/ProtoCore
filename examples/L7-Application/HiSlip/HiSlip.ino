// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file HiSlip.ino
 * @brief HiSLIP instrument controller - drive an instrument over the modern LXI transport on TCP
 *        4880 (PC_ENABLE_HISLIP), carrying SCPI (PC_ENABLE_SCPI).
 *
 * HiSLIP (IVI-6.1) is VXI-11's higher-throughput successor. A session uses TWO TCP connections to
 * port 4880 - a synchronous channel (the SCPI command/response stream) and an asynchronous channel
 * (out-of-band control) - correlated by a SessionID negotiated in the handshake. services/instrumentation/hislip is
 * a pure codec (it frames + parses messages); the sketch owns the two sockets and runs:
 *
 *   sync : Initialize            -> InitializeResponse       (learn the SessionID)
 *   async: AsyncInitialize(id)   -> AsyncInitializeResponse  (bind the second channel)
 *   sync : DataEND("*IDN?\n")    -> DataEND(<identity>)      (a SCPI query over HiSLIP)
 *
 * Point INSTRUMENT_IP at a real HiSLIP instrument or a simulator (e.g. python `pyvisa` with a
 * HiSLIP server, or the `PyHiSLIP` reference). See the README.
 *
 * Build flags (platformio.ini):  build_flags = -DPC_ENABLE_HISLIP=1
 */

#define PC_ENABLE_HISLIP 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/tcp.h"
#include "services/instrumentation/hislip/hislip.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *INSTRUMENT_IP = "192.168.1.60"; // a HiSLIP instrument on port 4880

static uint8_t c_buf[128];
static uint8_t c_resp[512];

// Read a full HiSLIP message (the 16-byte header + its payload) into c_resp. Returns true on a
// complete message; fills @p h with the parsed header (payload starts at c_resp + 16).
static bool hislip_recv(int cid, HislipHeader *h)
{
    size_t got = 0;
    unsigned long deadline = millis() + 3000;
    while (got < PC_HISLIP_HEADER_LEN && millis() < deadline)
    {
        if (Tcp.client->available(cid))
        {
            got += Tcp.client->read(cid, c_resp + got, PC_HISLIP_HEADER_LEN - got);
        }
    }
    if (got < PC_HISLIP_HEADER_LEN || !pc_hislip_parse_header(c_resp, got, h))
    {
        return false;
    }
    size_t total = PC_HISLIP_HEADER_LEN + (size_t)h->payload_len;
    if (total > sizeof(c_resp))
    {
        return false;
    }
    while (got < total && millis() < deadline)
    {
        if (Tcp.client->available(cid))
        {
            got += Tcp.client->read(cid, c_resp + got, total - got);
        }
    }
    return got == total;
}

static void run_session(const char *host)
{
    int sync_cid = Tcp.client->open(host, PC_HISLIP_PORT, 8000);
    if (sync_cid < 0)
    {
        Serial.println("[hislip] sync connect failed");
        return;
    }

    // 1) Initialize on the sync channel (offer v1.1, vendor "DW", sub-address "hislip0").
    size_t n = pc_hislip_build_initialize(c_buf, sizeof(c_buf), PC_HISLIP_VERSION_1_1, 0x4457, "hislip0");
    Tcp.client->send(sync_cid, c_buf, n);
    HislipHeader h;
    HislipInitializeResponse ir;
    if (!hislip_recv(sync_cid, &h) || !pc_hislip_parse_initialize_response(c_resp, PC_HISLIP_HEADER_LEN, &ir))
    {
        Serial.println("[hislip] no InitializeResponse");
        Tcp.client->close(sync_cid);
        return;
    }
    Serial.printf("[hislip] session=%u server-version=0x%04X overlap=%d\n", ir.session_id, ir.protocol_version,
                  ir.overlap);

    // 2) Bind the async channel with the negotiated SessionID.
    int async_cid = Tcp.client->open(host, PC_HISLIP_PORT, 8000);
    if (async_cid >= 0)
    {
        n = pc_hislip_build_async_initialize(c_buf, sizeof(c_buf), ir.session_id);
        Tcp.client->send(async_cid, c_buf, n);
        hislip_recv(async_cid, &h); // AsyncInitializeResponse (server vendor id in h.parameter)
    }

    // 3) Send "*IDN?" as a DataEND on the sync channel, read the identity back.
    uint32_t msg_id = PC_HISLIP_MESSAGE_ID_INIT;
    n = pc_hislip_build_data(c_buf, sizeof(c_buf), true, 0, msg_id, (const uint8_t *)"*IDN?\n", 6);
    Tcp.client->send(sync_cid, c_buf, n);
    msg_id = pc_hislip_next_message_id(msg_id);
    if (hislip_recv(sync_cid, &h) && (h.type == HislipMsg::DATA_END || h.type == HislipMsg::DATA))
    {
        Serial.printf("[hislip] *IDN? -> %.*s\n", (int)h.payload_len, (const char *)(c_resp + PC_HISLIP_HEADER_LEN));
    }
    else
    {
        Serial.println("[hislip] no *IDN? response");
    }

    if (async_cid >= 0)
    {
        Tcp.client->close(async_cid);
    }
    Tcp.client->close(sync_cid);
    Serial.println("[hislip] done");
}

void setup()
{
    Serial.begin(115200);
    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
    {
        delay(250);
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    static bool done = false;
    if (!done && millis() > 2000)
    {
        done = true;
        run_session(INSTRUMENT_IP); // Tcp.client->open resolves the dotted-quad host directly
    }
    delay(10);
}
