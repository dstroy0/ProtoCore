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
 * Build flags (platformio.ini):  build_flags = -DPROTOCORE_ENABLE_HISLIP=1 -DPROTOCORE_ENABLE_TCP_CLIENT=1 -DPROTOCORE_ENABLE_DNS_RESOLVER=1
 */

#define PROTOCORE_ENABLE_HISLIP 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/tcp/client/client.h"
#include "services/instrumentation/hislip/hislip.h"

// The TCP client is work-borrow: each entry reads its operands from TcpClientV and writes its
// outcome back there. These wrappers keep the session code below reading as plain calls.
static void tcp_close(int cid)
{
    TcpClientV.cid = cid;
    TcpClient.close(protocore_tcp_client_span());
}

// Dial and wait for the handshake (open() returns before the connection exists); -1 on failure.
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
            tcp_close(cid);
            return -1;
        }
        delay(1);
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

static size_t tcp_read(int cid, uint8_t *buf, size_t cap)
{
    TcpClientV.cid = cid;
    TcpClientV.io.buf = buf;
    TcpClientV.io.cap = cap;
    TcpClient.read(protocore_tcp_client_span());
    return TcpClientV.n;
}

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
        if (tcp_available(cid))
        {
            got += tcp_read(cid, c_resp + got, PROTOCORE_HISLIP_HEADER_LEN - got);
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
        if (tcp_available(cid))
        {
            got += tcp_read(cid, c_resp + got, total - got);
        }
    }
    return got == total;
}

static void run_session(const char *host)
{
    int sync_cid = tcp_open(host, PROTOCORE_HISLIP_PORT, 8000);
    if (sync_cid < 0)
    {
        Serial.println("[hislip] sync connect failed");
        return;
    }

    // 1) Initialize on the sync channel (offer v1.1, vendor "DW", sub-address "hislip0").
    size_t n = protocore_hislip_build_initialize(c_buf, sizeof(c_buf), PROTOCORE_HISLIP_VERSION_1_1, 0x4457, "hislip0");
    tcp_send(sync_cid, c_buf, n);
    HislipHeader h;
    HislipInitializeResponse ir;
    if (!hislip_recv(sync_cid, &h) || !protocore_hislip_parse_initialize_response(c_resp, PROTOCORE_HISLIP_HEADER_LEN, &ir))
    {
        Serial.println("[hislip] no InitializeResponse");
        tcp_close(sync_cid);
        return;
    }
    Serial.printf("[hislip] session=%u server-version=0x%04X overlap=%d\n", ir.session_id, ir.protocol_version,
                  ir.overlap);

    // 2) Bind the async channel with the negotiated SessionID.
    int async_cid = tcp_open(host, PROTOCORE_HISLIP_PORT, 8000);
    if (async_cid >= 0)
    {
        n = protocore_hislip_build_async_initialize(c_buf, sizeof(c_buf), ir.session_id);
        tcp_send(async_cid, c_buf, n);
        hislip_recv(async_cid, &h); // AsyncInitializeResponse (server vendor id in h.parameter)
    }

    // 3) Send "*IDN?" as a DataEND on the sync channel, read the identity back.
    uint32_t msg_id = PROTOCORE_HISLIP_MESSAGE_ID_INIT;
    n = protocore_hislip_build_data(c_buf, sizeof(c_buf), true, 0, msg_id, (const uint8_t *)"*IDN?\n", 6);
    tcp_send(sync_cid, c_buf, n);
    msg_id = protocore_hislip_next_message_id(msg_id);
    if (hislip_recv(sync_cid, &h) && (h.type == HISLIP_MSG_DATA_END || h.type == HISLIP_MSG_DATA))
    {
        Serial.printf("[hislip] *IDN? -> %.*s\n", (int)h.payload_len, (const char *)(c_resp + PROTOCORE_HISLIP_HEADER_LEN));
    }
    else
    {
        Serial.println("[hislip] no *IDN? response");
    }

    if (async_cid >= 0)
    {
        tcp_close(async_cid);
    }
    tcp_close(sync_cid);
    Serial.println("[hislip] done");
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
