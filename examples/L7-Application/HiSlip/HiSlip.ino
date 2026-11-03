// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file HiSlip.ino
 * @brief HiSLIP instrument controller - drive an instrument over the modern LXI transport on TCP
 *        4880 (PROTOCORE_ENABLE_HISLIP), carrying SCPI (PROTOCORE_ENABLE_SCPI).
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
 * Build flags (platformio.ini):  build_flags = -DPROTOCORE_ENABLE_TCP_CLIENT=1 -DPROTOCORE_ENABLE_HISLIP=1
 */

#define PROTOCORE_ENABLE_HISLIP 1
#define PROTOCORE_ENABLE_TCP_CLIENT 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/tcp/tcp.h"
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
    while (got < PROTOCORE_HISLIP_HEADER_LEN && millis() < deadline)
    {
        TcpClientV.cid = cid;
        TcpClient.available(protocore_tcp_client_span());
        if (TcpClientV.n)
        {
            TcpClientV.cid = cid;
            TcpClientV.io.buf = c_resp + got;
            TcpClientV.io.cap = PROTOCORE_HISLIP_HEADER_LEN - got;
            TcpClient.read(protocore_tcp_client_span());
            got += TcpClientV.n;
        }
    }
    if (got < PROTOCORE_HISLIP_HEADER_LEN || !protocore_hislip_parse_header(c_resp, got, h))
    {
        return false;
    }
    size_t total = PROTOCORE_HISLIP_HEADER_LEN + (size_t)h->payload_len;
    if (total > sizeof(c_resp))
    {
        return false;
    }
    while (got < total && millis() < deadline)
    {
        TcpClientV.cid = cid;
        TcpClient.available(protocore_tcp_client_span());
        if (TcpClientV.n)
        {
            TcpClientV.cid = cid;
            TcpClientV.io.buf = c_resp + got;
            TcpClientV.io.cap = total - got;
            TcpClient.read(protocore_tcp_client_span());
            got += TcpClientV.n;
        }
    }
    return got == total;
}

static void run_session(const char *host)
{
    TcpClientV.dial.host = host;
    TcpClientV.dial.port = PROTOCORE_HISLIP_PORT;
    TcpClientV.dial.timeout_ms = 8000;
    TcpClient.open(protocore_tcp_client_span());
    int sync_cid = TcpClientV.i32;
    if (sync_cid < 0)
    {
        Serial.println("[hislip] sync connect failed");
        return;
    }

    // 1) Initialize on the sync channel (offer v1.1, vendor "DW", sub-address "hislip0").
    size_t n = protocore_hislip_build_initialize(c_buf, sizeof(c_buf), PROTOCORE_HISLIP_VERSION_1_1, 0x4457, "hislip0");
    TcpClientV.cid = sync_cid;
    TcpClientV.io.data = c_buf;
    TcpClientV.io.len = n;
    TcpClient.send(protocore_tcp_client_span());
    HislipHeader h;
    HislipInitializeResponse ir;
    if (!hislip_recv(sync_cid, &h) || !protocore_hislip_parse_initialize_response(c_resp, PROTOCORE_HISLIP_HEADER_LEN, &ir))
    {
        Serial.println("[hislip] no InitializeResponse");
        TcpClientV.cid = sync_cid;
        TcpClient.close(protocore_tcp_client_span());
        return;
    }
    Serial.printf("[hislip] session=%u server-version=0x%04X overlap=%d\n", ir.session_id, ir.protocol_version,
                  ir.overlap);

    // 2) Bind the async channel with the negotiated SessionID.
    TcpClientV.dial.host = host;
    TcpClientV.dial.port = PROTOCORE_HISLIP_PORT;
    TcpClientV.dial.timeout_ms = 8000;
    TcpClient.open(protocore_tcp_client_span());
    int async_cid = TcpClientV.i32;
    if (async_cid >= 0)
    {
        n = protocore_hislip_build_async_initialize(c_buf, sizeof(c_buf), ir.session_id);
        TcpClientV.cid = async_cid;
        TcpClientV.io.data = c_buf;
        TcpClientV.io.len = n;
        TcpClient.send(protocore_tcp_client_span());
        hislip_recv(async_cid, &h); // AsyncInitializeResponse (server vendor id in h.parameter)
    }

    // 3) Send "*IDN?" as a DataEND on the sync channel, read the identity back.
    uint32_t msg_id = PROTOCORE_HISLIP_MESSAGE_ID_INIT;
    n = protocore_hislip_build_data(c_buf, sizeof(c_buf), true, 0, msg_id, (const uint8_t *)"*IDN?\n", 6);
    TcpClientV.cid = sync_cid;
    TcpClientV.io.data = c_buf;
    TcpClientV.io.len = n;
    TcpClient.send(protocore_tcp_client_span());
    msg_id = protocore_hislip_next_message_id(msg_id);
    if (hislip_recv(sync_cid, &h) && (h.type == HislipMsg::DATA_END || h.type == HislipMsg::DATA))
    {
        Serial.printf("[hislip] *IDN? -> %.*s\n", (int)h.payload_len, (const char *)(c_resp + PROTOCORE_HISLIP_HEADER_LEN));
    }
    else
    {
        Serial.println("[hislip] no *IDN? response");
    }

    if (async_cid >= 0)
    {
        TcpClientV.cid = async_cid;
        TcpClient.close(protocore_tcp_client_span());
    }
    TcpClientV.cid = sync_cid;
    TcpClient.close(protocore_tcp_client_span());
    Serial.println("[hislip] done");
}

void setup()
{
    Serial.begin(115200);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    while (!protocore_physical_wifi_up())
    {
        delay(250);
    }
    uint32_t ip = protocore_physical_egress_ipv4(); // library egress IP (network byte order), no Arduino WiFi
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
