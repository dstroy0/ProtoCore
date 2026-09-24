// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Vxi11.ino
 * @brief VXI-11 instrument controller - drive an instrument over the legacy LXI transport
 *        (VXI-11 / ONC RPC) carrying SCPI (PROTOCORE_ENABLE_VXI11).
 *
 * VXI-11 rides on ONC RPC (Sun RPC) with XDR over TCP. services/instrumentation/vxi11 is a pure codec (it builds
 * RPC calls + parses replies); the sketch owns the sockets and runs the standard session:
 *
 *   portmap GETPORT(0x0607AF/1/TCP) on port 111  -> the dynamic DEVICE_CORE port
 *   connect there, create_link("inst0")          -> a link id
 *   device_write("*IDN?\n")                       -> ask for the identity
 *   device_read()                                 -> the identity string
 *   destroy_link()
 *
 * Point INSTRUMENT_IP at a real VXI-11 / LXI instrument (or a python-vxi11 server). See the README.
 *
 * Build flags (platformio.ini):  build_flags = -DPROTOCORE_ENABLE_VXI11=1 -DPROTOCORE_ENABLE_TCP_CLIENT=1 -DPROTOCORE_ENABLE_DNS_RESOLVER=1
 */

#define PROTOCORE_ENABLE_VXI11 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "services/instrumentation/vxi11/vxi11.h"

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

static const char *INSTRUMENT_IP = "192.168.1.60"; // a VXI-11 instrument

static uint8_t c_req[256];
static uint8_t c_resp[512];

// Write one record-marked RPC call, read one single-fragment RPC reply (the bytes after the record
// mark) into c_resp. Returns the reply length, or 0 on timeout / overflow.
static size_t rpc_call(int cid, size_t req_len)
{
    if (req_len == 0 || !tcp_send(cid, c_req, req_len))
    {
        return 0;
    }
    uint8_t rm[4];
    size_t got = 0;
    unsigned long deadline = millis() + 3000;
    while (got < 4 && millis() < deadline)
    {
        if (tcp_available(cid))
        {
            got += tcp_read(cid, rm + got, 4 - got);
        }
    }
    bool last = false;
    uint32_t frag = 0;
    if (got < 4 || !protocore_rpc_parse_record_mark(rm, 4, &last, &frag) || frag > sizeof(c_resp))
    {
        return 0;
    }
    got = 0;
    while (got < frag && millis() < deadline)
    {
        if (tcp_available(cid))
        {
            got += tcp_read(cid, c_resp + got, frag - got);
        }
    }
    return got == frag ? got : 0;
}

static void run_session(const char *host)
{
    // 1) Ask the portmapper (TCP 111) for the DEVICE_CORE port.
    int pmap_cid = tcp_open(host, PROTOCORE_RPC_PMAP_PORT, 8000);
    if (pmap_cid < 0)
    {
        Serial.println("[vxi11] portmap connect failed");
        return;
    }
    size_t n =
        protocore_vxi11_build_getport(c_req, sizeof(c_req), 1, PROTOCORE_VXI11_CORE_PROG, PROTOCORE_VXI11_CORE_VERS, PROTOCORE_RPC_PROTO_TCP);
    uint32_t core_port = 0;
    if (!protocore_vxi11_parse_getport_resp(c_resp, rpc_call(pmap_cid, n), &core_port) || core_port == 0)
    {
        Serial.println("[vxi11] GETPORT failed");
        tcp_close(pmap_cid);
        return;
    }
    tcp_close(pmap_cid);
    Serial.printf("[vxi11] DEVICE_CORE port = %u\n", core_port);

    // 2) Open the core channel and create a link to "inst0".
    int core_cid = tcp_open(host, (uint16_t)core_port, 8000);
    if (core_cid < 0)
    {
        Serial.println("[vxi11] core connect failed");
        return;
    }
    n = protocore_vxi11_build_create_link(c_req, sizeof(c_req), 2, 0x44575345 /* "PCE" */, false, 0, "inst0");
    Vxi11CreateLinkResp link;
    if (!protocore_vxi11_parse_create_link_resp(c_resp, rpc_call(core_cid, n), &link) || link.error != PROTOCORE_VXI11_ERR_NONE)
    {
        Serial.println("[vxi11] create_link failed");
        tcp_close(core_cid);
        return;
    }
    Serial.printf("[vxi11] link=%d maxRecv=%u\n", link.lid, link.max_recv_size);

    // 3) Write "*IDN?" (END-terminated), then read the identity back.
    n = protocore_vxi11_build_device_write(c_req, sizeof(c_req), 3, link.lid, 10000, 0, PROTOCORE_VXI11_FLAG_END,
                                    (const uint8_t *)"*IDN?\n", 6);
    Vxi11WriteResp wr;
    protocore_vxi11_parse_write_resp(c_resp, rpc_call(core_cid, n), &wr);

    n = protocore_vxi11_build_device_read(c_req, sizeof(c_req), 4, link.lid, 1024, 10000, 0, 0, 0);
    Vxi11ReadResp rd;
    if (protocore_vxi11_parse_read_resp(c_resp, rpc_call(core_cid, n), &rd) && rd.error == PROTOCORE_VXI11_ERR_NONE)
    {
        Serial.printf("[vxi11] *IDN? -> %.*s\n", (int)rd.data_len, (const char *)rd.data);
    }
    else
    {
        Serial.println("[vxi11] device_read failed");
    }

    // 4) Close the link.
    n = protocore_vxi11_build_destroy_link(c_req, sizeof(c_req), 5, link.lid);
    int32_t err = 0;
    protocore_vxi11_parse_error_resp(c_resp, rpc_call(core_cid, n), &err);
    tcp_close(core_cid);
    Serial.println("[vxi11] done");
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
